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

#ifndef MOD_TILE_H
#define MOD_TILE_H

#include "protocol.h"
#include "store.h"
#include <apr_tables.h>
#include <netinet/in.h>

/** @brief Prime bucket count of the per-client IP delaypool hash table. */
#define DELAY_HASHTABLE_SIZE 100057
/** @brief Maximum number of whitelisted IPv4 addresses exempt from delaypool throttling. */
#define DELAY_HASHTABLE_WHITELIST_SIZE 13
/** @brief Default maximum token bucket capacity for cached tile requests per client IP. */
#define AVAILABLE_TILE_BUCKET_SIZE 5000
/** @brief Default maximum token bucket capacity for on-demand render requests per client IP. */
#define AVAILABLE_RENDER_BUCKET_SIZE 65
/**
 * @brief Default render token refill interval in microseconds (`1` render token per `5` seconds).
 *
 * Equivalent to `ModTileThrottlingRenders <size> 0.2`, whose second argument is expressed in
 * render requests per second.
 */
#define RENDER_TOPUP_RATE 5000000l
/**
 * @brief Default tile token refill interval in microseconds (`1` tile token per `1` second).
 *
 * Equivalent to `ModTileThrottlingTiles <size> 1`, whose second argument is expressed in
 * tile requests per second.
 */
#define TILE_TOPUP_RATE 1000000l

/** @brief Maximum length in bytes of a single INI configuration key or line buffer. */
#define INILINE_MAX 256

/** @brief Upper bound on zoom levels tracked in server-side Apache statistics and cache-control arrays. */
#define MAX_ZOOM_SERVER 30

/** @brief Cache/render outcome code: fresh tile served directly from storage without rendering. */
#define FRESH 1
/** @brief Cache/render outcome code: expired (`tileOld`) tile served stale from storage without re-rendering. */
#define OLD 2
/** @brief Cache/render outcome code: missing tile newly rendered on demand and served. */
#define FRESH_RENDER 3
/** @brief Cache/render outcome code: expired (`tileOld`) tile re-rendered on demand and served. */
#define OLD_RENDER 4
/** @brief Cache/render outcome code: heavily expired (`tileVeryOld`) tile re-rendered on demand and served. */
#define VERYOLD_RENDER 5
/** @brief Cache/render outcome code: heavily expired (`tileVeryOld`) tile served stale without re-rendering. */
#define VERYOLD 6

/** @brief Spin-wait sleep interval in microseconds when contending on the delaypool lock. */
#define CAMPOUT 10
/** @brief Maximum number of spin-wait iterations before giving up on acquiring the delaypool lock. */
#define MAXCAMP 10

/** @brief Default OpenStreetMap attribution string used when none is configured for a map layer. */
#define DEFAULT_ATTRIBUTION "&copy;<a href='http://www.openstreetmap.org/'>OpenStreetMap</a> and <a href='http://wiki.openstreetmap.org/wiki/Contributors'>contributors</a>, <a href='http://opendatacommons.org/licenses/odbl/'>(ODbL)</a>"

/**
 * @brief Freshness and existence classification of a requested tile in the storage backend.
 */
enum tileState {
	/** @brief Initial state before storage metadata has been queried. */
	tileUnknown,
	/** @brief Tile does not exist in storage (`stat_info.size < 0`). */
	tileMissing,
	/** @brief Tile exists in storage but its modification time predates the planet import timestamp. */
	tileOld,
	/** @brief Tile exists in storage and its age exceeds `very_old_threshold`. */
	tileVeryOld,
	/** @brief Tile exists in storage and is up-to-date. */
	tileCurrent
};

/**
 * @brief Per-client IP token bucket entry in the shared-memory rate-limiting delaypool.
 */
typedef struct delaypool_entry {
	/** @brief Client IPv6 address (or IPv4-mapped IPv6 address) occupying this hash slot. */
	struct in6_addr ip_addr;
	/** @brief Remaining cached-tile request tokens available to this client. */
	int available_tiles;
	/** @brief Remaining on-demand render request tokens available to this client. */
	int available_render_req;
} delaypool_entry;

/**
 * @brief Shared-memory rate-limiting state tracking per-IP token buckets and whitelisted prefixes.
 */
typedef struct delaypool {
	/** @brief Fixed-size hash table of per-client IP token buckets. */
	delaypool_entry users[DELAY_HASHTABLE_SIZE];
	/** @brief Array of whitelisted IPv4 addresses exempt from delaypool rate limits. */
	in_addr_t whitelist[DELAY_HASHTABLE_WHITELIST_SIZE];
	/** @brief Timestamp of the last global tile token refill pass. */
	apr_time_t last_tile_fillup;
	/** @brief Timestamp of the last global render token refill pass. */
	apr_time_t last_render_fillup;
	/** @brief Atomic spinlock flag (`0` unlocked, `1` locked) serializing shared-memory updates. */
	int locked;
} delaypool;

/**
 * @brief Shared-memory HTTP response, cache hit/miss, and latency counters exposed via `/mod_tile`.
 */
typedef struct stats_data {
	/** @brief Total HTTP 200 (OK) tile responses served. */
	apr_uint64_t noResp200;
	/** @brief Total HTTP 304 (Not Modified) conditional responses served. */
	apr_uint64_t noResp304;
	/** @brief Total HTTP 404 (Not Found) responses returned. */
	apr_uint64_t noResp404;
	/** @brief Total HTTP 503 (Service Unavailable) responses returned due to load or throttling. */
	apr_uint64_t noResp503;
	/** @brief Total HTTP 5xx server error responses returned. */
	apr_uint64_t noResp5XX;
	/** @brief Total HTTP responses with other status codes. */
	apr_uint64_t noRespOther;
	/** @brief Tiles served directly from cache while fresh (`FRESH`). */
	apr_uint64_t noFreshCache;
	/** @brief Missing tiles newly rendered on demand (`FRESH_RENDER`). */
	apr_uint64_t noFreshRender;
	/** @brief Expired tiles served stale from cache without re-rendering (`OLD`). */
	apr_uint64_t noOldCache;
	/** @brief Expired tiles re-rendered on demand (`OLD_RENDER`). */
	apr_uint64_t noOldRender;
	/** @brief Heavily expired tiles served stale from cache without re-rendering (`VERYOLD`). */
	apr_uint64_t noVeryOldCache;
	/** @brief Heavily expired tiles re-rendered on demand (`VERYOLD_RENDER`). */
	apr_uint64_t noVeryOldRender;
	/** @brief Per-zoom count of served tile responses (`0` through `MAX_ZOOM_SERVER`). */
	apr_uint64_t noRespZoom[MAX_ZOOM_SERVER + 1];
	/** @brief Cumulative tile retrieval and render wait time in microseconds across all zooms. */
	apr_uint64_t totalBufferRetrievalTime;
	/** @brief Total number of tile retrievals recorded in `totalBufferRetrievalTime`. */
	apr_uint64_t noTotalBufferRetrieval;
	/** @brief Per-zoom cumulative tile retrieval and render wait time in microseconds. */
	apr_uint64_t zoomBufferRetrievalTime[MAX_ZOOM_SERVER + 1];
	/** @brief Per-zoom count of tile retrievals recorded in `zoomBufferRetrievalTime`. */
	apr_uint64_t noZoomBufferRetrieval[MAX_ZOOM_SERVER + 1];

	/** @brief Per-layer array of HTTP 200 response counters indexed by layer index. */
	apr_uint64_t *noResp200Layer;
	/** @brief Per-layer array of HTTP 404 response counters indexed by layer index. */
	apr_uint64_t *noResp404Layer;

} stats_data;

/**
 * @brief Per-map-layer configuration record populated from Apache directives or `renderd.conf`.
 */
typedef struct {
	/** @brief Array of virtual host names (`ServerName` / `ServerAlias`) matched for this layer. */
	char **hostnames;
	/** @brief Attribution string advertised in TileJSON responses for this layer. */
	const char *attribution;
	/** @brief Base URL path prefix routed to this layer (e.g., `"/osm/"`). */
	const char *baseuri;
	/** @brief Cross-Origin Resource Sharing (`Access-Control-Allow-Origin`) header value. */
	const char *cors;
	/** @brief Human-readable description advertised in TileJSON responses for this layer. */
	const char *description;
	/** @brief File extension of rendered tiles without leading dot (e.g., `"png"`). */
	const char *fileExtension;
	/** @brief HTTP `Content-Type` MIME type returned for tiles in this layer (e.g., `"image/png"`). */
	const char *mimeType;
	/** @brief Storage backend URI or filesystem path configured for this layer. */
	const char *store;
	/** @brief Target map style identifier (`xmlname`) sent in IPC requests to `renderd`. */
	const char *xmlname;
	/** @brief Horizontal projection aspect ratio multiplier (default `1`). */
	int aspect_x;
	/** @brief Vertical projection aspect ratio multiplier (default `1`). */
	int aspect_y;
	/** @brief Non-zero if dynamic style parameterization options are enabled in tile URLs. */
	int enableOptions;
	/** @brief Maximum zoom level permitted for this layer. */
	int maxzoom;
	/** @brief Minimum zoom level permitted for this layer. */
	int minzoom;
	/** @brief Number of hostnames stored in the `hostnames` array. */
	int noHostnames;
} tile_config_rec;

/**
 * @brief Per-virtual-host Apache server configuration for `mod_tile`.
 */
typedef struct {
	/** @brief Array of `tile_config_rec` entries configured on this virtual host. */
	apr_array_header_t *configs;
	/** @brief Age threshold in microseconds beyond which an expired tile is classified as `tileVeryOld`. */
	apr_time_t very_old_threshold;
	/** @brief Hostname for which extended `Cache-Control` max-age headers are emitted (`ModTileCacheExtendedHost`). */
	const char *cache_extended_hostname;
	/** @brief UNIX domain socket path or TCP hostname used to connect to `renderd`. */
	const char *renderd_socket_name;
	/** @brief Default root directory or storage backend URI for cached tiles. */
	const char *tile_dir;
	/** @brief Multiplier applied to tile age since last modification when computing `Cache-Control` `max-age`. */
	double cache_duration_last_modified_factor;
	/** @brief `Cache-Control` `max-age` in seconds returned for dirty/expired tiles that could not be re-rendered. */
	int cache_duration_dirty;
	/** @brief `Cache-Control` `max-age` in seconds applied to zoom levels up to `cache_level_low_zoom`. */
	int cache_duration_low_zoom;
	/** @brief Upper bound in seconds on computed HTTP `Cache-Control` `max-age`. */
	int cache_duration_max;
	/** @brief `Cache-Control` `max-age` in seconds applied to zoom levels up to `cache_level_medium_zoom`. */
	int cache_duration_medium_zoom;
	/** @brief Lower bound in seconds on computed HTTP `Cache-Control` `max-age`. */
	int cache_duration_minimum;
	/** @brief Extended `Cache-Control` `max-age` in seconds served to `cache_extended_hostname`. */
	int cache_extended_duration;
	/** @brief Maximum zoom level treated as low-zoom for `cache_duration_low_zoom`. */
	int cache_level_low_zoom;
	/** @brief Maximum zoom level treated as medium-zoom for `cache_duration_medium_zoom`. */
	int cache_level_medium_zoom;
	/** @brief Maximum render token bucket size per IP (`ModTileThrottlingRenders`). */
	int delaypool_render_size;
	/** @brief Maximum tile token bucket size per IP (`ModTileThrottlingTiles`). */
	int delaypool_tile_size;
	/** @brief Non-zero to submit missing tile renders with `cmdRenderBulk` instead of `cmdRenderPrio`. */
	int enable_bulk_mode;
	/** @brief Non-zero to enable `/dirty` URL suffix handling for manual tile invalidation. */
	int enable_dirty_url;
	/** @brief Non-zero to aggregate statistics globally across all virtual hosts rather than per-vhost. */
	int enable_global_stats;
	/** @brief Non-zero to enable `/status` URL suffix handling returning tile freshness metadata. */
	int enable_status_url;
	/** @brief Non-zero to enable per-client IP delaypool rate limiting (`ModTileEnableTileThrottling`). */
	int enable_tile_throttling;
	/** @brief Non-zero (`1` first, `2` last) to extract client IP from `X-Forwarded-For` headers. */
	int enable_tile_throttling_xforward;
	/** @brief System 1-minute load average threshold above which missing tiles return HTTP 404. */
	int max_load_missing;
	/** @brief System 1-minute load average threshold above which expired tiles are served stale. */
	int max_load_old;
	/** @brief Per-zoom minimum cache age overrides in seconds (`0` through `MAX_ZOOM_SERVER`). */
	int mincachetime[MAX_ZOOM_SERVER + 1];
	/** @brief TCP port number used to connect to `renderd` when `renderd_socket_name` is a host (`0` for UNIX socket). */
	int renderd_socket_port;
	/** @brief Timeout in seconds to wait for low/normal-priority tile rendering (`ModTileRequestTimeout`). */
	int request_timeout;
	/** @brief Timeout in seconds to wait for high-priority missing tile rendering (`ModTileMissingRequestTimeout`). */
	int request_timeout_priority;
	/** @brief Microseconds per render token refill (`ModTileThrottlingRenders`). */
	long delaypool_render_rate;
	/** @brief Microseconds per tile token refill (`ModTileThrottlingTiles`). */
	long delaypool_tile_rate;
} tile_server_conf;

/**
 * @brief Per-HTTP-request context stored in Apache's `r->request_config` across translate_name and handler phases.
 */
typedef struct tile_request_data {
	/** @brief Parsed wire protocol request identifying the target `(xmlname, x, y, z, options)`. */
	struct protocol *cmd;
	/** @brief Active storage backend instance serving the matched map layer. */
	struct storage_backend *store;
	/** @brief Zero-based index of the matched `tile_config_rec` layer in `tile_server_conf.configs`. */
	int layerNumber;
	/** @brief Freshness state of the requested tile (`tileMissing`, `tileOld`, `tileVeryOld`, `tileCurrent`). */
	enum tileState tileState;
} tile_request_data;

#endif /* MOD_TILE_H */
