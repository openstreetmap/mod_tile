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

#ifndef RENDERD_H
#define RENDERD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"
#include "gen_tile.h"

#ifndef HAVE_DAEMON
	int daemon(int nochdir, int noclose);
#endif

/** @brief Maximum length in bytes of a single INI configuration key or line buffer. */
#define INILINE_MAX 256
/** @brief Maximum number of slave `renderd` instances configurable alongside the master daemon. */
#define MAX_SLAVES 5
/** @brief Startup timeout in seconds allocated per render worker thread per configured map style. */
#define RENDER_THREAD_STARTUP_TIMEOUT_SEC 5

	/**
	 * @brief Global daemon and Mapnik runtime settings parsed from a `[renderd]` INI section.
	 */
	typedef struct {
		/** @brief TCP listen hostname/IP address (`NULL` when using a UNIX domain socket). */
		const char *iphostname;
		/** @brief Directory path searched for Mapnik TrueType/OpenType fonts. */
		const char *mapnik_font_dir;
		/** @brief Directory path searched for Mapnik input datasource plugins. */
		const char *mapnik_plugins_dir;
		/** @brief INI section name identifying this `renderd` instance (e.g., `"renderd"`). */
		const char *name;
		/** @brief Filesystem path where the daemon writes its PID file. */
		const char *pid_filename;
		/** @brief UNIX domain socket path for IPC with `mod_tile` (`NULL` when listening over TCP). */
		const char *socketname;
		/** @brief Optional output file path for periodic `renderd` runtime statistics dumps. */
		const char *stats_filename;
		/** @brief Default root tile/metatile storage directory or backend connection string. */
		const char *tile_dir;
		/** @brief Maximum number of requests kept in the dirty queue. */
		int dirty_queue_limit;
		/** @brief TCP listen port number when `iphostname` is configured. */
		int ipport;
		/** @brief Non-zero to recursively scan subdirectories under `mapnik_font_dir`. */
		int mapnik_font_dir_recurse;
		/** @brief Number of worker rendering threads spawned for this daemon instance. */
		int num_threads;
		/** @brief Maximum number of requests kept in each time-critical request queue. */
		int request_queue_limit;
	} renderd_config;

	/**
	 * @brief Per-map-style configuration parsed from a `[mapname]` section in `renderd.conf`.
	 */
	typedef struct {
		/** @brief Attribution string advertised in TileJSON metadata for this map style. */
		const char *attribution;
		/** @brief Cross-Origin Resource Sharing (`Access-Control-Allow-Origin`) policy string. */
		const char *cors;
		/** @brief Human-readable map description advertised in TileJSON metadata. */
		const char *description;
		/** @brief File extension of rendered tiles without leading dot (e.g., `"png"`, `"jpg"`). */
		const char *file_extension;
		/** @brief Primary HTTP virtual host name serving this map style. */
		const char *host;
		/** @brief HTCP cache-invalidation target host/IP for notifying upstream caches (e.g., Squid). */
		const char *htcpip;
		/** @brief MIME type of rendered tiles (e.g., `"image/png"`). */
		const char *mime_type;
		/** @brief Mapnik image writer format string (e.g., `"png256"`, `"jpeg80"`, `"webp"`). */
		const char *output_format;
		/** @brief Optional dynamic style parameterization handler name (e.g., `"language"`). */
		const char *parameterization;
		/** @brief Space-separated HTTP virtual host aliases serving this map style. */
		const char *server_alias;
		/** @brief Tile storage root directory or backend connection URI overriding the daemon default. */
		const char *tile_dir;
		/** @brief Filesystem path to the Mapnik XML stylesheet file. */
		const char *xmlfile;
		/** @brief Logical map configuration identifier (`xmlname`) used in IPC requests. */
		const char *xmlname;
		/** @brief Base HTTP URL path prefix where tiles for this style are exposed (e.g., `"/osm/"`). */
		const char *xmluri;
		/** @brief Mapnik rendering scale factor (`1.0` for standard DPI, `2.0` for Retina/HiDPI). */
		double scale_factor;
		/** @brief Horizontal projection aspect ratio multiplier (typically `1`, or `2` for Plate Carrée). */
		int aspect_x;
		/** @brief Vertical projection aspect ratio multiplier (typically `1`). */
		int aspect_y;
		/** @brief Maximum zoom level permitted for this map style. */
		int max_zoom;
		/** @brief Minimum zoom level permitted for this map style. */
		int min_zoom;
		/** @brief Number of render threads initializing or serving this map style. */
		int num_threads;
		/** @brief Width and height of a single rendered tile in pixels (e.g., `256` or `512`). */
		int tile_px_size;
		/** @brief Count of worker threads that successfully loaded `xmlfile` during startup. */
		int total_threads_ok;
	} xmlconfigitem;

	extern struct request_queue *render_request_queue;

	void statsRenderFinish(int z, long time);
	void request_exit(void);
	void send_response(struct item *item, enum protoCmd rsp, int render_time);
	enum protoCmd rx_request(struct protocol *req, int fd);

#ifdef __cplusplus
}
#endif

#endif /* RENDERD_H */
