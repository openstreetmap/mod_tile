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

#ifndef MODTILE_H
#define MODTILE_H

#include "protocol.h"
#include "store.h"
#include <apr_tables.h>
#include <netinet/in.h>

/*Size of the delaypool hashtable*/
#define DELAY_HASHTABLE_SIZE 100057
#define DELAY_HASHTABLE_WHITELIST_SIZE 13
/*Number of tiles in the bucket */
#define AVAILABLE_TILE_BUCKET_SIZE 5000
/*Number of render request in the bucket */
#define AVAILABLE_RENDER_BUCKET_SIZE 65
/*Number of microseconds per render request. Currently set at no more than 1 render request per 5 seconds on average */
#define RENDER_TOPUP_RATE 5000000l
/*Number of microseconds per render request. Currently set at no more than 1 request per second on average */
#define TILE_TOPUP_RATE 1000000l

#define INILINE_MAX 256

#define MAX_ZOOM_SERVER 30

#define FRESH 1
#define OLD 2
#define FRESH_RENDER 3
#define OLD_RENDER 4
#define VERYOLD_RENDER 5
#define VERYOLD 6

/* Number of microseconds to camp out on the mutex */
#define CAMPOUT 10
/* Maximum number of times we camp out before giving up */
#define MAXCAMP 10

#define DEFAULT_ATTRIBUTION "&copy;<a href=\\\"http://www.openstreetmap.org/\\\">OpenStreetMap</a> and <a href=\\\"http://wiki.openstreetmap.org/wiki/Contributors\\\">contributors</a>, <a href=\\\"http://opendatacommons.org/licenses/odbl/\\\">(ODbL)</a>"

typedef struct delaypool_entry {
	struct in6_addr ip_addr;
	int available_tiles;
	int available_render_req;
} delaypool_entry;

typedef struct delaypool {
	delaypool_entry users[DELAY_HASHTABLE_SIZE];
	in_addr_t whitelist[DELAY_HASHTABLE_WHITELIST_SIZE];
	apr_time_t last_tile_fillup;
	apr_time_t last_render_fillup;
	int locked;
} delaypool;

typedef struct stats_data {
	apr_uint64_t noResp200;
	apr_uint64_t noResp304;
	apr_uint64_t noResp404;
	apr_uint64_t noResp503;
	apr_uint64_t noResp5XX;
	apr_uint64_t noRespOther;
	apr_uint64_t noFreshCache;
	apr_uint64_t noFreshRender;
	apr_uint64_t noOldCache;
	apr_uint64_t noOldRender;
	apr_uint64_t noVeryOldCache;
	apr_uint64_t noVeryOldRender;
	apr_uint64_t noRespZoom[MAX_ZOOM_SERVER + 1];
	apr_uint64_t totalBufferRetrievalTime;
	apr_uint64_t noTotalBufferRetrieval;
	apr_uint64_t zoomBufferRetrievalTime[MAX_ZOOM_SERVER + 1];
	apr_uint64_t noZoomBufferRetrieval[MAX_ZOOM_SERVER + 1];

	apr_uint64_t *noResp200Layer;
	apr_uint64_t *noResp404Layer;

} stats_data;

typedef struct {
	char **hostnames;
	const char *attribution;
	const char *baseuri;
	const char *cors;
	const char *description;
	const char *fileExtension;
	const char *mimeType;
	const char *store;
	const char *xmlname;
	int aspect_x;
	int aspect_y;
	int enableOptions;
	int maxzoom;
	int minzoom;
	int noHostnames;
} tile_config_rec;

typedef struct {
	apr_array_header_t *configs;
	apr_time_t very_old_threshold;
	const char *cache_extended_hostname;
	const char *renderd_socket_name;
	const char *tile_dir;
	double cache_duration_last_modified_factor;
	int cache_duration_dirty;
	int cache_duration_low_zoom;
	int cache_duration_max;
	int cache_duration_medium_zoom;
	int cache_duration_minimum;
	int cache_extended_duration;
	int cache_level_low_zoom;
	int cache_level_medium_zoom;
	int delaypool_render_size;
	int delaypool_tile_size;
	int enable_bulk_mode;
	int enable_dirty_url;
	int enable_global_stats;
	int enable_status_url;
	int enable_tile_throttling;
	int enable_tile_throttling_xforward;
	int max_load_missing;
	int max_load_old;
	int mincachetime[MAX_ZOOM_SERVER + 1];
	int renderd_socket_port;
	int request_timeout;
	int request_timeout_priority;
	long delaypool_render_rate;
	long delaypool_tile_rate;
} tile_server_conf;

typedef struct tile_request_data {
	struct protocol *cmd;
	struct storage_backend *store;
	int layerNumber;
} tile_request_data;

enum tileState { tileMissing,
		 tileOld,
		 tileVeryOld,
		 tileCurrent
	       };

#endif
