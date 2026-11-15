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

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <glib.h>
#include <map>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/color.hpp>
#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/font_engine_freetype.hpp>
#include <mapnik/image.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/image_view.hpp>
#include <mapnik/image_view_any.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/map.hpp>
#include <mapnik/params.hpp>
#include <mapnik/pixel_types.hpp>
#include <mapnik/version.hpp>
#include <pthread.h>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

#if MAPNIK_MAJOR_VERSION >= 4
#include <mapnik/geometry/box2d.hpp>
#else
#include <mapnik/box2d.hpp>
#endif

#include "cache_expire.h"
#include "g_logger.h"
#include "gen_tile.h"
#include "metatile.h"
#include "parameterize_style.hpp"
#include "protocol.h"
#include "render_config.h"
#include "renderd.h"
#include "request_queue.h"
#include "store.h"

#ifndef DEG_TO_RAD
/** @brief Conversion factor from degrees to radians. */
#define DEG_TO_RAD (M_PI / 180)
#endif

#ifndef RAD_TO_DEG
/** @brief Conversion factor from radians to degrees. */
#define RAD_TO_DEG (180 / M_PI)
#endif

/** @brief Default initial pixel dimension (width and height) for a loaded Mapnik Map. */
#define RENDER_SIZE (256 * (METATILE + 1))

using namespace mapnik;

/** @brief Compatibility alias mapping legacy Mapnik 2 image_data_32 to Mapnik 3+ image_rgba8. */
#define image_data_32 image_rgba8
/** @brief Compatibility alias mapping legacy Mapnik 2 image_32 to Mapnik 3+ image_rgba8. */
#define image_32 image_rgba8

/**
 * @brief Mutex serializing Mapnik XML stylesheet loading and font registration across render threads.
 *
 * Prevents thread-safety issues inside Mapnik/libxml2 and FreeType during concurrent startup initialization.
 */
static pthread_mutex_t map_load_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Spatial reference system (SRS) bounds and grid aspect ratio for a map projection.
 */
struct projectionconfig {
	/** @brief Minimum X (left/west) coordinate in projected map units. */
	double bound_x0;
	/** @brief Minimum Y (bottom/south) coordinate in projected map units. */
	double bound_y0;
	/** @brief Maximum X (right/east) coordinate in projected map units. */
	double bound_x1;
	/** @brief Maximum Y (top/north) coordinate in projected map units. */
	double bound_y1;
	/** @brief Horizontal tile grid multiplier at zoom 0 (e.g., 1 for Web Mercator, 2 for Plate Carree). */
	int aspect_x;
	/** @brief Vertical tile grid multiplier at zoom 0 (e.g., 1 for Web Mercator, 2 for British National Grid). */
	int aspect_y;
};

/**
 * @brief Per-thread runtime configuration and Mapnik state for a single configured map style.
 */
struct xmlmapconfig {
	/** @brief Base Mapnik Map instance loaded from the stylesheet XML. */
	Map map;
	/**
	 * @brief Cache of parameterized Map copies keyed by the options string.
	 *
	 * Avoids recreating PostGIS datasource objects (and their connection
	 * pools) on every render, which would accumulate in Mapnik's global
	 * ConnectionManager and exhaust memory under sustained load.
	 */
	std::map<std::string, mapnik::Map> parameterized_map_cache;
	/** @brief Public HTTP hostname used when constructing HTCP cache expiry URLs. */
	const char *host;
	/** @brief Hostname or IP address of the HTCP caching proxy to send purge packets to. */
	const char *htcphost;
	/** @brief Image encoding format passed to Mapnik (e.g., "png256", "jpeg", "webp"). */
	const char *output_format;
	/** @brief Filesystem path to the Mapnik XML stylesheet. */
	const char *xmlfile;
	/** @brief Logical map style name used in render protocol requests. */
	const char *xmlname;
	/** @brief Base URI path prefix for the tileset (used for HTCP cache expiry). */
	const char *xmluri;
	/** @brief Mapnik rendering scale factor (e.g., 1.0 for standard DPI, 2.0 for @2x retina tiles). */
	double scale;
	/** @brief Connected UDP socket descriptor for HTCP cache purges, or -1 if disabled/unconnected. */
	int htcpsock;
	/** @brief Maximum allowed zoom level for this map style. */
	int maxzoom;
	/** @brief Minimum allowed zoom level for this map style. */
	int minzoom;
	/** @brief Initialization status flag: 1 if map and storage backend loaded cleanly, 0 otherwise. */
	int ok;
	/** @brief Width and height of a single output tile in pixels (typically 256 or 512). */
	int tilesize;
	/** @brief Optional style parameterization callback, or NULL if parameterization is disabled. */
	parameterize_function_ptr parameterize_function;
	/** @brief Projected coordinate bounds and grid aspect ratios derived from the map's SRS. */
	struct projectionconfig *prj;
	/** @brief Storage backend handle used to stat and persist rendered metatiles. */
	struct storage_backend *store;

	/** @brief Construct an xmlmapconfig with a default 256x256 Mapnik Map canvas. */
	xmlmapconfig() : map(256, 256) {}
};

/**
 * @brief Determine projected world bounds and grid aspect ratios from a PROJ/SRS string.
 *
 * Recognizes Web Mercator (`+proj=merc +a=6378137 +b=6378137`), Plate Carree
 * (`+proj=eqc ...`), and British National Grid (`+proj=tmerc ... OSGB36`).
 * Falls back to Web Mercator bounds with a warning if the SRS string is unrecognized.
 *
 * @param srs Mapnik spatial reference system (PROJ) string.
 * @return Heap-allocated `projectionconfig` struct (caller owns memory).
 */
struct projectionconfig *get_projection(const char *srs)
{
	/* Heap-allocated projection configuration to populate and return */
	struct projectionconfig *prj;

	if (strstr(srs, "+proj=merc +a=6378137 +b=6378137") != NULL) {
		g_logger(G_LOG_LEVEL_DEBUG, "Using web mercator projection settings");
		prj = static_cast<struct projectionconfig *>(malloc(sizeof(struct projectionconfig)));
		prj->bound_x0 = -20037508.3428;
		prj->bound_x1 = 20037508.3428;
		prj->bound_y0 = -20037508.3428;
		prj->bound_y1 = 20037508.3428;
		prj->aspect_x = 1;
		prj->aspect_y = 1;
	} else if (strcmp(srs, "+proj=eqc +lat_ts=0 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Using plate carree projection settings");
		prj = static_cast<struct projectionconfig *>(malloc(sizeof(struct projectionconfig)));
		prj->bound_x0 = -20037508.3428;
		prj->bound_x1 = 20037508.3428;
		prj->bound_y0 = -10018754.1714;
		prj->bound_y1 = 10018754.1714;
		prj->aspect_x = 2;
		prj->aspect_y = 1;
	} else if (strcmp(srs, "+proj=tmerc +lat_0=49 +lon_0=-2 +k=0.9996012717 +x_0=400000 +y_0=-100000 +ellps=airy +datum=OSGB36 +units=m +no_defs") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Using bng projection settings");
		prj = static_cast<struct projectionconfig *>(malloc(sizeof(struct projectionconfig)));
		prj->bound_x0 = 0;
		prj->bound_y0 = 0;
		prj->bound_x1 = 700000;
		prj->bound_y1 = 1400000;
		prj->aspect_x = 1;
		prj->aspect_y = 2;
	} else {
		g_logger(G_LOG_LEVEL_WARNING, "Unknown projection string, using web mercator as never the less. %s", srs);
		prj = static_cast<struct projectionconfig *>(malloc(sizeof(struct projectionconfig)));
		prj->bound_x0 = -20037508.3428;
		prj->bound_x1 = 20037508.3428;
		prj->bound_y0 = -20037508.3428;
		prj->bound_y1 = 20037508.3428;
		prj->aspect_x = 1;
		prj->aspect_y = 1;
	}

	return prj;
}

/**
 * @brief Register FreeType font files from a directory with Mapnik's font engine.
 *
 * Verifies that `font_dir` can be opened, registers all discovered font faces
 * (optionally recursing into subdirectories), and logs each registered face name
 * and source file path at `G_LOG_LEVEL_DEBUG`.
 *
 * @param font_dir Filesystem path to the directory containing font files.
 * @param recurse  Non-zero to search subdirectories recursively, 0 for top-level only.
 */
static void load_fonts(const char *font_dir, int recurse)
{
	/* Directory handle used to validate existence and readability of font_dir */
	DIR *fonts = opendir(font_dir);

	if (!fonts) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Unable to open font directory: %s", font_dir);
		return;
	}

	closedir(fonts);

	g_logger(G_LOG_LEVEL_INFO, "Loading fonts%sfrom directory '%s'", recurse ? " recursively " : " ", font_dir);

	freetype_engine::register_fonts(font_dir, recurse);

	/* Snapshot of all font face -> file path mappings currently registered in Mapnik */
	freetype_engine::font_file_mapping_type face_name_mappings = freetype_engine::get_mapping();

	/* Iterator over registered font face mappings for debug logging */
	freetype_engine::font_file_mapping_type::iterator face_name_mappings_iterator = face_name_mappings.begin();

	while (face_name_mappings_iterator != face_name_mappings.end()) {
		const char *face_name = face_name_mappings_iterator->first.c_str();
		const char *font_file_name = face_name_mappings_iterator->second.second.c_str();
		g_logger(G_LOG_LEVEL_DEBUG, "Loaded font face '%s' from file '%s'", face_name, font_file_name);
		face_name_mappings_iterator++;
	}
}

/**
 * @brief Scale Mapnik layer datasource connection pools to match the configured thread count.
 *
 * Iterates through every layer in `m`; if a datasource does not already specify
 * `max_size`, sets `max_size` to `num_threads + 2` and recreates the datasource
 * instance so concurrent render threads do not starve waiting for database connections.
 *
 * @param m           Mapnik Map instance whose layer datasources will be updated.
 * @param num_threads Number of active rendering threads configured for this map.
 */
static void parameterize_map_max_connections(Map &m, int num_threads)
{
	/* Layer index loop counter */
	unsigned int i;
	/* Scratch buffer for formatting integer connection pool size strings */
	char *tmp = static_cast<char *>(malloc(20));

	for (i = 0; i < m.layer_count(); i++) {
		layer &l = m.get_layer(i);
		parameters params = l.datasource()->params();

		if (params.find("max_size") == params.end()) {
			snprintf(tmp, 20, "%i", num_threads + 2);
			params["max_size"] = std::string(tmp);
		}

		l.set_datasource(datasource_cache::instance().create(params));
	}

	free(tmp);
}

/**
 * @brief Validate whether tile coordinates `(x, y, z)` fall within valid bounds for `map`.
 *
 * Checks that `z` is within `[map->minzoom, map->maxzoom]` and that `x` and `y` lie
 * within `[0, (2^z * aspect) - 1]`. Logs an informational message if out of bounds.
 *
 * @param x   Tile X coordinate.
 * @param y   Tile Y coordinate.
 * @param z   Tile zoom level.
 * @param map Map configuration containing zoom limits and projection aspect ratios.
 * @return Non-zero (1) if coordinates are valid, 0 if out of bounds.
 */
static int check_xyz(int x, int y, int z, struct xmlmapconfig *map)
{
	/* Out-of-bounds flag (non-zero if any coordinate is invalid) */
	int oob;
	/* Base number of tiles per axis at zoom z before applying projection aspect ratio */
	int limit;

	/* Validate zoom level bounds */
	oob = (z < map->minzoom || z > map->maxzoom);

	if (!oob) {
		/* Valid x/y indices for tiles are 0 ... (2^zoom * aspect) - 1 */
		limit = (1 << z);
		oob = (x < 0 || x > (limit * map->prj->aspect_x - 1) || y < 0 || y > (limit * map->prj->aspect_y - 1));
	}

	if (oob) {
		g_logger(G_LOG_LEVEL_INFO, "got bad co-ords: x(%d) y(%d) z(%d)", x, y, z);
	}

	return !oob;
}

/**
 * @brief Compute the projected bounding box for a metatile anchored at `(x, y, z)`.
 *
 * Converts top-left-origin slippy map tile coordinates into bottom-left-origin
 * projected coordinates (`box2d<double>`). Automatically clamps the metatile span
 * to the world grid dimensions at very low zoom levels where `2^z < METATILE`.
 *
 * @param prj Projection bounds and aspect ratio configuration.
 * @param x   Top-left tile X coordinate of the metatile.
 * @param y   Top-left tile Y coordinate of the metatile.
 * @param z   Tile zoom level.
 * @return Projected bounding box covering the metatile area.
 */
mapnik::box2d<double> tile2prjbounds(struct projectionconfig *prj, int x, int y, int z)
{
	/* Number of tiles spanned horizontally/vertically in this metatile (clamped at low zooms) */
	int render_size_tx = MIN(METATILE, prj->aspect_x * (1 << z));
	int render_size_ty = MIN(METATILE, prj->aspect_y * (1 << z));

	/* Projected bounding box corners: (p0x, p0y) = bottom-left, (p1x, p1y) = top-right.
	 * Note that tile Y increases downward while projected Y increases upward. */
	double p0x = prj->bound_x0 + (prj->bound_x1 - prj->bound_x0) * ((double)x / (double)(prj->aspect_x * 1 << z));
	double p0y = (prj->bound_y1 - (prj->bound_y1 - prj->bound_y0) * (((double)y + render_size_ty) / (double)(prj->aspect_y * 1 << z)));
	double p1x = prj->bound_x0 + (prj->bound_x1 - prj->bound_x0) * (((double)x + render_size_tx) / (double)(prj->aspect_x * 1 << z));
	double p1y = (prj->bound_y1 - (prj->bound_y1 - prj->bound_y0) * ((double)y / (double)(prj->aspect_y * 1 << z)));

	g_logger(G_LOG_LEVEL_DEBUG, "Rendering projected coordinates %i %i %i -> %f|%f %f|%f to a %i x %i tile", z, x, y, p0x, p0y, p1x, p1y, render_size_tx, render_size_ty);

	mapnik::box2d<double> bbox(p0x, p0y, p1x, p1y);
	return bbox;
}

/**
 * @brief Render a metatile using Mapnik and slice it into individual encoded tiles.
 *
 * Selects either the base Mapnik Map or a cached parameterized Map variant, renders
 * the full metatile extent into an RGBA buffer via `mapnik::agg_renderer`, and
 * extracts each sub-tile view into `tiles` encoded in `map->output_format`.
 *
 * @param map     Map style configuration and Mapnik state for this worker thread.
 * @param x       Top-left tile X coordinate of the metatile.
 * @param y       Top-left tile Y coordinate of the metatile.
 * @param z       Tile zoom level.
 * @param options Style parameterization options string (used when `parameterize_function` is set).
 * @param tiles   Output `metaTile` container populated with encoded sub-tile images.
 * @return `cmdDone` on successful render and encoding, or `cmdNotDone` if Mapnik throws an exception.
 */
static enum protoCmd render(struct xmlmapconfig *map, int x, int y, int z, char *options, metaTile &tiles)
{
	/* Actual metatile width and height in tiles (clamped when world grid < METATILE) */
	unsigned int render_size_tx = MIN(METATILE, map->prj->aspect_x * (1 << z));
	unsigned int render_size_ty = MIN(METATILE, map->prj->aspect_y * (1 << z));

	/* Pointer to the Mapnik Map instance used for rendering (either cached parameterized copy or base map) */
	mapnik::Map *render_map;

	if (map->parameterize_function) {
		/* Look up or create a parameterized map for this options string.
		 * Re-using a cached copy avoids calling set_datasource() on every
		 * render, which would create a new PostGIS connection pool entry in
		 * Mapnik's global ConnectionManager each time and leak memory. */
		auto it = map->parameterized_map_cache.find(std::string(options));

		if (it == map->parameterized_map_cache.end()) {
			mapnik::Map parameterized_map = map->map;
			map->parameterize_function(parameterized_map, options);
			parameterized_map.load_fonts();
			auto inserted = map->parameterized_map_cache.emplace(std::string(options), std::move(parameterized_map));
			it = inserted.first;
			g_logger(G_LOG_LEVEL_DEBUG, "Cached new parameterized map for options '%s' (cache size: %zu)", options, map->parameterized_map_cache.size());
		}

		render_map = &it->second;
	} else {
		render_map = &map->map;
	}

	render_map->resize(render_size_tx * map->tilesize, render_size_ty * map->tilesize);
	render_map->zoom_to_box(tile2prjbounds(map->prj, x, y, z));

	if (render_map->buffer_size() == 0) {
		/* Apply default half-tile render buffer only if the XML stylesheet did not set buffer-size */
		render_map->set_buffer_size((map->tilesize >> 1) * map->scale);
	}

	/* Offscreen RGBA framebuffer; zero-initialization is skipped when a solid background color fills the canvas */
	mapnik::image_32 buf(render_size_tx * map->tilesize, render_size_ty * map->tilesize, !render_map->background());

	try {
		mapnik::agg_renderer<mapnik::image_32> ren(*render_map, buf, map->scale);
		ren.apply();
	} catch (std::exception const &ex) {
		g_logger(G_LOG_LEVEL_ERROR, "failed to render TILE %s %d %d-%d %d-%d", map->xmlname, z, x, x + render_size_tx - 1, y, y + render_size_ty - 1);
		g_logger(G_LOG_LEVEL_ERROR, "  reason: %s", ex.what());
		return cmdNotDone;
	}

	/* Slice the rendered metatile buffer into an render_size_tx x render_size_ty grid of encoded tiles */
	unsigned int xx, yy;

	for (yy = 0; yy < render_size_ty; yy++) {
		for (xx = 0; xx < render_size_tx; xx++) {
			mapnik::image_view<mapnik::image<mapnik::rgba8_t>> vw1(xx * map->tilesize, yy * map->tilesize, map->tilesize, map->tilesize, buf);
			struct mapnik::image_view_any vw(vw1);
			tiles.set(xx, yy, save_to_string(vw, map->output_format));
		}
	}

	return cmdDone; /* OK */
}

/**
 * @brief Initialize global Mapnik datasource plugins and FreeType fonts.
 *
 * Must be called once during `renderd` startup before spawning worker threads.
 *
 * @param plugins_dir      Directory path containing Mapnik input datasource plugins (`.input`).
 * @param font_dir         Directory path containing font files for Mapnik's FreeType engine.
 * @param font_dir_recurse Non-zero to recursively load fonts from subdirectories of `font_dir`.
 */
void render_init(const char *plugins_dir, const char *font_dir, int font_dir_recurse)
{
	g_logger(G_LOG_LEVEL_INFO, "Renderd is using mapnik version %i.%i.%i", MAPNIK_MAJOR_VERSION, MAPNIK_MINOR_VERSION, MAPNIK_PATCH_VERSION);
	mapnik::datasource_cache::instance().register_datasources(plugins_dir);
	load_fonts(font_dir, font_dir_recurse);
}

/**
 * @brief Worker thread entry point that initializes per-thread Mapnik instances and processes render requests.
 *
 * Each worker thread maintains its own `xmlmapconfig` array (because `mapnik::Map` is not
 * safe for concurrent rendering across threads). After loading all configured XML stylesheets
 * and storage backends, the thread loops indefinitely pulling items from `render_request_queue`,
 * rendering metatiles, saving them to storage, optionally sending HTCP cache purges, and
 * replying to the requesting client.
 *
 * @param arg Pointer to the shared array of `xmlconfigitem` definitions (`XMLCONFIGS_MAX` elements).
 * @return Always returns `NULL` (runs until process termination).
 */
void *render_thread(void *arg)
{
	/* Shared master configuration array passed from renderd main thread */
	xmlconfigitem *parentxmlconfig = (xmlconfigitem *)arg;
	/* Thread-local Mapnik map instances and storage handles for each configured style */
	xmlmapconfig maps[XMLCONFIGS_MAX];
	/* Loop index over configured map styles */
	int i;
	/* Count of active map configurations loaded into maps[] */
	int iMaxConfigs;
	/* Elapsed wall-clock render duration in milliseconds (-1 if not rendered) */
	int render_time;

	g_logger(G_LOG_LEVEL_DEBUG, "Starting rendering thread: %lu", (unsigned long)pthread_self());

	for (iMaxConfigs = 0; iMaxConfigs < XMLCONFIGS_MAX; ++iMaxConfigs) {
		if (parentxmlconfig[iMaxConfigs].xmlname == NULL || parentxmlconfig[iMaxConfigs].xmlfile == NULL) {
			break;
		}

		maps[iMaxConfigs].maxzoom = parentxmlconfig[iMaxConfigs].max_zoom;
		maps[iMaxConfigs].minzoom = parentxmlconfig[iMaxConfigs].min_zoom;
		maps[iMaxConfigs].output_format = strndup(parentxmlconfig[iMaxConfigs].output_format, PATH_MAX);
		maps[iMaxConfigs].parameterize_function = init_parameterization_function(parentxmlconfig[iMaxConfigs].parameterization);
		maps[iMaxConfigs].scale = parentxmlconfig[iMaxConfigs].scale_factor;
		maps[iMaxConfigs].store = init_storage_backend(parentxmlconfig[iMaxConfigs].tile_dir);
		maps[iMaxConfigs].tilesize = parentxmlconfig[iMaxConfigs].tile_px_size;
		maps[iMaxConfigs].xmlfile = strndup(parentxmlconfig[iMaxConfigs].xmlfile, PATH_MAX);
		maps[iMaxConfigs].xmlname = strndup(parentxmlconfig[iMaxConfigs].xmlname, PATH_MAX);

		if (maps[iMaxConfigs].store) {
			maps[iMaxConfigs].ok = 1;

			maps[iMaxConfigs].map.resize(RENDER_SIZE, RENDER_SIZE);

			try {
				pthread_mutex_lock(&map_load_mutex);
				mapnik::load_map(maps[iMaxConfigs].map, maps[iMaxConfigs].xmlfile);

				if (!maps[iMaxConfigs].parameterize_function) {
					maps[iMaxConfigs].map.load_fonts();
				}

				pthread_mutex_unlock(&map_load_mutex);

				/* If we have more than 10 rendering threads configured, we need to fix
				 * up the mapnik datasources to support larger postgres connection pools
				 */
				if (parentxmlconfig[iMaxConfigs].num_threads > 10) {
					g_logger(G_LOG_LEVEL_DEBUG, "Updating max_connection parameter for mapnik layers to reflect thread count");
					parameterize_map_max_connections(maps[iMaxConfigs].map, parentxmlconfig[iMaxConfigs].num_threads);
				}

				maps[iMaxConfigs].prj = get_projection(maps[iMaxConfigs].map.srs().c_str());
				__atomic_add_fetch(&parentxmlconfig[iMaxConfigs].total_threads_ok, 1, __ATOMIC_SEQ_CST);
			} catch (std::exception const &ex) {
				pthread_mutex_unlock(&map_load_mutex);
				g_logger(G_LOG_LEVEL_ERROR, "An error occurred while loading the map layer '%s': %s", maps[iMaxConfigs].xmlname, ex.what());
				maps[iMaxConfigs].ok = 0;
			} catch (...) {
				pthread_mutex_unlock(&map_load_mutex);
				g_logger(G_LOG_LEVEL_ERROR, "An unknown error occurred while loading the map layer '%s'", maps[iMaxConfigs].xmlname);
				maps[iMaxConfigs].ok = 0;
			}

#ifdef HTCP_EXPIRE_CACHE
			maps[iMaxConfigs].host = strndup(parentxmlconfig[iMaxConfigs].host, PATH_MAX);
			maps[iMaxConfigs].htcphost = strndup(parentxmlconfig[iMaxConfigs].htcpip, PATH_MAX);
			maps[iMaxConfigs].xmluri = strndup(parentxmlconfig[iMaxConfigs].xmluri, PATH_MAX);

			if (strnlen(maps[iMaxConfigs].htcphost, PATH_MAX) > 0) {
				maps[iMaxConfigs].htcpsock = init_cache_expire(maps[iMaxConfigs].htcphost);

				if (maps[iMaxConfigs].htcpsock > 0) {
					g_logger(G_LOG_LEVEL_DEBUG, "Successfully opened socket for HTCP cache expiry");
				} else {
					g_logger(G_LOG_LEVEL_ERROR, "Failed to open socket for HTCP cache expiry");
				}
			} else {
				maps[iMaxConfigs].htcpsock = -1;
			}

#endif /* HTCP_EXPIRE_CACHE */
		} else {
			maps[iMaxConfigs].ok = 0;
		}
	}

	while (1) {
		/* Protocol response status code sent back to the requesting client */
		enum protoCmd ret;
		/* Next queued render request item (NULL if queue is currently empty) */
		struct item *item = request_queue_fetch_request(render_request_queue);
		render_time = -1;

		if (item) {
			/* Wire protocol request payload inside the queue item */
			struct protocol *req = &item->req;

			/* At very low zoom levels the whole world may be smaller than METATILE */
			unsigned int size = MIN(METATILE, 1 << req->z);

			for (i = 0; i < iMaxConfigs; ++i) {
				if (!strcmp(maps[i].xmlname, req->xmlname)) {
					if (maps[i].ok) {
						if (check_xyz(item->mx, item->my, req->z, &(maps[i]))) {
							/* Container accumulating rendered sub-tiles for this metatile */
							metaTile tiles(req->xmlname, req->options, item->mx, item->my, req->z);

							/* Timestamp structs and millisecond counters for measuring render duration */
							timeval tim;
							gettimeofday(&tim, NULL);
							long t1 = tim.tv_sec * 1000 + (tim.tv_usec / 1000);

							/* Existing metatile metadata from storage (size > 0 indicates re-render) */
							struct stat_info sinfo = maps[i].store->tile_stat(maps[i].store, req->xmlname, req->options, item->mx, item->my, req->z);

							if (sinfo.size > 0)
								g_logger(G_LOG_LEVEL_DEBUG, "START TILE %s %d %d-%d %d-%d, age %.2f days",
									 req->xmlname, req->z, item->mx, item->mx + size - 1, item->my, item->my + size - 1,
									 (tim.tv_sec - sinfo.mtime) / 86400.0);
							else
								g_logger(G_LOG_LEVEL_DEBUG, "START TILE %s %d %d-%d %d-%d, new metatile",
									 req->xmlname, req->z, item->mx, item->mx + size - 1, item->my, item->my + size - 1);

							ret = render(&(maps[i]), item->mx, item->my, req->z, req->options, tiles);

							gettimeofday(&tim, NULL);
							long t2 = tim.tv_sec * 1000 + (tim.tv_usec / 1000);

							g_logger(G_LOG_LEVEL_DEBUG, "DONE TILE %s %d %d-%d %d-%d in %.3lf seconds",
								 req->xmlname, req->z, item->mx, item->mx + size - 1, item->my, item->my + size - 1, (t2 - t1) / 1000.0);

							render_time = t2 - t1;

							if (ret == cmdDone) {
								try {
									tiles.save(maps[i].store);
#ifdef HTCP_EXPIRE_CACHE
									tiles.expire_tiles(maps[i].htcpsock, maps[i].host, maps[i].xmluri);
#endif

								} catch (std::exception const &ex) {
									g_logger(G_LOG_LEVEL_ERROR, "Received exception when writing metatile to disk: %s", ex.what());
									ret = cmdNotDone;
								} catch (...) {
									/* Treat any unknown storage error as fatal and request daemon shutdown */
									g_logger(G_LOG_LEVEL_CRITICAL, "Failed writing metatile to disk with unknown error, requesting exit.");
									ret = cmdNotDone;
									request_exit();
								}
							}
						} else {
							g_logger(G_LOG_LEVEL_WARNING, "Received request for map layer %s is outside of acceptable bounds z(%i), x(%i), y(%i)",
								 req->xmlname, req->z, req->x, req->y);
							ret = cmdIgnore;
						}
					} else {
						g_logger(G_LOG_LEVEL_ERROR, "Received request for map layer '%s' which failed to load", req->xmlname);
						ret = cmdNotDone;
					}

					send_response(item, ret, render_time);

					if ((ret != cmdDone) && (ret != cmdIgnore)) {
						sleep(10); /* Back off briefly after a render/storage error to allow transient issues to recover */
					}

					break;
				}
			}

			if (i == iMaxConfigs) {
				g_logger(G_LOG_LEVEL_ERROR, "No map for: %s", req->xmlname);
				/* Reply and release the request, as for the other failures; otherwise
				 * the client waits for its full timeout and the request (and any later
				 * duplicates of it) stays on the render queue for good */
				send_response(item, cmdNotDone, -1);
			}
		} else {
			sleep(1); /* TODO: Use an event to indicate there are new requests */
		}
	}

	return NULL;
}
