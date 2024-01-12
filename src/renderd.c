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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>

#include "config.h"
#include "render_config.h"
#include "renderd.h"
#include "gen_tile.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "request_queue.h"
#include "g_logger.h"

// extern "C" {
#ifdef HAVE_INIPARSER_INIPARSER_H
#include <iniparser/iniparser.h>
#else
#include <iniparser.h>
#endif
// }

#define PFD_LISTEN        0
#define PFD_EXIT_PIPE     1
#define PFD_SPECIAL_COUNT 2

#ifndef MAIN_ALREADY_DEFINED
static pthread_t *render_threads;
static pthread_t *slave_threads;
static struct sigaction sigPipeAction, sigExitAction;
static pthread_t stats_thread;
#endif

static int exit_pipe_fd;

static renderd_config config;

int num_slave_threads;

int foreground = 0;

struct request_queue * render_request_queue;

static const char *cmdStr(enum protoCmd c)
{
	switch (c) {
		case cmdIgnore:
			return "Ignore";

		case cmdRender:
			return "Render";

		case cmdRenderPrio:
			return "RenderPrio";

		case cmdRenderLow:
			return "RenderLow";

		case cmdRenderBulk:
			return "RenderBulk";

		case cmdDirty:
			return "Dirty";

		case cmdDone:
			return "Done";

		case cmdNotDone:
			return "NotDone";

		default:
			return "unknown";
	}
}




void send_response(struct item *item, enum protoCmd rsp, int render_time)
{
	struct protocol *req = &item->req;
	struct item *prev;

	request_queue_remove_request(render_request_queue, item, render_time);

	while (item) {
		req = &item->req;

		if ((item->fd != FD_INVALID) && ((req->cmd == cmdRender) || (req->cmd == cmdRenderPrio) || (req->cmd == cmdRenderLow) || (req->cmd == cmdRenderBulk))) {
			req->cmd = rsp;
			g_logger(G_LOG_LEVEL_DEBUG, "Sending message %s to %d", cmdStr(rsp), item->fd);

			send_cmd(req, item->fd);

		}

		prev = item;
		item = item->duplicates;
		free(prev);
	}
}

enum protoCmd rx_request(struct protocol *req, int fd)
{
	struct item  *item;

	// Upgrade version 1 and 2 to  version 3
	if (req->ver == 1) {
		strcpy(req->xmlname, "default");
	}

	if (req->ver < 3) {
		strcpy(req->mimetype, "image/png");
		strcpy(req->options, "");
	} else if (req->ver != 3) {
		g_logger(G_LOG_LEVEL_ERROR, "Bad protocol version %d", req->ver);
		return cmdNotDone;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Got command %s fd(%d) xml(%s), z(%d), x(%d), y(%d), mime(%s), options(%s)",
		 cmdStr(req->cmd), fd, req->xmlname, req->z, req->x, req->y, req->mimetype, req->options);

	if ((req->cmd != cmdRender) && (req->cmd != cmdRenderPrio) && (req->cmd != cmdRenderLow) && (req->cmd != cmdDirty) && (req->cmd != cmdRenderBulk)) {
		g_logger(G_LOG_LEVEL_WARNING, "Ignoring unknown command %s fd(%d) xml(%s), z(%d), x(%d), y(%d)",
			 cmdStr(req->cmd), fd, req->xmlname, req->z, req->x, req->y);
		return cmdNotDone;
	}

	item = (struct item *)malloc(sizeof(*item));

	if (!item) {
		g_logger(G_LOG_LEVEL_ERROR, "malloc failed");
		return cmdNotDone;
	}

	item->req = *req;
	item->duplicates = NULL;
	item->fd = (req->cmd == cmdDirty) ? FD_INVALID : fd;

#ifdef METATILE
	/* Round down request co-ordinates to the neareast N (should be a power of 2)
	 * Note: request path is no longer consistent but this will be recalculated
	 * when the metatile is being rendered.
	 */
	item->mx = item->req.x & ~(METATILE - 1);
	item->my = item->req.y & ~(METATILE - 1);
#else
	item->mx = item->req.x;
	item->my = item->req.y;
#endif

	return request_queue_add_request(render_request_queue, item);
}

void request_exit(void)
{
	// Any write to the exit pipe will trigger a graceful exit
	char c = 0;

	if (write(exit_pipe_fd, &c, sizeof(c)) < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to write to the exit pipe: %s", strerror(errno));
	}
}

void process_loop(int listen_fd)
{
	int num_cslots = 0;
	int num_conns = 0;
	int pipefds[2];
	int exit_pipe_read;
	struct pollfd pfd[MAX_CONNECTIONS + 2];

	bzero(pfd, sizeof(pfd));

	// A pipe is used to allow the render threads to request an exit by the main process
	if (pipe(pipefds)) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to create pipe");
		return;
	}

	exit_pipe_fd = pipefds[1];
	exit_pipe_read = pipefds[0];

	pfd[PFD_LISTEN].fd = listen_fd;
	pfd[PFD_LISTEN].events = POLLIN;
	pfd[PFD_EXIT_PIPE].fd = exit_pipe_read;
	pfd[PFD_EXIT_PIPE].events = POLLIN;

	while (1) {
		struct sockaddr_un in_addr;
		socklen_t in_addrlen = sizeof(in_addr);
		int incoming, num, i;

		// timeout -1 means infinite timeout,
		// a value of 0 would return immediately
		num = poll(pfd, num_cslots + PFD_SPECIAL_COUNT, -1);

		if (num == -1) {
			g_logger(G_LOG_LEVEL_ERROR, "poll(): %s", strerror(errno));
		} else if (num) {
			if (pfd[PFD_EXIT_PIPE].revents & POLLIN) {
				// A render thread wants us to exit
				break;
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Data is available now on %d fds", num);

			if (pfd[PFD_LISTEN].revents & POLLIN) {
				incoming = accept(listen_fd, (struct sockaddr *) &in_addr, &in_addrlen);

				if (incoming < 0) {
					g_logger(G_LOG_LEVEL_ERROR, "accept(): %s", strerror(errno));
				} else {
					int add = 0;

					// Search for unused slot
					for (i = 0; i < num_cslots; i++) {
						if (pfd[i + PFD_SPECIAL_COUNT].fd < 0) {
							add = 1;
							break;
						}
					}

					// No unused slot found, add at end if space available
					if (!add) {
						if (num_cslots == MAX_CONNECTIONS) {
							g_logger(G_LOG_LEVEL_WARNING, "Connection limit(%d) reached. Dropping connection", MAX_CONNECTIONS);
							close(incoming);
						} else {
							i = num_cslots;
							add = 1;
							num_cslots++;
						}
					}

					if (add) {
						pfd[i + PFD_SPECIAL_COUNT].fd = incoming;
						pfd[i + PFD_SPECIAL_COUNT].events = POLLIN;
						num_conns ++;
						g_logger(G_LOG_LEVEL_DEBUG, "Got incoming connection, fd %d, number %d, total conns %d, total slots %d", incoming, i, num_conns, num_cslots);
					}
				}
			}

			for (i = 0; num && (i < num_cslots); i++) {
				int fd = pfd[i + PFD_SPECIAL_COUNT].fd;

				if (fd >= 0 && pfd[i + PFD_SPECIAL_COUNT].revents & POLLIN) {
					struct protocol cmd;
					int ret = 0;
					memset(&cmd, 0, sizeof(cmd));

					// TODO: to get highest performance we should loop here until we get EAGAIN
					ret = recv_cmd(&cmd, fd, 0);

					if (ret < 1) {
						num_conns--;
						g_logger(G_LOG_LEVEL_DEBUG, "Connection %d, fd %d closed, now %d left, total slots %d", i, fd, num_conns, num_cslots);
						request_queue_clear_requests_by_fd(render_request_queue, fd);
						close(fd);
						pfd[i + PFD_SPECIAL_COUNT].fd = -1;
					} else  {
						enum protoCmd rsp = rx_request(&cmd, fd);

						if (rsp == cmdNotDone) {
							cmd.cmd = rsp;
							g_logger(G_LOG_LEVEL_DEBUG, "Sending NotDone response(%d)", rsp);
							ret = send_cmd(&cmd, fd);
						}
					}
				}
			}
		} else {
			g_logger(G_LOG_LEVEL_ERROR, "Poll timeout");
		}
	}
}

/**
 * Periodically write out current stats to a stats file. This information
 * can then be used to monitor performance of renderd e.g. with a munin plugin
 */
void *stats_writeout_thread(void * arg)
{
	stats_struct lStats;
	int dirtQueueLength;
	int reqQueueLength;
	int reqPrioQueueLength;
	int reqLowQueueLength;
	int reqBulkQueueLength;
	int i;

	int noFailedAttempts = 0;
	char tmpName[PATH_MAX];

	snprintf(tmpName, sizeof(tmpName), "%s.tmp", config.stats_filename);

	g_logger(G_LOG_LEVEL_DEBUG, "Starting stats thread");

	while (1) {
		request_queue_copy_stats(render_request_queue, &lStats);

		reqPrioQueueLength = request_queue_no_requests_queued(render_request_queue, cmdRenderPrio);
		reqQueueLength = request_queue_no_requests_queued(render_request_queue, cmdRender);
		reqLowQueueLength = request_queue_no_requests_queued(render_request_queue, cmdRenderLow);
		dirtQueueLength = request_queue_no_requests_queued(render_request_queue, cmdDirty);
		reqBulkQueueLength = request_queue_no_requests_queued(render_request_queue, cmdRenderBulk);

		FILE * statfile = fopen(tmpName, "w");

		if (statfile == NULL) {
			g_logger(G_LOG_LEVEL_WARNING, "Failed to open stats file: %i", errno);
			noFailedAttempts++;

			if (noFailedAttempts > 3) {
				g_logger(G_LOG_LEVEL_ERROR, "Failed repeatedly to write stats, giving up");
				break;
			}

			continue;
		} else {
			noFailedAttempts = 0;
			fprintf(statfile, "ReqQueueLength: %i\n", reqQueueLength);
			fprintf(statfile, "ReqPrioQueueLength: %i\n", reqPrioQueueLength);
			fprintf(statfile, "ReqLowQueueLength: %i\n", reqLowQueueLength);
			fprintf(statfile, "ReqBulkQueueLength: %i\n", reqBulkQueueLength);
			fprintf(statfile, "DirtQueueLength: %i\n", dirtQueueLength);
			fprintf(statfile, "DropedRequest: %li\n", lStats.noReqDroped);
			fprintf(statfile, "ReqRendered: %li\n", lStats.noReqRender);
			fprintf(statfile, "TimeRendered: %li\n", lStats.timeReqRender);
			fprintf(statfile, "ReqPrioRendered: %li\n", lStats.noReqPrioRender);
			fprintf(statfile, "TimePrioRendered: %li\n", lStats.timeReqPrioRender);
			fprintf(statfile, "ReqLowRendered: %li\n", lStats.noReqLowRender);
			fprintf(statfile, "TimeLowRendered: %li\n", lStats.timeReqLowRender);
			fprintf(statfile, "ReqBulkRendered: %li\n", lStats.noReqBulkRender);
			fprintf(statfile, "TimeBulkRendered: %li\n", lStats.timeReqBulkRender);
			fprintf(statfile, "DirtyRendered: %li\n", lStats.noDirtyRender);
			fprintf(statfile, "TimeDirtyRendered: %li\n", lStats.timeReqDirty);

			for (i = 0; i <= MAX_ZOOM; i++) {
				fprintf(statfile, "ZoomRendered%02i: %li\n", i, lStats.noZoomRender[i]);
			}

			for (i = 0; i <= MAX_ZOOM; i++) {
				fprintf(statfile, "TimeRenderedZoom%02i: %li\n", i, lStats.timeZoomRender[i]);
			}

			fclose(statfile);

			if (rename(tmpName, config.stats_filename)) {
				g_logger(G_LOG_LEVEL_WARNING, "Failed to overwrite stats file: %i", errno);
				noFailedAttempts++;

				if (noFailedAttempts > 3) {
					g_logger(G_LOG_LEVEL_ERROR, "Failed repeatedly to overwrite stats, giving up");
					break;
				}

				continue;
			}
		}

		sleep(10);
	}

	return NULL;
}

int client_socket_init(renderd_config * sConfig)
{
	int fd, s;
	struct sockaddr_un * addrU;
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	char portnum[16];
	char ipstring[INET6_ADDRSTRLEN];

	if (sConfig->ipport > 0) {
		g_logger(G_LOG_LEVEL_INFO, "Initialising TCP/IP client socket to %s:%i", sConfig->iphostname, sConfig->ipport);

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_STREAM; /* TCP socket */
		hints.ai_flags = 0;
		hints.ai_protocol = 0;          /* Any protocol */
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;
		sprintf(portnum, "%i", sConfig->ipport);

		s = getaddrinfo(sConfig->iphostname, portnum, &hints, &result);

		if (s != 0) {
			g_logger(G_LOG_LEVEL_INFO, "failed to resolve hostname of rendering slave");
			return FD_INVALID;
		}

		/* getaddrinfo() returns a list of address structures.
		   Try each address until we successfully connect. */
		for (rp = result; rp != NULL; rp = rp->ai_next) {
			switch (rp->ai_family) {
				case AF_INET:
					inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), ipstring, rp->ai_addrlen);
					break;

				case AF_INET6:
					inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr), ipstring, rp->ai_addrlen);
					break;

				default:
					snprintf(ipstring, sizeof(ipstring), "address family %d", rp->ai_family);
					break;
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Connecting TCP socket to rendering daemon at %s", ipstring);
			fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

			if (fd < 0) {
				continue;
			}

			if (connect(fd, rp->ai_addr, rp->ai_addrlen) != 0) {
				g_logger(G_LOG_LEVEL_INFO, "failed to connect to rendering daemon (%s), trying next ip", ipstring);
				close(fd);
				fd = -1;
				continue;
			} else {
				break;
			}
		}

		freeaddrinfo(result);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_WARNING, "failed to connect to %s:%i", sConfig->iphostname, sConfig->ipport);
			return FD_INVALID;
		}

		g_logger(G_LOG_LEVEL_INFO, "socket %s:%i initialised to fd %i", sConfig->iphostname, sConfig->ipport, fd);
	} else {
		g_logger(G_LOG_LEVEL_INFO, "Initialising unix client socket on %s",
			 sConfig->socketname);
		addrU = (struct sockaddr_un *)malloc(sizeof(struct sockaddr_un));
		fd = socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_WARNING, "Could not obtain socket: %i", fd);
			free(addrU);
			return FD_INVALID;
		}

		bzero(addrU, sizeof(struct sockaddr_un));
		addrU->sun_family = AF_UNIX;
		strncpy(addrU->sun_path, sConfig->socketname, sizeof(addrU->sun_path) - 1);

		if (connect(fd, (struct sockaddr *) addrU, sizeof(struct sockaddr_un)) < 0) {
			g_logger(G_LOG_LEVEL_WARNING, "socket connect failed for: %s",
				 sConfig->socketname);
			close(fd);
			free(addrU);
			return FD_INVALID;
		}

		free(addrU);
		g_logger(G_LOG_LEVEL_INFO, "socket %s initialised to fd %i", sConfig->socketname,
			 fd);
	}

	return fd;
}

int server_socket_init(renderd_config *sConfig)
{
	struct sockaddr_un addrU;
	struct sockaddr_in6 addrI;
	mode_t old;
	int fd;

	if (sConfig->ipport > 0) {
		g_logger(G_LOG_LEVEL_INFO, "Initialising TCP/IP server socket on %s:%i",
			 sConfig->iphostname, sConfig->ipport);
		fd = socket(PF_INET6, SOCK_STREAM, 0);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "failed to create IP socket");
			exit(2);
		}

		bzero(&addrI, sizeof(addrI));
		addrI.sin6_family = AF_INET6;
		addrI.sin6_addr = in6addr_any;
		addrI.sin6_port = htons(sConfig->ipport);

		if (bind(fd, (struct sockaddr *) &addrI, sizeof(addrI)) < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "socket bind failed for: %s:%i",
				 sConfig->iphostname, sConfig->ipport);
			close(fd);
			exit(3);
		}
	} else {
		g_logger(G_LOG_LEVEL_INFO, "Initialising unix server socket on %s",
			 sConfig->socketname);

		fd = socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "failed to create unix socket");
			exit(2);
		}

		bzero(&addrU, sizeof(addrU));
		addrU.sun_family = AF_UNIX;
		strncpy(addrU.sun_path, sConfig->socketname, sizeof(addrU.sun_path) - 1);

		unlink(addrU.sun_path);

		old = umask(0); // Need daemon socket to be writeable by apache

		if (bind(fd, (struct sockaddr *) &addrU, sizeof(addrU)) < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "socket bind failed for: %s", sConfig->socketname);
			close(fd);
			exit(3);
		}

		umask(old);
	}

	if (listen(fd, QUEUE_MAX) < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "socket listen failed for %d", QUEUE_MAX);
		close(fd);
		exit(4);
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Created server socket %i", fd);

	return fd;

}

/**
 * This function is used as a the start function for the slave renderer thread.
 * It pulls a request from the central queue of requests and dispatches it to
 * the slave renderer. It then blocks and waits for the response with no timeout.
 * As it only sends one request at a time (there are as many slave_thread threads as there
 * are rendering threads on the slaves) nothing gets queued on the slave and should get
 * rendererd immediately. Thus overall, requests should be nicely load balanced between
 * all the rendering threads available both locally and in the slaves.
 */
void *slave_thread(void * arg)
{
	renderd_config * sConfig = (renderd_config *) arg;

	int pfd = FD_INVALID;
	int retry;
	size_t ret_size;

	struct protocol * resp;
	struct protocol * req_slave;

	req_slave = (struct protocol *)malloc(sizeof(struct protocol));
	resp = (struct protocol *)malloc(sizeof(struct protocol));
	bzero(req_slave, sizeof(struct protocol));
	bzero(resp, sizeof(struct protocol));

	while (1) {
		if (pfd == FD_INVALID) {
			pfd = client_socket_init(sConfig);

			if (pfd == FD_INVALID) {
				if (sConfig->ipport > 0) {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Failed to connect to render slave %s:%i, trying again in 30 seconds",
						 sConfig->iphostname, sConfig->ipport);
				} else {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Failed to connect to render slave %s, trying again in 30 seconds",
						 sConfig->socketname);
				}

				sleep(30);
				continue;
			}
		}

		enum protoCmd ret;
		struct item *item = request_queue_fetch_request(render_request_queue);

		if (item) {
			struct protocol *req = &item->req;
			req_slave->ver = PROTO_VER;
			req_slave->cmd = cmdRender;
			strcpy(req_slave->xmlname, req->xmlname);
			strcpy(req_slave->mimetype, req->mimetype);
			strcpy(req_slave->options, req->options);
			req_slave->x = req->x;
			req_slave->y = req->y;
			req_slave->z = req->z;

			/*Dispatch request to slave renderd*/
			retry = 2;
			g_logger(G_LOG_LEVEL_INFO,
				 "Dispatching request to slave thread on fd %i", pfd);

			do {
				ret_size = send_cmd(req_slave, pfd);

				if (ret_size == sizeof(struct protocol)) {
					//correctly sent command to slave
					break;
				}

				if (errno != EPIPE) {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Failed to send cmd to render slave, shutting down thread");
					free(resp);
					free(req_slave);
					close(pfd);
					return NULL;
				}

				g_logger(G_LOG_LEVEL_WARNING, "Failed to send cmd to render slave, retrying");
				close(pfd);
				pfd = client_socket_init(sConfig);

				if (pfd == FD_INVALID) {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Failed to re-connect to render slave, dropping request");
					ret = cmdNotDone;
					send_response(item, ret, -1);
					break;
				}
			} while (retry--);

			if (pfd == FD_INVALID || ret_size != sizeof(struct protocol)) {
				continue;
			}

			ret_size = 0;
			retry = 10;

			while ((ret_size < sizeof(struct protocol)) && (retry > 0)) {
				ret_size = recv(pfd, resp + ret_size, (sizeof(struct protocol)
								       - ret_size), 0);

				if ((errno == EPIPE) || ret_size == 0) {
					close(pfd);
					pfd = FD_INVALID;
					ret_size = 0;
					g_logger(G_LOG_LEVEL_ERROR, "Pipe to render slave closed");
					break;
				}

				retry--;
			}

			if (ret_size < sizeof(struct protocol)) {
				if (sConfig->ipport > 0) {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Invalid reply from render slave %s:%i, trying again in 30 seconds",
						 sConfig->iphostname, sConfig->ipport);
				} else {
					g_logger(G_LOG_LEVEL_ERROR,
						 "Invalid reply render slave %s, trying again in 30 seconds",
						 sConfig->socketname);
				}

				ret = cmdNotDone;
				send_response(item, ret, -1);
				sleep(30);
			} else {
				ret = resp->cmd;
				send_response(item, ret, -1);

				if (resp->cmd != cmdDone) {
					if (sConfig->ipport > 0) {
						g_logger(G_LOG_LEVEL_ERROR,
							 "Request from render slave %s:%i did not complete correctly",
							 sConfig->iphostname, sConfig->ipport);
					} else {
						g_logger(G_LOG_LEVEL_ERROR,
							 "Request from render slave %s did not complete correctly",
							 sConfig->socketname);
					}

					//Sleep for a while to make sure we don't overload the renderer
					//This only happens if it didn't correctly block on the rendering
					//request
					sleep(30);
				}
			}

		} else {
			sleep(1); // TODO: Use an event to indicate there are new requests
		}
	}

	free(resp);
	free(req_slave);
	return NULL;
}

#ifndef MAIN_ALREADY_DEFINED
int main(int argc, char **argv)
{
	int fd, i, j, k;

	int c;
	int active_slave = 0;
	char config_file_name[PATH_MAX] = RENDERD_CONFIG;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"config",     required_argument, 0, 'c'},
			{"foreground", no_argument,       0, 'f'},
			{"slave",      required_argument, 0, 's'},

			{"help",       no_argument,       0, 'h'},
			{"version",    no_argument,       0, 'V'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "c:fs:hV", long_options, &option_index);

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'f':
				foreground = 1;
				break;

			case 'c':
				strncpy(config_file_name, optarg, PATH_MAX - 1);
				config_file_name[PATH_MAX - 1] = 0;
				break;

			case 's':
				if (sscanf(optarg, "%i", &active_slave) != 1) {
					fprintf(stderr, "--slave needs to be numeric (%s)\n", optarg);
					active_slave = 0;
				}

				break;

			case 'h':
				fprintf(stderr, "Usage: renderd [OPTION] ...\n");
				fprintf(stderr, "Mapnik rendering daemon\n");
				fprintf(stderr, "  -c, --config=CONFIG   set location of config file (default %s)\n", RENDERD_CONFIG);
				fprintf(stderr, "  -f, --foreground      run in foreground\n");
				fprintf(stderr, "  -s, --slave=CONFIG_NR set which render slave this is (default 0)\n");
				fprintf(stderr, "\n");
				fprintf(stderr, "  -h, --help            display this help and exit\n");
				fprintf(stderr, "  -V, --version         display the version number and exit\n");
				exit(0);

			case 'V':
				fprintf(stdout, "%s\n", VERSION);
				exit(0);

			default:
				fprintf(stderr, "unknown config option '%c'\n", c);
				exit(1);
		}
	}

	if (access(config_file_name, F_OK) != 0) {
		fprintf(stderr, "Config file '%s' does not exist, please specify a valid file with -c/--config\n", config_file_name);
		exit(1);
	}

	g_logger(G_LOG_LEVEL_INFO, "Rendering daemon started (version %s)", VERSION);

	g_logger(G_LOG_LEVEL_INFO, "Initialising request queue");
	render_request_queue = request_queue_init();

	if (render_request_queue == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to initialise request queue");
		exit(1);
	}

	xmlconfigitem maps[XMLCONFIGS_MAX];
	bzero(maps, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

	renderd_config config_slaves[MAX_SLAVES];
	bzero(config_slaves, sizeof(renderd_config) * MAX_SLAVES);
	bzero(&config, sizeof(renderd_config));

	g_logger(G_LOG_LEVEL_INFO, "Parsing config file: %s", config_file_name);
	dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file: %s", config_file_name);
		exit(1);
	}

	num_slave_threads = 0;

	int iconf = -1;
	char buffer[PATH_MAX];

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section(s)");

	for (int section = 0; section < iniparser_getnsec(ini); section++) {
		const char *name = iniparser_getsecname(ini, section);

		if (strncmp(name, "renderd", 7) == 0) {
			/* this is a renderd config section */
			int render_sec = 0;

			if (sscanf(name, "renderd%i", &render_sec) != 1) {
				render_sec = 0;
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section %i: %s", render_sec, name);

			if (render_sec >= MAX_SLAVES) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i renderd config sections",
					 MAX_SLAVES);
				exit(7);
			}

			snprintf(buffer, sizeof(buffer), "%s:socketname", name);
			config_slaves[render_sec].socketname = iniparser_getstring(ini,
							       buffer, (char *) RENDERD_SOCKET);
			snprintf(buffer, sizeof(buffer), "%s:iphostname", name);
			config_slaves[render_sec].iphostname = iniparser_getstring(ini,
							       buffer, "");
			snprintf(buffer, sizeof(buffer), "%s:ipport", name);
			config_slaves[render_sec].ipport = iniparser_getint(ini, buffer, 0);
			snprintf(buffer, sizeof(buffer), "%s:num_threads", name);
			config_slaves[render_sec].num_threads = iniparser_getint(ini,
								buffer, NUM_THREADS);
			snprintf(buffer, sizeof(buffer), "%s:tile_dir", name);
			config_slaves[render_sec].tile_dir = iniparser_getstring(ini,
							     buffer, (char *) RENDERD_TILE_DIR);
			snprintf(buffer, sizeof(buffer), "%s:stats_file", name);
			config_slaves[render_sec].stats_filename = iniparser_getstring(ini,
					buffer, NULL);
			snprintf(buffer, sizeof(buffer), "%s:pid_file", name);
			config_slaves[render_sec].pid_filename = iniparser_getstring(ini,
					buffer, (char *) RENDERD_PIDFILE);

			if (config_slaves[render_sec].num_threads == -1) {
				config_slaves[render_sec].num_threads = sysconf(_SC_NPROCESSORS_ONLN);
			}

			if (render_sec == active_slave) {
				config.socketname = config_slaves[render_sec].socketname;
				config.iphostname = config_slaves[render_sec].iphostname;
				config.ipport = config_slaves[render_sec].ipport;
				config.num_threads = config_slaves[render_sec].num_threads;
				config.tile_dir = config_slaves[render_sec].tile_dir;
				config.stats_filename
					= config_slaves[render_sec].stats_filename;
				config.pid_filename
					= config_slaves[render_sec].pid_filename;
				config.mapnik_plugins_dir = iniparser_getstring(ini,
							    "mapnik:plugins_dir", (char *) MAPNIK_PLUGINS_DIR);
				config.mapnik_font_dir = iniparser_getstring(ini,
							 "mapnik:font_dir", (char *) MAPNIK_FONTS_DIR);
				config.mapnik_font_dir_recurse = iniparser_getboolean(ini,
								 "mapnik:font_dir_recurse", MAPNIK_FONTS_DIR_RECURSE);
			} else {
				num_slave_threads += config_slaves[render_sec].num_threads;
			}
		}
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section(s)");

	for (int section = 0; section < iniparser_getnsec(ini); section++) {
		const char *name = iniparser_getsecname(ini, section);

		if (strncmp(name, "renderd", 7) && strcmp(name, "mapnik")) {
			/* this is a map config section */
			if (config.num_threads == 0 || config.tile_dir == NULL) {
				g_logger(G_LOG_LEVEL_CRITICAL, "No valid (active) renderd config section available");
				exit(7);
			}

			iconf++;

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section %i: %s", iconf, name);

			if (iconf >= XMLCONFIGS_MAX) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Config: more than %d configurations found", XMLCONFIGS_MAX);
				exit(7);
			}

			if (strlen(name) >= (XMLCONFIG_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "XML name too long: %s", name);
				exit(7);
			}

			strcpy(maps[iconf].xmlname, name);

			snprintf(buffer, sizeof(buffer), "%s:uri", name);
			const char *ini_uri = iniparser_getstring(ini, buffer, (char *)"");

			if (strlen(ini_uri) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "URI too long: %s", ini_uri);
				exit(7);
			}

			strcpy(maps[iconf].xmluri, ini_uri);

			snprintf(buffer, sizeof(buffer), "%s:xml", name);
			const char *ini_xmlpath = iniparser_getstring(ini, buffer, (char *)"");

			if (strlen(ini_xmlpath) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "XML path too long: %s", ini_xmlpath);
				exit(7);
			}

			strcpy(maps[iconf].xmlfile, ini_xmlpath);

			snprintf(buffer, sizeof(buffer), "%s:host", name);
			const char *ini_hostname = iniparser_getstring(ini, buffer, (char *) "");

			if (strlen(ini_hostname) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Host name too long: %s", ini_hostname);
				exit(7);
			}

			strcpy(maps[iconf].host, ini_hostname);

			snprintf(buffer, sizeof(buffer), "%s:htcphost", name);
			const char *ini_htcpip = iniparser_getstring(ini, buffer, (char *) "");

			if (strlen(ini_htcpip) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "HTCP host name too long: %s", ini_htcpip);
				exit(7);
			}

			strcpy(maps[iconf].htcpip, ini_htcpip);

			snprintf(buffer, sizeof(buffer), "%s:tilesize", name);
			const char *ini_tilesize = iniparser_getstring(ini, buffer, (char *) "256");
			maps[iconf].tile_px_size = atoi(ini_tilesize);

			if (maps[iconf].tile_px_size < 1) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Tile size is invalid: %s", ini_tilesize);
				exit(7);
			}

			snprintf(buffer, sizeof(buffer), "%s:scale", name);
			const char *ini_scale = iniparser_getstring(ini, buffer, (char *) "1.0");
			maps[iconf].scale_factor = atof(ini_scale);

			if (maps[iconf].scale_factor < 0.1 || maps[iconf].scale_factor > 8.0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Scale factor is invalid: %s", ini_scale);
				exit(7);
			}

			snprintf(buffer, sizeof(buffer), "%s:tiledir", name);
			const char *ini_tiledir = iniparser_getstring(ini, buffer, (char *) config.tile_dir);

			if (strlen(ini_tiledir) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Tiledir too long: %s", ini_tiledir);
				exit(7);
			}

			strcpy(maps[iconf].tile_dir, ini_tiledir);

			snprintf(buffer, sizeof(buffer), "%s:maxzoom", name);
			const char *ini_maxzoom = iniparser_getstring(ini, buffer, "18");
			maps[iconf].max_zoom = atoi(ini_maxzoom);

			if (maps[iconf].max_zoom > MAX_ZOOM) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too large. Renderd currently only supports up to zoom level %i", maps[iconf].max_zoom, MAX_ZOOM);
				exit(7);
			}

			snprintf(buffer, sizeof(buffer), "%s:minzoom", name);
			const char *ini_minzoom = iniparser_getstring(ini, buffer, "0");
			maps[iconf].min_zoom = atoi(ini_minzoom);

			if (maps[iconf].min_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is too small. Minimum zoom level has to be greater or equal to 0", maps[iconf].min_zoom);
				exit(7);
			}

			if (maps[iconf].min_zoom > maps[iconf].max_zoom) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is larger than max zoom (%i).", maps[iconf].min_zoom, maps[iconf].max_zoom);
				exit(7);
			}

			snprintf(buffer, sizeof(buffer), "%s:parameterize_style", name);
			const char *ini_parameterize = iniparser_getstring(ini, buffer, "");

			if (strlen(ini_parameterize) >= (PATH_MAX - 1)) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Parameterize_style too long: %s", ini_parameterize);
				exit(7);
			}

			strcpy(maps[iconf].parameterization, ini_parameterize);

			snprintf(buffer, sizeof(buffer), "%s:type", name);
			const char *ini_type = iniparser_getstring(ini, buffer, "png image/png png256");

			char ini_fileExtension[INILINE_MAX] = "png";
			char ini_mimeType[INILINE_MAX] = "image/png";
			char ini_outputFormat[INILINE_MAX] = "png256";

			sscanf(ini_type, "%[^ ] %[^ ] %[^;#]", ini_fileExtension, ini_mimeType, ini_outputFormat);

			strcpy(maps[iconf].output_format, ini_outputFormat);

			/* Pass this information into the rendering threads,
			 * as it is needed to configure mapniks number of connections
			 */
			maps[iconf].num_threads = config.num_threads;
		}
	}

	if (config.ipport > 0) {
		g_logger(G_LOG_LEVEL_INFO, "config renderd: ip socket=%s:%i", config.iphostname, config.ipport);
	} else {
		g_logger(G_LOG_LEVEL_INFO, "config renderd: unix socketname=%s", config.socketname);
	}

	g_logger(G_LOG_LEVEL_INFO, "config renderd: num_threads=%d", config.num_threads);

	if (active_slave == 0) {
		g_logger(G_LOG_LEVEL_INFO, "config renderd: num_slave_threads=%d", num_slave_threads);
	}

	g_logger(G_LOG_LEVEL_INFO, "config renderd: tile_dir=%s", config.tile_dir);
	g_logger(G_LOG_LEVEL_INFO, "config renderd: stats_file=%s", config.stats_filename);
	g_logger(G_LOG_LEVEL_INFO, "config renderd: pid_file=%s", config.pid_filename);
	g_logger(G_LOG_LEVEL_INFO, "config mapnik:  plugins_dir=%s", config.mapnik_plugins_dir);
	g_logger(G_LOG_LEVEL_INFO, "config mapnik:  font_dir=%s", config.mapnik_font_dir);
	g_logger(G_LOG_LEVEL_INFO, "config mapnik:  font_dir_recurse=%d", config.mapnik_font_dir_recurse);

	for (i = 0; i < MAX_SLAVES; i++) {
		if (config_slaves[i].num_threads == 0) {
			continue;
		}

		if (i == active_slave) {
			g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): Active", i);
		}

		if (config_slaves[i].ipport > 0) {
			g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): ip socket=%s:%i", i,
				 config_slaves[i].iphostname, config_slaves[i].ipport);
		} else {
			g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): unix socketname=%s", i,
				 config_slaves[i].socketname);
		}

		g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): num_threads=%d", i,
			 config_slaves[i].num_threads);
		g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): tile_dir=%s", i,
			 config_slaves[i].tile_dir);
		g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): stats_file=%s", i,
			 config_slaves[i].stats_filename);
		g_logger(G_LOG_LEVEL_INFO, "config renderd(%i): pid_file=%s", i,
			 config_slaves[i].pid_filename);
	}

	for (iconf = 0; iconf < XMLCONFIGS_MAX; ++iconf) {
		if (maps[iconf].xmlname[0] != 0) {
			g_logger(G_LOG_LEVEL_INFO, "config map %d:   name(%s) file(%s) uri(%s) output_format(%s) htcp(%s) host(%s)",
				 iconf, maps[iconf].xmlname, maps[iconf].xmlfile, maps[iconf].xmluri,
				 maps[iconf].output_format, maps[iconf].htcpip, maps[iconf].host);
		}
	}

	fd = server_socket_init(&config);

#if 0

	if (fcntl(fd, F_SETFD, O_RDWR | O_NONBLOCK) < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "setting socket non-block failed");
		close(fd);
		exit(5);
	}

#endif

	sigPipeAction.sa_handler = SIG_IGN;

	if (sigaction(SIGPIPE, &sigPipeAction, NULL) < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "failed to register signal handler");
		close(fd);
		exit(6);
	}

	sigExitAction.sa_handler = (void *) request_exit;

	sigaction(SIGHUP, &sigExitAction, NULL);

	sigaction(SIGINT, &sigExitAction, NULL);

	sigaction(SIGTERM, &sigExitAction, NULL);

	render_init(config.mapnik_plugins_dir, config.mapnik_font_dir, config.mapnik_font_dir_recurse);

	/* unless the command line said to run in foreground mode, fork and detach from terminal */
	if (foreground) {
		g_logger(G_LOG_LEVEL_INFO, "Running in foreground mode...");
	} else {
		if (daemon(0, 0) != 0) {
			g_logger(G_LOG_LEVEL_ERROR, "can't daemonize: %s", strerror(errno));
		}

		/* write pid file */
		FILE *pidfile = fopen(config.pid_filename, "w");

		if (pidfile) {
			(void) fprintf(pidfile, "%d\n", getpid());
			(void) fclose(pidfile);
		}
	}

	if (config.stats_filename != NULL) {
		if (pthread_create(&stats_thread, NULL, stats_writeout_thread, NULL)) {
			g_logger(G_LOG_LEVEL_WARNING, "Could not create stats writeout thread");
		}
	} else {
		g_logger(G_LOG_LEVEL_INFO, "No stats file specified in config. Stats reporting disabled");
	}

	render_threads = (pthread_t *) malloc(sizeof(pthread_t) * config.num_threads);

	for (i = 0; i < config.num_threads; i++) {
		if (pthread_create(&render_threads[i], NULL, render_thread, (void *)maps)) {
			g_logger(G_LOG_LEVEL_CRITICAL, "error spawning render thread");
			close(fd);
			exit(7);
		}
	}

	if (active_slave == 0) {
		//Only the master renderd opens connections to its slaves
		k = 0;
		slave_threads
			= (pthread_t *) malloc(sizeof(pthread_t) * num_slave_threads);

		for (i = 1; i < MAX_SLAVES; i++) {
			for (j = 0; j < config_slaves[i].num_threads; j++) {
				if (pthread_create(&slave_threads[k++], NULL, slave_thread,
						   (void *) &config_slaves[i])) {
					g_logger(G_LOG_LEVEL_CRITICAL, "error spawning render thread");
					close(fd);
					exit(7);
				}
			}
		}
	}

	process_loop(fd);

	unlink(config.socketname);
	close(fd);
	return 0;
}
#endif
