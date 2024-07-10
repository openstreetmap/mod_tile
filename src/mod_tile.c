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

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC

#include <ap_config.h>
#include <apr.h>
#include <apr_errno.h>
#include <apr_file_info.h>
#include <apr_general.h>
#include <apr_global_mutex.h>
#include <apr_hooks.h>
#include <apr_portable.h>
#include <apr_proc_mutex.h>
#include <apr_shm.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_thread_proc.h>
#include <apr_time.h>
#include <apr_want.h>
#include <arpa/inet.h>
#include <errno.h>
#include <httpd.h>
#include <http_config.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <util_md5.h>

#include "config.h"
#include "mod_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "renderd.h"
#include "renderd_config.h"
#include "store.h"
#include "sys_utils.h"

module AP_MODULE_DECLARE_DATA tile_module;

#if !defined(OS2) && !defined(WIN32) && !defined(BEOS) && !defined(NETWARE)
#include <unixd.h>
#define MOD_TILE_SET_MUTEX_PERMS /* XXX Apache should define something */
#endif

APLOG_USE_MODULE(tile);

#if (defined(__FreeBSD__) || defined(__MACH__)) && !defined(s6_addr32)
#define s6_addr32 __u6_addr.__u6_addr32
#endif

apr_shm_t *stats_shm;
apr_shm_t *delaypool_shm;
apr_global_mutex_t *stats_mutex;
apr_global_mutex_t *delaypool_mutex;

char *stats_mutexfilename;
char *delaypool_mutexfilename;
int layerCount = 0;
int global_max_zoom = 0;

struct storage_backends {
	struct storage_backend **stores;
	int noBackends;
};

static int error_message(request_rec *r, const char *format, ...)
__attribute__((format(printf, 2, 3)));

static int error_message(request_rec *r, const char *format, ...)
{
	va_list ap;
	char *msg;
	va_start(ap, format);

	msg = apr_pvsprintf(r->pool, format, ap);

	if (msg) {
		// ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", msg);
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "%s", msg);
		r->content_type = "text/plain";

		if (!r->header_only) {
			ap_rputs(msg, r);
		}
	}

	va_end(ap);
	return OK;
}

static int socket_init(request_rec *r)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	struct sockaddr_un addr;
	char portnum[16];
	char ipstring[INET6_ADDRSTRLEN];
	int fd, s;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (scfg->renderd_socket_port > 0) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Connecting to renderd on %s:%i via TCP", scfg->renderd_socket_name, scfg->renderd_socket_port);

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;	 /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_STREAM; /* TCP socket */
		hints.ai_flags = 0;
		hints.ai_protocol = 0; /* Any protocol */
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;
		snprintf(portnum, 16, "%i", scfg->renderd_socket_port);

		s = getaddrinfo(scfg->renderd_socket_name, portnum, &hints, &result);

		if (s != 0) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "failed to resolve hostname of rendering daemon");
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

			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Connecting TCP socket to rendering daemon at %s", ipstring);
			fd = socket(rp->ai_family, rp->ai_socktype,
				    rp->ai_protocol);

			if (fd < 0) {
				continue;
			}

			if (connect(fd, rp->ai_addr, rp->ai_addrlen) != 0) {
				ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "failed to connect to rendering daemon (%s), trying next ip", ipstring);
				close(fd);
				fd = -1;
				continue;
			} else {
				break;
			}
		}

		freeaddrinfo(result);

		if (fd < 0) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "failed to create tcp socket");
			return FD_INVALID;
		}

	} else {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Connecting to renderd on Unix socket %s", scfg->renderd_socket_name);

		fd = socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "failed to create unix socket");
			return FD_INVALID;
		}

		bzero(&addr, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, scfg->renderd_socket_name, sizeof(addr.sun_path) - sizeof(char));

		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "socket connect failed for: %s with reason: %s", scfg->renderd_socket_name, strerror(errno));
			close(fd);
			return FD_INVALID;
		}
	}

	return fd;
}

static int request_tile(request_rec *r, struct protocol *cmd, int renderImmediately)
{
	int fd;
	int ret = 0;
	int retry = 1;
	struct protocol resp;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	fd = socket_init(r);

	if (fd == FD_INVALID) {
		ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, "Failed to connect to renderer");
		return 0;
	}

	// cmd has already been partial filled, fill in the rest
	switch (renderImmediately) {
		case 0: {
			cmd->cmd = cmdDirty;
			break;
		}

		case 1: {
			cmd->cmd = cmdRenderLow;
			break;
		}

		case 2: {
			cmd->cmd = cmdRender;
			break;
		}

		case 3: {
			cmd->cmd = cmdRenderPrio;
			break;
		}
	}

	if (scfg->enable_bulk_mode) {
		cmd->cmd = cmdRenderBulk;
	}

	ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Requesting style(%s) z(%d) x(%d) y(%d) from renderer with priority %d", cmd->xmlname, cmd->z, cmd->x, cmd->y, cmd->cmd);

	do {
		switch (cmd->ver) {
			case 2:
				ret = send(fd, cmd, sizeof(struct protocol_v2), 0);
				break;

			case 3:
				ret = send(fd, cmd, sizeof(struct protocol), 0);
				break;
		}

		if ((ret == sizeof(struct protocol_v2)) || (ret == sizeof(struct protocol))) {
			break;
		}

		if (errno != EPIPE) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "request_tile: Failed to send request to renderer: %s", strerror(errno));
			close(fd);
			return 0;
		}

		close(fd);

		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "request_tile: Reconnecting to rendering socket after failed request due to sigpipe");

		fd = socket_init(r);

		if (fd == FD_INVALID) {
			return 0;
		}
	} while (retry--);

	if (renderImmediately) {
		int timeout = (renderImmediately > 2 ? scfg->request_timeout_priority : scfg->request_timeout);
		struct pollfd rx;
		int s;

		while (1) {
			rx.fd = fd;
			rx.events = POLLIN;
			s = poll(&rx, 1, timeout * 1000);

			if (s > 0) {
				bzero(&resp, sizeof(struct protocol));
				ret = recv(fd, &resp, sizeof(struct protocol_v2), 0);

				if (ret != sizeof(struct protocol_v2)) {
					ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "request_tile: Failed to read response from rendering socket. Got %d bytes but expected %d. Errno %d (%s)",
						      ret, (int)sizeof(struct protocol_v2), errno, strerror(errno));
					break;
				}

				if (resp.ver == 3) {
					ret += recv(fd, ((void *)&resp) + sizeof(struct protocol_v2), sizeof(struct protocol) - sizeof(struct protocol_v2), 0);
				}

				if (cmd->x == resp.x && cmd->y == resp.y && cmd->z == resp.z && !strcmp(cmd->xmlname, resp.xmlname)) {
					close(fd);

					if (resp.cmd == cmdDone) {
						return 1;
					} else {
						return 0;
					}
				} else {
					ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
						      "Response does not match request: xml(%s,%s) z(%d,%d) x(%d,%d) y(%d,%d)", cmd->xmlname,
						      resp.xmlname, cmd->z, resp.z, cmd->x, resp.x, cmd->y, resp.y);
				}
			} else if (s == 0) {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
					      "request_tile: Request xml(%s) z(%d) x(%d) y(%d) could not be rendered in %i seconds",
					      cmd->xmlname, cmd->z, cmd->x, cmd->y,
					      timeout);
				break;
			} else {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
					      "request_tile: Request xml(%s) z(%d) x(%d) y(%d) timeout %i seconds failed with reason: %s",
					      cmd->xmlname, cmd->z, cmd->x, cmd->y,
					      timeout, strerror(errno));
				break;
			}
		}
	}

	close(fd);
	return 0;
}

static apr_status_t cleanup_storage_backend(void *data)
{
	struct storage_backends *stores = (struct storage_backends *)data;
	int i;

	for (i = 0; i < stores->noBackends; i++) {
		if (stores->stores[i]) {
			stores->stores[i]->close_storage(stores->stores[i]);
		}
	}

	return APR_SUCCESS;
}

static struct storage_backend *get_storage_backend(request_rec *r, int tile_layer)
{
	struct storage_backends *stores = NULL;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	tile_config_rec *tile_configs = (tile_config_rec *)scfg->configs->elts;
	tile_config_rec *tile_config = &tile_configs[tile_layer];
	apr_thread_t *current_thread = r->connection->current_thread;
	apr_pool_t *lifecycle_pool = apr_thread_pool_get(current_thread);
	char *memkey = apr_psprintf(r->pool, "mod_tile_storage_backends");
	apr_os_thread_t os_thread = apr_os_thread_current();

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "get_storage_backend: Retrieving storage back end for tile layer %i in pool %pp and thread %li",
		      tile_layer, lifecycle_pool, (unsigned long)os_thread);

	if (apr_pool_userdata_get((void **)&stores, memkey, lifecycle_pool) != APR_SUCCESS) {
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "get_storage_backend: Failed horribly!");
	}

	if (stores == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "get_storage_backend: No storage backends for this lifecycle %pp, creating it in thread %li", lifecycle_pool, (unsigned long)os_thread);
		stores = (struct storage_backends *)apr_pcalloc(lifecycle_pool, sizeof(struct storage_backends));
		stores->stores = (struct storage_backend **)apr_pcalloc(lifecycle_pool, sizeof(struct storage_backend *) * scfg->configs->nelts);
		stores->noBackends = scfg->configs->nelts;

		if (apr_pool_userdata_set(stores, memkey, &cleanup_storage_backend, lifecycle_pool) != APR_SUCCESS) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "get_storage_backend: Failed horribly to set user_data!");
		}
	} else {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "get_storage_backend: Found backends (%pp) for this lifecycle %pp in thread %li", stores, lifecycle_pool, (unsigned long)os_thread);
	}

	if (stores->stores[tile_layer] == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "get_storage_backend: No storage backend in current lifecycle %pp in thread %li for current tile layer %i",
			      lifecycle_pool, (unsigned long)os_thread, tile_layer);
		stores->stores[tile_layer] = init_storage_backend(tile_config->store);
	} else {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "get_storage_backend: Storage backend found in current lifecycle %pp for current tile layer %i in thread %li",
			      lifecycle_pool, tile_layer, (unsigned long)os_thread);
	}

	return stores->stores[tile_layer];
}

static enum tileState tile_state(request_rec *r, struct protocol *cmd)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	struct stat_info stat;
	struct tile_request_data *rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);

	stat = rdata->store->tile_stat(rdata->store, cmd->xmlname, cmd->options, cmd->x, cmd->y, cmd->z);

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_state: determined state of %s %i %i %i on store %pp: Tile size: %" APR_OFF_T_FMT ", expired: %i created: %li",
		      cmd->xmlname, cmd->x, cmd->y, cmd->z, rdata->store, stat.size, stat.expired, stat.mtime);

	r->finfo.mtime = stat.mtime * 1000000;
	r->finfo.atime = stat.atime * 1000000;
	r->finfo.ctime = stat.ctime * 1000000;

	if (stat.size < 0) {
		return tileMissing;
	}

	if (stat.expired) {
		if ((r->request_time - r->finfo.mtime) < scfg->very_old_threshold) {
			return tileOld;
		} else {
			return tileVeryOld;
		}
	}

	return tileCurrent;
}

/**
 * Add CORS ( Cross-origin resource sharing ) headers. http://www.w3.org/TR/cors/
 * CORS allows requests that would otherwise be forbidden under the same origin policy.
 */
static int add_cors(request_rec *r, const char *cors)
{
	const char *headers;
	const char *origin = apr_table_get(r->headers_in, "Origin");
	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking if CORS headers need to be added: Origin: %s Policy: %s", origin, cors);

	if (!origin) {
		return DONE;
	} else {
		if ((strcmp(cors, "*") == 0) || strstr(cors, origin)) {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Origin header is allowed under the CORS policy. Adding Access-Control-Allow-Origin");

			if (strcmp(cors, "*") == 0) {
				apr_table_setn(r->headers_out, "Access-Control-Allow-Origin",
					       apr_psprintf(r->pool, "%s", cors));
			} else {
				apr_table_setn(r->headers_out, "Access-Control-Allow-Origin",
					       apr_psprintf(r->pool, "%s", origin));
				apr_table_setn(r->headers_out, "Vary",
					       apr_psprintf(r->pool, "%s", "Origin"));
			}

			if (strcmp(r->method, "OPTIONS") == 0 &&
					apr_table_get(r->headers_in, "Access-Control-Request-Method")) {
				headers = apr_table_get(r->headers_in, "Access-Control-Request-Headers");

				if (headers) {
					apr_table_setn(r->headers_out, "Access-Control-Allow-Headers",
						       apr_psprintf(r->pool, "%s", headers));
				}

				apr_table_setn(r->headers_out, "Access-Control-Max-Age",
					       apr_psprintf(r->pool, "%i", 604800));
				return OK;
			} else {
				return DONE;
			}
		} else {
			ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Origin header (%s) is NOT allowed under the CORS policy (%s). Rejecting request", origin, cors);
			return HTTP_FORBIDDEN;
		}
	}
}

static void add_expiry(request_rec *r, struct protocol *cmd)
{
	apr_time_t holdoff;
	apr_table_t *t = r->headers_out;
	enum tileState state = tile_state(r, cmd);
	apr_finfo_t *finfo = &r->finfo;
	char *timestr;
	long int maxAge, minCache, lastModified;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	struct tile_request_data *rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	tile_config_rec *tile_configs = (tile_config_rec *)scfg->configs->elts;
	tile_config_rec *tile_config = &tile_configs[rdata->layerNumber];

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "expires(%s), uri(%s),, path_info(%s)\n",
		      r->handler, r->uri, r->path_info);

	/* If the hostname matches the "extended caching hostname" then set the cache age accordingly */
	if ((strlen(scfg->cache_extended_hostname) != 0) && (strstr(r->hostname,
			scfg->cache_extended_hostname) != NULL)) {
		maxAge = scfg->cache_extended_duration;
	} else {

		/* Test if the tile we are serving is out of date, then set a low maxAge*/
		if (state == tileOld) {
			holdoff = (scfg->cache_duration_dirty / 2.0) * (rand() / (RAND_MAX + 1.0));
			maxAge = scfg->cache_duration_dirty + holdoff;
		} else {
			// cache heuristic based on zoom level
			if (cmd->z > tile_config->maxzoom) {
				minCache = 0;
				ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
					      "z (%i) is larger than MAXZOOM %i\n", cmd->z, tile_config->maxzoom);
			} else {
				minCache = scfg->mincachetime[cmd->z];
			}

			// Time to the next known complete rerender
			// planetTimestamp = apr_time_sec(getPlanetTime(r)
			//        + apr_time_from_sec(PLANET_INTERVAL) - r->request_time);
			// Time since the last render of this tile
			lastModified = (int)(((double)apr_time_sec(r->request_time - finfo->mtime)) * scfg->cache_duration_last_modified_factor);
			// Add a random jitter of 3 hours to space out cache expiry
			holdoff = (3 * 60 * 60) * (rand() / (RAND_MAX + 1.0));

			// maxAge = MAX(minCache, planetTimestamp);
			maxAge = minCache;
			maxAge = MAX(maxAge, lastModified);
			maxAge += holdoff;

			ap_log_rerror(
				APLOG_MARK,
				APLOG_DEBUG,
				0,
				r,
				"caching heuristics: zoom level based %ld; last modified %ld\n",
				minCache, lastModified);
		}

		maxAge = MIN(maxAge, scfg->cache_duration_max);
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Setting tiles maxAge to %ld\n", maxAge);

	apr_table_mergen(t, "Cache-Control",
			 apr_psprintf(r->pool, "max-age=%li", maxAge));
	timestr = (char *)apr_palloc(r->pool, APR_RFC822_DATE_LEN);
	apr_rfc822_date(timestr, (apr_time_from_sec(maxAge) + r->request_time));
	apr_table_setn(t, "Expires", timestr);
}

static int get_global_lock(request_rec *r, apr_global_mutex_t *mutex)
{
	apr_status_t rs;
	int camped;

	for (camped = 0; camped < MAXCAMP; camped++) {
		rs = apr_global_mutex_trylock(mutex);

		if (APR_STATUS_IS_EBUSY(rs)) {
			apr_sleep(CAMPOUT);
		} else if (rs == APR_SUCCESS) {
			return 1;
		} else if (APR_STATUS_IS_ENOTIMPL(rs)) {
			/* If it's not implemented, just hang in the mutex. */
			rs = apr_global_mutex_lock(mutex);

			if (rs == APR_SUCCESS) {
				return 1;
			} else {
				ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Could not get hardlock");
				return 0;
			}
		} else {
			ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, "Unknown return status from trylock");
			return 0;
		}
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Timedout trylock");
	return 0;
}

static int incRespCounter(int resp, request_rec *r, struct protocol *cmd, int layerNumber)
{
	stats_data *stats;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (!scfg->enable_global_stats) {
		/* If tile stats reporting is not enable
		 * pretend we correctly updated the counter to
		 * not fill the logs with warnings about failed
		 * stats
		 */
		return 1;
	}

	if (get_global_lock(r, stats_mutex) != 0) {
		stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);

		switch (resp) {
			case OK: {
				stats->noResp200++;

				if (cmd != NULL) {
					stats->noRespZoom[cmd->z]++;
					stats->noResp200Layer[layerNumber]++;
				}

				break;
			}

			case HTTP_NOT_MODIFIED: {
				stats->noResp304++;

				if (cmd != NULL) {
					stats->noRespZoom[cmd->z]++;
					stats->noResp200Layer[layerNumber]++;
				}

				break;
			}

			case HTTP_NOT_FOUND: {
				stats->noResp404++;
				stats->noResp404Layer[layerNumber]++;
				break;
			}

			case HTTP_SERVICE_UNAVAILABLE: {
				stats->noResp503++;
				break;
			}

			case HTTP_INTERNAL_SERVER_ERROR: {
				stats->noResp5XX++;
				break;
			}

			default: {
				stats->noRespOther++;
			}
		}

		apr_global_mutex_unlock(stats_mutex);
		/* Swallowing the result because what are we going to do with it at
		 * this stage?
		 */
		return 1;
	} else {
		return 0;
	}
}

static int incFreshCounter(int status, request_rec *r)
{
	stats_data *stats;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (!scfg->enable_global_stats) {
		/* If tile stats reporting is not enable
		 * pretend we correctly updated the counter to
		 * not fill the logs with warnings about failed
		 * stats
		 */
		return 1;
	}

	if (get_global_lock(r, stats_mutex) != 0) {
		stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);

		switch (status) {
			case FRESH: {
				stats->noFreshCache++;
				break;
			}

			case FRESH_RENDER: {
				stats->noFreshRender++;
				break;
			}

			case OLD: {
				stats->noOldCache++;
				break;
			}

			case VERYOLD: {
				stats->noVeryOldCache++;
				break;
			}

			case OLD_RENDER: {
				stats->noOldRender++;
				break;
			}

			case VERYOLD_RENDER: {
				stats->noVeryOldRender++;
				break;
			}
		}

		apr_global_mutex_unlock(stats_mutex);
		/* Swallowing the result because what are we going to do with it at
		 * this stage?
		 */
		return 1;
	} else {
		return 0;
	}
}

static int incTimingCounter(apr_uint64_t duration, int z, request_rec *r)
{
	stats_data *stats;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (!scfg->enable_global_stats) {
		/* If tile stats reporting is not enable
		 * pretend we correctly updated the counter to
		 * not fill the logs with warnings about failed
		 * stats
		 */
		return 1;
	}

	if (get_global_lock(r, stats_mutex) != 0) {
		stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
		stats->totalBufferRetrievalTime += duration;
		stats->zoomBufferRetrievalTime[z] += duration;
		stats->noTotalBufferRetrieval++;
		stats->noZoomBufferRetrieval[z]++;
		apr_global_mutex_unlock(stats_mutex);
		/* Swallowing the result because what are we going to do with it at
		 * this stage?
		 */
		return 1;
	} else {
		return 0;
	}
}

static int delay_allowed(request_rec *r, enum tileState state)
{
	delaypool *delayp;
	int delay = 0;
	int i, j;
	char *strtok_state;
	char *tmp;
	const char *ip_addr = NULL;
	apr_time_t now;
	int tiles_topup;
	int render_topup;
	uint32_t hashkey;
	struct in_addr sin_addr;
	struct in6_addr ip;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	delayp = (delaypool *)apr_shm_baseaddr_get(delaypool_shm);
	ip_addr = r->useragent_ip;

	if (scfg->enable_tile_throttling_xforward) {
		char *ip_addrs = apr_pstrdup(r->pool, apr_table_get(r->headers_in, "X-Forwarded-For"));

		if (ip_addrs) {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking throttling delays: Found X-Forwarded-For header \"%s\", forwarded by %s", ip_addrs, r->connection->client_ip);
			// X-Forwarded-For can be a chain of proxies deliminated by , The first entry in the list is the client, the last entry is the remote address seen by the proxy
			// closest to the tileserver.
			strtok_state = NULL;
			tmp = apr_strtok(ip_addrs, ", ", &strtok_state);
			ip_addr = tmp;

			// Use the last entry in the chain of X-Forwarded-For instead of the client, i.e. the entry added by the proxy closest to the tileserver
			// If this is a reverse proxy under our control, its X-Forwarded-For can be trusted.
			if (scfg->enable_tile_throttling_xforward == 2) {
				while ((tmp = apr_strtok(NULL, ", ", &strtok_state)) != NULL) {
					ip_addr = tmp;
				}
			}

			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking throttling delays for IP %s, forwarded by %s", ip_addr, r->connection->client_ip);
		}
	}

	if (inet_pton(AF_INET, ip_addr, &sin_addr) > 0) {
		// ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking delays: for IP %s appears to be an IPv4 address", ip_addr);
		memset(ip.s6_addr, 0, 16);
		memcpy(&(ip.s6_addr[12]), &(sin_addr.s_addr), 4);
		hashkey = sin_addr.s_addr % DELAY_HASHTABLE_WHITELIST_SIZE;

		if (delayp->whitelist[hashkey] == sin_addr.s_addr) {
			return 1;
		}
	} else {
		if (inet_pton(AF_INET6, ip_addr, &ip) <= 0) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Checking delays: for IP %s. Don't know what it is", ip_addr);
			return 0;
		}
	}

	hashkey = (ip.s6_addr32[0] ^ ip.s6_addr32[1] ^ ip.s6_addr32[2] ^ ip.s6_addr32[3]) % DELAY_HASHTABLE_SIZE;

	/* If a delaypool fillup is ongoing, just skip accounting to not block on a lock */
	if (delayp->locked) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "skipping delay pool accounting, during fillup procedure\n");
		return 1;
	}

	if (get_global_lock(r, delaypool_mutex) == 0) {
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Could not acquire lock, skipping delay pool accounting\n");
		return 1;
	};

	if (memcmp(&(delayp->users[hashkey].ip_addr), &ip, sizeof(struct in6_addr)) == 0) {
		/* Repeat the process to determine if we have tockens in the bucket, as the fillup only runs once a client hits an empty bucket,
		   so in the mean time, the bucket might have been filled */
		for (j = 0; j < 3; j++) {
			// ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking delays: Current poolsize: %i tiles and %i renders\n", delayp->users[hashkey].available_tiles, delayp->users[hashkey].available_render_req);
			delay = 0;

			if (delayp->users[hashkey].available_tiles > 0) {
				delayp->users[hashkey].available_tiles--;
			} else {
				delay = 1;
			}

			if (state == tileMissing) {
				if (delayp->users[hashkey].available_render_req > 0) {
					delayp->users[hashkey].available_render_req--;
				} else {
					delay = 2;
				}
			}

			if (delay > 0) {
				/* If we are on the second round, we really  hit an empty delaypool, timeout for a while to slow down clients */
				if (j > 0) {
					apr_global_mutex_unlock(delaypool_mutex);
					ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Delaypool: Client %s has hit its limits, throttling (%i)\n", ip_addr, delay);
					sleep(CLIENT_PENALTY);

					if (get_global_lock(r, delaypool_mutex) == 0) {
						ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Could not acquire lock, but had to delay\n");
						return 0;
					};
				}

				/* We hit an empty bucket, so run the bucket fillup procedure to check if new tokens should have arrived in the mean time. */
				now = apr_time_now();
				tiles_topup = (now - delayp->last_tile_fillup) / scfg->delaypool_tile_rate;
				render_topup = (now - delayp->last_render_fillup) / scfg->delaypool_render_rate;

				// ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Filling up pools with %i tiles and %i renders\n", tiles_topup, render_topup);
				if ((tiles_topup > 0) || (render_topup > 0)) {
					delayp->locked = 1;

					for (i = 0; i < DELAY_HASHTABLE_SIZE; i++) {
						delayp->users[i].available_tiles += tiles_topup;
						delayp->users[i].available_render_req += render_topup;

						if (delayp->users[i].available_tiles > scfg->delaypool_tile_size) {
							delayp->users[i].available_tiles = scfg->delaypool_tile_size;
						}

						if (delayp->users[i].available_render_req > scfg->delaypool_render_size) {
							delayp->users[i].available_render_req = scfg->delaypool_render_size;
						}
					}

					delayp->locked = 0;
				}

				delayp->last_tile_fillup += scfg->delaypool_tile_rate * tiles_topup;
				delayp->last_render_fillup += scfg->delaypool_render_rate * render_topup;

			} else {
				break;
			}
		}
	} else {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Creating a new delaypool for ip %s\n", ip_addr);
		memcpy(&(delayp->users[hashkey].ip_addr), &ip, sizeof(struct in6_addr));
		delayp->users[hashkey].available_tiles = scfg->delaypool_tile_size;
		delayp->users[hashkey].available_render_req = scfg->delaypool_render_size;
		delay = 0;
	}

	apr_global_mutex_unlock(delaypool_mutex);

	if (delay > 0) {
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Delaypool: Client %s has hit its limits, rejecting (%i)\n", ip_addr, delay);
		return 0;
	} else {
		return 1;
	}
}

static int tile_storage_hook(request_rec *r)
{
	//    char abs_path[PATH_MAX];
	double avg;
	int renderPrio = 0;
	enum tileState state;
	tile_server_conf *scfg;
	struct tile_request_data *rdata;
	struct protocol *cmd;

	if (!r->handler) {
		return DECLINED;
	}

	ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_storage_hook: handler(%s), uri(%s)",
		      r->handler, r->uri);

	// Any status request is OK. tile_dirty also doesn't need to be handled, as tile_handler_dirty will take care of it
	if (!strcmp(r->handler, "tile_status") || !strcmp(r->handler, "tile_dirty") || !strcmp(r->handler, "tile_mod_stats") || !(strcmp(r->handler, "tile_json"))) {
		return OK;
	}

	if (strcmp(r->handler, "tile_serve")) {
		return DECLINED;
	}

	rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	cmd = rdata->cmd;

	if (cmd == NULL) {
		return DECLINED;
	}

	avg = get_load_avg();
	state = tile_state(r, cmd);

	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (scfg->enable_tile_throttling && !delay_allowed(r, state)) {
		if (!incRespCounter(HTTP_SERVICE_UNAVAILABLE, r, cmd, rdata->layerNumber)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase response stats counter");
		}

		return HTTP_SERVICE_UNAVAILABLE;
	}

	switch (state) {
		case tileCurrent:
			if (!incFreshCounter(FRESH, r)) {
				ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
					      "Failed to increase fresh stats counter");
			}

			return OK;
			break;

		case tileOld:
		case tileVeryOld:
			if (scfg->enable_bulk_mode) {
				return OK;
			} else if (avg > scfg->max_load_old) {
				// Too much load to render it now, mark dirty but return old tile
				request_tile(r, cmd, 0);
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Load (%f) greater than max_load_old (%d). Mark dirty and deliver from cache.", avg, scfg->max_load_old);

				if (!incFreshCounter((state == tileVeryOld) ? VERYOLD : OLD, r)) {
					ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
						      "Failed to increase fresh stats counter");
				}

				return OK;
			}

			renderPrio = (state == tileVeryOld) ? 2 : 1;
			break;

		case tileMissing:
			if (avg > scfg->max_load_missing) {
				request_tile(r, cmd, 0);
				ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Load (%f) greater than max_load_missing (%d). Return HTTP_NOT_FOUND.", avg, scfg->max_load_missing);

				if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
					ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
						      "Failed to increase response stats counter");
				}

				return HTTP_NOT_FOUND;
			}

			renderPrio = 3;
			break;
	}

	if (request_tile(r, cmd, renderPrio)) {
		// TODO: update finfo
		if (!incFreshCounter(FRESH_RENDER, r)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase fresh stats counter");
		}

		return OK;
	}

	if (state == tileOld) {
		if (!incFreshCounter(OLD_RENDER, r)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase fresh stats counter");
		}

		return OK;
	}

	if (state == tileVeryOld) {
		if (!incFreshCounter(VERYOLD_RENDER, r)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase fresh stats counter");
		}

		return OK;
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_storage_hook: Missing tile was not rendered in time. Returning File Not Found");

	if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
			      "Failed to increase response stats counter");
	}

	return HTTP_NOT_FOUND;
}

static int tile_translate(request_rec *r)
{
	int i, n, limit, oob;
	char option[11];
	char extension[256];

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	tile_config_rec *tile_configs = (tile_config_rec *)scfg->configs->elts;

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: uri(%s)", r->uri);

	/*
	 * The page /mod_tile returns global stats about the number of tiles
	 * handled and in what state those tiles were.
	 * This should probably not be hard coded
	 */
	if (!strncmp("/mod_tile", r->uri, strlen("/mod_tile"))) {
		r->handler = "tile_mod_stats";
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
			      "tile_translate: retrieving global mod_tile stats");
		return OK;
	}

	/*
	 * The page /metrics returns global stats in Prometheus format.
	 */
	if (!strncmp("/metrics", r->uri, strlen("/metrics"))) {
		r->handler = "tile_metrics";
		ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
			      "tile_translate: retrieving global mod_tile metrics");
		return OK;
	}

	for (i = 0; i < scfg->configs->nelts; ++i) {
		tile_config_rec *tile_config = &tile_configs[i];

		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: testing baseuri(%s) name(%s) extension(%s)",
			      tile_config->baseuri, tile_config->xmlname, tile_config->fileExtension);

		if (!strncmp(tile_config->baseuri, r->uri, strlen(tile_config->baseuri))) {

			struct tile_request_data *rdata = (struct tile_request_data *)apr_pcalloc(r->pool, sizeof(struct tile_request_data));
			struct protocol *cmd = (struct protocol *)apr_pcalloc(r->pool, sizeof(struct protocol));
			bzero(cmd, sizeof(struct protocol));
			bzero(rdata, sizeof(struct tile_request_data));

			if (!strncmp(r->uri + strlen(tile_config->baseuri), "tile-layer.json", strlen("tile-layer.json"))) {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Requesting tileJSON for tilelayer %s", tile_config->xmlname);
				r->handler = "tile_json";
				rdata->layerNumber = i;
				ap_set_module_config(r->request_config, &tile_module, rdata);
				return OK;
			}

			char parameters[XMLCONFIG_MAX];

			if (tile_config->enableOptions) {
				cmd->ver = PROTO_VER;
				n = sscanf(r->uri + strlen(tile_config->baseuri), "%40[^/]/%d/%d/%d.%255[a-z]/%10s", parameters, &(cmd->z), &(cmd->x), &(cmd->y), extension, option);

				if (n < 5) {
					ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Invalid URL for tilelayer %s with options", tile_config->xmlname);
					return DECLINED;
				}
			} else {
				cmd->ver = 2;
				n = sscanf(r->uri + strlen(tile_config->baseuri), "%d/%d/%d.%255[a-z]/%10s", &(cmd->z), &(cmd->x), &(cmd->y), extension, option);

				if (n < 4) {
					ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Invalid URL for tilelayer %s without options", tile_config->xmlname);
					return DECLINED;
				}

				parameters[0] = 0;
			}

			if (strcmp(extension, tile_config->fileExtension) != 0) {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Invalid file extension (%s) for tilelayer %s, required %s",
					      extension, tile_config->xmlname, tile_config->fileExtension);
				return DECLINED;
			}

			oob = (cmd->z < tile_config->minzoom || cmd->z > tile_config->maxzoom);

			if (!oob) {
				// valid x/y for tiles are 0 ... 2^zoom-1
				limit = (1 << cmd->z);
				oob = (cmd->x < 0 || cmd->x > (limit * tile_config->aspect_x - 1) || cmd->y < 0 || cmd->y > (limit * tile_config->aspect_y - 1));
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: request for %s was %i %i %i", tile_config->xmlname, cmd->x, cmd->y, limit);
			}

			if (oob) {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: request for %s was outside of allowed bounds", tile_config->xmlname);
				sleep(CLIENT_PENALTY);
				// Don't increase stats counter here,
				// As we are interested in valid tiles only
				return HTTP_NOT_FOUND;
			}

			strcpy(cmd->xmlname, tile_config->xmlname);
			strcpy(cmd->mimetype, tile_config->mimeType);
			strcpy(cmd->options, parameters);

			// Store a copy for later
			rdata->cmd = cmd;
			rdata->layerNumber = i;
			rdata->store = get_storage_backend(r, i);

			if (rdata->store == NULL || rdata->store->storage_ctx == NULL) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "tile_translate: failed to get valid storage backend/storage backend context");

				if (!incRespCounter(HTTP_INTERNAL_SERVER_ERROR, r, cmd, rdata->layerNumber)) {
					ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Failed to increase response stats counter");
				}

				return HTTP_INTERNAL_SERVER_ERROR;
			}

			ap_set_module_config(r->request_config, &tile_module, rdata);

			r->filename = NULL;

			if ((tile_config->enableOptions && (n == 6)) || (!tile_config->enableOptions && (n == 5))) {
				if (!strcmp(option, "status")) {
					r->handler = "tile_status";
				} else if (!strcmp(option, "dirty")) {
					r->handler = "tile_dirty";
				} else {
					return DECLINED;
				}
			} else {
				r->handler = "tile_serve";
			}

			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: op(%s) xml(%s) mime(%s) z(%d) x(%d) y(%d)",
				      r->handler, cmd->xmlname, tile_config->mimeType, cmd->z, cmd->x, cmd->y);

			return OK;
		}
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: No suitable tile layer found");
	return DECLINED;
}

static int tile_handler_dirty(request_rec *r)
{
	tile_server_conf *scfg;
	struct tile_request_data *rdata;
	struct protocol *cmd;

	if (strcmp(r->handler, "tile_dirty")) {
		return DECLINED;
	}

	rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	cmd = rdata->cmd;

	if (cmd == NULL) {
		return DECLINED;
	}

	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	// Is /dirty URL enabled?
	if (!scfg->enable_dirty_url) {
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "tile_handler_dirty: /dirty URL is not enabled");
		return HTTP_NOT_FOUND;
	}

	if (scfg->enable_bulk_mode) {
		return OK;
	}

	request_tile(r, cmd, 0);
	return error_message(r, "Tile submitted for rendering\n");
}

static int tile_handler_status(request_rec *r)
{
	tile_server_conf *scfg;
	enum tileState state;
	char mtime_str[APR_CTIME_LEN];
	char atime_str[APR_CTIME_LEN];
	char storage_id[PATH_MAX];
	struct tile_request_data *rdata;
	struct protocol *cmd;

	if (strcmp(r->handler, "tile_status")) {
		return DECLINED;
	}

	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	// Is /status URL enabled?
	if (!scfg->enable_status_url) {
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "tile_handler_status: /status URL is not enabled");
		return HTTP_NOT_FOUND;
	}

	rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	cmd = rdata->cmd;

	if (cmd == NULL) {
		sleep(CLIENT_PENALTY);
		return HTTP_NOT_FOUND;
	}

	state = tile_state(r, cmd);

	if (state == tileMissing)
		return error_message(r, "No tile could not be found at storage location: %s\n",
				     rdata->store->tile_storage_id(rdata->store, cmd->xmlname, cmd->options, cmd->x, cmd->y, cmd->z, storage_id));

	apr_ctime(mtime_str, r->finfo.mtime);
	apr_ctime(atime_str, r->finfo.atime);

	return error_message(r, "Tile is %s. Last rendered at %s. Last accessed at %s. Stored in %s\n\n"
			     "(Dates might not be accurate. Rendering time might be reset to an old date for tile expiry."
			     " Access times might not be updated on all file systems)\n",
			     (state == tileOld) ? "due to be rendered" : "clean", mtime_str, atime_str,
			     rdata->store->tile_storage_id(rdata->store, cmd->xmlname, cmd->options, cmd->x, cmd->y, cmd->z, storage_id));
}

/**
 * Implement a tilejson description page for the tile layer.
 * This follows the tilejson specification of mapbox ( https://github.com/mapbox/tilejson-spec/tree/master/2.0.0 )
 */
static int tile_handler_json(request_rec *r)
{
	char *buf;
	int len;
	char *timestr;
	long int maxAge = 7 * 24 * 60 * 60;
	apr_table_t *t = r->headers_out;
	int i;
	char *md5;
	struct tile_request_data *rdata;
	tile_server_conf *scfg;
	tile_config_rec *tile_configs;
	tile_config_rec *tile_config;

	if (strcmp(r->handler, "tile_json")) {
		return DECLINED;
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Handling tile json request\n");

	rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	tile_configs = (tile_config_rec *)scfg->configs->elts;
	tile_config = &tile_configs[rdata->layerNumber];
	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Handling tile json request for layer %s\n", tile_config->xmlname);

	if (tile_config->cors) {
		int resp = add_cors(r, tile_config->cors);

		if (resp != DONE) {
			return resp;
		}
	}

	buf = (char *)malloc(8 * 1024);

	snprintf(buf, 8 * 1024,
		 "{\n"
		 "\t\"tilejson\": \"2.0.0\",\n"
		 "\t\"schema\": \"xyz\",\n"
		 "\t\"name\": \"%s\",\n"
		 "\t\"description\": \"%s\",\n"
		 "\t\"attribution\": \"%s\",\n"
		 "\t\"minzoom\": %i,\n"
		 "\t\"maxzoom\": %i,\n"
		 "\t\"tiles\": [\n",
		 tile_config->xmlname, (tile_config->description ? tile_config->description : ""), tile_config->attribution, tile_config->minzoom, tile_config->maxzoom);

	for (i = 0; i < tile_config->noHostnames; i++) {
		strncat(buf, "\t\t\"", 8 * 1024 - strlen(buf) - 1);
		strncat(buf, tile_config->hostnames[i], 8 * 1024 - strlen(buf) - 1);
		strncat(buf, tile_config->baseuri, 8 * 1024 - strlen(buf) - 1);
		strncat(buf, "{z}/{x}/{y}.", 8 * 1024 - strlen(buf) - 1);
		strncat(buf, tile_config->fileExtension, 8 * 1024 - strlen(buf) - 1);
		strncat(buf, "\"", 8 * 1024 - strlen(buf) - 1);

		if (i < tile_config->noHostnames - 1) {
			strncat(buf, ",", 8 * 1024 - strlen(buf) - 1);
		}

		strncat(buf, "\n", 8 * 1024 - strlen(buf) - 1);
	}

	strncat(buf, "\t]\n}\n", 8 * 1024 - strlen(buf) - 1);
	len = strlen(buf);

	/*
	 * Add HTTP headers. Make this file cachable for 1 week
	 */
	md5 = ap_md5_binary(r->pool, (unsigned char *)buf, len);
	apr_table_setn(r->headers_out, "ETag",
		       apr_psprintf(r->pool, "\"%s\"", md5));
	ap_set_content_type(r, "application/json");
	ap_set_content_length(r, len);
	apr_table_mergen(t, "Cache-Control",
			 apr_psprintf(r->pool, "max-age=%li", maxAge));
	timestr = (char *)apr_palloc(r->pool, APR_RFC822_DATE_LEN);
	apr_rfc822_date(timestr, (apr_time_from_sec(maxAge) + r->request_time));
	apr_table_setn(t, "Expires", timestr);
	ap_rwrite(buf, len, r);
	free(buf);

	return OK;
}

static int tile_handler_mod_stats(request_rec *r)
{
	stats_data *stats;
	stats_data local_stats;
	int i;
	tile_server_conf *scfg;
	tile_config_rec *tile_configs;

	if (strcmp(r->handler, "tile_mod_stats")) {
		return DECLINED;
	}

	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	tile_configs = (tile_config_rec *)scfg->configs->elts;

	if (!scfg->enable_global_stats) {
		return error_message(r, "Stats are not enabled for this server");
	}

	if (get_global_lock(r, stats_mutex) != 0) {
		// Copy over the global counter variable into
		// local variables, that we can immediately
		// release the lock again
		stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
		memcpy(&local_stats, stats, sizeof(stats_data));
		local_stats.noResp200Layer = (apr_uint64_t *)malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
		memcpy(local_stats.noResp200Layer, stats->noResp200Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
		local_stats.noResp404Layer = (apr_uint64_t *)malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
		memcpy(local_stats.noResp404Layer, stats->noResp404Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
		apr_global_mutex_unlock(stats_mutex);
	} else {
		return error_message(r, "Failed to acquire lock, can't display stats");
	}

	ap_rprintf(r, "NoResp200: %" APR_UINT64_T_FMT "\n", local_stats.noResp200);
	ap_rprintf(r, "NoResp304: %" APR_UINT64_T_FMT "\n", local_stats.noResp304);
	ap_rprintf(r, "NoResp404: %" APR_UINT64_T_FMT "\n", local_stats.noResp404);
	ap_rprintf(r, "NoResp503: %" APR_UINT64_T_FMT "\n", local_stats.noResp503);
	ap_rprintf(r, "NoResp5XX: %" APR_UINT64_T_FMT "\n", local_stats.noResp5XX);
	ap_rprintf(r, "NoRespOther: %" APR_UINT64_T_FMT "\n", local_stats.noRespOther);
	ap_rprintf(r, "NoFreshCache: %" APR_UINT64_T_FMT "\n", local_stats.noFreshCache);
	ap_rprintf(r, "NoOldCache: %" APR_UINT64_T_FMT "\n", local_stats.noOldCache);
	ap_rprintf(r, "NoVeryOldCache: %" APR_UINT64_T_FMT "\n", local_stats.noVeryOldCache);
	ap_rprintf(r, "NoFreshRender: %" APR_UINT64_T_FMT "\n", local_stats.noFreshRender);
	ap_rprintf(r, "NoOldRender: %" APR_UINT64_T_FMT "\n", local_stats.noOldRender);
	ap_rprintf(r, "NoVeryOldRender: %" APR_UINT64_T_FMT "\n", local_stats.noVeryOldRender);

	for (i = 0; i <= global_max_zoom; i++) {
		ap_rprintf(r, "NoRespZoom%02i: %" APR_UINT64_T_FMT "\n", i, local_stats.noRespZoom[i]);
	}

	ap_rprintf(r, "NoTileBufferReads: %" APR_UINT64_T_FMT "\n", local_stats.noTotalBufferRetrieval);
	ap_rprintf(r, "DurationTileBufferReads: %" APR_UINT64_T_FMT "\n", local_stats.totalBufferRetrievalTime);

	for (i = 0; i <= global_max_zoom; i++) {
		ap_rprintf(r, "NoTileBufferReadZoom%02i: %" APR_UINT64_T_FMT "\n", i, local_stats.noZoomBufferRetrieval[i]);
		ap_rprintf(r, "DurationTileBufferReadZoom%02i: %" APR_UINT64_T_FMT "\n", i, local_stats.zoomBufferRetrievalTime[i]);
	}

	for (i = 0; i < scfg->configs->nelts; ++i) {
		tile_config_rec *tile_config = &tile_configs[i];
		ap_rprintf(r, "NoRes200Layer%s: %" APR_UINT64_T_FMT "\n", tile_config->baseuri, local_stats.noResp200Layer[i]);
		ap_rprintf(r, "NoRes404Layer%s: %" APR_UINT64_T_FMT "\n", tile_config->baseuri, local_stats.noResp404Layer[i]);
	}

	free(local_stats.noResp200Layer);
	free(local_stats.noResp404Layer);
	return OK;
}

static int tile_handler_metrics(request_rec *r)
{
	stats_data *stats;
	stats_data local_stats;
	int i;
	tile_server_conf *scfg;
	tile_config_rec *tile_configs;

	if (strcmp(r->handler, "tile_metrics")) {
		return DECLINED;
	}

	scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);
	tile_configs = (tile_config_rec *)scfg->configs->elts;

	if (!scfg->enable_global_stats) {
		return error_message(r, "Stats are not enabled for this server");
	}

	if (get_global_lock(r, stats_mutex) != 0) {
		// Copy over the global counter variable into
		// local variables, that we can immediately
		// release the lock again
		stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
		memcpy(&local_stats, stats, sizeof(stats_data));
		local_stats.noResp200Layer = (apr_uint64_t *)malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
		memcpy(local_stats.noResp200Layer, stats->noResp200Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
		local_stats.noResp404Layer = (apr_uint64_t *)malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
		memcpy(local_stats.noResp404Layer, stats->noResp404Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
		apr_global_mutex_unlock(stats_mutex);
	} else {
		return error_message(r, "Failed to acquire lock, can't display stats");
	}

	ap_rprintf(r, "# HELP modtile_http_responses_total Number of HTTP responses by response code\n");
	ap_rprintf(r, "# TYPE modtile_http_responses_total counter\n");
	ap_rprintf(r, "modtile_http_responses_total{status=\"200\"} %" APR_UINT64_T_FMT "\n", local_stats.noResp200);
	ap_rprintf(r, "modtile_http_responses_total{status=\"304\"} %" APR_UINT64_T_FMT "\n", local_stats.noResp304);
	ap_rprintf(r, "modtile_http_responses_total{status=\"404\"} %" APR_UINT64_T_FMT "\n", local_stats.noResp404);
	ap_rprintf(r, "modtile_http_responses_total{status=\"503\"} %" APR_UINT64_T_FMT "\n", local_stats.noResp503);
	ap_rprintf(r, "modtile_http_responses_total{status=\"5XX\"} %" APR_UINT64_T_FMT "\n", local_stats.noResp5XX);
	ap_rprintf(r, "modtile_http_responses_total{status=\"other\"} %" APR_UINT64_T_FMT "\n", local_stats.noRespOther);

	ap_rprintf(r, "# HELP modtile_tiles_total Tiles served\n");
	ap_rprintf(r, "# TYPE modtile_tiles_total counter\n");
	ap_rprintf(r, "modtile_tiles_total{age=\"fresh\",rendered=\"no\"} %" APR_UINT64_T_FMT "\n", local_stats.noFreshCache);
	ap_rprintf(r, "modtile_tiles_total{age=\"old\",rendered=\"no\"} %" APR_UINT64_T_FMT "\n", local_stats.noOldCache);
	ap_rprintf(r, "modtile_tiles_total{age=\"outdated\",rendered=\"no\"} %" APR_UINT64_T_FMT "\n", local_stats.noVeryOldCache);
	ap_rprintf(r, "modtile_tiles_total{age=\"fresh\",rendered=\"yes\"} %" APR_UINT64_T_FMT "\n", local_stats.noFreshRender);
	ap_rprintf(r, "modtile_tiles_total{age=\"old\",rendered=\"attempted\"} %" APR_UINT64_T_FMT "\n", local_stats.noOldRender);
	ap_rprintf(r, "modtile_tiles_total{age=\"outdated\",rendered=\"attempted\"} %" APR_UINT64_T_FMT "\n", local_stats.noVeryOldRender);

	ap_rprintf(r, "# HELP modtile_zoom_responses_total Tiles served by zoom level\n");
	ap_rprintf(r, "# TYPE modtile_zoom_responses_total counter\n");

	for (i = 0; i <= global_max_zoom; i++) {
		ap_rprintf(r, "modtile_zoom_responses_total{zoom=\"%02i\"} %" APR_UINT64_T_FMT "\n", i, local_stats.noRespZoom[i]);
	}

	ap_rprintf(r, "# HELP modtile_tile_reads_total Tiles served from the tile buffer\n");
	ap_rprintf(r, "# TYPE modtile_tile_reads_total counter\n");

	for (i = 0; i <= global_max_zoom; i++) {
		ap_rprintf(r, "modtile_tile_reads_total{zoom=\"%02i\"} %" APR_UINT64_T_FMT "\n", i, local_stats.noZoomBufferRetrieval[i]);
	}

	ap_rprintf(r, "# HELP modtile_tile_reads_seconds_total Tile buffer duration\n");
	ap_rprintf(r, "# TYPE modtile_tile_reads_seconds_total counter\n");

	for (i = 0; i <= global_max_zoom; i++) {
		ap_rprintf(r, "modtile_tile_reads_seconds_total{zoom=\"%02i\"} %lf\n", i, (double)local_stats.zoomBufferRetrievalTime[i] / 1000000.0);
	}

	ap_rprintf(r, "# HELP modtile_layer_responses_total Layer responses\n");
	ap_rprintf(r, "# TYPE modtile_layer_responses_total counter\n");

	for (i = 0; i < scfg->configs->nelts; ++i) {
		tile_config_rec *tile_config = &tile_configs[i];
		ap_rprintf(r, "modtile_layer_responses_total{layer=\"%s\",status=\"200\"} %" APR_UINT64_T_FMT "\n", tile_config->baseuri, local_stats.noResp200Layer[i]);
		ap_rprintf(r, "modtile_layer_responses_total{layer=\"%s\",status=\"404\"} %" APR_UINT64_T_FMT "\n", tile_config->baseuri, local_stats.noResp404Layer[i]);
	}

	free(local_stats.noResp200Layer);
	free(local_stats.noResp404Layer);
	return OK;
}

static int tile_handler_serve(request_rec *r)
{
	const int tile_max = MAX_SIZE;
	char err_msg[PATH_MAX];
	char id[PATH_MAX];
	char *buf;
	int len;
	int compressed;
	apr_status_t errstatus;
	struct timeval start, end;
	char *md5;
	tile_config_rec *tile_configs;
	struct tile_request_data *rdata;
	struct protocol *cmd;

	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(r->server->module_config, &tile_module);

	if (strcmp(r->handler, "tile_serve")) {
		return DECLINED;
	}

	rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
	cmd = rdata->cmd;

	if (cmd == NULL) {
		sleep(CLIENT_PENALTY);

		if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase response stats counter");
		}

		return HTTP_NOT_FOUND;
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_handler_serve: xml(%s) z(%d) x(%d) y(%d)", cmd->xmlname, cmd->z, cmd->x, cmd->y);

	tile_configs = (tile_config_rec *)scfg->configs->elts;

	if (tile_configs[rdata->layerNumber].cors) {
		int resp = add_cors(r, tile_configs[rdata->layerNumber].cors);

		if (resp != DONE) {
			return resp;
		}
	}

	gettimeofday(&start, NULL);

	// FIXME: It is a waste to do the malloc + read if we are fulfilling a HEAD or returning a 304.
	buf = (char *)malloc(tile_max);

	if (!buf) {
		if (!incRespCounter(HTTP_INTERNAL_SERVER_ERROR, r, cmd, rdata->layerNumber)) {
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
				      "Failed to increase response stats counter");
		}

		return HTTP_INTERNAL_SERVER_ERROR;
	}

	err_msg[0] = 0;

	len = rdata->store->tile_read(rdata->store, cmd->xmlname, cmd->options, cmd->x, cmd->y, cmd->z, buf, tile_max, &compressed, err_msg);
	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
		      "Read tile of length %i from %s: %s", len, rdata->store->tile_storage_id(rdata->store, cmd->xmlname, cmd->options, cmd->x, cmd->y, cmd->z, id), err_msg);

	if (len > 0) {
		if (compressed) {
			const char *accept_encoding = apr_table_get(r->headers_in, "Accept-Encoding");

			if (accept_encoding && strstr(accept_encoding, "gzip")) {
				r->content_encoding = "gzip";
			} else {
				ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
					      "Tile data is compressed, but user agent doesn't support Content-Encoding and we don't know how to decompress it server side");
				// TODO: decompress the output stream before sending it to client
			}
		}

#if 0
		// Set default Last-Modified and Etag headers
		ap_update_mtime(r, r->finfo.mtime);
		ap_set_last_modified(r);
		ap_set_etag(r);
#else
		// Use MD5 hash as only cache attribute.
		// If a tile is re-rendered and produces the same output
		// then we can continue to use the previous cached copy
		md5 = ap_md5_binary(r->pool, (unsigned char *)buf, len);
		apr_table_setn(r->headers_out, "ETag",
			       apr_psprintf(r->pool, "\"%s\"", md5));
#endif
		ap_set_content_type(r, tile_configs[rdata->layerNumber].mimeType);
		ap_set_content_length(r, len);
		add_expiry(r, cmd);

		gettimeofday(&end, NULL);
		incTimingCounter((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec), cmd->z, r);

		if ((errstatus = ap_meets_conditions(r)) != OK) {
			free(buf);

			if (!incRespCounter(errstatus, r, cmd, rdata->layerNumber)) {
				ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
					      "Failed to increase response stats counter");
			}

			return errstatus;
		} else {
			ap_rwrite(buf, len, r);
			free(buf);

			if (!incRespCounter(errstatus, r, cmd, rdata->layerNumber)) {
				ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
					      "Failed to increase response stats counter");
			}

			return OK;
		}
	}

	free(buf);
	ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Failed to read tile from disk: %s", err_msg);

	if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
			      "Failed to increase response stats counter");
	}

	return DECLINED;
}

/*
 * This routine is called in the parent, so we'll set up the shared
 * memory segment and mutex here.
 */

static int mod_tile_post_config(apr_pool_t *pconf, apr_pool_t *plog,
				apr_pool_t *ptemp, server_rec *s)
{
	void *data; /* These two help ensure that we only init once. */
	const char *userdata_key = "mod_tile_init_module";
	apr_status_t rs;
	stats_data *stats;
	delaypool *delayp;
	int i;

	/*
	 * The following checks if this routine has been called before.
	 * This is necessary because the parent process gets initialized
	 * a couple of times as the server starts up, and we don't want
	 * to create any more mutexes and shared memory segments than
	 * we're actually going to use.
	 */
	apr_pool_userdata_get(&data, userdata_key, s->process->pool);

	if (!data) {
		apr_pool_userdata_set((const void *)1, userdata_key,
				      apr_pool_cleanup_null, s->process->pool);
		return OK;
	} /* Kilroy was here */

	/* Create the shared memory segment
	 * would prefer to use scfg->configs->nelts here but that does
	 * not seem to be set at this stage, so rely on previously set layerCount */

	rs = apr_shm_create(&stats_shm, sizeof(stats_data) + layerCount * 2 * sizeof(apr_uint64_t),
			    NULL, pconf);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
			     "Failed to create 'stats' shared memory segment");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	rs = apr_shm_create(&delaypool_shm, sizeof(delaypool),
			    NULL, pconf);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
			     "Failed to create 'delaypool' shared memory segment");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* Created it, now let's zero it out */
	stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
	stats->noResp200 = 0;
	stats->noResp304 = 0;
	stats->noResp404 = 0;
	stats->noResp503 = 0;
	stats->noResp5XX = 0;

	for (i = 0; i <= global_max_zoom; i++) {
		stats->noRespZoom[i] = 0;
	}

	stats->totalBufferRetrievalTime = 0;
	stats->noTotalBufferRetrieval = 0;

	for (i = 0; i <= global_max_zoom; i++) {
		stats->zoomBufferRetrievalTime[i] = 0;
		stats->noZoomBufferRetrieval[i] = 0;
	}

	stats->noRespOther = 0;
	stats->noFreshCache = 0;
	stats->noFreshRender = 0;
	stats->noOldCache = 0;
	stats->noOldRender = 0;

	/* the "stats" block does not have a fixed size; it is a fixed-size struct
	 * followed by two arrays with one element each per layer. All of this sits
	 * in one shared memory block, and for ease of use, pointers from inside the
	 * struct point to the arrays. */
	stats->noResp404Layer = (apr_uint64_t *)((char *)stats + sizeof(stats_data));
	stats->noResp200Layer = (apr_uint64_t *)((char *)stats + sizeof(stats_data) + sizeof(apr_uint64_t) * layerCount);

	/* zero out all the non-fixed-length stuff */
	for (i = 0; i < layerCount; i++) {
		stats->noResp404Layer[i] = 0;
		stats->noResp200Layer[i] = 0;
	}

	delayp = (delaypool *)apr_shm_baseaddr_get(delaypool_shm);

	delayp->last_tile_fillup = apr_time_now();
	delayp->last_render_fillup = apr_time_now();

	for (i = 0; i < DELAY_HASHTABLE_SIZE; i++) {
		memset(&(delayp->users[i].ip_addr), 0, sizeof(struct in6_addr));
		delayp->users[i].available_tiles = 0;
		delayp->users[i].available_render_req = 0;
	}

	for (i = 0; i < DELAY_HASHTABLE_WHITELIST_SIZE; i++) {
		delayp->whitelist[i] = (in_addr_t)0;
	}

	/* TODO: need a way to initialise the delaypool whitelist */

	/* Create global mutex */

	/*
	 * Create another unique filename to lock upon. Note that
	 * depending on OS and locking mechanism of choice, the file
	 * may or may not be actually created.
	 */
	stats_mutexfilename = apr_psprintf(pconf, "%s/httpd_mutex_stats.%ld", P_tmpdir, (long int)getpid());

	rs = apr_global_mutex_create(&stats_mutex, (const char *)stats_mutexfilename,
				     APR_LOCK_DEFAULT, pconf);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
			     "Failed to create mutex on file %s",
			     stats_mutexfilename);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

#ifdef MOD_TILE_SET_MUTEX_PERMS
	rs = ap_unixd_set_global_mutex_perms(stats_mutex);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
			     "Parent could not set permissions on mod_tile "
			     "mutex: check User and Group directives");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

#endif /* MOD_TILE_SET_MUTEX_PERMS */

	/*
	 * Create another unique filename to lock upon. Note that
	 * depending on OS and locking mechanism of choice, the file
	 * may or may not be actually created.
	 */
	delaypool_mutexfilename = apr_psprintf(pconf, "%s/httpd_mutex_delaypool.%ld", P_tmpdir, (long int)getpid());

	rs = apr_global_mutex_create(&delaypool_mutex, (const char *)delaypool_mutexfilename,
				     APR_LOCK_DEFAULT, pconf);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
			     "Failed to create mutex on file %s",
			     delaypool_mutexfilename);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

#ifdef MOD_TILE_SET_MUTEX_PERMS
	rs = ap_unixd_set_global_mutex_perms(delaypool_mutex);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
			     "Parent could not set permissions on mod_tile "
			     "mutex: check User and Group directives");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

#endif /* MOD_TILE_SET_MUTEX_PERMS */

	return OK;
}

/*
 * This routine gets called when a child inits. We use it to attach
 * to the shared memory segment, and reinitialize the mutex and setup
 * connections to storage backends.
 */

static void mod_tile_child_init(apr_pool_t *p, server_rec *s)
{
	apr_status_t rs;

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
		     "Initialising a new Apache child instance");

	/*
	 * Re-open the mutex for the child. Note we're reusing
	 * the mutex pointer global here.
	 */
	rs = apr_global_mutex_child_init(&stats_mutex,
					 (const char *)stats_mutexfilename,
					 p);

	if (rs != APR_SUCCESS) {
		ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
			     "Failed to reopen mutex on file %s",
			     stats_mutexfilename);
		/* There's really nothing else we can do here, since
		 * This routine doesn't return a status. */
		exit(1); /* Ugly, but what else? */
	}
}

static void register_hooks(__attribute__((unused)) apr_pool_t *p)
{
	ap_hook_post_config(mod_tile_post_config, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_child_init(mod_tile_child_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_serve, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_dirty, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_status, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_json, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_mod_stats, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(tile_handler_metrics, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_translate_name(tile_translate, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_map_to_storage(tile_storage_hook, NULL, NULL, APR_HOOK_FIRST);
}

static const char *_add_tile_config(cmd_parms *cmd,
				    const char *baseuri, const char *name, int minzoom, int maxzoom, int aspect_x, int aspect_y,
				    const char *fileExtension, const char *mimeType, const char *description, const char *attribution,
				    const char *server_alias, const char *cors, const char *tile_dir, const int enableOptions)
{
	tile_server_conf *scfg;
	tile_config_rec *tilecfg;

	scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	tilecfg = (tile_config_rec *)apr_array_push(scfg->configs);

	int attribution_len = strnlen(attribution, PATH_MAX);
	int baseuri_len = strnlen(baseuri, PATH_MAX);
	int hostnames_len = 1;
	int server_alias_len = strnlen(server_alias, PATH_MAX);
	int tile_dir_len = strnlen(tile_dir, PATH_MAX);

	// Set attribution to default
	if (attribution_len == 0) {
		attribution = apr_pstrdup(cmd->pool, DEFAULT_ATTRIBUTION);
	}

	// Ensure URI string ends with a trailing slash
	if (baseuri_len == 0) {
		baseuri = apr_pstrdup(cmd->pool, "/");
	} else if (baseuri[baseuri_len - 1] != '/') {
		baseuri = apr_psprintf(cmd->pool, "%s/", baseuri);
	}

	// If server_alias is set, increment hostnames_len
	if (server_alias_len > 0) {
		hostnames_len++;
	}

	// Set tile_dir to default
	if (tile_dir_len == 0) {
		tile_dir = apr_pstrndup(cmd->pool, scfg->tile_dir, PATH_MAX);
	}

	char **hostnames = (char **)malloc(sizeof(char *) * hostnames_len);

	// Set first hostname to server_hostname value (if set,) otherwise use localhost
	if (cmd->server->server_hostname == NULL) {
		hostnames[0] = apr_pstrdup(cmd->pool, "http://localhost");
		ap_log_perror(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, cmd->pool,
			      "Could not determine hostname of server to configure TileJSON request output for '%s', using '%s'.", name, hostnames[0]);
	} else {
		hostnames[0] = apr_pstrcat(cmd->pool, "http://", cmd->server->server_hostname, NULL);
	}

	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool,
		      "Using server hostname '%s' to configure TileJSON request output for '%s'.", hostnames[0], name);

	// Set second hostname to server_alias value (if set)
	if (server_alias_len > 0) {
		// Ensure second hostname string does not end with a trailing slash
		if (server_alias[server_alias_len - 1] == '/') {
			hostnames[1] = apr_pstrndup(cmd->pool, server_alias, server_alias_len - 1);
		} else {
			hostnames[1] = apr_pstrdup(cmd->pool, server_alias);
		}

		ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool,
			      "Also using server hostname '%s' to configure TileJSON request output for '%s'.", hostnames[1], name);
	}

	tilecfg->aspect_x = aspect_x;
	tilecfg->aspect_y = aspect_y;
	tilecfg->attribution = attribution;
	tilecfg->baseuri = baseuri;
	tilecfg->cors = cors;
	tilecfg->description = description;
	tilecfg->enableOptions = enableOptions;
	tilecfg->fileExtension = fileExtension;
	tilecfg->hostnames = hostnames;
	tilecfg->maxzoom = maxzoom;
	tilecfg->mimeType = mimeType;
	tilecfg->minzoom = minzoom;
	tilecfg->noHostnames = hostnames_len;
	tilecfg->store = tile_dir;
	tilecfg->xmlname = name;

	if (maxzoom > global_max_zoom) {
		global_max_zoom = maxzoom;
	}

	ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, cmd->server,
		     "Loading tile config %s at %s for zooms %i - %i from tile directory %s with extension .%s and mime type %s",
		     tilecfg->xmlname, tilecfg->baseuri, tilecfg->minzoom, tilecfg->maxzoom, tilecfg->store, tilecfg->fileExtension, tilecfg->mimeType);

	layerCount++;
	return NULL;
}

static const char *add_tile_mime_config(cmd_parms *cmd, void *mconfig, const char *baseuri, const char *name, const char *fileExtension)
{
	char *cors = NULL;
	char *mimeType = "image/png";

	if (strcmp(fileExtension, "js") == 0) {
		cors = "*";
		mimeType = "text/javascript";
	}

	ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, cmd->server,
		     "AddTileMimeConfig will be deprecated in a future release, please use the following instead: AddTileConfig %s %s mimetype=%s extension=%s",
		     baseuri, name, mimeType, fileExtension);
	return _add_tile_config(cmd, baseuri, name, 0, MAX_ZOOM, 1, 1, fileExtension, mimeType, "", "", "", cors, "", 0);
}

static const char *add_tile_config(cmd_parms *cmd, void *mconfig, int argc, char *const argv[])
{
	if (argc < 1) {
		return ("AddTileConfig error, URL path not defined");
	}

	if (argc < 2) {
		return ("AddTileConfig error, name of renderd config not defined");
	}

	int maxzoom = MAX_ZOOM;
	int minzoom = 0;
	char *baseuri = argv[0];
	char *name = argv[1];
	char *fileExtension = "png";
	char *mimeType = "image/png";
	char *tile_dir = "";

	for (int i = 2; i < argc; i++) {
		char *value = strchr(argv[i], '=');

		if (value) {
			*value++ = 0;

			if (!strcmp(argv[i], "maxzoom")) {
				maxzoom = strtol(value, NULL, 10);
			} else if (!strcmp(argv[i], "minzoom")) {
				minzoom = strtol(value, NULL, 10);
			} else if (!strcmp(argv[i], "extension")) {
				fileExtension = value;
			} else if (!strcmp(argv[i], "mimetype")) {
				mimeType = value;
			} else if (!strcmp(argv[i], "tile_dir")) {
				tile_dir = value;
			}
		}
	}

	if ((minzoom < 0) || (maxzoom > MAX_ZOOM_SERVER)) {
		return "AddTileConfig error, the configured zoom level lies outside of the range supported by this server";
	}

	return _add_tile_config(cmd, baseuri, name, minzoom, maxzoom, 1, 1, fileExtension, mimeType, "", "", "", "", tile_dir, 0);
}

static const char *load_tile_config(cmd_parms *cmd, void *mconfig, const char *config_file_name)
{
	struct stat buffer;

	if (stat(config_file_name, &buffer) != 0) {
		return "LoadTileConfigFile error, unable to open config file";
	}

	const char *result;

	xmlconfigitem maps[XMLCONFIGS_MAX];

	process_map_sections(config_file_name, maps, "", 0);

	for (int i = 0; i < XMLCONFIGS_MAX; i++) {
		if (maps[i].xmlname != NULL) {
			result = _add_tile_config(cmd,
						  maps[i].xmluri, maps[i].xmlname, maps[i].min_zoom, maps[i].max_zoom, maps[i].aspect_x, maps[i].aspect_y,
						  maps[i].file_extension, maps[i].mime_type, maps[i].description, maps[i].attribution,
						  maps[i].server_alias, maps[i].cors, maps[i].tile_dir, strlen(maps[i].parameterization));

			if (result != NULL) {
				return result;
			}
		}
	}

	return NULL;
}

static const char *arg_to_apr_int64_t(cmd_parms *cmd, const char *buf, apr_int64_t *dest, const char *config_directive_name)
{
	char *end;
	apr_int64_t arg = apr_strtoi64(buf, &end, 10);

	if (end == buf) {
		return apr_pstrcat(cmd->pool, config_directive_name, " argument must be an integer", NULL);
	}

	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %" APR_INT64_T_FMT, config_directive_name, arg);
	*dest = arg;
	return NULL;
}

static const char *arg_to_double(cmd_parms *cmd, const char *buf, double *dest, const char *config_directive_name)
{
	char *end;
	double arg = strtod(buf, &end);

	if (end == buf) {
		return apr_pstrcat(cmd->pool, config_directive_name, " argument must be a float", NULL);
	}

	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %f", config_directive_name, arg);
	*dest = arg;
	return NULL;
}

static const char *arg_to_int(cmd_parms *cmd, const char *buf, int *dest, const char *config_directive_name)
{
	char *end;
	int arg = (int)apr_strtoi64(buf, &end, 10);

	if (end == buf) {
		return apr_pstrcat(cmd->pool, config_directive_name, " argument must be an integer", NULL);
	}

	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %i", config_directive_name, arg);
	*dest = arg;
	return NULL;
}

static const char *arg_to_string(cmd_parms *cmd, const char *buf, const char **dest, const char *config_directive_name)
{
	*dest = apr_pstrndup(cmd->pool, buf, PATH_MAX);
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", config_directive_name, *dest);
	return NULL;
}

static const char *mod_tile_request_timeout_config(cmd_parms *cmd, void *mconfig, const char *request_timeout_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, request_timeout_string, &scfg->request_timeout, cmd->directive->directive);
}

static const char *mod_tile_request_timeout_priority_config(cmd_parms *cmd, void *mconfig, const char *request_timeout_priority_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, request_timeout_priority_string, &scfg->request_timeout_priority, cmd->directive->directive);
}

static const char *mod_tile_max_load_old_config(cmd_parms *cmd, void *mconfig, const char *max_load_old_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, max_load_old_string, &scfg->max_load_old, cmd->directive->directive);
}

static const char *mod_tile_max_load_missing_config(cmd_parms *cmd, void *mconfig, const char *max_load_missing_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, max_load_missing_string, &scfg->max_load_missing, cmd->directive->directive);
}

static const char *mod_tile_very_old_threshold_config(cmd_parms *cmd, void *mconfig, const char *very_old_threshold_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_apr_int64_t(cmd, very_old_threshold_string, &scfg->very_old_threshold, cmd->directive->directive);
}

static const char *mod_tile_renderd_socket_name_config(cmd_parms *cmd, void *mconfig, const char *renderd_socket_name_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_string(cmd, renderd_socket_name_string, &scfg->renderd_socket_name, cmd->directive->directive);
}

static const char *mod_tile_renderd_socket_address_config(cmd_parms *cmd, void *mconfig, const char *renderd_socket_address_string, const char *renderd_socket_port_string)
{
	const char *renderd_socket_address_result;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	renderd_socket_address_result = arg_to_string(cmd, renderd_socket_address_string, &scfg->renderd_socket_name, "ModTileRenderdSocketAddr first");

	if (renderd_socket_address_result != NULL) {
		return renderd_socket_address_result;
	}

	return arg_to_int(cmd, renderd_socket_port_string, &scfg->renderd_socket_port, "ModTileRenderdSocketAddr second");
}

static const char *mod_tile_tile_dir_config(cmd_parms *cmd, void *mconfig, const char *tile_dir_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_string(cmd, tile_dir_string, &scfg->tile_dir, cmd->directive->directive);
}

static const char *mod_tile_cache_extended_hostname_config(cmd_parms *cmd, void *mconfig, const char *cache_extended_hostname_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_string(cmd, cache_extended_hostname_string, &scfg->cache_extended_hostname, cmd->directive->directive);
}

static const char *mod_tile_cache_extended_duration_config(cmd_parms *cmd, void *mconfig, const char *cache_extended_duration_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, cache_extended_duration_string, &scfg->cache_extended_duration, cmd->directive->directive);
}

static const char *mod_tile_cache_duration_last_modified_factor_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_last_modified_factor_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_double(cmd, cache_duration_last_modified_factor_string, &scfg->cache_duration_last_modified_factor, cmd->directive->directive);
}

static const char *mod_tile_cache_duration_max_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_max_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, cache_duration_max_string, &scfg->cache_duration_max, cmd->directive->directive);
}

static const char *mod_tile_cache_duration_dirty_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_dirty_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, cache_duration_dirty_string, &scfg->cache_duration_dirty, cmd->directive->directive);
}

static const char *mod_tile_cache_duration_minimum_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_minimum_string)
{
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	return arg_to_int(cmd, cache_duration_minimum_string, &scfg->cache_duration_minimum, cmd->directive->directive);
}

static const char *mod_tile_cache_duration_low_config(cmd_parms *cmd, void *mconfig, const char *cache_level_low_zoom_string, const char *cache_duration_low_zoom_string)
{
	const char *cache_level_low_zoom_result;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	cache_level_low_zoom_result = arg_to_int(cmd, cache_level_low_zoom_string, &scfg->cache_level_low_zoom, "ModTileCacheDurationLowZoom first");

	if (cache_level_low_zoom_result != NULL) {
		return cache_level_low_zoom_result;
	}

	return arg_to_int(cmd, cache_duration_low_zoom_string, &scfg->cache_duration_low_zoom, "ModTileCacheDurationLowZoom second");
}

static const char *mod_tile_cache_duration_medium_config(cmd_parms *cmd, void *mconfig, const char *cache_level_medium_zoom_string, const char *cache_duration_medium_zoom_string)
{
	const char *cache_level_medium_zoom_result;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	cache_level_medium_zoom_result = arg_to_int(cmd, cache_level_medium_zoom_string, &scfg->cache_level_medium_zoom, "ModTileCacheDurationMediumZoom first");

	if (cache_level_medium_zoom_result != NULL) {
		return cache_level_medium_zoom_result;
	}

	return arg_to_int(cmd, cache_duration_medium_zoom_string, &scfg->cache_duration_medium_zoom, "ModTileCacheDurationMediumZoom second");
}

static const char *mod_tile_enable_stats(cmd_parms *cmd, void *mconfig, int enable_global_stats)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_global_stats ? "On" : "Off");
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	scfg->enable_global_stats = enable_global_stats;
	return NULL;
}

static const char *mod_tile_enable_throttling(cmd_parms *cmd, void *mconfig, int enable_tile_throttling)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_tile_throttling ? "On" : "Off");
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	scfg->enable_tile_throttling = enable_tile_throttling;
	return NULL;
}

static const char *mod_tile_enable_throttling_xforward(cmd_parms *cmd, void *mconfig, const char *enable_tile_throttling_xforward_string)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_tile_throttling_xforward_string);
	const char *enable_tile_throttling_xforward_result;
	int enable_tile_throttling_xforward;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	enable_tile_throttling_xforward_result = arg_to_int(cmd, enable_tile_throttling_xforward_string, &enable_tile_throttling_xforward, cmd->directive->directive);

	if (enable_tile_throttling_xforward_result != NULL) {
		return enable_tile_throttling_xforward_result;
	}

	if ((enable_tile_throttling_xforward < 0) || (enable_tile_throttling_xforward > 2)) {
		return "ModTileEnableTileThrottlingXForward needs integer argument between 0 and 2 (0 => off; 1 => use client; 2 => use last entry in chain";
	}

	scfg->enable_tile_throttling_xforward = enable_tile_throttling_xforward;
	return NULL;
}

static const char *mod_tile_enable_bulk_mode(cmd_parms *cmd, void *mconfig, int enable_bulk_mode)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_bulk_mode ? "On" : "Off");
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	scfg->enable_bulk_mode = enable_bulk_mode;
	return NULL;
}

static const char *mod_tile_enable_status_url(cmd_parms *cmd, void *mconfig, int enable_status_url)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_status_url ? "On" : "Off");
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	scfg->enable_status_url = enable_status_url;
	return NULL;
}

static const char *mod_tile_enable_dirty_url(cmd_parms *cmd, void *mconfig, int enable_dirty_url)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, cmd->pool, "Setting %s argument to %s", cmd->directive->directive, enable_dirty_url ? "On" : "Off");
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	scfg->enable_dirty_url = enable_dirty_url;
	return NULL;
}

static const char *mod_tile_delaypool_tiles_config(cmd_parms *cmd, void *mconfig, const char *delaypool_tile_size_string, const char *top_up_tile_rate_string)
{
	const char *delaypool_tile_size_result, *top_up_tile_rate_result;
	double top_up_tile_rate;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	delaypool_tile_size_result = arg_to_int(cmd, delaypool_tile_size_string, &scfg->delaypool_tile_size, "ModTileThrottlingTiles first");

	if (delaypool_tile_size_result != NULL) {
		return delaypool_tile_size_result;
	}

	top_up_tile_rate_result = arg_to_double(cmd, top_up_tile_rate_string, &top_up_tile_rate, "ModTileThrottlingTiles second");

	if (top_up_tile_rate_result != NULL) {
		return top_up_tile_rate_result;
	}

	/*Convert topup rate into microseconds per tile */
	scfg->delaypool_tile_rate = (APR_USEC_PER_SEC / top_up_tile_rate);
	return NULL;
}

static const char *mod_tile_delaypool_render_config(cmd_parms *cmd, void *mconfig, const char *delaypool_render_size_string, const char *top_up_render_rate_string)
{
	const char *delaypool_render_size_result, *top_up_render_rate_result;
	double top_up_render_rate;
	tile_server_conf *scfg = (tile_server_conf *)ap_get_module_config(cmd->server->module_config, &tile_module);
	delaypool_render_size_result = arg_to_int(cmd, delaypool_render_size_string, &scfg->delaypool_render_size, "ModTileThrottlingRenders first");

	if (delaypool_render_size_result != NULL) {
		return delaypool_render_size_result;
	}

	top_up_render_rate_result = arg_to_double(cmd, top_up_render_rate_string, &top_up_render_rate, "ModTileThrottlingRenders second");

	if (top_up_render_rate_result != NULL) {
		return top_up_render_rate_result;
	}

	/*Convert topup rate into microseconds per tile */
	scfg->delaypool_render_rate = (APR_USEC_PER_SEC / top_up_render_rate);
	return NULL;
}

static void *create_tile_config(apr_pool_t *p, server_rec *s)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, p, "Running create_tile_config");
	tile_server_conf *scfg = (tile_server_conf *)apr_pcalloc(p, sizeof(tile_server_conf));

	scfg->cache_duration_dirty = 15 * 60;
	scfg->cache_duration_last_modified_factor = 0.0;
	scfg->cache_duration_low_zoom = 6 * 24 * 60 * 60;
	scfg->cache_duration_max = 7 * 24 * 60 * 60;
	scfg->cache_duration_medium_zoom = 1 * 24 * 60 * 60;
	scfg->cache_duration_minimum = 3 * 60 * 60;
	scfg->cache_extended_duration = 0;
	scfg->cache_extended_hostname = "";
	scfg->cache_level_low_zoom = 0;
	scfg->cache_level_medium_zoom = 0;
	scfg->configs = apr_array_make(p, 4, sizeof(tile_config_rec));
	scfg->delaypool_render_rate = RENDER_TOPUP_RATE;
	scfg->delaypool_render_size = AVAILABLE_RENDER_BUCKET_SIZE;
	scfg->delaypool_tile_rate = RENDER_TOPUP_RATE;
	scfg->delaypool_tile_size = AVAILABLE_TILE_BUCKET_SIZE;
	scfg->enable_bulk_mode = 0;
	scfg->enable_dirty_url = 1;
	scfg->enable_global_stats = 1;
	scfg->enable_status_url = 1;
	scfg->enable_tile_throttling = 0;
	scfg->enable_tile_throttling_xforward = 0;
	scfg->max_load_missing = MAX_LOAD_MISSING;
	scfg->max_load_old = MAX_LOAD_OLD;
	scfg->renderd_socket_name = apr_pstrndup(p, RENDERD_SOCKET, PATH_MAX);
	scfg->renderd_socket_port = 0;
	scfg->request_timeout = REQUEST_TIMEOUT;
	scfg->request_timeout_priority = REQUEST_TIMEOUT;
	scfg->tile_dir = apr_pstrndup(p, RENDERD_TILE_DIR, PATH_MAX);
	scfg->very_old_threshold = VERYOLD_THRESHOLD;

	return scfg;
}

static void *merge_tile_config(apr_pool_t *p, void *basev, void *overridesv)
{
	ap_log_perror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, p, "Running merge_tile_config");
	tile_server_conf *scfg = (tile_server_conf *)apr_pcalloc(p, sizeof(tile_server_conf));
	tile_server_conf *scfg_base = (tile_server_conf *)basev;
	tile_server_conf *scfg_over = (tile_server_conf *)overridesv;

	scfg->cache_duration_dirty = scfg_over->cache_duration_dirty;
	scfg->cache_duration_last_modified_factor = scfg_over->cache_duration_last_modified_factor;
	scfg->cache_duration_low_zoom = scfg_over->cache_duration_low_zoom;
	scfg->cache_duration_max = scfg_over->cache_duration_max;
	scfg->cache_duration_medium_zoom = scfg_over->cache_duration_medium_zoom;
	scfg->cache_duration_minimum = scfg_over->cache_duration_minimum;
	scfg->cache_extended_duration = scfg_over->cache_extended_duration;
	scfg->cache_extended_hostname = apr_pstrndup(p, scfg_over->cache_extended_hostname, PATH_MAX);
	scfg->cache_level_low_zoom = scfg_over->cache_level_low_zoom;
	scfg->cache_level_medium_zoom = scfg_over->cache_level_medium_zoom;
	scfg->configs = apr_array_append(p, scfg_base->configs, scfg_over->configs);
	scfg->delaypool_render_rate = scfg_over->delaypool_render_rate;
	scfg->delaypool_render_size = scfg_over->delaypool_render_size;
	scfg->delaypool_tile_rate = scfg_over->delaypool_tile_rate;
	scfg->delaypool_tile_size = scfg_over->delaypool_tile_size;
	scfg->enable_bulk_mode = scfg_over->enable_bulk_mode;
	scfg->enable_dirty_url = scfg_over->enable_dirty_url;
	scfg->enable_global_stats = scfg_over->enable_global_stats;
	scfg->enable_status_url = scfg_over->enable_status_url;
	scfg->enable_tile_throttling = scfg_over->enable_tile_throttling;
	scfg->enable_tile_throttling_xforward = scfg_over->enable_tile_throttling_xforward;
	scfg->max_load_missing = scfg_over->max_load_missing;
	scfg->max_load_old = scfg_over->max_load_old;
	scfg->renderd_socket_name = apr_pstrndup(p, scfg_over->renderd_socket_name, PATH_MAX);
	scfg->renderd_socket_port = scfg_over->renderd_socket_port;
	scfg->request_timeout = scfg_over->request_timeout;
	scfg->request_timeout_priority = scfg_over->request_timeout_priority;
	scfg->tile_dir = apr_pstrndup(p, scfg_over->tile_dir, PATH_MAX);
	scfg->very_old_threshold = scfg_over->very_old_threshold;

	// Construct a table of minimum cache times per zoom level
	for (int i = 0; i <= MAX_ZOOM_SERVER; i++) {
		if (i <= scfg->cache_level_low_zoom) {
			scfg->mincachetime[i] = scfg->cache_duration_low_zoom;
		} else if (i <= scfg->cache_level_medium_zoom) {
			scfg->mincachetime[i] = scfg->cache_duration_medium_zoom;
		} else {
			scfg->mincachetime[i] = scfg->cache_duration_minimum;
		}
	}

	return scfg;
}

static const command_rec tile_cmds[] = {
	AP_INIT_FLAG("ModTileBulkMode", mod_tile_enable_bulk_mode, NULL, OR_OPTIONS, "On Off - make all requests to renderd with bulk render priority, never mark tiles dirty"),
	AP_INIT_FLAG("ModTileEnableDirtyURL", mod_tile_enable_dirty_url, NULL, OR_OPTIONS, "On Off - whether to handle .../dirty urls "),
	AP_INIT_FLAG("ModTileEnableStats", mod_tile_enable_stats, NULL, OR_OPTIONS, "On Off - enable of keeping stats about what mod_tile is serving"),
	AP_INIT_FLAG("ModTileEnableStatusURL", mod_tile_enable_status_url, NULL, OR_OPTIONS, "On Off - whether to handle .../status urls "),
	AP_INIT_FLAG("ModTileEnableTileThrottling", mod_tile_enable_throttling, NULL, OR_OPTIONS, "On Off - enable of throttling of IPs that excessively download tiles such as scrapers"),
	AP_INIT_TAKE1("LoadTileConfigFile", load_tile_config, NULL, OR_OPTIONS, "load an entire renderd config file"),
	AP_INIT_TAKE1("ModTileCacheDurationDirty", mod_tile_cache_duration_dirty_config, NULL, OR_OPTIONS, "Set the cache expiry for serving dirty tiles"),
	AP_INIT_TAKE1("ModTileCacheDurationMax", mod_tile_cache_duration_max_config, NULL, OR_OPTIONS, "Set the maximum cache expiry in seconds"),
	AP_INIT_TAKE1("ModTileCacheDurationMinimum", mod_tile_cache_duration_minimum_config, NULL, OR_OPTIONS, "Set the minimum cache expiry"),
	AP_INIT_TAKE1("ModTileCacheExtendedDuration", mod_tile_cache_extended_duration_config, NULL, OR_OPTIONS, "set length of extended period caching"),
	AP_INIT_TAKE1("ModTileCacheExtendedHostName", mod_tile_cache_extended_hostname_config, NULL, OR_OPTIONS, "set hostname for extended period caching"),
	AP_INIT_TAKE1("ModTileCacheLastModifiedFactor", mod_tile_cache_duration_last_modified_factor_config, NULL, OR_OPTIONS, "Set the factor by which the last modified determines cache expiry"),
	AP_INIT_TAKE1("ModTileEnableTileThrottlingXForward", mod_tile_enable_throttling_xforward, NULL, OR_OPTIONS, "0 1 2 - use X-Forwarded-For http header to determin IP for throttling when available. 0 => off, 1 => use first entry, 2 => use last entry of the caching chain"),
	AP_INIT_TAKE1("ModTileMaxLoadMissing", mod_tile_max_load_missing_config, NULL, OR_OPTIONS, "Set max load for rendering missing tiles"),
	AP_INIT_TAKE1("ModTileMaxLoadOld", mod_tile_max_load_old_config, NULL, OR_OPTIONS, "Set max load for rendering old tiles"),
	AP_INIT_TAKE1("ModTileMissingRequestTimeout", mod_tile_request_timeout_priority_config, NULL, OR_OPTIONS, "Set timeout in seconds on missing mod_tile requests"),
	AP_INIT_TAKE1("ModTileRenderdSocketName", mod_tile_renderd_socket_name_config, NULL, OR_OPTIONS, "Set name of unix domain socket for connecting to rendering daemon"),
	AP_INIT_TAKE1("ModTileRequestTimeout", mod_tile_request_timeout_config, NULL, OR_OPTIONS, "Set timeout in seconds on mod_tile requests"),
	AP_INIT_TAKE1("ModTileTileDir", mod_tile_tile_dir_config, NULL, OR_OPTIONS, "Set name of tile cache directory"),
	AP_INIT_TAKE1("ModTileVeryOldThreshold", mod_tile_very_old_threshold_config, NULL, OR_OPTIONS, "set the time threshold from when an outdated tile ist considered very old and rendered with slightly higher priority."),
	AP_INIT_TAKE2("ModTileCacheDurationLowZoom", mod_tile_cache_duration_low_config, NULL, OR_OPTIONS, "Set the minimum cache duration and zoom level for low zoom tiles"),
	AP_INIT_TAKE2("ModTileCacheDurationMediumZoom", mod_tile_cache_duration_medium_config, NULL, OR_OPTIONS, "Set the minimum cache duration and zoom level for medium zoom tiles"),
	AP_INIT_TAKE2("ModTileRenderdSocketAddr", mod_tile_renderd_socket_address_config, NULL, OR_OPTIONS, "Set address and port of the TCP socket for connecting to rendering daemon"),
	AP_INIT_TAKE2("ModTileThrottlingRenders", mod_tile_delaypool_render_config, NULL, OR_OPTIONS, "Set the initial bucket size (number of tiles) and top up rate (tiles per second) for throttling tile request per IP"),
	AP_INIT_TAKE2("ModTileThrottlingTiles", mod_tile_delaypool_tiles_config, NULL, OR_OPTIONS, "Set the initial bucket size (number of tiles) and top up rate (tiles per second) for throttling tile request per IP"),
	AP_INIT_TAKE3("AddTileMimeConfig", add_tile_mime_config, NULL, OR_OPTIONS, "path, name of renderd config and file extension to use"),
	AP_INIT_TAKE_ARGV("AddTileConfig", add_tile_config, NULL, OR_OPTIONS, "path, name of renderd config and optional key-value pairs to use"),
	{NULL}
};

module AP_MODULE_DECLARE_DATA tile_module = {
STANDARD20_MODULE_STUFF,
NULL,		/* dir config creater */
NULL,		/* dir merger --- default is to override */
create_tile_config, /* server config */
merge_tile_config,	/* merge server config */
tile_cmds,		/* command apr_table_t */
register_hooks	/* register hooks */
};
