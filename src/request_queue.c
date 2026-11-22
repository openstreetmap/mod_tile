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
#include <limits.h>
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

/** @brief Smallest number of hash buckets used for the duplicate-detection index. */
#define MIN_HASHIDX_SIZE HASHIDX_SIZE

/**
 * @brief Size the duplicate-detection hash index from the configured queue limits.
 *
 * Keeps the index roughly proportional to the configured queue capacity, so that duplicate lookup
 * stays cheap while large queues are backlogged.
 *
 * @param request_limit Maximum number of requests in each time-critical queue.
 * @param dirty_limit   Maximum number of requests in the dirty queue.
 * @return Number of hash buckets, at least `MIN_HASHIDX_SIZE`.
 */
static int request_queue_hash_size(int request_limit, int dirty_limit)
{
	size_t queue_capacity = (4 * (size_t)request_limit) + (size_t)dirty_limit;

	if (queue_capacity > INT_MAX) {
		return INT_MAX;
	}

	return MAX(MIN_HASHIDX_SIZE, (int)queue_capacity);
}

/**
 * @brief Hash at most `max_len` bytes of a NUL-terminated string (djb2-style multiplier 33).
 *
 * @param value   String to hash.
 * @param max_len Maximum number of bytes to read.
 * @return 64-bit hash of the string.
 */
static uint64_t string_hash(const char *value, size_t max_len)
{
	uint64_t hash = 0;

	for (size_t i = 0; (i < max_len) && (value[i] != 0); i++) {
		hash = (hash * 33) + (unsigned char)value[i];
	}

	return hash;
}

/**
 * @brief Check whether two request items address the same tile with the same style, mime type, and options.
 *
 * @param item First request item.
 * @param test Second request item.
 * @return Non-zero if both items identify the same render request.
 */
static int same_tile_request(struct item *item, struct item *test)
{
	return (item->mx == test->mx) &&
	       (item->my == test->my) &&
	       (item->req.z == test->req.z) &&
	       (strncmp(item->req.xmlname, test->req.xmlname, sizeof(item->req.xmlname)) == 0) &&
	       (strncmp(item->req.mimetype, test->req.mimetype, sizeof(item->req.mimetype)) == 0) &&
	       (strncmp(item->req.options, test->req.options, sizeof(item->req.options)) == 0);
}

/**
 * @brief Compute a hash bucket index for a metatile render request item.
 *
 * Combines hashes of `item->req.xmlname`, `item->req.mimetype`, and `item->req.options` with the zoom level `z`
 * and metatile coordinates `mx`, `my` into a 64-bit key, mixes it, and reduces it modulo `queue->hashidxSize`.
 *
 * @param queue Request queue containing `hashidxSize`.
 * @param item  Render request item to hash.
 * @return Zero-based bucket index in `[0, queue->hashidxSize - 1]`.
 */
static int calcHashKey(struct request_queue *queue, struct item *item)
{
	uint64_t key;
	uint64_t name_hash = string_hash(item->req.xmlname, sizeof(item->req.xmlname));
	uint64_t mimetype_hash = string_hash(item->req.mimetype, sizeof(item->req.mimetype));
	uint64_t options_hash = string_hash(item->req.options, sizeof(item->req.options));

	key = ((name_hash & 0x1FF) << 52) +
	      ((mimetype_hash & 0xFF) << 44) +
	      ((options_hash & 0xFF) << 36) +
	      ((uint64_t)(item->req.z) << 32) +
	      ((uint64_t)(item->mx & 0xFFFF) << 16) +
	      (item->my & 0xFFFF);
	/* Avalanche every identity field into the low bits. Configurable
	 * capacities can yield a power-of-two index size, where the modulo
	 * keeps only the low bits; without mixing, same-row metatiles would
	 * all land in one bucket and chain scans would turn quadratic. */
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdULL;
	key ^= key >> 33;
	key *= 0xc4ceb9fe1a85ec53ULL;
	key ^= key >> 33;
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

			if (same_tile_request(item, test)) {
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

		if (same_tile_request(item, test)) {
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
 * @brief Return the counter tracking the number of items in the given queue type, or `NULL` for unknown types.
 */
static int *request_queue_counter(struct request_queue *queue, enum queueEnum queue_type)
{
	switch (queue_type) {
	case queueRequestPrio:
		return &(queue->reqPrioNum);

	case queueRequest:
		return &(queue->reqNum);

	case queueRequestLow:
		return &(queue->reqLowNum);

	case queueDirty:
		return &(queue->dirtyNum);

	case queueRequestBulk:
		return &(queue->reqBulkNum);

	default:
		return NULL;
	}
}

/**
 * @brief Return the configured maximum number of items for the given queue type.
 */
static int request_queue_limit(struct request_queue *queue, enum queueEnum queue_type)
{
	return (queue_type == queueDirty) ? queue->dirtyLimit : queue->requestLimit;
}

/**
 * @brief Return the position of a queue type in the pending order (0 is served first), or `INT_MAX` if it is not pending.
 */
static int request_queue_rank(enum queueEnum queue_type)
{
	switch (queue_type) {
	case queueRequestPrio:
		return 0;

	case queueRequest:
		return 1;

	case queueRequestLow:
		return 2;

	case queueDirty:
		return 3;

	case queueRequestBulk:
		return 4;

	default:
		return INT_MAX;
	}
}

/**
 * @brief Map a render command to its queue type; returns 0 if the command is not queued.
 */
static int request_queue_for_cmd(enum protoCmd cmd, enum queueEnum *queue_type)
{
	switch (cmd) {
	case cmdRenderPrio:
		*queue_type = queueRequestPrio;
		return 1;

	case cmdRender:
		*queue_type = queueRequest;
		return 1;

	case cmdRenderLow:
		*queue_type = queueRequestLow;
		return 1;

	case cmdDirty:
		*queue_type = queueDirty;
		return 1;

	case cmdRenderBulk:
		*queue_type = queueRequestBulk;
		return 1;

	default:
		return 0;
	}
}

/**
 * @brief Attach `duplicate` to the `duplicates` list of `item` so it receives a response when `item` completes.
 */
static void request_queue_attach_duplicate(struct item *item, struct item *duplicate)
{
	duplicate->duplicates = item->duplicates;
	item->duplicates = duplicate;
	duplicate->inQueue = queueDuplicate;
}

/**
 * @brief Remove `item` from the circular list it is linked in.
 */
static void request_queue_unlink_item(struct item *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}

/**
 * @brief Remove `item` from the pending list and keep the per-rank tail pointers consistent. Caller must hold `queue->qLock`.
 */
static void request_queue_unlink_pending(struct request_queue *queue, struct item *item)
{
	int rank = request_queue_rank(item->inQueue);

	if (rank < PENDING_QUEUE_RANKS && queue->pendingTail[rank] == item) {
		if ((item->prev != &(queue->pendingHead)) && (request_queue_rank(item->prev->inQueue) == rank)) {
			queue->pendingTail[rank] = item->prev;
		} else {
			queue->pendingTail[rank] = &(queue->pendingHead);
		}
	}

	request_queue_unlink_item(item);
}

/**
 * @brief Insert `item` at the end of its rank in the pending list. Caller must hold `queue->qLock`.
 */
static void request_queue_insert_pending(struct request_queue *queue, struct item *item)
{
	struct item *pos;
	int rank = request_queue_rank(item->inQueue);

	if (rank >= PENDING_QUEUE_RANKS) {
		return;
	}

	pos = &(queue->pendingHead);

	for (int i = rank; i >= 0; i--) {
		if (queue->pendingTail[i] != &(queue->pendingHead)) {
			pos = queue->pendingTail[i];
			break;
		}
	}

	item->next = pos->next;
	item->prev = pos;
	item->prev->next = item;
	item->next->prev = item;
	queue->pendingTail[rank] = item;
}

/**
 * @brief Move a pending `item` to the higher-ranked queue requested by `duplicate`, if that queue has room.
 *
 * The duplicate is attached to `item`, so its client still receives the response.
 * Caller must hold `queue->qLock`.
 *
 * @return 1 if `item` was promoted and `duplicate` attached, 0 otherwise.
 */
static int request_queue_promote_pending(struct request_queue *queue, struct item *item, struct item *duplicate)
{
	enum queueEnum target_queue;
	int *current_count;
	int *target_count;

	if (!request_queue_for_cmd(duplicate->req.cmd, &target_queue)) {
		return 0;
	}

	if (target_queue == queueDirty) {
		return 0;
	}

	if (request_queue_rank(target_queue) >= request_queue_rank(item->inQueue)) {
		return 0;
	}

	target_count = request_queue_counter(queue, target_queue);
	current_count = request_queue_counter(queue, item->inQueue);

	if (target_count == NULL || current_count == NULL) {
		return 0;
	}

	if (*target_count >= request_queue_limit(queue, target_queue)) {
		return 0;
	}

	request_queue_unlink_pending(queue, item);
	(*current_count)--;

	item->inQueue = target_queue;
	item->originatedQueue = target_queue;
	request_queue_insert_pending(queue, item);
	(*target_count)++;
	request_queue_attach_duplicate(item, duplicate);
	pthread_cond_signal(&queue->qCond);

	return 1;
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
		if (request_queue_promote_pending(queue, item, test)) {
			return cmdIgnore;
		}

		if ((item->inQueue == queueRender) || (item->inQueue == queueRequest) || (item->inQueue == queueRequestPrio) || (item->inQueue == queueRequestLow)) {
			request_queue_attach_duplicate(item, test);
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

	item = queue->pendingHead.next;

	if (item) {
		switch (item->inQueue) {
		case queueRequestPrio: {
			queue->reqPrioNum--;
			queue->stats.noReqPrioRender++;
			break;
		}

		case queueRequest: {
			queue->reqNum--;
			queue->stats.noReqRender++;
			break;
		}

		case queueRequestLow: {
			queue->reqLowNum--;
			queue->stats.noReqLowRender++;
			break;
		}

		case queueDirty: {
			queue->dirtyNum--;
			queue->stats.noDirtyRender++;
			break;
		}

		case queueRequestBulk: {
			queue->reqBulkNum--;
			queue->stats.noReqBulkRender++;
			break;
		}

		default:
			break;
		}

		request_queue_unlink_pending(queue, item);

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

	/**Only need to scan the pending priority queue and render queue.
	 * Most dirty queue entries have FD_INVALID, but promoted duplicates may
	 * carry a client fd while the primary item still represents the same tile.
	 */
	pthread_mutex_lock(&(queue->qLock));

	for (int i = 0; i < 2; i++) {
		switch (i) {
		case 0: {
			queueHead = &(queue->pendingHead);
			break;
		}

		case 1: {
			queueHead = &(queue->renderHead);
			break;
		}
		}

		item = queueHead->next;

		while (item != queueHead) {
			if (queueHead == &(queue->pendingHead) && item->inQueue == queueDirty) {
				int rank = request_queue_rank(queueDirty);

				item = queue->pendingTail[rank]->next;
				continue;
			}

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
	enum protoCmd add_status = cmdIgnore;
	int item_added = 0;
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

	// New request, add it to render or dirty queue
	if ((req->cmd == cmdRender) && (queue->reqNum < queue->requestLimit)) {
		item->inQueue = queueRequest;
		item->originatedQueue = queueRequest;
		queue->reqNum++;
		item_added = 1;
	} else if ((req->cmd == cmdRenderPrio) && (queue->reqPrioNum < queue->requestLimit)) {
		item->inQueue = queueRequestPrio;
		item->originatedQueue = queueRequestPrio;
		queue->reqPrioNum++;
		item_added = 1;
	} else if ((req->cmd == cmdRenderLow) && (queue->reqLowNum < queue->requestLimit)) {
		item->inQueue = queueRequestLow;
		item->originatedQueue = queueRequestLow;
		queue->reqLowNum++;
		item_added = 1;
	} else if ((req->cmd == cmdRenderBulk) && (queue->reqBulkNum < queue->requestLimit)) {
		item->inQueue = queueRequestBulk;
		item->originatedQueue = queueRequestBulk;
		queue->reqBulkNum++;
		item_added = 1;
	} else if (queue->dirtyNum < queue->dirtyLimit) {
		item->inQueue = queueDirty;
		item->originatedQueue = queueDirty;
		queue->dirtyNum++;
		item->fd = FD_INVALID; // No response after render
		add_status = cmdNotDone;
		item_added = 1;
	} else {
		/* All queues are saturated; drop the request */
		queue->stats.noReqDroped++;
		pthread_mutex_unlock(&(queue->qLock));
		free(item);
		return cmdNotDone;
	}

	if (item_added) {
		request_queue_insert_pending(queue, item);
		/* In addition to the linked list, add item to a hash table index
		 * for faster lookup of pending requests.
		 */
		insert_item_idx(queue, item);

		pthread_cond_signal(&queue->qCond);
	} else {
		/* Unreachable defensive fallback */
		/* LCOV_EXCL_START */
		free(item);
		/* LCOV_EXCL_STOP */
	}

	pthread_mutex_unlock(&queue->qLock);

	return add_status;
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

	request_queue_unlink_item(request);
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
 * @brief Allocate and initialize a new `request_queue` with empty queues and a hash index sized from the queue limits.
 *
 * Initializes `qLock`, `qCond`, the circular doubly-linked pending and render list sentinel heads,
 * zeroed statistics counters, and the hash table.
 *
 * @param request_limit Maximum number of requests in each time-critical queue (must be at least 1).
 * @param dirty_limit   Maximum number of requests in the dirty queue (must not be negative).
 * @return Newly allocated `struct request_queue`, or `NULL` if the limits are invalid or memory or pthread initialization fails.
 */
struct request_queue *request_queue_init_with_limits(int request_limit, int dirty_limit)

{
	/* Return code from pthread synchronization primitive initialization */
	int res;
	/* Newly allocated request_queue instance */
	struct request_queue *queue = (struct request_queue *)calloc(1, sizeof(struct request_queue));

	if (queue == NULL) {
		return NULL;
	}

	if (request_limit < 1 || dirty_limit < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "Invalid request queue limits: request_queue_limit=%i dirty_queue_limit=%i", request_limit, dirty_limit);
		free(queue);
		return NULL;
	}

	queue->requestLimit = request_limit;
	queue->dirtyLimit = dirty_limit;

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
	queue->pendingHead.next = queue->pendingHead.prev = &(queue->pendingHead);
	queue->renderHead.next = queue->renderHead.prev = &(queue->renderHead);

	for (int i = 0; i < PENDING_QUEUE_RANKS; i++) {
		queue->pendingTail[i] = &(queue->pendingHead);
	}

	queue->hashidxSize = request_queue_hash_size(request_limit, dirty_limit);
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
 * @brief Allocate and initialize a new `request_queue` with the default queue limits (`REQ_LIMIT`, `DIRTY_LIMIT`).
 *
 * @return Newly allocated `struct request_queue`, or `NULL` if memory or pthread initialization fails.
 */
struct request_queue *request_queue_init()
{
	return request_queue_init_with_limits(REQ_LIMIT, DIRTY_LIMIT);
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
