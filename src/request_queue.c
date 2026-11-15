/*
 * Copyright (c) 2007 - 2026 by mod_tile contributors (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see http://www.gnu.org/licenses/.
 */

#include <glib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "g_logger.h"
#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "request_queue.h"

/**
 * @brief Compute a hash bucket index for a metatile render request item.
 *
 * Combines a byte-sum hash of `item->req.xmlname` (9 bits), zoom level `z` (4 bits),
 * metatile X coordinate `mx` (24 bits), and metatile Y coordinate `my` (24 bits)
 * into a 64-bit integer and reduces it modulo `queue->hashidxSize`.
 *
 * @param queue Request queue containing `hashidxSize`.
 * @param item  Render request item whose style name and `(mx, my, z)` are hashed.
 * @return Zero-based bucket index in `[0, queue->hashidxSize - 1]`.
 */
static int calcHashKey(struct request_queue *queue, struct item *item)
{
	/* Additive byte checksum of the map style name */
	uint64_t xmlnameHash = 0;
	/* 64-bit composite key packing xmlnameHash, z, mx, and my */
	uint64_t key;

	for (int i = 0; (i < (int)sizeof(item->req.xmlname)) && (item->req.xmlname[i] != 0); i++) {
		xmlnameHash += item->req.xmlname[i];
	}

	key = ((uint64_t)(xmlnameHash & 0x1FF) << 52) + ((uint64_t)(item->req.z) << 48) + ((uint64_t)(item->mx & 0xFFFFFF) << 24) + (item->my & 0xFFFFFF);
	return key % queue->hashidxSize;
}

/**
 * @brief Look up an existing queued or in-flight request matching `(mx, my, z, xmlname)`.
 *
 * Caller must hold `queue->qLock`.
 *
 * @param queue Request queue whose hash index will be searched.
 * @param item  Candidate request item to match against.
 * @return Pointer to the matching `struct item` already in the queue, or `NULL` if none exists.
 */
static struct item *lookup_item_idx(struct request_queue *queue, struct item *item)
{
	/* Current hash bucket chain node being inspected */
	struct item_idx *nextItem;
	/* Candidate item stored in the current hash bucket node */
	struct item *test;
	/* Hash bucket index for `item` */
	int key = calcHashKey(queue, item);

	if (queue->item_hashidx[key].item == NULL) {
		return NULL;
	} else {
		nextItem = &(queue->item_hashidx[key]);

		while (nextItem != NULL) {
			test = nextItem->item;

			if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z == test->req.z) && (!strcmp(item->req.xmlname, test->req.xmlname))) {
				return test;
			} else {
				nextItem = nextItem->next;
			}
		}
	}

	return NULL;
}

/**
 * @brief Insert a request item into the queue's hash table index for O(1) duplicate lookup.
 *
 * Stores `item` directly in the inline head slot `item_hashidx[key]` if empty, or
 * appends a heap-allocated `struct item_idx` node to the bucket's collision chain.
 * Caller must hold `queue->qLock`.
 *
 * @param queue Request queue owning the hash index.
 * @param item  Request item to index.
 */
static void insert_item_idx(struct request_queue *queue, struct item *item)
{
	/* Current and previous nodes while walking the bucket collision chain */
	struct item_idx *nextItem;
	struct item_idx *prevItem;
	/* Hash bucket index for `item` */
	int key = calcHashKey(queue, item);

	if (queue->item_hashidx[key].item == NULL) {
		queue->item_hashidx[key].item = item;
	} else {
		prevItem = &(queue->item_hashidx[key]);
		nextItem = queue->item_hashidx[key].next;

		while (nextItem) {
			prevItem = nextItem;
			nextItem = nextItem->next;
		}

		nextItem = (struct item_idx *)malloc(sizeof(struct item_idx));
		nextItem->item = item;
		nextItem->next = NULL;
		prevItem->next = nextItem;
	}
}

/**
 * @brief Remove a request item from the queue's hash table index.
 *
 * Handles both inline head-slot removal (promoting and freeing the first chained overflow
 * node if present) and interior/tail chained node removal. Caller must hold `queue->qLock`.
 *
 * @param queue Request queue owning the hash index.
 * @param item  Request item to remove from the index.
 */
static void remove_item_idx(struct request_queue *queue, struct item *item)
{
	/* Hash bucket index for `item` */
	int key = calcHashKey(queue, item);
	/* Current and preceding bucket chain nodes during traversal */
	struct item_idx *nextItem;
	struct item_idx *prevItem;
	/* Item stored in the current bucket node */
	struct item *test;

	if (queue->item_hashidx[key].item == NULL) {
		/* Item not present in index */
		return;
	}

	prevItem = &(queue->item_hashidx[key]);
	nextItem = &(queue->item_hashidx[key]);

	while (nextItem != NULL) {
		test = nextItem->item;

		if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z == test->req.z) && (!strcmp(item->req.xmlname, test->req.xmlname))) {
			/* Found matching item; unlink it from the bucket chain */
			nextItem->item = NULL;

			if (nextItem->next != NULL) {
				if (nextItem == &(queue->item_hashidx[key])) {
					/* Promote the first overflow node into the inline head slot and free the overflow node */
					prevItem = nextItem->next;
					memcpy(&(queue->item_hashidx[key]), nextItem->next,
					       sizeof(struct item_idx));
					free(prevItem);
				} else {
					prevItem->next = nextItem->next;
				}
			} else {
				prevItem->next = NULL;
			}

			if (nextItem != &(queue->item_hashidx[key])) {
				free(nextItem);
			}

			return;
		} else {
			prevItem = nextItem;
			nextItem = nextItem->next;
		}
	}
}

/**
 * @brief Check whether a matching metatile request is already queued or being rendered.
 *
 * If an identical request is actively rendering (`queueRender`) or waiting in an
 * interactive queue (`queueRequest`, `queueRequestPrio`, `queueRequestLow`), attaches
 * `test` to the existing item's `duplicates` list so all waiting clients receive a
 * response when rendering completes, and returns `cmdIgnore`.
 *
 * If an identical request is in `queueDirty` or `queueRequestBulk` (where clients do not
 * block on completion), returns `cmdNotDone` so the caller immediately releases `test`.
 * Caller must hold `queue->qLock`.
 *
 * @param queue Request queue to inspect.
 * @param test  Newly arrived request item to deduplicate.
 * @return `cmdIgnore` if coalesced as a duplicate, `cmdNotDone` if matched in dirty/bulk
 *         queues, or `cmdRender` if no matching request is currently pending.
 */
static enum protoCmd pending(struct request_queue *queue, struct item *test)
{
	/* Existing matching request found in the hash index, or NULL */
	struct item *item;

	item = lookup_item_idx(queue, test);

	if (item != NULL) {
		if ((item->inQueue == queueRender) || (item->inQueue == queueRequest) || (item->inQueue == queueRequestPrio) || (item->inQueue == queueRequestLow)) {
			/* Prepend `test` to the existing item's duplicate chain so it receives a reply on completion */
			test->duplicates = item->duplicates;
			item->duplicates = test;
			test->inQueue = queueDuplicate;
			return cmdIgnore;
		} else if ((item->inQueue == queueDirty) || (item->inQueue == queueRequestBulk)) {
			return cmdNotDone;
		}
	}

	return cmdRender;
}

/**
 * @brief Pop the highest-priority pending request from `queue` and move it to the active render list.
 *
 * Blocks on `queue->qCond` until at least one pending queue is non-empty. Dequeues in
 * strict priority order:
 *   1. `reqPrioHead` (`cmdRenderPrio`)
 *   2. `reqHead` (`cmdRender`)
 *   3. `reqLowHead` (`cmdRenderLow`)
 *   4. `dirtyHead` (`cmdDirty`)
 *   5. `reqBulkHead` (`cmdRenderBulk`)
 *
 * Moves the selected item onto `renderHead` with `inQueue = queueRender` while keeping
 * it registered in the hash index so incoming duplicate requests still coalesce with it.
 *
 * @param queue Request queue instance.
 * @return Pointer to the dequeued `struct item` now marked `queueRender`.
 */
struct item *request_queue_fetch_request(struct request_queue *queue)
{
	/* Next request item selected for rendering */
	struct item *item = NULL;

	pthread_mutex_lock(&(queue->qLock));

	while ((queue->reqNum == 0) && (queue->dirtyNum == 0) && (queue->reqLowNum == 0) && (queue->reqPrioNum == 0) && (queue->reqBulkNum == 0)) {
		pthread_cond_wait(&(queue->qCond), &(queue->qLock));
	}

	if (queue->reqPrioNum) {
		item = queue->reqPrioHead.next;
		queue->reqPrioNum--;
		queue->stats.noReqPrioRender++;
	} else if (queue->reqNum) {
		item = queue->reqHead.next;
		queue->reqNum--;
		queue->stats.noReqRender++;
	} else if (queue->reqLowNum) {
		item = queue->reqLowHead.next;
		queue->reqLowNum--;
		queue->stats.noReqLowRender++;
	} else if (queue->dirtyNum) {
		item = queue->dirtyHead.next;
		queue->dirtyNum--;
		queue->stats.noDirtyRender++;
	} else if (queue->reqBulkNum) {
		item = queue->reqBulkHead.next;
		queue->reqBulkNum--;
		queue->stats.noReqBulkRender++;
	}

	if (item) {
		/* Unlink item from its source priority queue */
		item->next->prev = item->prev;
		item->prev->next = item->next;

		/* Insert item at the head of the active render list */
		item->prev = &(queue->renderHead);
		item->next = queue->renderHead.next;
		queue->renderHead.next->prev = item;
		queue->renderHead.next = item;
		item->inQueue = queueRender;
	}

	pthread_mutex_unlock(&queue->qLock);

	return item;
}

/**
 * @brief Invalidate all queued/rendering references to a disconnected client socket `fd`.
 *
 * Walks the interactive and active render queues (`reqHead`, `renderHead`, `reqPrioHead`,
 * `reqBulkHead`, `reqLowHead`) plus any attached `duplicates` chains, setting matching
 * `fd` fields to `FD_INVALID` so worker threads do not attempt to write responses to a
 * closed or reused descriptor. (`dirtyHead` items already hold `FD_INVALID`.)
 *
 * @param queue Request queue instance.
 * @param fd    Client socket file descriptor that was closed or errored.
 */
void request_queue_clear_requests_by_fd(struct request_queue *queue, int fd)
{
	/* Current queue list item, duplicate-chain iterator, and current circular sentinel head */
	struct item *item, *dupes, *queueHead;

	/* Only need to scan the interactive/render queues; items on dirtyHead already have FD_INVALID */
	pthread_mutex_lock(&(queue->qLock));

	for (int i = 0; i < 5; i++) {
		switch (i) {
		case 0: {
			queueHead = &(queue->reqHead);
			break;
		}

		case 1: {
			queueHead = &(queue->renderHead);
			break;
		}

		case 2: {
			queueHead = &(queue->reqPrioHead);
			break;
		}

		case 3: {
			queueHead = &(queue->reqBulkHead);
			break;
		}

		case 4: {
			queueHead = &(queue->reqLowHead);
			break;
		}
		}

		item = queueHead->next;

		while (item != queueHead) {
			if (item->fd == fd) {
				item->fd = FD_INVALID;
			}

			dupes = item->duplicates;

			while (dupes) {
				if (dupes->fd == fd) {
					dupes->fd = FD_INVALID;
				}

				dupes = dupes->duplicates;
			}

			item = item->next;
		}
	}

	pthread_mutex_unlock(&(queue->qLock));
}

/**
 * @brief Enqueue a new tile render request or coalesce it with an existing pending item.
 *
 * Deduplicates `item` via pending(). If unique, places `item` onto the queue matching
 * `item->req.cmd` (`reqHead`, `reqPrioHead`, `reqLowHead`, or `reqBulkHead`) when below
 * `REQ_LIMIT`, or falls back to `dirtyHead` (with `fd = FD_INVALID`) when below
 * `DIRTY_LIMIT`. Drops and frees `item` if both limits are saturated.
 *
 * @param queue Request queue instance (must not be `NULL`).
 * @param item  Heap-allocated request item (ownership transferred to `queue` or freed on drop).
 * @return `cmdIgnore` if queued for synchronous client notification (or coalesced as a
 *         duplicate), or `cmdNotDone` if relegated to `dirtyHead`, dropped, or already dirty/bulk.
 */
enum protoCmd request_queue_add_request(struct request_queue *queue, struct item *item)
{
	/* Deduplication status returned by pending() */
	enum protoCmd status;
	/* Pointer to the wire protocol payload inside `item` */
	const struct protocol *req;
	/* Sentinel head of the target circular doubly-linked queue */
	struct item *list = NULL;
	req = &(item->req);

	if (queue == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "queue is NULL");
		exit(3);
	}

	pthread_mutex_lock(&(queue->qLock));

	/* Check for a matching request in the current rendering or pending queues */
	status = pending(queue, item);

	if (status == cmdNotDone) {
		/* Found a match in the dirty/bulk queue; caller cannot block on it */
		pthread_mutex_unlock(&(queue->qLock));
		free(item);
		return cmdNotDone;
	}

	if (status == cmdIgnore) {
		/* Found a match in an active/interactive queue; item was attached as a duplicate */
		pthread_mutex_unlock(&(queue->qLock));
		return cmdIgnore;
	}

	/* New unique request: route to the priority queue matching req->cmd, or fall back to dirtyHead */
	if ((req->cmd == cmdRender) && (queue->reqNum < REQ_LIMIT)) {
		list = &(queue->reqHead);
		item->inQueue = queueRequest;
		item->originatedQueue = queueRequest;
		queue->reqNum++;
	} else if ((req->cmd == cmdRenderPrio) && (queue->reqPrioNum < REQ_LIMIT)) {
		list = &(queue->reqPrioHead);
		item->inQueue = queueRequestPrio;
		item->originatedQueue = queueRequestPrio;
		queue->reqPrioNum++;
	} else if ((req->cmd == cmdRenderLow) && (queue->reqLowNum < REQ_LIMIT)) {
		list = &(queue->reqLowHead);
		item->inQueue = queueRequestLow;
		item->originatedQueue = queueRequestLow;
		queue->reqLowNum++;
	} else if ((req->cmd == cmdRenderBulk) && (queue->reqBulkNum < REQ_LIMIT)) {
		list = &(queue->reqBulkHead);
		item->inQueue = queueRequestBulk;
		item->originatedQueue = queueRequestBulk;
		queue->reqBulkNum++;
	} else if (queue->dirtyNum < DIRTY_LIMIT) {
		list = &(queue->dirtyHead);
		item->inQueue = queueDirty;
		item->originatedQueue = queueDirty;
		queue->dirtyNum++;
		item->fd = FD_INVALID; /* Background dirty render sends no client response */
	} else {
		/* All queues are saturated; drop the request */
		queue->stats.noReqDroped++;
		pthread_mutex_unlock(&(queue->qLock));
		free(item);
		return cmdNotDone;
	}

	if (list) {
		/* Append item to tail of circular list (immediately before sentinel `list`) */
		item->next = list;
		item->prev = list->prev;
		item->prev->next = item;
		list->prev = item;
		/* Register item in the hash table index for O(1) duplicate detection */
		insert_item_idx(queue, item);

		pthread_cond_signal(&queue->qCond);
	} else {
		/* Unreachable defensive fallback */
		/* LCOV_EXCL_START */
		free(item);
		/* LCOV_EXCL_STOP */
	}

	pthread_mutex_unlock(&queue->qLock);

	return (list == &(queue->dirtyHead)) ? cmdNotDone : cmdIgnore;
}

/**
 * @brief Unlink a completed request from the active render list, remove it from the hash index,
 *        and record render timing statistics.
 *
 * Does not free `request` itself (the caller frees `request` and any chained `duplicates`
 * after sending responses).
 *
 * @param queue       Request queue instance.
 * @param request     Completed request item currently on `renderHead`.
 * @param render_time Elapsed render duration in milliseconds (recorded if `> 0`).
 */
void request_queue_remove_request(struct request_queue *queue, struct item *request, int render_time)
{
	pthread_mutex_lock(&(queue->qLock));

	if (request->inQueue != queueRender) {
		g_logger(G_LOG_LEVEL_WARNING, "Removing request from queue, even though not on rendering queue");
	}

	if (render_time > 0) {
		switch (request->originatedQueue) {
		case queueRequestPrio: {
			queue->stats.timeReqPrioRender += render_time;
			break;
		}

		case queueRequest: {
			queue->stats.timeReqRender += render_time;
			break;
		}

		case queueRequestLow: {
			queue->stats.timeReqLowRender += render_time;
			break;
		}

		case queueDirty: {
			queue->stats.timeReqDirty += render_time;
			break;
		}

		case queueRequestBulk: {
			queue->stats.timeReqBulkRender += render_time;
			break;
		}

		default:
			break;
		}

		queue->stats.noZoomRender[request->req.z]++;
		queue->stats.timeZoomRender[request->req.z] += render_time;
	}

	request->next->prev = request->prev;
	request->prev->next = request->next;
	remove_item_idx(queue, request);
	pthread_mutex_unlock(&(queue->qLock));
}

/**
 * @brief Query the current number of pending items in a specific priority sub-queue.
 *
 * @param queue    Request queue instance.
 * @param priority Command enum identifying the sub-queue (`cmdRenderPrio`, `cmdRender`,
 *                 `cmdRenderLow`, `cmdDirty`, or `cmdRenderBulk`).
 * @return Current item count in the requested sub-queue, or `-1` if `priority` is unrecognized.
 */
int request_queue_no_requests_queued(struct request_queue *queue, enum protoCmd priority)
{
	/* Item count for the requested sub-queue (-1 if priority enum is unrecognized) */
	int noReq = -1;
	pthread_mutex_lock(&(queue->qLock));

	switch (priority) {
	case cmdRenderPrio:
		noReq = queue->reqPrioNum;
		break;

	case cmdRender:
		noReq = queue->reqNum;
		break;

	case cmdRenderLow:
		noReq = queue->reqLowNum;
		break;

	case cmdDirty:
		noReq = queue->dirtyNum;
		break;

	case cmdRenderBulk:
		noReq = queue->reqBulkNum;
		break;

	default:
		break;
	}

	pthread_mutex_unlock(&queue->qLock);
	return noReq;
}

/**
 * @brief Copy a thread-safe snapshot of the queue's cumulative render statistics.
 *
 * @param queue Request queue instance.
 * @param stats Destination `stats_struct` buffer populated under `queue->qLock`.
 */
void request_queue_copy_stats(struct request_queue *queue, stats_struct *stats)
{
	pthread_mutex_lock(&(queue->qLock));
	memcpy(stats, &(queue->stats), sizeof(stats_struct));
	pthread_mutex_unlock(&queue->qLock);
}

/**
 * @brief Allocate and initialize a new `request_queue` with empty circular queues and hash index.
 *
 * Initializes `qLock`, `qCond`, all six circular doubly-linked list sentinel heads
 * (`reqHead`, `reqPrioHead`, `reqLowHead`, `reqBulkHead`, `dirtyHead`, `renderHead`),
 * zeroed statistics counters, and the `HASHIDX_SIZE`-bucket hash table.
 *
 * @return Newly allocated `struct request_queue`, or `NULL` if memory or pthread initialization fails.
 */
struct request_queue *request_queue_init()
{
	/* Return code from pthread synchronization primitive initialization */
	int res;
	/* Newly allocated request_queue instance */
	struct request_queue *queue = (struct request_queue *)calloc(1, sizeof(struct request_queue));

	if (queue == NULL) {
		return NULL;
	}

	res = pthread_mutex_init(&(queue->qLock), NULL);

	if (res != 0) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to create mutex for request_queue");
		free(queue);
		return NULL;
	}

	res = pthread_cond_init(&(queue->qCond), NULL);

	if (res != 0) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to create condition variable for request_queue");
		pthread_mutex_destroy(&(queue->qLock));
		free(queue);
		return NULL;
	}

	queue->stats.noDirtyRender = 0;
	queue->stats.noReqDroped = 0;
	queue->stats.noReqRender = 0;
	queue->stats.noReqPrioRender = 0;
	queue->stats.noReqLowRender = 0;
	queue->stats.noReqBulkRender = 0;

	/* Initialize all circular doubly-linked list sentinels to point to themselves */
	queue->reqHead.next = queue->reqHead.prev = &(queue->reqHead);
	queue->reqPrioHead.next = queue->reqPrioHead.prev = &(queue->reqPrioHead);
	queue->reqLowHead.next = queue->reqLowHead.prev = &(queue->reqLowHead);
	queue->reqBulkHead.next = queue->reqBulkHead.prev = &(queue->reqBulkHead);
	queue->dirtyHead.next = queue->dirtyHead.prev = &(queue->dirtyHead);
	queue->renderHead.next = queue->renderHead.prev = &(queue->renderHead);
	queue->hashidxSize = HASHIDX_SIZE;
	queue->item_hashidx = (struct item_idx *)malloc(sizeof(struct item_idx) * queue->hashidxSize);

	if (queue->item_hashidx == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to allocate memory for request queue hash index");
		pthread_cond_destroy(&(queue->qCond));
		pthread_mutex_destroy(&(queue->qLock));
		free(queue);
		return NULL;
	}

	bzero(queue->item_hashidx, sizeof(struct item_idx) * queue->hashidxSize);

	return queue;
}

/**
 * @brief Destroy the request queue mutex and free the hash index and queue structure.
 *
 * @param queue Request queue instance to tear down.
 */
void request_queue_close(struct request_queue *queue)
{
	/* TODO: Free items and chained item_idx nodes if the queues are not empty at closing time */
	pthread_mutex_destroy(&(queue->qLock));
	free(queue->item_hashidx);
	free(queue);
}
