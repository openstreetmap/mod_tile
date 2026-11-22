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

#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include "gen_tile.h"
#include "render_config.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Prime bucket count for the hash table indexing queued and in-flight metatile render requests. */
#define HASHIDX_SIZE 2213
#define PENDING_QUEUE_RANKS 5

	/**
	 * @brief Cumulative render request counts and elapsed render durations tracked by `request_queue`.
	 */
	typedef struct {
		/** @brief Total number of completed background dirty-tile re-render jobs (`cmdDirty`). */
		long noDirtyRender;
		/** @brief Total number of completed standard interactive render jobs (`cmdRender`). */
		long noReqRender;
		/** @brief Total number of completed high-priority interactive render jobs (`cmdRenderPrio`). */
		long noReqPrioRender;
		/** @brief Total number of completed low-priority interactive render jobs (`cmdRenderLow`). */
		long noReqLowRender;
		/** @brief Total number of completed bulk/batch render jobs (`cmdRenderBulk`). */
		long noReqBulkRender;
		/** @brief Total number of incoming requests dropped due to full queues or shutdown. */
		long noReqDroped;
		/** @brief Per-zoom-level count of completed metatile renders (`0` through `MAX_ZOOM`). */
		long noZoomRender[MAX_ZOOM + 1];
		/** @brief Cumulative render duration in milliseconds for `cmdRender` jobs. */
		long timeReqRender;
		/** @brief Cumulative render duration in milliseconds for `cmdRenderPrio` jobs. */
		long timeReqPrioRender;
		/** @brief Cumulative render duration in milliseconds for `cmdRenderLow` jobs. */
		long timeReqLowRender;
		/** @brief Cumulative render duration in milliseconds for `cmdRenderBulk` jobs. */
		long timeReqBulkRender;
		/** @brief Cumulative render duration in milliseconds for `cmdDirty` jobs. */
		long timeReqDirty;
		/** @brief Per-zoom-level cumulative render duration in milliseconds (`0` through `MAX_ZOOM`). */
		long timeZoomRender[MAX_ZOOM + 1];
	} stats_struct;

	/**
	 * @brief Hash bucket chain node mapping a metatile hash key to an active or queued `struct item`.
	 */
	struct item_idx {
		/** @brief Next hash collision entry in the bucket chain (`NULL` at end of chain). */
		struct item_idx *next;
		/** @brief Pointer to the indexed metatile render request item. */
		struct item *item;
	};

	/**
	 * @brief Thread-safe multi-priority metatile render queue with hash-based duplicate coalescing.
	 */
	struct request_queue {
		/** @brief Maximum number of requests kept in the dirty queue. */
		int dirtyLimit;
		/** @brief Number of buckets allocated in `item_hashidx`, sized from the queue limits (at least `HASHIDX_SIZE`). */
		int hashidxSize;
		/** @brief Maximum number of requests kept in each of the standard, high-prio, low-prio, and bulk queues. */
		int requestLimit;
		/** @brief Sentinel head of the single pending list ordered by queue rank, and of the active-render list. */
		struct item pendingHead, renderHead;
		/** @brief Last pending item of each queue rank (`PENDING_QUEUE_RANKS`), or `&pendingHead` when the rank is empty. */
		struct item *pendingTail[PENDING_QUEUE_RANKS];
		/** @brief Array of `hashidxSize` bucket heads indexing all queued and in-flight requests by `(xmlname, mimetype, options, mx, my, z)`. */
		struct item_idx *item_hashidx;
		/** @brief Current item counts in the standard, high-prio, low-prio, bulk, and dirty queues. */
		int reqNum, reqPrioNum, reqLowNum, reqBulkNum, dirtyNum;
		/** @brief Mutex protecting all queue lists, hash buckets, counters, and `stats`. */
		pthread_mutex_t qLock;
		/** @brief Condition variable signaled when new work is enqueued to wake idle render worker threads. */
		pthread_cond_t qCond;
		/** @brief Cumulative throughput and latency counters for requests processed by this queue. */
		stats_struct stats;
	};

	struct request_queue *request_queue_init();
	struct request_queue *request_queue_init_with_limits(int request_limit, int dirty_limit);
	void request_queue_close(struct request_queue *queue);

	struct item *request_queue_fetch_request(struct request_queue *queue);
	enum protoCmd request_queue_add_request(struct request_queue *queue, struct item *request);

	void request_queue_remove_request(struct request_queue *queue, struct item *request, int render_time);
	void request_queue_clear_requests_by_fd(struct request_queue *queue, int fd);

	int request_queue_no_requests_queued(struct request_queue *queue, enum protoCmd);
	void request_queue_copy_stats(struct request_queue *queue, stats_struct *stats);

#ifdef __cplusplus
}
#endif

#endif /* REQUEST_QUEUE_H */
