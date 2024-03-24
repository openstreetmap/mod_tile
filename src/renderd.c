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
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "gen_tile.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"
#include "renderd.h"
#include "renderd_config.h"
#include "request_queue.h"

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
			return "Unknown";
	}
}




void send_response(struct item *item, enum protoCmd rsp, int render_time)
{
	request_queue_remove_request(render_request_queue, item, render_time);

	while (item) {
		struct item *prev;
		struct protocol *req = &item->req;

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
		g_logger(G_LOG_LEVEL_WARNING, "Ignoring invalid command %s fd(%d) xml(%s), z(%d), x(%d), y(%d)",
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

	g_logger(G_LOG_LEVEL_INFO, "Sending exit request");

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
			g_logger(G_LOG_LEVEL_DEBUG, "poll(): %s", strerror(errno));
		} else if (num) {
			if (pfd[PFD_EXIT_PIPE].revents & POLLIN) {
				g_logger(G_LOG_LEVEL_INFO, "Received exit request, exiting process_loop");
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

	g_logger(G_LOG_LEVEL_DEBUG, "Starting stats writeout thread: %lu", (unsigned long) pthread_self());

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

				if (noFailedAttempts > 6) {
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
		snprintf(portnum, 16, "%i", sConfig->ipport);

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
				g_logger(G_LOG_LEVEL_WARNING, "failed to connect to rendering daemon (%s), trying next ip", ipstring);
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
		const int enable = 1;

		g_logger(G_LOG_LEVEL_INFO, "Initialising TCP/IP server socket on %s:%i",
			 sConfig->iphostname, sConfig->ipport);
		fd = socket(PF_INET6, SOCK_STREAM, 0);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "failed to create IP socket");
			exit(2);
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "setsockopt SO_REUSEADDR failed for: %s:%i",
				 sConfig->iphostname, sConfig->ipport);
			exit(3);
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "setsockopt SO_REUSEPORT failed for: %s:%i",
				 sConfig->iphostname, sConfig->ipport);
			exit(3);
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
	int retry, seconds = 30;
	size_t ret_size;

	struct protocol * resp;
	struct protocol * req_slave;

	req_slave = (struct protocol *)malloc(sizeof(struct protocol));
	resp = (struct protocol *)malloc(sizeof(struct protocol));
	bzero(req_slave, sizeof(struct protocol));
	bzero(resp, sizeof(struct protocol));

	g_logger(G_LOG_LEVEL_DEBUG, "Starting slave thread: %lu", (unsigned long) pthread_self());

	while (1) {
		if (pfd == FD_INVALID) {
			pfd = client_socket_init(sConfig);

			if (pfd == FD_INVALID) {
				if (sConfig->ipport > 0) {
					g_logger(G_LOG_LEVEL_ERROR, "Failed to connect to Renderd slave at %s:%i, trying again in %i seconds", sConfig->iphostname, sConfig->ipport, seconds);
				} else {
					g_logger(G_LOG_LEVEL_ERROR, "Failed to connect to Renderd slave at %s, trying again in %i seconds", sConfig->socketname, seconds);
				}

				sleep(seconds);
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

			if (sConfig->ipport > 0) {
				g_logger(G_LOG_LEVEL_INFO, "Dispatching request to Renderd slave at %s:%i on fd %i", sConfig->iphostname, sConfig->ipport, pfd);
			} else {
				g_logger(G_LOG_LEVEL_INFO, "Dispatching request to Renderd slave at %s on fd %i", sConfig->socketname, pfd);
			}

			do {
				ret_size = send_cmd(req_slave, pfd);

				if (ret_size == sizeof(struct protocol)) {
					//correctly sent command to slave
					break;
				}

				if (errno != EPIPE) {
					g_logger(G_LOG_LEVEL_ERROR, "Failed to send cmd to Renderd slave, shutting down slave thread");
					free(resp);
					free(req_slave);
					close(pfd);
					return NULL;
				}

				g_logger(G_LOG_LEVEL_WARNING, "Failed to send cmd to Renderd slave, retrying");
				close(pfd);
				pfd = client_socket_init(sConfig);

				if (pfd == FD_INVALID) {
					g_logger(G_LOG_LEVEL_ERROR, "Failed to re-connect to Renderd slave, dropping request");
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
					g_logger(G_LOG_LEVEL_ERROR, "Pipe to Renderd slave closed");
					break;
				}

				retry--;
			}

			if (ret_size < sizeof(struct protocol)) {
				if (sConfig->ipport > 0) {
					g_logger(G_LOG_LEVEL_ERROR, "Invalid reply from Renderd slave at %s:%i, trying again in %i seconds", sConfig->iphostname, sConfig->ipport, seconds);
				} else {
					g_logger(G_LOG_LEVEL_ERROR, "Invalid reply from Renderd slave at %s, trying again in %i seconds", sConfig->socketname, seconds);
				}

				ret = cmdNotDone;
				send_response(item, ret, -1);
				sleep(seconds);
			} else {
				ret = resp->cmd;
				send_response(item, ret, -1);

				if (resp->cmd != cmdDone) {
					if (sConfig->ipport > 0) {
						g_logger(G_LOG_LEVEL_ERROR, "Request from Renderd slave at %s:%i did not complete correctly", sConfig->iphostname, sConfig->ipport);
					} else {
						g_logger(G_LOG_LEVEL_ERROR, "Request from Renderd slave at %s did not complete correctly", sConfig->socketname);
					}

					//Sleep for a while to make sure we don't overload the renderer
					//This only happens if it didn't correctly block on the rendering
					//request
					sleep(seconds);
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
	const char *config_file_name_default = RENDERD_CONFIG;
	int active_renderd_section_num_default = 0;

	const char *config_file_name = config_file_name_default;
	int active_renderd_section_num = active_renderd_section_num_default;

	int config_file_name_passed = 0;
	int active_renderd_section_num_passed = 0;

	int fd, i, j, k;

	int c;

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
			case 'c': /* -c, --config */
				config_file_name = strndup(optarg, PATH_MAX);
				config_file_name_passed = 1;

				struct stat buffer;

				if (stat(config_file_name, &buffer) != 0) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Config file '%s' does not exist, please specify a valid file", config_file_name);
					return 1;
				}

				break;

			case 'f': /* -f, --foreground */
				foreground = 1;
				break;

			case 's': /* -s, --slave */
				active_renderd_section_num = min_max_int_opt(optarg, "active renderd section", 0, -1);
				active_renderd_section_num_passed = 1;
				break;

			case 'h': /* -h, --help */
				fprintf(stderr, "Usage: renderd [OPTION] ...\n");
				fprintf(stderr, "Mapnik rendering daemon\n");
				fprintf(stderr, "  -c, --config=CONFIG               specify the renderd config file (default is '%s')\n", config_file_name_default);
				fprintf(stderr, "  -f, --foreground                  run in foreground\n");
				fprintf(stderr, "  -s, --slave=CONFIG_SECTION_NR     set which renderd section to use (default is '%i')\n", active_renderd_section_num_default);
				fprintf(stderr, "\n");
				fprintf(stderr, "  -h, --help                        display this help and exit\n");
				fprintf(stderr, "  -V, --version                     display the version number and exit\n");
				return 0;

			case 'V': /* -V, --version */
				fprintf(stdout, "%s\n", VERSION);
				return 0;

			default:
				fprintf(stderr, "unknown config option '%c'\n", c);
				return 1;
		}
	}

	g_logger(G_LOG_LEVEL_INFO, "Renderd started (version %s)", VERSION);

	process_config_file(config_file_name, active_renderd_section_num, G_LOG_LEVEL_INFO);

	if (config_file_name_passed) {
		free((void *)config_file_name);
	}

	g_logger(G_LOG_LEVEL_INFO, "Initialising request queue");
	render_request_queue = request_queue_init();

	if (render_request_queue == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to initialise request queue");
		return 1;
	}

	fd = server_socket_init(&config);

#if 0

	if (fcntl(fd, F_SETFD, O_RDWR | O_NONBLOCK) < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "setting socket non-block failed");
		close(fd);
		return 5;
	}

#endif

	sigPipeAction.sa_handler = SIG_IGN;

	if (sigaction(SIGPIPE, &sigPipeAction, NULL) < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "failed to register signal handler");
		close(fd);
		return 6;
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

	if (strnlen(config.stats_filename, PATH_MAX - 1)) {
		if (pthread_create(&stats_thread, NULL, stats_writeout_thread, NULL)) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Could not spawn stats writeout thread");
			close(fd);
			return 7;
		}
	} else {
		g_logger(G_LOG_LEVEL_INFO, "No stats file specified in config. Stats reporting disabled");
	}

	render_threads = (pthread_t *) malloc(sizeof(pthread_t) * config.num_threads);

	for (i = 0; i < config.num_threads; i++) {
		if (pthread_create(&render_threads[i], NULL, render_thread, (void *)maps)) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Could not spawn rendering thread");
			close(fd);
			return 7;
		}
	}

	if (active_renderd_section_num == 0) {
		// Only the master renderd opens connections to its slaves
		k = 0;
		slave_threads = (pthread_t *) malloc(sizeof(pthread_t) * num_slave_threads);

		for (i = 1; i < MAX_SLAVES; i++) {
			for (j = 0; j < config_slaves[i].num_threads; j++) {
				if (pthread_create(&slave_threads[k++], NULL, slave_thread, (void *) &config_slaves[i])) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Could not spawn slave thread");
					close(fd);
					return 7;
				}
			}
		}
	} else {
		for (i = 0; i < MAX_SLAVES; i++) {
			if (active_renderd_section_num != i && config_slaves[i].num_threads != 0) {
				g_logger(G_LOG_LEVEL_DEBUG, "Freeing unused renderd config section %i: %s", i, config_slaves[i].name);
				free_renderd_section(config_slaves[i]);
			}
		}
	}

	process_loop(fd);

	unlink(config.socketname);
	free_map_sections(maps);
	free_renderd_sections(config_slaves);
	close(fd);
	return 0;
}
#endif
