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

#include <cstring>
#include <sys/syscall.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "protocol.h"
#include "render_config.h"
#include "request_queue.h"

#define NO_WORKER_THREADS 10
#define ITEMS_PER_THREAD 15

static int calcHashKey(struct request_queue *queue, struct item *item);
static struct item *lookup_item_idx(struct request_queue *queue, struct item *item);
static void insert_item_idx(struct request_queue *queue, struct item *item);
static void remove_item_idx(struct request_queue *queue, struct item *item);
static enum protoCmd pending(struct request_queue *queue, struct item *test);

// Helper function to test asynchronous thread fetching
static void *async_fetch_thread(void *arg)
{
	struct request_queue *queue = static_cast<struct request_queue *>(arg);
	struct item *fetched = request_queue_fetch_request(queue);

	if (fetched) {
		request_queue_remove_request(queue, fetched, 0);
		free(fetched);
	}

	return NULL;
}

// Helper function to create initialized test items safely
static struct item *create_test_item(enum protoCmd cmd, const char *xmlname, int x, uint64_t y, int z, int fd)
{
	struct item *item = static_cast<struct item *>(calloc(1, sizeof(struct item)));
	REQUIRE(item != NULL);

	item->req.ver = PROTO_VER;
	item->req.cmd = cmd;
	if (xmlname) {
		strncpy(item->req.xmlname, xmlname, sizeof(item->req.xmlname) - 1);
	} else {
		strncpy(item->req.xmlname, XMLCONFIG_DEFAULT, sizeof(item->req.xmlname) - 1);
	}
	item->req.x = x;
	item->req.y = y;
	item->req.z = z;

	item->mx = x;
	item->my = y;
	item->fd = fd;
	item->duplicates = NULL;

	return item;
}

// Helper function for multi-threaded consumer operations
static void *consumer_thread(void *arg)
{
	struct request_queue *queue = static_cast<struct request_queue *>(arg);

	for (int i = 0; i < ITEMS_PER_THREAD; i++) {
		struct item *item = request_queue_fetch_request(queue);
		if (item) {
			request_queue_remove_request(queue, item, 5);
			free(item);
		}
	}
	return NULL;
}

// Helper function for multi-threaded producer operations
static void *producer_thread(void *arg)
{
	struct request_queue *queue = static_cast<struct request_queue *>(arg);
	uint64_t tid;

#ifdef __MACH__
	pthread_threadid_np(NULL, &tid);
#elif defined(__FreeBSD__)
	tid = reinterpret_cast<uint64_t>(pthread_self());
#else
	tid = syscall(SYS_gettid);
#endif

	for (int i = 0; i < ITEMS_PER_THREAD; i++) {
		struct item *item = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, i, tid, 10, FD_INVALID);
		request_queue_add_request(queue, item);
	}
	return NULL;
}

TEST_CASE("request_queue.c", "[request_queue]")
{
	err_log_lines.clear();

	SECTION("request_queue_init function")
	{
		SECTION("Initialization creates fully zeroed structure and valid queues")
		{
			struct request_queue *queue = request_queue_init();

			REQUIRE(queue != NULL);
			REQUIRE(queue->item_hashidx != NULL);
			REQUIRE(queue->hashidxSize == HASHIDX_SIZE);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, static_cast<enum protoCmd>(999)) == -1);

			stats_struct stats;
			memset(&stats, 0xFF, sizeof(stats_struct));
			request_queue_copy_stats(queue, &stats);

			REQUIRE(stats.noDirtyRender == 0);
			REQUIRE(stats.noReqDroped == 0);
			REQUIRE(stats.noReqRender == 0);
			REQUIRE(stats.noReqPrioRender == 0);
			REQUIRE(stats.noReqLowRender == 0);
			REQUIRE(stats.noReqBulkRender == 0);

			request_queue_close(queue);
		}

		SECTION("Handles calloc failure")
		{
			fail_next_calloc = true;

			struct request_queue *queue = request_queue_init();

			REQUIRE(queue == NULL);
			REQUIRE(err_log_lines == "");
		}

		SECTION("Handles pthread_mutex_init failure")
		{
			fail_next_pthread_mutex_init = true;

			struct request_queue *queue = request_queue_init();

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to create mutex for request_queue"));

			REQUIRE(queue == NULL);
		}

		SECTION("Handles pthread_cond_init failure")
		{
			fail_next_pthread_cond_init = true;

			struct request_queue *queue = request_queue_init();

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to create condition variable for request_queue"));

			REQUIRE(queue == NULL);
		}

		SECTION("Handles malloc failure for hash index")
		{
			fail_next_malloc = true;

			struct request_queue *queue = request_queue_init();

			REQUIRE(queue == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to allocate memory for request queue hash index"));
		}
	}

	struct request_queue *queue = request_queue_init();
	REQUIRE(queue != NULL);

	SECTION("calcHashKey function")
	{
		SECTION("Calculates key correctly across coordinate bounds")
		{
			struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 100, 200, 10, 1);

			int key = calcHashKey(queue, item);
			REQUIRE(key >= 0);
			REQUIRE(key < queue->hashidxSize);

			free(item);
		}
	}

	SECTION("insert_item_idx, lookup_item_idx & remove_item_idx functions")
	{
		SECTION("Returns NULL when item is not in index and finds item when present")
		{
			struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 100, 200, 10, 1);

			REQUIRE(lookup_item_idx(queue, item) == NULL);

			insert_item_idx(queue, item);
			REQUIRE(lookup_item_idx(queue, item) == item);

			remove_item_idx(queue, item);
			REQUIRE(lookup_item_idx(queue, item) == NULL);

			free(item);
		}
	}

	SECTION("pending function")
	{
		SECTION("Identifies new, duplicate render, and duplicate dirty items")
		{
			struct item *itemNorm = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 10, 10, 10, 1);
			struct item *dupNorm = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 10, 10, 10, 2);

			REQUIRE(pending(queue, itemNorm) == cmdRender);

			request_queue_add_request(queue, itemNorm);

			REQUIRE(pending(queue, dupNorm) == cmdIgnore);

			struct item *fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 0);
			free(itemNorm);
			free(dupNorm);
		}
	}

	SECTION("request_queue_add_request function")
	{
		SECTION("Single request addition across priorities")
		{
			struct item *itemPrio = create_test_item(cmdRenderPrio, XMLCONFIG_DEFAULT, 1, 1, 10, 101);
			struct item *itemNorm = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 2, 2, 10, 102);
			struct item *itemLow = create_test_item(cmdRenderLow, XMLCONFIG_DEFAULT, 3, 3, 10, 103);
			struct item *itemBulk = create_test_item(cmdRenderBulk, XMLCONFIG_DEFAULT, 4, 4, 10, 104);
			struct item *itemDirty = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, 5, 5, 10, 105);

			REQUIRE(request_queue_add_request(queue, itemNorm) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);

			REQUIRE(request_queue_add_request(queue, itemPrio) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);

			REQUIRE(request_queue_add_request(queue, itemLow) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1);

			REQUIRE(request_queue_add_request(queue, itemBulk) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1);

			REQUIRE(request_queue_add_request(queue, itemDirty) == cmdNotDone);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1);
			REQUIRE(itemDirty->fd == FD_INVALID);

			struct item *fetched;
			fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 10);
			fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 20);
			fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 30);
			fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 40);
			fetched = request_queue_fetch_request(queue);
			request_queue_remove_request(queue, fetched, 50);

			free(itemPrio);
			free(itemNorm);
			free(itemLow);
			free(itemDirty);
			free(itemBulk);
		}

		SECTION("Exact matching items form a duplicate chain on render queues")
		{
			struct item *original = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 100, 200, 12, 10);
			struct item *duplicate1 = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 100, 200, 12, 11);
			struct item *duplicate2 = create_test_item(cmdRenderLow, XMLCONFIG_DEFAULT, 100, 200, 12, 12);

			REQUIRE(request_queue_add_request(queue, original) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);

			REQUIRE(request_queue_add_request(queue, duplicate1) == cmdIgnore);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
			REQUIRE(duplicate1->inQueue == queueDuplicate);
			REQUIRE(original->duplicates == duplicate1);

			REQUIRE(request_queue_add_request(queue, duplicate2) == cmdIgnore);
			REQUIRE(duplicate2->duplicates == duplicate1);
			REQUIRE(original->duplicates == duplicate2);

			struct item *fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == original);

			request_queue_remove_request(queue, fetched, 0);

			free(original);
			free(duplicate1);
			free(duplicate2);
		}

		SECTION("Dirty queue pending requests reject duplicates with cmdNotDone")
		{
			struct item *dirtyItem = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, 300, 400, 8, FD_INVALID);
			struct item *dupDirty = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 300, 400, 8, 20);

			REQUIRE(request_queue_add_request(queue, dirtyItem) == cmdNotDone);

			REQUIRE(request_queue_add_request(queue, dupDirty) == cmdNotDone);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1);

			struct item *fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == dirtyItem);
			request_queue_remove_request(queue, fetched, 0);

			free(dirtyItem);
		}

		SECTION("Requests overflow from render queues to dirty queue when REQ_LIMIT reached")
		{
			for (int i = 0; i < REQ_LIMIT; i++) {
				struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, i, 0, 5, 1);
				REQUIRE(request_queue_add_request(queue, item) == cmdIgnore);
			}
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == REQ_LIMIT);

			struct item *overflowItem = create_test_item(cmdRender, XMLCONFIG_DEFAULT, REQ_LIMIT + 1, 0, 5, 2);
			REQUIRE(request_queue_add_request(queue, overflowItem) == cmdNotDone);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == REQ_LIMIT);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1);

			for (int i = 0; i < REQ_LIMIT + 1; i++) {
				struct item *fetched = request_queue_fetch_request(queue);
				REQUIRE(fetched != NULL);
				request_queue_remove_request(queue, fetched, 0);
				free(fetched);
			}
		}

		SECTION("Requests are dropped when DIRTY_LIMIT reached")
		{
			for (int i = 0; i < DIRTY_LIMIT; i++) {
				struct item *item = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, i, 100, 5, FD_INVALID);
				REQUIRE(request_queue_add_request(queue, item) == cmdNotDone);
			}
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == DIRTY_LIMIT);

			struct item *droppedItem = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, DIRTY_LIMIT + 1, 100, 5, FD_INVALID);
			REQUIRE(request_queue_add_request(queue, droppedItem) == cmdNotDone);

			stats_struct stats;
			request_queue_copy_stats(queue, &stats);
			REQUIRE(stats.noReqDroped == 1);

			for (int i = 0; i < DIRTY_LIMIT; i++) {
				struct item *fetched = request_queue_fetch_request(queue);
				REQUIRE(fetched != NULL);
				request_queue_remove_request(queue, fetched, 0);
				free(fetched);
			}
		}

		SECTION("Multiple items colliding in the same hash bucket are chained correctly")
		{
			struct item *itemA = create_test_item(cmdRender, "map_a", 10, 10, 5, 1);
			struct item *itemB = create_test_item(cmdRender, "map_b", 10, 10, 5, 2);

			request_queue_add_request(queue, itemA);
			request_queue_add_request(queue, itemB);

			struct item *fetchedA = request_queue_fetch_request(queue);
			struct item *fetchedB = request_queue_fetch_request(queue);

			REQUIRE(fetchedA != NULL);
			REQUIRE(fetchedB != NULL);

			request_queue_remove_request(queue, fetchedA, 0);
			request_queue_remove_request(queue, fetchedB, 0);

			free(itemA);
			free(itemB);
		}

		SECTION("Handles NULL queue", "should exit 3")
		{
			struct item *test_item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 1, 1, 1, 1);

			if (setjmp(exit_jump) == 0) {
				request_queue_add_request(NULL, test_item);
				FAIL("request_queue_add_request should have called exit(3) but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to NULL queue");
			}

			REQUIRE(exit_status == 3);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("queue is NULL"));
			free(test_item);
		}
	}

	SECTION("request_queue_fetch_request function")
	{
		SECTION("Fetches items following strict priority ordering")
		{
			struct item *itemPrio = create_test_item(cmdRenderPrio, XMLCONFIG_DEFAULT, 1, 1, 10, 101);
			struct item *itemNorm = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 2, 2, 10, 102);
			struct item *itemLow = create_test_item(cmdRenderLow, XMLCONFIG_DEFAULT, 3, 3, 10, 103);
			struct item *itemBulk = create_test_item(cmdRenderBulk, XMLCONFIG_DEFAULT, 4, 4, 10, 104);
			struct item *itemDirty = create_test_item(cmdDirty, XMLCONFIG_DEFAULT, 5, 5, 10, 105);

			request_queue_add_request(queue, itemNorm);
			request_queue_add_request(queue, itemPrio);
			request_queue_add_request(queue, itemLow);
			request_queue_add_request(queue, itemBulk);
			request_queue_add_request(queue, itemDirty);

			struct item *fetched;
			fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemPrio);
			fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemNorm);
			fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemLow);
			fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemDirty);
			fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemBulk);

			request_queue_remove_request(queue, itemPrio, 0);
			request_queue_remove_request(queue, itemNorm, 0);
			request_queue_remove_request(queue, itemLow, 0);
			request_queue_remove_request(queue, itemDirty, 0);
			request_queue_remove_request(queue, itemBulk, 0);

			free(itemPrio);
			free(itemNorm);
			free(itemLow);
			free(itemDirty);
			free(itemBulk);
		}

		SECTION("Concurrent enqueuing and dequeuing maintains state integrity")
		{
			pthread_t producers[NO_WORKER_THREADS];
			pthread_t consumers[NO_WORKER_THREADS];

			for (int i = 0; i < NO_WORKER_THREADS; i++) {
				pthread_create(&producers[i], NULL, producer_thread, queue);
				pthread_create(&consumers[i], NULL, consumer_thread, queue);
			}

			for (int i = 0; i < NO_WORKER_THREADS; i++) {
				pthread_join(producers[i], NULL);
				pthread_join(consumers[i], NULL);
			}

			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

			stats_struct stats;
			request_queue_copy_stats(queue, &stats);
			REQUIRE(stats.noDirtyRender == (NO_WORKER_THREADS * ITEMS_PER_THREAD));
		}

		SECTION("Blocks on condition variable when empty")
		{
			pthread_t thread;
			pthread_create(&thread, NULL, async_fetch_thread, queue);

			usleep(10000);

			struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 1, 1, 1, 1);
			request_queue_add_request(queue, item);

			pthread_join(thread, NULL);
		}
	}

	SECTION("request_queue_remove_request function")
	{
		SECTION("Stat updates during request removal across queues")
		{
			struct item *itemPrio = create_test_item(cmdRenderPrio, XMLCONFIG_DEFAULT, 10, 10, 5, 1);
			request_queue_add_request(queue, itemPrio);

			struct item *fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == itemPrio);

			request_queue_remove_request(queue, fetched, 150);

			stats_struct stats;
			request_queue_copy_stats(queue, &stats);
			REQUIRE(stats.noReqPrioRender == 1);
			REQUIRE(stats.timeReqPrioRender == 150);
			REQUIRE(stats.noZoomRender[5] == 1);
			REQUIRE(stats.timeZoomRender[5] == 150);

			free(itemPrio);
		}

		SECTION("Logs warning when item is not in queueRender", "should execute warning path")
		{
			struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 1, 1, 1, 1);
			request_queue_add_request(queue, item);

			request_queue_remove_request(queue, item, 0);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Removing request from queue, even though not on rendering queue"));

			free(item);
		}

		SECTION("Switch default for unknown originatedQueue", "should hit default switch case")
		{
			struct item *item = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 2, 2, 2, 1);
			request_queue_add_request(queue, item);

			struct item *fetched = request_queue_fetch_request(queue);
			REQUIRE(fetched == item);

			fetched->originatedQueue = static_cast<decltype(fetched->originatedQueue)>(999);
			request_queue_remove_request(queue, fetched, 50);

			free(item);
		}
	}

	SECTION("remove_item_idx function")
	{
		SECTION("Handles empty hash bucket lookup", "should return safely when bucket is empty")
		{
			struct item *phantom = create_test_item(cmdRender, "phantom_map", 9999, 9999, 1, 1);
			phantom->inQueue = queueRender;

			phantom->next = phantom;
			phantom->prev = phantom;

			request_queue_remove_request(queue, phantom, 0);

			free(phantom);
		}

		SECTION("Handles removing non-head middle and tail nodes in collision chain")
		{
			struct item *itemA = create_test_item(cmdRender, "ab", 100, 100, 10, 1);
			struct item *itemB = create_test_item(cmdRenderLow, "ba", 100, 100, 10, 2);
			struct item *itemC = create_test_item(cmdRenderBulk, "c`", 100, 100, 10, 3);

			itemA->next = itemA;
			itemA->prev = itemA;
			itemA->inQueue = queueRender;
			itemB->next = itemB;
			itemB->prev = itemB;
			itemB->inQueue = queueRender;
			itemC->next = itemC;
			itemC->prev = itemC;
			itemC->inQueue = queueRender;

			insert_item_idx(queue, itemA);
			insert_item_idx(queue, itemB);
			insert_item_idx(queue, itemC);

			request_queue_remove_request(queue, itemB, 0);
			request_queue_remove_request(queue, itemC, 0);
			request_queue_remove_request(queue, itemA, 0);

			free(itemA);
			free(itemB);
			free(itemC);
		}
	}

	SECTION("request_queue_clear_requests_by_fd function")
	{
		int target_fd = 42;
		int safe_fd = 99;

		struct item *item1 = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 1, 1, 10, target_fd);
		struct item *dup1 = create_test_item(cmdRender, XMLCONFIG_DEFAULT, 1, 1, 10, target_fd);

		struct item *item2 = create_test_item(cmdRenderPrio, XMLCONFIG_DEFAULT, 2, 2, 10, target_fd);
		struct item *item3 = create_test_item(cmdRenderBulk, XMLCONFIG_DEFAULT, 3, 3, 10, safe_fd);

		request_queue_add_request(queue, item1);
		request_queue_add_request(queue, dup1);
		request_queue_add_request(queue, item2);
		request_queue_add_request(queue, item3);

		SECTION("Invalidate target FD across primary requests and duplicate chains")
		{
			request_queue_clear_requests_by_fd(queue, target_fd);

			REQUIRE(item1->fd == FD_INVALID);
			REQUIRE(dup1->fd == FD_INVALID);
			REQUIRE(item2->fd == FD_INVALID);
			REQUIRE(item3->fd == safe_fd);

			for (int i = 0; i < 3; i++) {
				struct item *fetched = request_queue_fetch_request(queue);
				request_queue_remove_request(queue, fetched, 0);
				free(fetched);
			}
			free(dup1);
		}
	}

	SECTION("request_queue_no_requests_queued function")
	{
		SECTION("Accurately counts queued items per priority", "returns -1 for invalid cmd")
		{
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
			REQUIRE(request_queue_no_requests_queued(queue, static_cast<enum protoCmd>(999)) == -1);
		}
	}

	SECTION("request_queue_copy_stats function")
	{
		SECTION("Copies accurate runtime stats into target struct")
		{
			stats_struct stats;
			memset(&stats, 0xFF, sizeof(stats_struct));
			request_queue_copy_stats(queue, &stats);

			REQUIRE(stats.noDirtyRender == 0);
			REQUIRE(stats.noReqDroped == 0);
			REQUIRE(stats.noReqRender == 0);
			REQUIRE(stats.noReqPrioRender == 0);
			REQUIRE(stats.noReqLowRender == 0);
			REQUIRE(stats.noReqBulkRender == 0);
		}
	}

	request_queue_close(queue);
}
