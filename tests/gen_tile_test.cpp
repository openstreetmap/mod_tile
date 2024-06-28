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

// https://github.com/catchorg/Catch2/blob/v2.13.9/docs/own-main.md#let-catch2-take-full-control-of-args-and-config
#define CATCH_CONFIG_RUNNER

#include <cstdio>
#include <glib.h>
#include <mapnik/version.hpp>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <time.h>
#include <tuple>
#include <unistd.h>

#if MAPNIK_MAJOR_VERSION >= 4
#include <mapnik/geometry/box2d.hpp>
#else
#include <mapnik/box2d.hpp>
#endif

#include "catch/catch.hpp"
#include "catch_test_common.hpp"
#include "config.h"
#include "g_logger.h"
#include "gen_tile.h"
#include "metatile.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"
#include "renderd.h"
#include "request_queue.h"
#include "store.h"

#define NO_QUEUE_REQUESTS 9
#define NO_TEST_REPEATS 100
#define NO_THREADS 100

extern struct projectionconfig *get_projection(const char *srs);
extern mapnik::box2d<double> tile2prjbounds(struct projectionconfig *prj, int x, int y, int z);

// mutex to guard access to the shared render request counter
static pthread_mutex_t item_counter_lock;

struct item *init_render_request(enum protoCmd type)
{
	static int counter;
	struct item *item = (struct item *)malloc(sizeof(struct item));
	bzero(item, sizeof(struct item));
	item->req.ver = PROTO_VER;
	strcpy(item->req.xmlname, "default");
	item->req.cmd = type;
	pthread_mutex_lock(&item_counter_lock);
	item->mx = counter++;
	pthread_mutex_unlock(&item_counter_lock);
	return item;
}

void *addition_thread(void *arg)
{
	struct request_queue *queue = (struct request_queue *)arg;
	struct item *item;
	uint64_t threadid;
#ifdef __MACH__ // Mac OS X does not support SYS_gettid
	pthread_threadid_np(NULL, &threadid);
#elif __FreeBSD__ // FreeBSD does not support SYS_getid either
	threadid = (uint64_t)pthread_self();
#else
	threadid = syscall(SYS_gettid);
#endif

	// Requests need to be unique across threads to avoid being discarded as duplicates,
	// thereby ensuring the queue counts can be compared correctly.
	// To that end, use a thread ID for the Y coordinate, complementing
	// the existing sequence counter used for the X coordinate.
	for (int i = 0; i < NO_QUEUE_REQUESTS; i++) {
		item = init_render_request(cmdDirty);
		item->my = threadid;
		request_queue_add_request(queue, item);
	}

	return NULL;
}

void *fetch_thread(void *arg)
{
	struct request_queue *queue = (struct request_queue *)arg;

	for (int i = 0; i < NO_QUEUE_REQUESTS; i++) {
		request_queue_fetch_request(queue);
	}

	return NULL;
}

std::string create_tile_dir(std::string dir_name = "mod_tile_test", const char *tmp_dir = getenv("TMPDIR"))
{
	if (tmp_dir == NULL) {
		tmp_dir = P_tmpdir;
	}

	std::string tile_dir(tmp_dir);
	tile_dir.append("/").append(dir_name);

	mkdir(tile_dir.c_str(), 0777);

	return tile_dir;
}

int delete_tile_dir(std::string tile_dir)
{
	return rmdir(tile_dir.c_str());
}

TEST_CASE("renderd/queueing", "request queueing")
{
	SECTION("renderd/queueing/initialisation", "test the initialisation of the request queue") {
		request_queue *queue = request_queue_init();
		REQUIRE(queue != NULL);
		request_queue_close(queue);
	}

	SECTION("renderd/queueing/simple request add", "test the addition of a single request") {
		request_queue *queue = request_queue_init();
		struct item *item = init_render_request(cmdRender);

		enum protoCmd res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/simple request add priority", "test the addition of requests with different priorities") {
		struct item *item;
		enum protoCmd res;
		request_queue *queue = request_queue_init();

		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);
		item = init_render_request(cmdRender);
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);
		item = init_render_request(cmdRenderPrio);
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);
		item = init_render_request(cmdRenderLow);
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

		item = init_render_request(cmdRenderBulk);
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

		item = init_render_request(cmdDirty);
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdNotDone);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1);

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/simple request fetch", "test the fetching of a single request") {
		request_queue *queue = request_queue_init();

		struct item *item = init_render_request(cmdRender);
		request_queue_add_request(queue, item);
		struct item *item2 = request_queue_fetch_request(queue);
		REQUIRE(item == item2);
		request_queue_remove_request(queue, item2, 0);
		free(item2);

		item = init_render_request(cmdRenderPrio);
		request_queue_add_request(queue, item);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(item == item2);
		request_queue_remove_request(queue, item2, 0);
		free(item2);

		item = init_render_request(cmdRenderLow);
		request_queue_add_request(queue, item);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(item == item2);
		request_queue_remove_request(queue, item2, 0);
		free(item2);

		item = init_render_request(cmdRenderBulk);
		request_queue_add_request(queue, item);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(item == item2);
		request_queue_remove_request(queue, item2, 0);
		free(item2);

		item = init_render_request(cmdDirty);
		request_queue_add_request(queue, item);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(item == item2);
		request_queue_remove_request(queue, item2, 0);
		free(item2);

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/simple request fetch priority", "test the fetching of requests with different priorities and their ordering") {
		struct item *item2;

		request_queue *queue = request_queue_init();

		struct item *itemR = init_render_request(cmdRender);
		request_queue_add_request(queue, itemR);
		struct item *itemB = init_render_request(cmdRenderBulk);
		request_queue_add_request(queue, itemB);
		struct item *itemD = init_render_request(cmdDirty);
		request_queue_add_request(queue, itemD);
		struct item *itemRP = init_render_request(cmdRenderPrio);
		request_queue_add_request(queue, itemRP);
		struct item *itemL = init_render_request(cmdRenderLow);
		request_queue_add_request(queue, itemL);

		// We should be retrieving items in the order RenderPrio, Render, Dirty, Bulk
		item2 = request_queue_fetch_request(queue);
		INFO("itemRP: " << itemRP);
		INFO("itemR: " << itemR);
		INFO("itemL: " << itemL);
		INFO("itemD: " << itemD);
		INFO("itemB: " << itemB);
		REQUIRE(itemRP == item2);
		request_queue_remove_request(queue, item2, 0);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(itemR == item2);
		request_queue_remove_request(queue, item2, 0);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(itemL == item2);
		request_queue_remove_request(queue, item2, 0);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(itemD == item2);
		request_queue_remove_request(queue, item2, 0);
		item2 = request_queue_fetch_request(queue);
		REQUIRE(itemB == item2);

		free(itemR);
		free(itemB);
		free(itemD);
		free(itemRP);
		free(itemL);

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/pending requests", "test if de-duplication of requests work") {
		enum protoCmd res;
		struct item *item;
		request_queue *queue = request_queue_init();

		// Submitting initial request
		item = init_render_request(cmdRender);
		item->mx = 0;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);

		// Submit duplicate request, check that queue length hasn't increased
		item = init_render_request(cmdRender);
		item->mx = 0;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 1);

		// Submit second request
		item = init_render_request(cmdRender);
		item->mx = 1;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 2);

		// Submit first request to render prio
		item = init_render_request(cmdRenderPrio);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);

		// Submit duplicate to render prio
		item = init_render_request(cmdRenderPrio);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);

		// Submit duplicate to dirty, check that de-duplication works across queues
		item = init_render_request(cmdDirty);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

		// Submit duplicate to request low, check that de-duplication works across queues
		item = init_render_request(cmdRenderLow);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0);

		// Submit duplicate to request low, check that de-duplication works across queues
		item = init_render_request(cmdRender);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		// There were 2 render request submitted earlier in the test, so a
		// number of 2 is the same as before.
		REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 2);

		// Submit duplicate to bulk, check that de-duplication works across queues
		item = init_render_request(cmdRenderBulk);
		item->mx = 2;
		res = request_queue_add_request(queue, item);
		REQUIRE(res == cmdIgnore);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1);
		REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0);

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/overflow requests", "test if requests correctly overflow from one request priority to the next") {
		enum protoCmd res;
		struct item *item;
		request_queue *queue = request_queue_init();

		for (int i = 1; i < (2 * REQ_LIMIT + DIRTY_LIMIT + 2); i++) {
			item = init_render_request(cmdRenderPrio);
			res = request_queue_add_request(queue, item);
			INFO("i: " << i);
			INFO("NoPrio: " << request_queue_no_requests_queued(queue, cmdRenderPrio));
			INFO("NoRend: " << request_queue_no_requests_queued(queue, cmdRender));
			INFO("NoDirt: " << request_queue_no_requests_queued(queue, cmdDirty));
			INFO("NoBulk: " << request_queue_no_requests_queued(queue, cmdRenderBulk));

			if (i <= REQ_LIMIT) {
				REQUIRE(res == cmdIgnore);
				REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == i);
			} else if (i <= (REQ_LIMIT + DIRTY_LIMIT)) {
				// Requests should overflow into the dirty queue
				REQUIRE(res == cmdNotDone);
				REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT);
				REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == (i - REQ_LIMIT));
			} else {
				// Requests should be dropped altogether
				REQUIRE(res == cmdNotDone);
				REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT);
				REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == DIRTY_LIMIT);
			}
		}

		request_queue_close(queue);
	}

	SECTION("renderd/queueing/multithreading request addition", "test if there are any issues with multithreading") {
		pthread_t *addition_threads;
		request_queue *queue;

		REQUIRE((NO_THREADS * NO_QUEUE_REQUESTS) < DIRTY_LIMIT);

		for (int j = 0; j < NO_TEST_REPEATS; j++) { // As we are looking for race conditions, repeat this test many times
			addition_threads = (pthread_t *)calloc(NO_THREADS, sizeof(pthread_t));
			queue = request_queue_init();
			void *status;

			for (int i = 0; i < NO_THREADS; i++) {
				if (pthread_create(&addition_threads[i], NULL, addition_thread,
						   (void *)queue)) {
					INFO("Failed to create thread");
					REQUIRE(1 == 0);
				}
			}

			for (int i = 0; i < NO_THREADS; i++) {
				pthread_join(addition_threads[i], &status);
			}

			INFO("Iteration " << j);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == (NO_THREADS * NO_QUEUE_REQUESTS));

			request_queue_close(queue);
			free(addition_threads);
		}
	}

	SECTION("renderd/queueing/multithreading request fetch", "test if there are any issues with multithreading") {
		pthread_t *fetch_threads;
		struct request_queue *queue;
		struct item *item;

		for (int j = 0; j < NO_TEST_REPEATS; j++) { // As we are looking for race conditions, repeat this test many times
			fetch_threads = (pthread_t *)calloc(NO_THREADS, sizeof(pthread_t));
			queue = request_queue_init();
			void *status;

			for (int i = 0; i < (NO_THREADS * NO_QUEUE_REQUESTS); i++) {
				item = init_render_request(cmdDirty);
				request_queue_add_request(queue, item);
			}

			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == (NO_THREADS * NO_QUEUE_REQUESTS));

			for (int i = 0; i < NO_THREADS; i++) {
				if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
						   (void *)queue)) {
					INFO("Failed to create thread");
					REQUIRE(1 == 0);
				}
			}

			for (int i = 0; i < NO_THREADS; i++) {
				pthread_join(fetch_threads[i], &status);
			}

			INFO("Iteration " << j);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

			request_queue_close(queue);
			free(fetch_threads);
		}
	}

	SECTION("renderd/queueing/multithreading request fetch on empty queue", "test if there are any issues with multithreading") {
		pthread_t *fetch_threads;
		pthread_t *addition_threads;
		struct request_queue *queue;

		for (int j = 0; j < NO_TEST_REPEATS; j++) { // As we are looking for race conditions, repeat this test many times
			fetch_threads = (pthread_t *)calloc(NO_THREADS, sizeof(pthread_t));
			addition_threads = (pthread_t *)calloc(NO_THREADS, sizeof(pthread_t));
			queue = request_queue_init();
			void *status;

			for (int i = 0; i < NO_THREADS; i++) {
				if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
						   (void *)queue)) {
					INFO("Failed to create thread");
					REQUIRE(1 == 0);
				}
			}

			for (int i = 0; i < NO_THREADS; i++) {
				if (pthread_create(&addition_threads[i], NULL, addition_thread,
						   (void *)queue)) {
					INFO("Failed to create thread");
					REQUIRE(1 == 0);
				}
			}

			for (int i = 0; i < NO_THREADS; i++) {
				pthread_join(fetch_threads[i], &status);
				pthread_join(addition_threads[i], &status);
			}

			INFO("Iteration " << j);
			REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0);

			request_queue_close(queue);
			free(fetch_threads);
			free(addition_threads);
		}
	}

	SECTION("renderd/queueing/clear fd", "Test if the clearing of fd work for queues") {
		struct request_queue *queue = request_queue_init();
		struct item *item;

		item = init_render_request(cmdRender);
		item->fd = 1;
		request_queue_add_request(queue, item);
		item = init_render_request(cmdRender);
		item->fd = 2;
		request_queue_add_request(queue, item);
		request_queue_clear_requests_by_fd(queue, 2);
		item = init_render_request(cmdRender);
		item->fd = 3;
		request_queue_add_request(queue, item);
		item = init_render_request(cmdRender);
		item->fd = 4;
		request_queue_add_request(queue, item);
		item = init_render_request(cmdRender);
		item->fd = 5;
		request_queue_add_request(queue, item);
		item = init_render_request(cmdRender);
		item->fd = 6;
		request_queue_add_request(queue, item);
		request_queue_clear_requests_by_fd(queue, 4);

		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == 1);
		request_queue_remove_request(queue, item, 0);
		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == FD_INVALID);
		request_queue_remove_request(queue, item, 0);
		free(item);
		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == 3);
		request_queue_remove_request(queue, item, 0);
		free(item);
		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == FD_INVALID);
		request_queue_remove_request(queue, item, 0);
		free(item);
		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == 5);
		request_queue_remove_request(queue, item, 0);
		free(item);
		item = request_queue_fetch_request(queue);
		REQUIRE(item->fd == 6);
		request_queue_remove_request(queue, item, 0);
		free(item);

		request_queue_close(queue);
	}
}

TEST_CASE("renderd", "tile generation")
{
	int found, ret;
	std::string err_log_lines, out_log_lines;

	SECTION("render_init", "should throw nice error if paths are invalid") {
		start_capture();
		render_init("doesnotexist", "doesnotexist", 1);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = err_log_lines.find("Unable to open font directory: doesnotexist");
		REQUIRE(found > -1);
	}

	SECTION("rx_request/bad", "should return cmdNotDone") {
		int pipefd[2];
		pipe(pipefd);
		struct protocol *req = (struct protocol *)malloc(sizeof(struct protocol));
		std::string expected_mimetype = "image/png", expected_options = "", expected_xmlname = "default";

		req->x = 1024;
		req->y = 1024;
		req->z = 10;
		strcpy(req->mimetype, "mimetype");
		strcpy(req->options, "options");
		strcpy(req->xmlname, "xmlname");

		// Unknown command
		req->cmd = (enum protoCmd)4096;

		// Invalid version
		req->ver = 4;

		start_capture();
		ret = rx_request(req, pipefd[1]);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == cmdNotDone);
		found = err_log_lines.find("Bad protocol version " + std::to_string(req->ver));
		REQUIRE(found > -1);

		// Valid version
		req->ver = 1;

		REQUIRE((std::string)req->mimetype != expected_mimetype);
		REQUIRE((std::string)req->options != expected_options);
		REQUIRE((std::string)req->xmlname != expected_xmlname);

		start_capture();
		ret = rx_request(req, pipefd[1]);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE((std::string)req->mimetype == expected_mimetype);
		REQUIRE((std::string)req->options == expected_options);
		REQUIRE((std::string)req->xmlname == expected_xmlname);

		REQUIRE(ret == cmdNotDone);
		found = err_log_lines.find("Ignoring invalid command Unknown fd(" + std::to_string(pipefd[1]));
		REQUIRE(found > -1);

		// Valid command
		req->cmd = cmdNotDone;

		start_capture();
		ret = rx_request(req, pipefd[1]);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == cmdNotDone);
		found = err_log_lines.find("Ignoring invalid command NotDone fd(" + std::to_string(pipefd[1]));
		REQUIRE(found > -1);

		free(req);
	}

	SECTION("send_response", "should complete") {
		auto rsp = GENERATE(cmdRender, cmdRenderPrio, cmdRenderLow, cmdRenderBulk);

		render_request_queue = request_queue_init();
		struct item *item = init_render_request(rsp);
		request_queue_add_request(render_request_queue, item);

		start_capture(1);
		send_response(item, rsp, -1);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = out_log_lines.find("Sending message Render");
		REQUIRE(found > -1);

		request_queue_close(render_request_queue);
		SUCCEED();
	}
}

TEST_CASE("storage-backend", "Tile storage backend router")
{
	int found;
	std::string err_log_lines, out_log_lines, tile_dir = create_tile_dir();

	SECTION("storage/initialise", "should return NULL") {
		start_capture();

		// empty string
		std::string empty;
		REQUIRE(init_storage_backend(empty.c_str()) == NULL);

		// invalid path
		std::string tile_dir_invalid;
		tile_dir_invalid = tile_dir + "/" + "invalid";
		REQUIRE(init_storage_backend(tile_dir_invalid.c_str()) == NULL);

		// file
		std::string file_name;
		file_name = tile_dir + "/" + "file";
		std::ofstream file{file_name};
		REQUIRE(init_storage_backend(file_name.c_str()) == NULL);
		std::remove(file_name.c_str());

		// non-existent backend
		char non_existent_backend[] = "non-existent backend";
		REQUIRE(init_storage_backend(non_existent_backend) == NULL);

		// Check log output
		std::tie(err_log_lines, out_log_lines) = end_capture();

		// empty string
		found = err_log_lines.find("init_storage_backend: Options string was empty");
		REQUIRE(found > -1);

		// invalid path
		found = err_log_lines.find("init_storage_backend: Failed to stat");
		REQUIRE(found > -1);
		found = err_log_lines.find("No such file or directory");
		REQUIRE(found > -1);

		// file
		found = err_log_lines.find("is not a directory");
		REQUIRE(found > -1);

		// non-existent backend
		found = err_log_lines.find("init_storage_backend: No valid storage backend found for options: non-existent backend");
		REQUIRE(found > -1);
	}

	delete_tile_dir(tile_dir);
}

TEST_CASE("file storage-backend", "File Tile storage backend")
{
	std::string tile_dir = create_tile_dir();
	std::string xmlconfig("default");

	SECTION("storage/initialise", "should return tile_dir storage_ctx") {
		struct storage_backend *store = NULL;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);
		REQUIRE((char *)store->storage_ctx == tile_dir);

		store->close_storage(store);
	}

	SECTION("storage/stat/non existent", "should return 0 size") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 0, 0, 0);
		REQUIRE(sinfo.size < 0);

		store->close_storage(store);
	}

	SECTION("storage/read/non existent", "should return 0 size") {
		struct storage_backend *store = NULL;
		int size;
		char *buf = (char *)malloc(10000);
		int compressed;
		char *err_msg = (char *)malloc(10000);

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		size = store->tile_read(store, xmlconfig.c_str(), "", 0, 0, 0, buf, 10000, &compressed, err_msg);
		REQUIRE(size < 0);

		store->close_storage(store);
		free(buf);
		free(err_msg);
	}

	SECTION("storage/write/full metatile", "should complete") {
		struct storage_backend *store = NULL;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		// Ensure metatile is deleted
		store->metatile_delete(store, xmlconfig.c_str(), 1024, 1024, 10);

		store->close_storage(store);
		SUCCEED();
	}

	SECTION("storage/stat/full metatile", "should complete") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		time_t before_write, after_write;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + METATILE, 1024, 10);
		time(&before_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);
		time(&after_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + METATILE + yy, 1024 + xx, 10);
				REQUIRE(sinfo.size > 0);
				REQUIRE(sinfo.expired == 0);
				REQUIRE(sinfo.atime > 0);
				REQUIRE(sinfo.mtime >= before_write);
				REQUIRE(sinfo.mtime <= before_write);
			}
		}

		// Ensure metatile is deleted
		store->metatile_delete(store, xmlconfig.c_str(), 1024 + METATILE, 1024, 10);

		store->close_storage(store);
		SUCCEED();
	}

	SECTION("storage/read/full metatile", "should complete") {
		struct storage_backend *store = NULL;
		char *buf;
		char *buf_tmp;
		char msg[4096];
		int compressed;
		int tile_size;

		buf = (char *)malloc(8196);
		buf_tmp = (char *)malloc(8196);

		time_t before_write, after_write;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + METATILE, 1024, 10);
		time(&before_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);
		time(&after_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				tile_size = store->tile_read(store, xmlconfig.c_str(), "", 1024 + METATILE + xx, 1024 + yy, 10, buf, 8195, &compressed, msg);
				REQUIRE(tile_size == 12);
				snprintf(buf_tmp, 8196, "DEADBEAF %i %i", xx, yy);
				REQUIRE(memcmp(buf_tmp, buf, 11) == 0);
			}
		}

		// Ensure metatile is deleted
		store->metatile_delete(store, xmlconfig.c_str(), 1024 + METATILE, 1024, 10);

		store->close_storage(store);
		free(buf);
		free(buf_tmp);
		SUCCEED();
	}

	SECTION("storage/read/partial metatile", "should return correct data") {
		struct storage_backend *store = NULL;
		char *buf;
		char *buf_tmp;
		char msg[4096];
		int compressed;
		int tile_size;

		buf = (char *)malloc(8196);
		buf_tmp = (char *)malloc(8196);

		time_t before_write, after_write;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + 2 * METATILE, 1024, 10);
		time(&before_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < (METATILE >> 1); xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);
		time(&after_write);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				tile_size = store->tile_read(store, xmlconfig.c_str(), "", 1024 + 2 * METATILE + xx, 1024 + yy, 10, buf, 8195, &compressed, msg);

				if (xx >= (METATILE >> 1)) {
					REQUIRE(tile_size == 0);
				} else {
					REQUIRE(tile_size == 12);
					snprintf(buf_tmp, 8196, "DEADBEAF %i %i", xx, yy);
					REQUIRE(memcmp(buf_tmp, buf, 11) == 0);
				}
			}
		}

		// Ensure metatile is deleted
		store->metatile_delete(store, xmlconfig.c_str(), 1024 + 2 * METATILE, 1024, 10);

		store->close_storage(store);
		free(buf);
		free(buf_tmp);
	}

	SECTION("storage/expire metatile", "should expire the tile") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);
		REQUIRE(sinfo.size > 0);

		store->metatile_expire(store, xmlconfig.c_str(), 1024 + 3 * METATILE, 1024, 10);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);
		REQUIRE(sinfo.size > 0);
		REQUIRE(sinfo.expired > 0);

		// Ensure metatile is deleted
		store->metatile_delete(store, xmlconfig.c_str(), 1024 + 3 * METATILE, 1024, 10);

		store->close_storage(store);
	}

	SECTION("storage/delete metatile", "should delete the tile") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);
		REQUIRE(sinfo.size > 0);

		store->metatile_delete(store, xmlconfig.c_str(), 1024 + 4 * METATILE, 1024, 10);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);
		REQUIRE(sinfo.size < 0);

		store->close_storage(store);
	}

	SECTION("storage/tile_storage_id", "should return -1") {
		struct storage_backend *store = NULL;
		char *string = (char *)malloc(PATH_MAX - 1);

		store = init_storage_backend(tile_dir.c_str());
		REQUIRE(store != NULL);

		string = store->tile_storage_id(store, xmlconfig.c_str(), "", 0, 0, 0, string);
		REQUIRE((std::string)string == "file://" + tile_dir + "/" + xmlconfig + "/0/0/0/0/0/0.meta");

		store->close_storage(store);
	}

	delete_tile_dir(tile_dir);
}

TEST_CASE("memcached storage-backend", "MemcacheD Tile storage backend")
{
	int found;
	std::string err_log_lines, out_log_lines;
	struct storage_backend *store = NULL;

#ifdef HAVE_LIBMEMCACHED
	SECTION("memcached storage/initialise", "should not return NULL") {
		start_capture(1);
		REQUIRE(init_storage_backend("memcached://") != NULL);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = out_log_lines.find("init_storage_memcached: Creating memcached ctx with options");
		REQUIRE(found > -1);
	}
#else
	SECTION("memcached storage/initialise", "should return NULL") {
		start_capture();
		REQUIRE(init_storage_backend("memcached://") == NULL);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = err_log_lines.find("init_storage_memcached: Support for memcached has not been compiled into this program");
		REQUIRE(found > -1);
	}
#endif
}

TEST_CASE("null storage-backend", "NULL Tile storage backend")
{
	std::string xmlconfig("default");

	SECTION("storage/initialise", "should return NULL storage_ctx") {
		struct storage_backend *store = NULL;

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);
		REQUIRE(store->storage_ctx == NULL);

		store->close_storage(store);
	}

	SECTION("storage/stat", "should return -1") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 0, 0, 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.ctime == 0);
		REQUIRE(sinfo.expired == 1);
		REQUIRE(sinfo.size == -1);

		store->close_storage(store);
	}

	SECTION("storage/read", "should return -1") {
		struct storage_backend *store = NULL;
		int size;
		char *buf = (char *)malloc(10000);
		int compressed;
		char *err_msg = (char *)malloc(10000);

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		size = store->tile_read(store, xmlconfig.c_str(), "", 0, 0, 0, buf, 10000, &compressed, err_msg);
		REQUIRE(size == -1);

		store->close_storage(store);
		free(buf);
		free(err_msg);
	}

	SECTION("storage/write/full metatile", "should complete") {
		struct storage_backend *store = NULL;

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		store->close_storage(store);
		SUCCEED();
	}

	SECTION("storage/expire metatile", "should return 0") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.ctime == 0);
		REQUIRE(sinfo.expired == 1);
		REQUIRE(sinfo.size == -1);

		int expired = store->metatile_expire(store, xmlconfig.c_str(), 1024 + 3 * METATILE, 1024, 10);
		REQUIRE(expired == 0);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 3 * METATILE, 1024, 10);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.ctime == 0);
		REQUIRE(sinfo.expired == 1);
		REQUIRE(sinfo.size == -1);

		store->close_storage(store);
	}

	SECTION("storage/delete metatile", "should return 0") {
		struct storage_backend *store = NULL;
		struct stat_info sinfo;

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		metaTile tiles(xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
			}
		}

		tiles.save(store);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.ctime == 0);
		REQUIRE(sinfo.expired == 1);
		REQUIRE(sinfo.size == -1);

		int deleted = store->metatile_delete(store, xmlconfig.c_str(), 1024 + 4 * METATILE, 1024, 10);
		REQUIRE(deleted == 0);

		sinfo = store->tile_stat(store, xmlconfig.c_str(), "", 1024 + 4 * METATILE, 1024, 10);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.atime == 0);
		REQUIRE(sinfo.ctime == 0);
		REQUIRE(sinfo.expired == 1);
		REQUIRE(sinfo.size == -1);

		store->close_storage(store);
	}

	SECTION("storage/tile_storage_id", "should return -1") {
		struct storage_backend *store = NULL;
		char *string = (char *)malloc(PATH_MAX - 1);

		store = init_storage_backend("null://");
		REQUIRE(store != NULL);

		string = store->tile_storage_id(store, xmlconfig.c_str(), "", 0, 0, 0, string);
		REQUIRE((std::string)string == "null://");

		store->close_storage(store);
	}
}

TEST_CASE("rados storage-backend", "RADOS Tile storage backend")
{
	SECTION("storage/initialise", "should return NULL") {
		int found;
		std::string err_log_lines, out_log_lines;
		struct storage_backend *store = NULL;

		start_capture();
		REQUIRE(init_storage_backend("rados://") == NULL);
		std::tie(err_log_lines, out_log_lines) = end_capture();

#ifdef HAVE_LIBRADOS
		found = err_log_lines.find("init_storage_rados: failed to read rados config file");
#else
		found = err_log_lines.find("init_storage_rados: Support for rados has not been compiled into this program");
#endif
		REQUIRE(found > -1);
	}
}

TEST_CASE("ro_composite storage-backend", "RO Composite Tile storage backend")
{
	int found;
	std::string err_log_lines, out_log_lines;
	struct storage_backend *store = NULL;

#ifndef HAVE_CAIRO
	SECTION("storage/initialise", "should return NULL") {
		start_capture();
		REQUIRE(init_storage_backend("composite:{") == NULL);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = err_log_lines.find("init_storage_ro_coposite: Support for compositing storage has not been compiled into this program");
		REQUIRE(found > -1);
	}
#endif
}

TEST_CASE("ro_http_proxy storage-backend", "RO HTTP Proxy Tile storage backend")
{
	int found;
	std::string err_log_lines, out_log_lines;
	struct storage_backend *store = NULL;

#ifdef HAVE_LIBCURL
	SECTION("storage/initialise", "should return 1") {
		struct storage_backend *store = NULL;

		store = init_storage_backend("ro_http_proxy://");
		REQUIRE(store != NULL);

		store->close_storage(store);
	}
#else
	SECTION("storage/initialise", "should return NULL") {
		start_capture();
		REQUIRE(init_storage_backend("ro_http_proxy://") == NULL);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found = err_log_lines.find("init_storage_ro_http_proxy: Support for curl and therefore the http proxy storage has not been compiled into this program");
		REQUIRE(found > -1);
	}
#endif
}

TEST_CASE("projections", "Test projections")
{
	SECTION("projections/bounds/spherical", "should return 1") {
		const char *projection_srs;
		int x_multiplier, y_multiplier;
		double expected_minx, expected_miny, expected_maxx, expected_maxy;
		double expected_rounded_minx, expected_rounded_miny, expected_rounded_maxx, expected_rounded_maxy;
		mapnik::box2d<double> bbox;

		std::tie(projection_srs, x_multiplier, y_multiplier, expected_minx, expected_miny, expected_maxx, expected_maxy, expected_rounded_minx, expected_rounded_miny, expected_rounded_maxx, expected_rounded_maxy) =
		GENERATE(table<const char *, int, int, double, double, double, double, double, double, double, double>({
			// 313086.06785625 = 2*20037508.3428 / (2^10 / 8)
			std::make_tuple("+proj=merc +a=6378137 +b=6378137",
					1, 1,
					-20037508.3428, -20037508.3428, 20037508.3428, 20037508.3428,
					19724422.0, 19724422.0, -19724422.0, -19724422.0),
			// 156543.033928125 = 2*20037508.3428 / (2^10 / 8 * 2)
			std::make_tuple("+proj=eqc +lat_ts=0 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs",
					2, 1,
					-20037508.3428, -10018754.1714, 20037508.3428, 10018754.1714,
					19880965.0, 9862211.0, -19880965.0, -9862211.0),
			// 5468.75 = 700000 / (2^10 / 8)
			std::make_tuple("+proj=tmerc +lat_0=49 +lon_0=-2 +k=0.9996012717 +x_0=400000 +y_0=-100000 +ellps=airy +datum=OSGB36 +units=m +no_defs",
					1, 2,
					0.0, 0.0, 700000.0, 1400000.0,
					694531.0, 1394531.0, 5469.0, 5469.0),
			// 313086.06785625 = 2*20037508.3428 / (2^10 / 8)
			std::make_tuple("",
					1, 1,
					-20037508.3428, -20037508.3428, 20037508.3428, 20037508.3428,
					19724422.0, 19724422.0, -19724422.0, -19724422.0)}));

		struct projectionconfig *prj = get_projection(projection_srs);

		bbox = tile2prjbounds(prj, 0, 0, 0);
		REQUIRE(bbox.minx() == expected_minx);
		REQUIRE(bbox.miny() == expected_miny);
		REQUIRE(bbox.maxx() == expected_maxx);
		REQUIRE(bbox.maxy() == expected_maxy);

		bbox = tile2prjbounds(prj, 0, 0, 10);
		REQUIRE(bbox.minx() == expected_minx);
		REQUIRE(round(bbox.miny()) == expected_rounded_miny);
		REQUIRE(round(bbox.maxx()) == expected_rounded_maxx);
		REQUIRE(bbox.maxy() == expected_maxy);

		bbox = tile2prjbounds(prj, (x_multiplier * (1 << 10) - METATILE), (y_multiplier * (1 << 10) - METATILE), 10);
		REQUIRE(round(bbox.minx()) == expected_rounded_minx);
		REQUIRE(bbox.miny() == expected_miny);
		REQUIRE(bbox.maxx() == expected_maxx);
		REQUIRE(round(bbox.maxy()) == expected_rounded_maxy);

		free(prj);
	}
}

TEST_CASE("g_logger", "Test g_logger.c")
{
	int found_err, found_out, log_level;
	std::string err_log_lines, out_log_lines, expected_output;

	std::tie(log_level, expected_output) =
	GENERATE(table<int, std::string>({
		std::make_tuple(G_LOG_LEVEL_ERROR, "ERROR"),
		std::make_tuple(G_LOG_LEVEL_CRITICAL, "CRITICAL"),
		std::make_tuple(G_LOG_LEVEL_WARNING, "WARNING"),
		std::make_tuple(G_LOG_LEVEL_MESSAGE, "MESSAGE"),
		std::make_tuple(G_LOG_LEVEL_INFO, "INFO"),
		std::make_tuple(G_LOG_LEVEL_DEBUG, "DEBUG"),
		std::make_tuple(0, "UNKNOWN")}));

	SECTION("g_logger_level_name: " + expected_output, "should return the expected string") {
		std::string result;
		result = g_logger_level_name(log_level);
		REQUIRE(result == expected_output);
	}

	SECTION("g_logger background: " + expected_output, "should log the expected string") {
		start_capture();
		g_logger(log_level, "BACKGROUND TEST");
		std::tie(err_log_lines, out_log_lines) = end_capture();

		expected_output += ": BACKGROUND TEST";

		found_err = err_log_lines.find(expected_output);
		found_out = out_log_lines.find(expected_output);

		switch (log_level) {
			case 0:
				// BACKGROUND UNKNOWN messages do not log
				REQUIRE(found_err == -1);
				REQUIRE(found_out == -1);
				break;

			case G_LOG_LEVEL_DEBUG:
				// BACKGROUND DEBUG messages do not log
				REQUIRE(found_err == -1);
				REQUIRE(found_out == -1);
				break;

			default:
				REQUIRE(found_err > -1);
				REQUIRE(found_out == -1);
		}
	}

	SECTION("g_logger foreground: " + expected_output, "should log the expected string") {
		std::string message = expected_output + " FOREGROUND TEST";
		start_capture();
		foreground = 1;
		g_logger(log_level, message.c_str());
		std::tie(err_log_lines, out_log_lines) = end_capture();
		foreground = 0;

		found_err = err_log_lines.find(message);
		found_out = out_log_lines.find(message);

		switch (log_level) {
			case 0:
				REQUIRE(found_err == -1);
				REQUIRE(found_out == -1);
				break;

			case G_LOG_LEVEL_INFO:
				// FOREGROUND INFO messages log to stdout
				REQUIRE(found_err == -1);
				REQUIRE(found_out > -1);
				break;

			case G_LOG_LEVEL_DEBUG:
				// FOREGROUND DEBUG messages log to stdout and only when G_MESSAGES_DEBUG={all,debug}
				REQUIRE(found_err == -1);
				REQUIRE(found_out == -1);
				break;

			default:
				REQUIRE(found_err > -1);
				REQUIRE(found_out == -1);
		}
	}

	SECTION("g_logger foreground debug: " + expected_output, "should log the expected string") {
		std::string message = expected_output + " FOREGROUND DEBUG TEST";
		start_capture(1);
		g_logger(log_level, message.c_str());
		std::tie(err_log_lines, out_log_lines) = end_capture();

		found_err = err_log_lines.find(message);
		found_out = out_log_lines.find(message);

		switch (log_level) {
			case 0:
				REQUIRE(found_err == -1);
				REQUIRE(found_out == -1);
				break;

			case G_LOG_LEVEL_INFO:
				// FOREGROUND INFO messages log to stdout
				REQUIRE(found_err == -1);
				REQUIRE(found_out > -1);
				break;

			case G_LOG_LEVEL_DEBUG:
				// FOREGROUND DEBUG messages log to stdout and only when G_MESSAGES_DEBUG={all,debug}
				REQUIRE(found_err == -1);
				REQUIRE(found_out > -1);
				break;

			default:
				REQUIRE(found_err > -1);
				REQUIRE(found_out == -1);
		}
	}
}

TEST_CASE("metatile", "Test metatile.cpp")
{
	SECTION("metatile/get & set", "should return the same string") {
		metaTile tiles("", "", 1024, 1024, 10);

		for (int yy = 0; yy < METATILE; yy++) {
			for (int xx = 0; xx < METATILE; xx++) {
				std::string tile_data("DEADBEAF " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_data);
				REQUIRE(tile_data == tiles.get(xx, yy));
			}
		}
	}
}

TEST_CASE("protocol_helper", "Test protocol_helper.c")
{
	int block = 0, fd, found, ret;
	std::string err_log_lines, out_log_lines;
	struct protocol *cmd = (struct protocol *)malloc(sizeof(struct protocol));

	cmd->x = 1024;
	cmd->y = 1024;
	cmd->z = 10;

	SECTION("send_cmd/ver invalid version", "should return -1") {
		// Version must be 1, 2 or 3
		cmd->ver = 0;

		start_capture();
		ret = send_cmd(cmd, fd);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == -1);
		found = err_log_lines.find("Failed to send render cmd with unknown protocol version 0");
		REQUIRE(found > -1);

		// Version must be 1, 2 or 3
		cmd->ver = 4;

		start_capture();
		ret = send_cmd(cmd, fd);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == -1);
		found = err_log_lines.find("Failed to send render cmd with unknown protocol version 4");
		REQUIRE(found > -1);
	}

	SECTION("send_cmd/fd invalid", "should return -1") {
		cmd->ver = 1;

		start_capture();
		ret = send_cmd(cmd, fd);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == -1);
		found = err_log_lines.find("Failed to send render cmd on fd");
		REQUIRE(found > -1);
	}

	SECTION("recv_cmd/fd invalid debug", "should return -1") {
		cmd->ver = 1;

		start_capture(1);
		ret = recv_cmd(cmd, fd, block);
		std::tie(err_log_lines, out_log_lines) = end_capture();

		REQUIRE(ret == -1);
		found = out_log_lines.find("Failed to read cmd on fd");
		REQUIRE(found > -1);
	}

	free(cmd);
}

int main(int argc, char *argv[])
{
	capture_stderr();
	openlog("gen_tile_test", LOG_PID | LOG_PERROR, LOG_DAEMON);
	pthread_mutex_init(&item_counter_lock, NULL);
	int result = Catch::Session().run(argc, argv);
	pthread_mutex_destroy(&item_counter_lock);
	get_captured_stderr();
	return result;
}
