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

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "protocol.h"
#include "render_submit_queue.h"

extern int maxLoad;
extern int no_workers;
extern int work_complete;
extern pthread_cond_t qCondNotEmpty;
extern pthread_cond_t qCondNotFull;
extern pthread_mutex_t qLock;
extern pthread_mutex_t qStatsLock;
extern pthread_t *workers;
extern struct qItem *qHead;
extern struct qItem *qTail;
extern struct speed_stats performance_stats;
extern unsigned int qLen;
extern unsigned int qMaxLen;

static struct protocol *fetch(void);
static int process(struct protocol *cmd, int fd);

TEST_CASE("render_submit_queue.c", "[render_submit_queue]")
{
	maxLoad = 999;

	SECTION("fetch function")
	{
		struct protocol *fetch_response = NULL;

		err_log_lines.clear();
		exit_status = 0;

		pthread_mutex_unlock(&qLock);

		SECTION("qLen=0, work_complete=1", "should return NULL")
		{
			qLen = 0;
			work_complete = 1;

			fetch_response = fetch();

			REQUIRE(fetch_response == NULL);
		}

		SECTION("qLen=0, work_complete=0", "should block then return NULL cleanly")
		{
			qLen = 0;
			work_complete = 0;

			pthread_t worker;
			pthread_create(&worker, NULL, (void *(*)(void *))fetch, NULL);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			pthread_mutex_lock(&qLock);
			work_complete = 1;
			pthread_cond_broadcast(&qCondNotEmpty);
			pthread_mutex_unlock(&qLock);

			pthread_join(worker, (void **)&fetch_response);
			REQUIRE(fetch_response == NULL);
		}

		SECTION("qHead!=NULL, qLen=2, work_complete=0", "should return popped item")
		{
			char *map1 = strdup("map1");
			char *map2 = strdup("map2");
			qItem *item2 = new qItem{map2, 1024, 1024, 10, NULL};
			qHead = new qItem{map1, 1024, 1024, 10, item2};
			qTail = item2;
			qLen = 2;
			work_complete = 0;

			fetch_response = fetch();

			REQUIRE(fetch_response != NULL);
			REQUIRE(qLen == 1);
			REQUIRE(qHead == item2);

			free(fetch_response);
			free(item2->mapname);
			delete item2;
		}

		SECTION("qHead!=NULL, qLen=1, work_complete=0", "should return popped item")
		{
			char *mapname = strdup("mapname");
			qHead = new qItem{mapname, 1024, 1024, 10, NULL};
			qTail = qHead;
			qLen = 1;
			work_complete = 0;

			fetch_response = fetch();

			REQUIRE(fetch_response != NULL);
			REQUIRE(qLen == 0);
			REQUIRE(qHead == NULL);

			free(fetch_response);
		}

		SECTION("qHead=NULL, qLen=1, work_complete=0", "should exit 1")
		{
			qHead = NULL;
			qLen = 1;
			work_complete = 0;

			if (setjmp(exit_jump) == 0) {
				fetch_response = fetch();
				FAIL("fetch should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to null qHead failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Queue failure, null qHead with " + std::to_string(qLen) + " items in list"));
		}
	}

	SECTION("process function")
	{
		int fd, ret;
		int pipefd[2];
		REQUIRE(pipe(pipefd) == 0);
		struct protocol *cmd = (struct protocol *)calloc(1, sizeof(struct protocol));

		cmd->cmd = cmdRender;
		cmd->ver = 3;
		cmd->x = 1024;
		cmd->y = 1024;
		cmd->z = 10;

		auto start = std::chrono::high_resolution_clock::now();

		fd = pipefd[0];

		err_log_lines.clear();

		SECTION("process with incomplete response", "should return 0")
		{
			ret = process(cmd, fd);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to read cmd on fd " + std::to_string(fd)));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::Contains("Got response"));
		}

		SECTION("process with incomplete response", "should return 0")
		{
			fail_next_recv_reponse_size = 1;
			ret = process(cmd, fd);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read incomplete cmd on fd " + std::to_string(fd)));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::Contains("Got response"));
		}

		SECTION("process with cmdNotDone response", "should return positive after sleeping 1 second")
		{
			fail_next_next_recv_reponse_size = 64;
			fail_next_next_recv_reponse_version = 3;
			fail_next_recv_reponse_size = sizeof(struct protocol) - 64;
			fail_next_recv_reponse_version = 3;
			fail_next_recv_reponse_cmd = cmdNotDone;

			ret = process(cmd, fd);

			REQUIRE(ret > 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Rendering not done with command"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("sleeping 1s"));
			REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() >= 100);
		}

		SECTION("process with cmdDone response", "should return positive")
		{
			pthread_mutex_lock(&qStatsLock);
			performance_stats.stat[10].time_max = -1;
			performance_stats.stat[10].noRendered = 0;
			pthread_mutex_unlock(&qStatsLock);

			fail_next_next_recv_reponse_size = 64;
			fail_next_next_recv_reponse_version = 3;
			fail_next_recv_reponse_size = sizeof(struct protocol) - 64;
			fail_next_recv_reponse_version = 3;
			fail_next_recv_reponse_cmd = cmdDone;

			ret = process(cmd, fd);

			REQUIRE(ret > 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got response"));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::Contains("Rendering not done with command"));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::Contains("sleeping 1s"));
			REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() < 100);

			pthread_mutex_lock(&qStatsLock);
			REQUIRE(performance_stats.stat[10].noRendered == 1);
			REQUIRE(performance_stats.stat[10].time_max >= 0);
			pthread_mutex_unlock(&qStatsLock);
		}
	}

	SECTION("thread_main function")
	{
		void *ret = NULL;
		std::string socket_path = std::string(P_tmpdir) + "/renderd.render_submit_queue.thread_main.sock";

		err_log_lines.clear();
		exit_status = 0;
		pthread_mutex_unlock(&qLock);

		exit_pthread = false;
		pass_next_connect = false;
		fail_next_next_recv_reponse_size = 0;
		fail_next_next_recv_reponse_version = 0;
		fail_next_recv_reponse_cmd = 0;
		fail_next_recv_reponse_size = 0;
		fail_next_recv_reponse_version = 0;

		SECTION("thread_main with unix domain socket", "should return")
		{
			ret = thread_main((void *)socket_path.c_str());

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("connect failed for: " + socket_path));
		}

		SECTION("thread_main with passing connect", "should return")
		{
			pass_next_connect = true;
			qLen = 0;
			work_complete = 1;

			ret = thread_main((void *)socket_path.c_str());
			REQUIRE(ret == NULL);
		}

		SECTION("thread_main with passing connect and failing process", "should not return after 30 seconds")
		{
			pass_next_connect = true;

			char *mapname = strdup("mapname");
			qHead = new qItem{mapname, 1024, 1024, 10, NULL};
			qTail = qHead;
			qLen = 1;
			work_complete = 0;

			pthread_t worker;
			pthread_create(&worker, NULL, thread_main, (void *)socket_path.c_str());

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			pthread_cond_broadcast(&qCondNotEmpty);

			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			pthread_mutex_lock(&qLock);
			exit_pthread = true;
			pthread_cond_broadcast(&qCondNotEmpty);
			pthread_mutex_unlock(&qLock);

			pthread_join(worker, &ret);

			REQUIRE(qLen == 0);
			REQUIRE(qHead == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("connection to renderd lost"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("sleeping for 30 seconds"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("attempting to reconnect"));
		}

		SECTION("thread_main with passing connect and passing process", "should return")
		{
			pass_next_connect = true;

			char *mapname = strdup("mapname");
			qHead = new qItem{mapname, 1024, 1024, 10, NULL};
			qTail = qHead;
			qLen = 1;
			work_complete = 0;

			fail_next_next_recv_reponse_size = 64;
			fail_next_next_recv_reponse_version = 3;
			fail_next_recv_reponse_cmd = cmdDone;
			fail_next_recv_reponse_size = sizeof(struct protocol) - 64;
			fail_next_recv_reponse_version = 3;

			pthread_t worker;
			pthread_create(&worker, NULL, thread_main, (void *)socket_path.c_str());

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			pthread_cond_broadcast(&qCondNotEmpty);

			pthread_mutex_lock(&qLock);
			work_complete = 1;
			pthread_cond_broadcast(&qCondNotEmpty);
			pthread_mutex_unlock(&qLock);

			pthread_join(worker, &ret);

			REQUIRE(qLen == 0);
			REQUIRE(qHead == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got response"));
		}
	}

	SECTION("make_connection function")
	{
		int ret;

		err_log_lines.clear();
		exit_status = 0;

		SECTION("Unix domain socket")
		{
			std::string socket_path = std::string(P_tmpdir) + "/renderd.render_submit_queue.make_connection.sock";

			SECTION("make_connection with unix domain socket", "should return positive")
			{
				pass_next_connect = true;

				ret = make_connection(socket_path.c_str());

				REQUIRE(ret > 0);
			}

			SECTION("make_connection handles socket failure by exiting", "should exit 2")
			{
				fail_next_socket = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call due to socket failure");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("failed to create unix socket"));
			}
		}

		SECTION("Network socket")
		{
			std::string ip_host = "127.0.0.1";
			std::string ip_port = "1234";
			std::string socket_path = ip_host + ":" + ip_port;

			SECTION("make_connection with network socket", "should return positive")
			{
				pass_next_connect = true;

				ret = make_connection(socket_path.c_str());

				REQUIRE(ret > 0);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Connecting to " + ip_host + ", port " + ip_port + "/tcp"));
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Trying " + socket_path));
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Connected to " + socket_path));
			}

			SECTION("Network socket with empty host uses RENDERD_HOST default", "should return positive")
			{
				pass_next_connect = true;

				ret = make_connection(std::string(":" + ip_port).c_str());

				REQUIRE(ret > 0);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Connecting to " + std::string(RENDERD_HOST) + ", port " + ip_port + "/tcp"));
			}

			SECTION("Network socket with invalid port defaults to RENDERD_PORT", "should return positive")
			{
				pass_next_connect = true;

				ret = make_connection(std::string(ip_host + ":0").c_str());

				REQUIRE(ret > 0);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("port " + std::to_string(RENDERD_PORT) + "/tcp"));
			}

			SECTION("Network socket handles getaddrinfo failure", "should exit 2")
			{
				fail_next_getaddrinfo = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call due to getaddrinfo failure");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("cannot resolve hostname"));
			}

			SECTION("Network socket fails to create socket inside loop", "should exit 2")
			{
				pass_next_connect = true;
				fail_next_socket = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call when no socket could be created");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("cannot connect to any address for " + ip_host));
			}

			SECTION("Network socket fails all connection attempts", "should exit 2")
			{
				fail_next_connect = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call when all address connections fail");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("cannot connect to any address for " + ip_host));
			}

			SECTION("Network socket handles getnameinfo failure", "should exit 2")
			{
				fail_next_getnameinfo = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call due to getnameinfo failure");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("cannot retrieve name info: Name could not be resolved at this time") || Catch::Matchers::Contains("cannot retrieve name info: Temporary failure in name resolution"));
			}

			SECTION("Network socket handles getprotobyname failure", "should exit 2")
			{
				fail_next_getprotobyname = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call due to getprotobyname failure");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("cannot find TCP protocol number"));
			}
		}
	}

	SECTION("check_load function")
	{
		err_log_lines.clear();

		auto start = std::chrono::high_resolution_clock::now();

		SECTION("check_load with max load of 999", "should return")
		{
			check_load();
		}

		SECTION("check_load with max load of 999 and unobtainable load average (which returns 1000)", "should return after sleeping 5 seconds")
		{
			fail_next_getloadavg = true;
			check_load();
			REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() >= 100);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Load average 1000.00, sleeping 5s"));
		}
	}

	SECTION("enqueue function")
	{
		err_log_lines.clear();
		exit_status = 0;
		pthread_mutex_unlock(&qLock);

		qHead = NULL;
		qTail = NULL;
		qLen = 0;
		qMaxLen = 10;

		SECTION("enqueue handles malloc failure by exiting", "should exit 1")
		{
			fail_next_malloc = true;

			if (setjmp(exit_jump) == 0) {
				enqueue(XMLCONFIG_DEFAULT, 0, 0, 0);
				FAIL("make_connection should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to malloc failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Enqueueing tile (xmlname=" + std::string(XMLCONFIG_DEFAULT) + " x=0 y=0 z=0)"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error allocating queue item: Cannot allocate memory"));
		}

		SECTION("enqueue handles strdup failure by exiting", "should exit 1")
		{
			fail_next_strdup = true;

			if (setjmp(exit_jump) == 0) {
				enqueue(XMLCONFIG_DEFAULT, 0, 0, 0);
				FAIL("make_connection should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to malloc failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Enqueueing tile (xmlname=" + std::string(XMLCONFIG_DEFAULT) + " x=0 y=0 z=0)"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error allocating queue item mapname: Cannot allocate memory"));
		}

		SECTION("enqueue into empty queue (qTail == NULL)", "should set qHead and qTail")
		{
			enqueue(XMLCONFIG_DEFAULT, 10, 20, 5);

			REQUIRE(qLen == 1);
			REQUIRE(qHead != NULL);
			REQUIRE(qTail == qHead);
			REQUIRE(std::string(qHead->mapname) == XMLCONFIG_DEFAULT);
			REQUIRE(qHead->x == 10);
			REQUIRE(qHead->y == 20);
			REQUIRE(qHead->z == 5);
			REQUIRE(qHead->next == NULL);

			free(qHead->mapname);
			delete qHead;
			qHead = qTail = NULL;
			qLen = 0;
		}

		SECTION("enqueue into non-empty queue (qTail != NULL)", "should append to tail")
		{
			char *map1 = strdup("map1");
			qItem *first = new qItem{map1, 1, 1, 1, NULL};
			qHead = first;
			qTail = first;
			qLen = 1;

			enqueue(XMLCONFIG_DEFAULT, 10, 20, 5);

			REQUIRE(qLen == 2);
			REQUIRE(qHead == first);
			REQUIRE(qTail != first);
			REQUIRE(first->next == qTail);
			REQUIRE(std::string(qTail->mapname) == XMLCONFIG_DEFAULT);

			free(first->mapname);
			delete first;
			free(qTail->mapname);
			delete qTail;
			qHead = qTail = NULL;
			qLen = 0;
		}

		SECTION("enqueue when queue is full (qLen == qMaxLen)", "should wait on qCondNotFull then enqueue")
		{
			qLen = 1;
			qMaxLen = 1;

			char *map1 = strdup("map1");
			qItem *first = new qItem{map1, 1, 1, 1, NULL};
			qHead = first;
			qTail = first;

			pthread_t worker;
			pthread_create(&worker, NULL, [](void *arg) -> void * {
				enqueue((const char *)arg, 2, 2, 2);
				return NULL; }, (void *)XMLCONFIG_DEFAULT);

			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			pthread_mutex_lock(&qLock);
			qMaxLen = 10;
			pthread_cond_signal(&qCondNotFull);
			pthread_mutex_unlock(&qLock);

			pthread_join(worker, NULL);

			REQUIRE(qLen == 2);
			REQUIRE(qTail != first);

			free(first->mapname);
			delete first;
			free(qTail->mapname);
			delete qTail;
			qHead = qTail = NULL;
			qLen = 0;
		}

		SECTION("enqueue handles pthread_cond_wait error", "should return")
		{
			pthread_mutex_lock(&qLock);
			qLen = 1;
			qMaxLen = 1;

			char *map1 = strdup("map1");
			qItem *first = new qItem{map1, 1, 1, 1, NULL};
			qHead = first;
			qTail = first;
			pthread_mutex_unlock(&qLock);

			fail_next_pthread_cond_wait = true;

			pthread_t worker;
			pthread_create(&worker, NULL, [](void *arg) -> void * {
				enqueue((const char *)arg, 2, 2, 2);
				return NULL; }, (void *)XMLCONFIG_DEFAULT);

			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			pthread_mutex_lock(&qLock);
			qMaxLen = 10;
			pthread_cond_signal(&qCondNotFull);
			pthread_mutex_unlock(&qLock);

			pthread_join(worker, NULL);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("pthread_cond_wait(qCondNotFull)"));

			pthread_mutex_lock(&qLock);
			free(first->mapname);
			delete first;
			if (qTail && qTail != first) {
				free(qTail->mapname);
				delete qTail;
			}
			qHead = qTail = NULL;
			qLen = 0;
			pthread_mutex_unlock(&qLock);
		}
	}

	SECTION("finish_workers function")
	{
		err_log_lines.clear();

		SECTION("finish_workers", "should return")
		{
			std::atomic<bool> worker_release{false};
			std::atomic<int> workers_ready{0};

			auto slow_worker = [](void *arg) -> void * {
				auto *ready = static_cast<std::atomic<int> *>(arg);
				(*ready)++;

				while (ready->load() < 100) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				return NULL;
			};

			no_workers = 2;
			workers = (pthread_t *)calloc(sizeof(pthread_t), no_workers);
			for (int i = 0; i < no_workers; i++) {
				pthread_create(&workers[i], NULL, slow_worker, &workers_ready);
			}
			REQUIRE(workers != NULL);
			REQUIRE(no_workers == 2);

			while (workers_ready.load() < 2) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			workers_ready.store(100);
			finish_workers();

			REQUIRE(workers == NULL);
			REQUIRE(no_workers == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Waiting for rendering threads to finish"));
		}
	}

	SECTION("print_statistics function")
	{
		err_log_lines.clear();

		pthread_mutex_lock(&qStatsLock);
		memset(&performance_stats, 0, sizeof(performance_stats));
		pthread_mutex_unlock(&qStatsLock);

		SECTION("print_statistics with zero rendered tiles (skips zoom loop)", "should return")
		{
			print_statistics();

			REQUIRE_THAT(err_log_lines, !Catch::Matchers::Contains("Zoom"));
		}

		SECTION("print_statistics with boundary zoom levels (0 and MAX_ZOOM)", "should return")
		{
			pthread_mutex_lock(&qStatsLock);
			performance_stats.stat[0].noRendered = 1;
			performance_stats.stat[0].time_min = 200;
			performance_stats.stat[0].time_max = 200;
			performance_stats.stat[0].time_total = 200;

			performance_stats.stat[MAX_ZOOM].noRendered = 5;
			performance_stats.stat[MAX_ZOOM].time_min = 50;
			performance_stats.stat[MAX_ZOOM].time_max = 1000;
			performance_stats.stat[MAX_ZOOM].time_total = 2500;
			pthread_mutex_unlock(&qStatsLock);

			print_statistics();

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Zoom 00: min:  0.2    avg:  0.2     max:  0.2     over a total of      0.2s in 1 requests"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Zoom " + std::to_string(MAX_ZOOM) + ": min:  0.1    avg:  0.5     max:  1.0     over a total of      2.5s in 5 requests"));
		}
	}

	SECTION("spawn_workers function")
	{
		int thread_num = 2;
		std::string socket_path = std::string(P_tmpdir) + "/renderd.render_submit_queue.spawn_workers.sock";

		err_log_lines.clear();
		exit_status = 0;

		SECTION("spawn_workers with non-existent unix domain socket", "should return")
		{
			spawn_workers(thread_num, socket_path.c_str(), maxLoad);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Starting " + std::to_string(thread_num) + " rendering threads"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("connect failed for: " + socket_path));
		}

		SECTION("spawn_workers handles calloc failure by exiting", "should exit 1")
		{
			fail_next_calloc = true;

			if (setjmp(exit_jump) == 0) {
				spawn_workers(thread_num, socket_path.c_str(), maxLoad);
				FAIL("spawn_workers should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to calloc failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Starting " + std::to_string(thread_num) + " rendering threads"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error allocating worker memory: Cannot allocate memory"));
		}

		SECTION("spawn_workers handles pthread_create failure by exiting", "should exit 1")
		{
			fail_next_pthread_create = true;

			if (setjmp(exit_jump) == 0) {
				spawn_workers(thread_num, socket_path.c_str(), maxLoad);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				FAIL("spawn_workers should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to pthread_create failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Starting " + std::to_string(thread_num) + " rendering threads"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Thread creation failed: Resource temporarily unavailable"));
		}
	}

	SECTION("wait_for_empty_queue function")
	{
		err_log_lines.clear();

		SECTION("qLen=0", "should return")
		{
			qLen = 0;
			wait_for_empty_queue();
		}

		SECTION("qLen=1", "should return")
		{
			qLen = 1;
			pthread_t worker;
			pthread_create(&worker, NULL, (void *(*)(void *))wait_for_empty_queue, NULL);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			qLen = 0;
			pthread_cond_broadcast(&qCondNotFull);

			pthread_join(worker, NULL);
		}
	}
}
