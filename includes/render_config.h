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

#ifndef RENDER_CONFIG_H
#define RENDER_CONFIG_H

#include "config.h"

/** @brief Maximum supported map zoom level. */
#define MAX_ZOOM 20

/** @brief Maximum single tile payload size in bytes (`1 MiB`) served to a client. */
#define MAX_SIZE (1 * 1024 * 1024)

/** @brief Enable directory-hashed path layout (`z/x4/x3/x2/x1/x0.meta`) for metatile storage. */
#define DIRECTORY_HASH

/**
 * @brief Legacy flat filesystem root where OpenLayers fetches `z/x/y.png` tiles directly.
 *
 * Used only when `DIRECTORY_HASH` is undefined.
 */
#define TILE_PATH "/var/www/html/osm_tiles2"

#ifndef RENDERD_TILE_DIR
/** @brief Default root filesystem directory for cached tile and metatile storage. */
#define RENDERD_TILE_DIR "/var/cache/renderd/tiles"
#endif

/**
 * @brief Default 1-minute load average threshold above which expired tiles are served stale without re-rendering.
 *
 * Overridable in Apache configuration via `ModTileMaxLoadOld`.
 */
#define MAX_LOAD_OLD 16

/**
 * @brief Default 1-minute load average threshold above which missing tiles return HTTP 404 instead of rendering.
 *
 * Overridable in Apache configuration via `ModTileMaxLoadMissing`.
 */
#define MAX_LOAD_MISSING 50

/** @brief Default 1-minute load average threshold above which all tile requests return HTTP 404. */
#define MAX_LOAD_ANY 100

/**
 * @brief Age threshold in microseconds (`365 days`) beyond which an expired tile is promoted from low to normal render priority.
 *
 * Computed as `1000000 * 3600 * 24 * 365 = 31536000000000`.
 */
#define VERYOLD_THRESHOLD 31536000000000

#ifndef RENDERD_CONFIG
/** @brief Default path to the `renderd` INI configuration file. */
#define RENDERD_CONFIG "/etc/renderd.conf"
#endif

#ifndef XMLCONFIG_DEFAULT
/** @brief Fallback map configuration name used when none is explicitly specified. */
#define XMLCONFIG_DEFAULT "default"
#endif

#ifndef XMLCONFIGS_MAX
/** @brief Maximum number of map style configurations supported simultaneously by `mod_tile`. */
#define XMLCONFIGS_MAX 100
#endif

#ifndef RENDERD_PIDFILE
/** @brief Default PID file path written by the `renderd` daemon. */
#define RENDERD_PIDFILE "/run/renderd/renderd.pid"
#endif

#ifndef MAPNIK_PLUGINS_DIR
/** @brief Default directory path searched for Mapnik input datasource plugins. */
#define MAPNIK_PLUGINS_DIR "/usr/local/lib64/mapnik/input"
#endif

#ifndef MAPNIK_FONTS_DIR
/** @brief Default directory path searched for Mapnik TrueType/OpenType fonts. */
#define MAPNIK_FONTS_DIR "/usr/local/lib64/mapnik/fonts"
#endif

#ifndef MAPNIK_FONTS_DIR_RECURSE
/** @brief Default flag controlling whether `MAPNIK_FONTS_DIR` is scanned recursively (`1`) or top-level only (`0`). */
#define MAPNIK_FONTS_DIR_RECURSE 0
#endif

/** @brief Typical interval in seconds (`7 days`) between planet database imports, used to estimate tile expiry. */
#define PLANET_INTERVAL (7 * 24 * 60 * 60)

/** @brief Sentinel timestamp file path touched upon completion of a planet import. */
#define PLANET_TIMESTAMP "/planet-import-complete"

/**
 * @brief Default timeout in seconds to wait for `renderd` to finish rendering a requested tile.
 *
 * Overridable in Apache configuration via `ModTileRequestTimeout`.
 */
#define REQUEST_TIMEOUT (3)

/** @brief Sentinel value representing an invalid or closed file/socket descriptor. */
#define FD_INVALID (-1)

#ifndef MIN
/** @brief Return the lesser of `x` and `y`. */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
/** @brief Return the greater of `x` and `y`. */
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

/** @brief Maximum number of concurrent client socket connections accepted by `renderd`. */
#define MAX_CONNECTIONS (2048)

/** @brief Default number of worker rendering threads spawned by `renderd`. */
#define NUM_THREADS (4)

/**
 * @brief Width and height in tiles of a single rendered metatile (`METATILE x METATILE` tiles).
 *
 * Must be a power of 2 (e.g., `2`, `4`, `8`, `16`).
 */
#define METATILE (8)

/** @brief Default maximum queue depth for interactive render queues. */
#define QUEUE_MAX (64)

/** @brief Maximum queue depth for standard/bulk interactive render requests. */
#define REQ_LIMIT (256)

/** @brief Maximum queue depth for background dirty-tile re-render requests. */
#define DIRTY_LIMIT (8000)

/** @brief Penalty delay in seconds imposed on clients sending malformed protocol requests. */
#define CLIENT_PENALTY (3)

#endif /* RENDER_CONFIG_H */
