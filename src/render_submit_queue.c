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

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "g_logger.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"
#include "render_submit_queue.h"
#include "sys_utils.h"

/** @brief Mutex protecting the submission queue (`qHead`, `qTail`, `qLen`, and `work_complete`). */
static pthread_mutex_t qLock;
/** @brief Mutex protecting aggregate per-zoom render timing statistics (`performance_stats`). */
static pthread_mutex_t qStatsLock;
/** @brief Condition variable signaled when an item is enqueued or `work_complete` is set. */
static pthread_cond_t qCondNotEmpty;
/** @brief Condition variable signaled whenever a worker dequeues an item (reducing `qLen`). */
static pthread_cond_t qCondNotFull;

/** @brief System 1-minute load average threshold above which workers pause submitting requests. */
static int maxLoad = 0;

/** @brief Maximum capacity of the bounded submission queue (initialized to `no_workers`). */
static unsigned int qMaxLen;
/** @brief Current number of queued items waiting between `qHead` and `qTail`. */
static unsigned int qLen;

/** @brief Global per-zoom-level render count and latency statistics. */
struct speed_stats performance_stats;

/** @brief Head (dequeue end) and tail (enqueue end) pointers of the FIFO work queue. */
static struct qItem *qHead, *qTail;

/** @brief Number of active worker threads spawned by spawn_workers(). */
static int no_workers;
/** @brief Dynamically allocated array of worker `pthread_t` handles. */
static pthread_t *workers;
/** @brief Shutdown flag set by finish_workers() to wake idle workers and terminate them once drained. */
static int work_complete;

/**
 * @brief Pop the next tile render request from the bounded queue and build a protocol command.
 *
 * Blocks on `qCondNotEmpty` while the queue is empty. Returns `NULL` once `work_complete`
 * is signaled and no items remain. Otherwise dequeues the head `qItem`, signals
 * `qCondNotFull`, frees the queue node, and returns a heap-allocated protocol v2
 * `cmdRenderBulk` message.
 *
 * @return Heap-allocated `struct protocol` command (caller owns memory), or `NULL` when shutting down.
 */
static struct protocol *fetch(void)
{
	pthread_mutex_lock(&qLock);

	while (qLen == 0) {
		if (work_complete) {
			pthread_mutex_unlock(&qLock);
			return NULL;
		}

		pthread_cond_wait(&qCondNotEmpty, &qLock);
	}

	/* Dequeue head item from FIFO list */
	if (!qHead) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Queue failure, null qHead with %d items in list", qLen);
		exit(1);
	}

	/* Dequeued node being removed from the front of the list */
	struct qItem *e = qHead;

	if (--qLen == 0) {
		qHead = NULL;
		qTail = NULL;
	} else {
		qHead = qHead->next;
	}

	pthread_cond_signal(&qCondNotFull);
	pthread_mutex_unlock(&qLock);

	/* Heap-allocated renderd wire command populated from the dequeued node */
	struct protocol *cmd = (struct protocol *)malloc(sizeof(struct protocol));

	cmd->ver = 2;
	cmd->cmd = cmdRenderBulk;
	cmd->z = e->z;
	cmd->x = e->x;
	cmd->y = e->y;
	strncpy(cmd->xmlname, e->mapname, XMLCONFIG_MAX - 1);

	free(e->mapname);
	free(e);

	return cmd;
}

/**
 * @brief Send a single render command to `renderd` and wait synchronously for its reply.
 *
 * Measures round-trip wall-clock latency in milliseconds. If `renderd` replies with
 * `cmdDone`, updates `performance_stats` for `cmd->z` under `qStatsLock`. If `renderd`
 * replies with any non-`cmdDone` status (e.g., busy/retry), sleeps 1 second before returning.
 *
 * @param cmd Pointer to the protocol command to send.
 * @param fd  Connected socket file descriptor to `renderd`.
 * @return Positive byte count returned by recv_cmd() if a response was received,
 *         or `0` if reading the response failed (indicating a lost connection).
 */
static int process(struct protocol *cmd, int fd)
{
	/* Wall-clock timestamp struct used to measure round-trip render duration */
	struct timeval tim;
	/* Start timestamp in milliseconds (later reused to hold elapsed duration `t2 - t1`) */
	time_t t1;
	/* Completion timestamp in milliseconds */
	time_t t2;
	/* Byte count returned by recv_cmd(), or 0 on read failure */
	int ret = 0;
	/* Response protocol message read back from renderd */
	struct protocol rsp;

	gettimeofday(&tim, NULL);
	t1 = tim.tv_sec * 1000 + (tim.tv_usec / 1000);

	g_logger(G_LOG_LEVEL_DEBUG, "Sending request");

	if (send_cmd(cmd, fd) < 1) {
		g_logger(G_LOG_LEVEL_ERROR, "send error: %s", strerror(errno));
	}

	bzero(&rsp, sizeof(rsp));

	g_logger(G_LOG_LEVEL_DEBUG, "Waiting for response");

	ret = recv_cmd(&rsp, fd, 1);

	if (ret < 1) {
		return 0;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Got response %i", rsp.cmd);

	if (rsp.cmd != cmdDone) {
		/* Backoff delay in seconds when renderd does not complete the request */
		int seconds = 1;
		g_logger(G_LOG_LEVEL_DEBUG, "Rendering not done with command %i, sleeping %is", rsp.cmd, seconds);
		sleep(seconds);
	} else {
		gettimeofday(&tim, NULL);
		t2 = tim.tv_sec * 1000 + (tim.tv_usec / 1000);
		pthread_mutex_lock(&qStatsLock);
		t1 = t2 - t1;
		performance_stats.stat[cmd->z].noRendered++;
		performance_stats.stat[cmd->z].time_total += t1;

		if ((performance_stats.stat[cmd->z].time_min > t1) || (performance_stats.stat[cmd->z].time_min == 0)) {
			performance_stats.stat[cmd->z].time_min = t1;
		}

		if (performance_stats.stat[cmd->z].time_max < t1) {
			performance_stats.stat[cmd->z].time_max = t1;
		}

		pthread_mutex_unlock(&qStatsLock);
	}

	return ret;
}

/**
 * @brief Open a stream connection to `renderd` over a Unix domain socket or TCP/IP socket.
 *
 * Interprets `spath` based on its leading character:
 *   - Leading `'/'`: Connects to an `AF_UNIX` stream socket at filesystem path `spath`.
 *     Returns `FD_INVALID` if `connect()` fails.
 *   - Otherwise: Parses `<host>[:<port>]` (defaulting host to `RENDERD_HOST` and port to
 *     `RENDERD_PORT`), resolves addresses via `getaddrinfo()`, and connects over TCP.
 *     Terminates the process with `exit(2)` on unrecoverable socket/DNS/connect errors.
 *
 * @param spath Socket path (starting with `'/'`) or TCP `host[:port]` specification.
 * @return Connected socket file descriptor, or `FD_INVALID` if a Unix socket connection failed.
 */
int make_connection(const char *spath)
{
	/* Connected socket file descriptor, or FD_INVALID on failure */
	int fd = FD_INVALID;

	if (spath[0] == '/') {
		/* Unix domain socket address structure */
		struct sockaddr_un addr;

		fd = socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "failed to create unix socket");
			exit(2);
		}

		bzero(&addr, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, spath, sizeof(addr.sun_path) - 1);

		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			close(fd);
			return FD_INVALID;
		}

	} else {
		/* Optional ':' separator pointing to the port portion of spath */
		const char *d = strchr(spath, ':');
		/* Heap-allocated target hostname string */
		char *hostname;
		/* Numeric TCP port (defaults to RENDERD_PORT) */
		u_int16_t port = RENDERD_PORT;
		/* Decimal string representation of `port` for getaddrinfo() */
		char port_s[6];
		/* Total length of spath and byte length of the hostname prefix */
		size_t spath_len = strlen(spath);
		size_t hostname_len = d ? (size_t)(d - spath) : spath_len;

		if (!hostname_len) {
			hostname = strdup(RENDERD_HOST);
		} else {
			hostname = strndup(spath, hostname_len);
			assert(hostname != NULL);
		}

		if (d) {
			port = atoi(d + 1);

			if (!port) {
				port = RENDERD_PORT;
			}
		}

		snprintf(port_s, sizeof(port_s), "%u", port);

		g_logger(G_LOG_LEVEL_DEBUG, "Connecting to %s, port %u/tcp", hostname, port);

		/* Protocol entry used to look up the TCP protocol number */
		struct protoent *protocol = getprotobyname("tcp");

		if (!protocol) {
			g_logger(G_LOG_LEVEL_CRITICAL, "cannot find TCP protocol number");
			exit(2);
		}

		/* Address resolution criteria and head of resolved addrinfo linked list */
		struct addrinfo hints;
		struct addrinfo *result;

		memset(&hints, 0, sizeof(hints));

		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = 0;
		hints.ai_protocol = protocol->p_proto;
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;

		/* Return status from getaddrinfo() */
		int ai = getaddrinfo(hostname, port_s, &hints, &result);

		if (ai != 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "cannot resolve hostname %s", hostname);
			exit(2);
		}

		/* Iterator pointer traversing resolved socket addresses */
		struct addrinfo *rp;

		for (rp = result; rp != NULL; rp = rp->ai_next) {
			fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

			if (fd == -1) {
				continue;
			}

			/* Numeric host and service strings formatted for debug logging */
			char resolved_addr[NI_MAXHOST];
			char resolved_port[NI_MAXSERV];
			int name_info = getnameinfo(rp->ai_addr, rp->ai_addrlen, resolved_addr, sizeof(resolved_addr), resolved_port, sizeof(resolved_port), NI_NUMERICHOST | NI_NUMERICSERV);

			if (name_info != 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "cannot retrieve name info: %s", gai_strerror(name_info));
				exit(2);
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Trying %s:%s", resolved_addr, resolved_port);

			if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
				g_logger(G_LOG_LEVEL_DEBUG, "Connected to %s:%s", resolved_addr, resolved_port);
				break;
			}
		}

		freeaddrinfo(result);

		if (rp == NULL) {
			g_logger(G_LOG_LEVEL_CRITICAL, "cannot connect to any address for %s", hostname);
			exit(2);
		}

		free(hostname);
	}

	return fd;
}

/**
 * @brief Throttle worker submission while the system 1-minute load average meets or exceeds `maxLoad`.
 *
 * Polls `get_load_avg()` in a loop and sleeps 5 seconds per iteration until system load
 * drops below `maxLoad`.
 */
void check_load(void)
{
	/* Current 1-minute system load average */
	double avg = get_load_avg();

	while (avg >= maxLoad) {
		/* Polling interval in seconds while waiting for load to drop */
		int seconds = 5;
		g_logger(G_LOG_LEVEL_DEBUG, "Load average %.2f, sleeping %is", avg, seconds);
		sleep(seconds);
		avg = get_load_avg();
	}
}

/**
 * @brief Log the elapsed time and average throughput (tiles/sec) for a batch of rendered tiles.
 *
 * @param start Timestamp captured at the start of the batch.
 * @param end   Timestamp captured at the end of the batch.
 * @param num   Total number of tiles/metatiles processed during the interval.
 */
void display_rate(struct timeval start, struct timeval end, int num)
{
	/* Whole seconds and microseconds deltas between `start` and `end` */
	int d_s, d_us;
	/* Total elapsed duration in fractional seconds */
	float sec;

	d_s = end.tv_sec - start.tv_sec;
	d_us = end.tv_usec - start.tv_usec;

	sec = d_s + d_us / 1000000.0;

	g_logger(G_LOG_LEVEL_MESSAGE, "\t%d in %.2f seconds (%.2f/s)", num, sec, num / sec);
}

/**
 * @brief Enqueue a metatile render request onto the bounded worker submission queue.
 *
 * Allocates a `qItem` node, blocks on `qCondNotFull` if the queue has reached `qMaxLen`,
 * appends the node to the FIFO list, and signals `qCondNotEmpty` to wake a waiting worker.
 *
 * @param xmlname Logical map style name (duplicated onto the heap).
 * @param x       Top-left tile X coordinate of the metatile.
 * @param y       Top-left tile Y coordinate of the metatile.
 * @param z       Tile zoom level.
 */
void enqueue(const char *xmlname, int x, int y, int z)
{
	g_logger(G_LOG_LEVEL_DEBUG, "Enqueueing tile (xmlname=%s x=%i y=%i z=%i)", xmlname, x, y, z);

	/* Newly allocated queue node for this tile request */
	struct qItem *e = (struct qItem *)malloc(sizeof(struct qItem));

	if (!e) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Error allocating queue item: %s", strerror(errno));
		exit(1);
	}

	e->mapname = strdup(xmlname);
	e->x = x;
	e->y = y;
	e->z = z;
	e->next = NULL;

	if (!e->mapname) {
		free(e);
		g_logger(G_LOG_LEVEL_CRITICAL, "Error allocating queue item mapname: %s", strerror(errno));
		exit(1);
	}

	pthread_mutex_lock(&qLock);

	/* Wait until a worker consumes an item and frees queue capacity */
	while (qLen == qMaxLen) {
		int ret = pthread_cond_wait(&qCondNotFull, &qLock);

		if (ret != 0) {
			g_logger(G_LOG_LEVEL_WARNING, "pthread_cond_wait(qCondNotFull): %s", strerror(ret));
		}
	}

	/* Append item to tail of FIFO queue */
	if (qTail) {
		qTail->next = e;
	} else {
		qHead = e;
	}

	qTail = e;
	qLen++;
	pthread_cond_signal(&qCondNotEmpty);

	pthread_mutex_unlock(&qLock);
}

/**
 * @brief Signal all worker threads to finish remaining queued items and join them.
 *
 * Sets `work_complete = 1`, broadcasts `qCondNotEmpty` so idle workers exit `fetch()`,
 * joins every worker thread via `pthread_join()`, and frees the `workers` handle array.
 */
void finish_workers(void)
{
	g_logger(G_LOG_LEVEL_MESSAGE, "Waiting for rendering threads to finish");
	pthread_mutex_lock(&qLock);
	work_complete = 1;
	pthread_mutex_unlock(&qLock);
	pthread_cond_broadcast(&qCondNotEmpty);

	for (int i = 0; i < no_workers; i++) {
		pthread_join(workers[i], NULL);
	}

	free(workers);
	no_workers = 0;
	workers = NULL;
}

/**
 * @brief Log per-zoom-level minimum, average, maximum, and cumulative render times.
 *
 * Iterates from zoom `0` through `MAX_ZOOM`, skipping any zoom levels with zero rendered
 * requests, and formats millisecond timings from `performance_stats` as seconds.
 */
void print_statistics(void)
{
	/* Zoom level loop counter */
	int i;
	g_logger(G_LOG_LEVEL_MESSAGE, "*****************************************************");

	for (i = 0; i <= MAX_ZOOM; i++) {
		if (performance_stats.stat[i].noRendered == 0) {
			continue;
		}

		g_logger(G_LOG_LEVEL_MESSAGE, "Zoom %02i: min: %4.1f    avg: %4.1f     max: %4.1f     over a total of %8.1fs in %i requests",
			 i, performance_stats.stat[i].time_min / 1000.0, (performance_stats.stat[i].time_total / (float)performance_stats.stat[i].noRendered) / 1000.0,
			 performance_stats.stat[i].time_max / 1000.0, performance_stats.stat[i].time_total / 1000.0, performance_stats.stat[i].noRendered);
	}

	g_logger(G_LOG_LEVEL_MESSAGE, "*****************************************************");
}

/**
 * @brief Initialize synchronization primitives and spawn the pool of render-client worker threads.
 *
 * Configures the bounded queue capacity (`qMaxLen = num`) and system load ceiling (`maxLoad`),
 * initializes `qLock`, `qStatsLock`, `qCondNotEmpty`, and `qCondNotFull`, and launches `num`
 * threads running thread_main().
 *
 * @param num      Number of worker threads to spawn.
 * @param spath    Socket path or `host[:port]` string passed to each worker thread.
 * @param max_load Maximum system 1-minute load average allowed before throttling workers.
 */
void spawn_workers(int num, const char *spath, int max_load)
{
	/* Worker creation loop counter */
	int i;
	no_workers = num;
	maxLoad = max_load;

	/* Initialize request queue synchronization primitives */
	pthread_mutex_init(&qLock, NULL);
	pthread_mutex_init(&qStatsLock, NULL);
	pthread_cond_init(&qCondNotEmpty, NULL);
	pthread_cond_init(&qCondNotFull, NULL);

	qMaxLen = no_workers;

	g_logger(G_LOG_LEVEL_MESSAGE, "Starting %d rendering threads", no_workers);
	workers = (pthread_t *)calloc(no_workers, sizeof(pthread_t));

	if (!workers) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Error allocating worker memory: %s", strerror(errno));
		exit(1);
	}

	for (i = 0; i < no_workers; i++) {
		/* Error code returned by pthread_create() (0 on success) */
		int errnum = pthread_create(&workers[i], NULL, thread_main, (void *)spath);

		if (errnum) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Thread creation failed: %s", strerror(errnum));
			exit(1);
		}
	}
}

/**
 * @brief Worker thread main loop that fetches queued requests and submits them to `renderd`.
 *
 * Opens a dedicated connection to `renderd` via make_connection(), then repeatedly checks
 * system load, pops the next command via fetch(), and submits it via process(). If the
 * connection drops mid-flight, retries connecting every 30 seconds and re-submits the
 * interrupted command until it succeeds.
 *
 * @param arg `const char *` socket path or `host[:port]` string forwarded to make_connection().
 * @return Always returns `NULL` once the queue is drained and `work_complete` is set.
 */
void *thread_main(void *arg)
{
	/* Target renderd socket path or host:port passed via pthread_create() */
	const char *spath = (const char *)arg;
	/* Dedicated client socket descriptor to renderd for this worker */
	int fd = make_connection(spath);

	if (fd < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "connect failed for: %s", spath);
		return NULL;
	}

	while (1) {
		/* Next dequeued render command to submit */
		struct protocol *cmd;
		check_load();

		if (!(cmd = fetch())) {
			break;
		}

		/* Submit command; if the connection drops, reconnect in a loop and retry */
		while (process(cmd, fd) < 1) {
			g_logger(G_LOG_LEVEL_ERROR, "connection to renderd lost");
			close(fd);
			fd = FD_INVALID;

			while (fd < 0) {
				/* Retry delay in seconds between reconnection attempts */
				int seconds = 30;
				g_logger(G_LOG_LEVEL_WARNING, "sleeping for %i seconds", seconds);
				sleep(30);
				g_logger(G_LOG_LEVEL_WARNING, "attempting to reconnect");
				fd = make_connection(spath);
			}
		}

		free(cmd);
	}

	close(fd);

	return NULL;
}

/**
 * @brief Block the calling thread until all currently queued items have been popped by workers.
 *
 * Waits on `qCondNotFull` (which is signaled every time fetch() decrements `qLen`) until
 * `qLen` reaches `0`. Note that the final popped items may still be in flight inside
 * process() when this function returns; call finish_workers() to wait for completion.
 */
void wait_for_empty_queue()
{
	pthread_mutex_lock(&qLock);

	while (qLen > 0) {
		pthread_cond_wait(&qCondNotFull, &qLock);
	}

	pthread_mutex_unlock(&qLock);
}
