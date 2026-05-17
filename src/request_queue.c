/*
 * Copyright (c) 2007 - 2023 by mod_tile contributors (see AUTHORS file)
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>

#include "render_config.h"
#include "request_queue.h"
#include "g_logger.h"

#define MIN_HASHIDX_SIZE 2213

static int request_queue_hash_size(int request_limit, int dirty_limit)
{
	/*
	 * Keep the de-duplication index roughly proportional to the configured
	 * queue capacity. Large dirty queues are useful only if duplicate lookup
	 * remains cheap while the queue is backlogged.
	 */
	size_t queue_capacity = (4 * (size_t) request_limit) + (size_t) dirty_limit;

	if (queue_capacity > INT_MAX) {
		return INT_MAX;
	}

	return MAX(MIN_HASHIDX_SIZE, (int) queue_capacity);
}

static uint64_t string_hash(const char *value, size_t max_len)
{
	uint64_t hash = 0;

	for (size_t i = 0; (i < max_len) && (value[i] != 0); i++) {
		hash = (hash * 33) + (unsigned char)value[i];
	}

	return hash;
}

static int same_tile_request(struct item *item, struct item *test)
{
	return (item->mx == test->mx) &&
	       (item->my == test->my) &&
	       (item->req.z == test->req.z) &&
	       (strncmp(item->req.xmlname, test->req.xmlname, sizeof(item->req.xmlname)) == 0) &&
	       (strncmp(item->req.mimetype, test->req.mimetype, sizeof(item->req.mimetype)) == 0) &&
	       (strncmp(item->req.options, test->req.options, sizeof(item->req.options)) == 0);
}

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
	return key % queue->hashidxSize;
}

static struct item * lookup_item_idx(struct request_queue * queue, struct item * item)
{
	struct item_idx * nextItem;
	struct item * test;

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

static void insert_item_idx(struct request_queue * queue, struct item *item)
{
	struct item_idx * nextItem;
	struct item_idx * prevItem;

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

static void remove_item_idx(struct request_queue * queue, struct item * item)
{
	int key = calcHashKey(queue, item);
	struct item_idx * nextItem;
	struct item_idx * prevItem;
	struct item * test;

	if (queue->item_hashidx[key].item == NULL) {
		//item not in index;
		return;
	}

	prevItem = &(queue->item_hashidx[key]);
	nextItem = &(queue->item_hashidx[key]);

	while (nextItem != NULL) {
		test = nextItem->item;

		if (same_tile_request(item, test)) {
			/*
			 * Found item, removing it from list
			 */
			nextItem->item = NULL;

			if (nextItem->next != NULL) {
				if (nextItem == &(queue->item_hashidx[key])) {
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

static int request_queue_limit(struct request_queue *queue, enum queueEnum queue_type)
{
	return (queue_type == queueDirty) ? queue->dirtyLimit : queue->requestLimit;
}

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

static void request_queue_attach_duplicate(struct item *item, struct item *duplicate)
{
	duplicate->duplicates = item->duplicates;
	item->duplicates = duplicate;
	duplicate->inQueue = queueDuplicate;
}

static void request_queue_unlink_item(struct item *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}

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

static enum protoCmd pending(struct request_queue * queue, struct item *test)
{
	// check all queues and render list to see if this request already queued
	// If so, add this new request as a duplicate
	// call with qLock held
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

struct item *request_queue_fetch_request(struct request_queue * queue)
{
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

		//Add item to render queue
		item->prev = &(queue->renderHead);
		item->next = queue->renderHead.next;
		queue->renderHead.next->prev = item;
		queue->renderHead.next = item;
		item->inQueue = queueRender;
	}

	pthread_mutex_unlock(&queue->qLock);

	return item;
}

/* If a fd becomes invalid for returning request information, remove it from all
 * requests to not send feedback to invalid FDs
 */
void request_queue_clear_requests_by_fd(struct request_queue * queue, int fd)
{
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

enum protoCmd request_queue_add_request(struct request_queue * queue, struct item *item)
{
	enum protoCmd status;
	const struct protocol *req;
	enum protoCmd add_status = cmdIgnore;
	int item_added = 0;
	req = &(item->req);

	if (queue == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "queue os NULL");
		exit(3);
	}

	pthread_mutex_lock(&(queue->qLock));

	// Check for a matching request in the current rendering or dirty queues
	status = pending(queue, item);

	if (status == cmdNotDone) {
		// We found a match in the dirty queue, can not wait for it
		pthread_mutex_unlock(&(queue->qLock));
		free(item);
		return cmdNotDone;
	}

	if (status == cmdIgnore) {
		// Found a match in render queue, item added as duplicate
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
		// The queue is severely backlogged. Drop request
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
		free(item);
	}

	pthread_mutex_unlock(&queue->qLock);

	return add_status;
}

void request_queue_remove_request(struct request_queue * queue, struct item * request, int render_time)
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

int request_queue_no_requests_queued(struct request_queue * queue, enum protoCmd priority)
{
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

void request_queue_copy_stats(struct request_queue * queue, stats_struct * stats)
{
	pthread_mutex_lock(&(queue->qLock));
	memcpy(stats, &(queue->stats), sizeof(stats_struct));
	pthread_mutex_unlock(&queue->qLock);
}

struct request_queue * request_queue_init_with_limits(int request_limit, int dirty_limit)
{
	int res;
	struct request_queue * queue = calloc(1, sizeof(struct request_queue));

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

	queue->pendingHead.next = queue->pendingHead.prev = &(queue->pendingHead);
	queue->renderHead.next = queue->renderHead.prev = &(queue->renderHead);

	for (int i = 0; i < PENDING_QUEUE_RANKS; i++) {
		queue->pendingTail[i] = &(queue->pendingHead);
	}

	queue->hashidxSize = request_queue_hash_size(request_limit, dirty_limit);
	queue->item_hashidx = (struct item_idx *) malloc(sizeof(struct item_idx) * queue->hashidxSize);

	if (queue->item_hashidx == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to initialise request queue index with %i entries", queue->hashidxSize);
		pthread_mutex_destroy(&(queue->qLock));
		free(queue);
		return NULL;
	}

	bzero(queue->item_hashidx, sizeof(struct item_idx) * queue->hashidxSize);

	return queue;
}

struct request_queue * request_queue_init()
{
	return request_queue_init_with_limits(DEFAULT_REQUEST_QUEUE_LIMIT, DEFAULT_DIRTY_QUEUE_LIMIT);
}

void request_queue_close(struct request_queue * queue)
{
	//TODO: Free items if the queues are not empty at closing time
	pthread_mutex_destroy(&(queue->qLock));
	free(queue->item_hashidx);
	free(queue);
}
