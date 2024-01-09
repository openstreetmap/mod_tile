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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "render_submit_queue.h"
#include "sys_utils.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"

static pthread_mutex_t qLock;
static pthread_mutex_t qStatsLock;
static pthread_cond_t qCondNotEmpty;
static pthread_cond_t qCondNotFull;

static int maxLoad = 0;

static unsigned int qMaxLen;
static unsigned int qLen;
struct qItem {
	char *mapname;
	int x, y, z;
	struct qItem *next;
};

struct speed_stat {
	time_t time_min;
	time_t time_max;
	time_t time_total;
	int    noRendered;
};

struct speed_stats {
	struct speed_stat stat[MAX_ZOOM + 1];
};

struct speed_stats performance_stats;

static struct qItem *qHead, *qTail;

static int no_workers;
static pthread_t *workers;
static int work_complete;

static void check_load(void)
{
	double avg = get_load_avg();

	while (avg >= maxLoad) {
		/* printf("Load average %d, sleeping\n", avg); */
		sleep(5);
		avg = get_load_avg();
	}
}

static int process(struct protocol * cmd, int fd)
{
	struct timeval tim;
	time_t t1;
	time_t t2;
	int ret = 0;
	struct protocol rsp;

	gettimeofday(&tim, NULL);
	t1 = tim.tv_sec * 1000 + (tim.tv_usec / 1000);

	//printf("Sending request\n");
	if (send_cmd(cmd, fd) < 1) {
		perror("send error");
	};

	//printf("Waiting for response\n");
	bzero(&rsp, sizeof(rsp));

	ret = recv_cmd(&rsp, fd, 1);

	if (ret < 1) {
		return 0;
	}

	//printf("Got response %i\n", rsp.cmd);

	if (rsp.cmd != cmdDone) {
		printf("rendering failed with command %i, pausing.\n", rsp.cmd);
		sleep(10);
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

	if (!ret) {
		perror("Socket send error");
	}

	return ret;
}

static struct protocol * fetch(void)
{
	pthread_mutex_lock(&qLock);

	while (qLen == 0) {
		if (work_complete) {
			pthread_mutex_unlock(&qLock);
			return NULL;
		}

		pthread_cond_wait(&qCondNotEmpty, &qLock);
	}

	// Fetch item from queue
	if (!qHead) {
		fprintf(stderr, "Queue failure, null qHead with %d items in list\n", qLen);
		exit(1);
	}

	struct qItem *e = qHead;

	if (--qLen == 0) {
		qHead = NULL;
		qTail = NULL;
	} else {
		qHead = qHead->next;
	}

	pthread_cond_signal(&qCondNotFull);
	pthread_mutex_unlock(&qLock);

	struct protocol * cmd = malloc(sizeof(struct protocol));;

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

void enqueue(const char *xmlname, int x, int y, int z)
{
	// Add this path in the local render queue
	struct qItem *e = malloc(sizeof(struct qItem));

	e->mapname = strdup(xmlname);
	e->x = x;
	e->y = y;
	e->z = z;
	e->next = NULL;

	if (!e->mapname) {
		fprintf(stderr, "Malloc failure\n");
		exit(1);
	}

	pthread_mutex_lock(&qLock);

	while (qLen == qMaxLen) {
		int ret = pthread_cond_wait(&qCondNotFull, &qLock);

		if (ret != 0) {
			fprintf(stderr, "pthread_cond_wait(qCondNotFull): %s\n", strerror(ret));
		}
	}

	// Append item to end of queue
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

int make_connection(const char *spath)
{
	int fd;

	if (spath[0] == '/') {
		// Create a Unix socket
		struct sockaddr_un addr;

		fd = socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			fprintf(stderr, "failed to create unix socket\n");
			exit(2);
		}

		bzero(&addr, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, spath, sizeof(addr.sun_path) - 1);

		if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
			close(fd);
			return -1;
		}

	} else {
		// Create a network socket
		const char *d = strchr(spath, ':');
		char *hostname;
		u_int16_t port = RENDERD_PORT;
		char port_s[6];
		size_t spath_len = strlen(spath);
		size_t hostname_len = d ? d - spath : spath_len;

		if (!hostname_len) {
			hostname = strdup(RENDERD_HOST);
		} else {
			hostname = malloc(hostname_len + sizeof('\0'));
			assert(hostname != NULL);
			strncpy(hostname, spath, hostname_len);
		}

		if (d) {
			port = atoi(d + 1);

			if (!port) {
				port = RENDERD_PORT;
			}
		}

		snprintf(port_s, sizeof(port_s), "%u", port);

		printf("Connecting to %s, port %u/tcp\n", hostname, port);

		struct protoent *protocol = getprotobyname("tcp");

		if (!protocol) {
			fprintf(stderr, "cannot find TCP protocol number\n");
			exit(2);
		}

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

		int ai = getaddrinfo(hostname, port_s, &hints, &result);

		if (ai != 0) {
			fprintf(stderr, "cannot resolve hostname %s\n", hostname);
			exit(2);
		}

		struct addrinfo *rp;

		for (rp = result; rp != NULL; rp = rp->ai_next) {
			fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

			if (fd == -1) {
				continue;
			}

			char resolved_addr[NI_MAXHOST];
			char resolved_port[NI_MAXSERV];
			int name_info = getnameinfo(rp->ai_addr, rp->ai_addrlen, resolved_addr, sizeof(resolved_addr), resolved_port, sizeof(resolved_port), NI_NUMERICHOST | NI_NUMERICSERV);

			if (name_info != 0) {
				fprintf(stderr, "cannot retrieve name info: %d\n", name_info);
				exit(2);
			}

			fprintf(stderr, "Trying %s:%s\n", resolved_addr, resolved_port);

			if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
				printf("Connected to %s:%s\n", resolved_addr, resolved_port);
				break;
			}

		}

		freeaddrinfo(result);

		if (rp == NULL) {
			fprintf(stderr, "cannot connect to any address for %s\n", hostname);
			exit(2);
		}

	}

	return fd;
}

void *thread_main(void *arg)
{
	const char *spath = (const char *)arg;
	int fd = make_connection(spath);

	if (fd < 0) {
		fprintf(stderr, "connect failed for: %s\n", spath);
		return NULL;
	}

	while (1) {
		struct protocol * cmd;
		check_load();

		if (!(cmd = fetch())) {
			break;
		}

		while (process(cmd, fd) < 1) {
			fprintf(stderr, "connection to renderd lost\n");
			close(fd);
			fd = -1;

			while (fd < 0) {
				fprintf(stderr, "sleeping for 30 seconds\n");
				sleep(30);
				fprintf(stderr, "attempting to reconnect\n");
				fd = make_connection(spath);
			}
		}

		free(cmd);
	}

	close(fd);

	return NULL;
}

void spawn_workers(int num, const char *spath, int max_load)
{
	int i;
	no_workers = num;
	maxLoad = max_load;

	// Setup request queue
	pthread_mutex_init(&qLock, NULL);
	pthread_mutex_init(&qStatsLock, NULL);
	pthread_cond_init(&qCondNotEmpty, NULL);
	pthread_cond_init(&qCondNotFull, NULL);

	qMaxLen = no_workers;

	printf("Starting %d rendering threads\n", no_workers);
	workers = calloc(sizeof(pthread_t), no_workers);

	if (!workers) {
		perror("Error allocating worker memory");
		exit(1);
	}

	for (i = 0; i < no_workers; i++) {
		if (pthread_create(&workers[i], NULL, thread_main, (void *)spath)) {
			perror("Thread creation failed");
			exit(1);
		}
	}
}

void print_statistics(void)
{
	int i;
	printf("*****************************************************\n");

	for (i = 0; i <= MAX_ZOOM; i++) {
		if (performance_stats.stat[i].noRendered == 0) {
			continue;
		}

		printf("Zoom %02i: min: %4.1f    avg: %4.1f     max: %4.1f     over a total of %8.1fs in %i requests\n",
		       i, performance_stats.stat[i].time_min / 1000.0, (performance_stats.stat[i].time_total /  performance_stats.stat[i].noRendered) / 1000.0,
		       performance_stats.stat[i].time_max / 1000.0, performance_stats.stat[i].time_total / 1000.0, performance_stats.stat[i].noRendered);
	}

	printf("*****************************************************\n");
	printf("*****************************************************\n");
}

void wait_for_empty_queue()
{

	pthread_mutex_lock(&qLock);

	while (qLen > 0) {
		pthread_cond_wait(&qCondNotFull, &qLock);
	}

	pthread_mutex_unlock(&qLock);
}

void finish_workers(void)
{
	int i;

	printf("Waiting for rendering threads to finish\n");
	pthread_mutex_lock(&qLock);
	work_complete = 1;
	pthread_mutex_unlock(&qLock);
	pthread_cond_broadcast(&qCondNotEmpty);

	for (i = 0; i < no_workers; i++) {
		pthread_join(workers[i], NULL);
	}

	free(workers);
	workers = NULL;
}
