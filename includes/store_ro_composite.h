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

#ifndef STORE_RO_COMPOSITE_H
#define STORE_RO_COMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

#ifdef HAVE_CAIRO
#include <cairo/cairo.h>

#include "protocol.h"
#include "store.h"

	/**
	 * @brief Single-tile cache holding the most recently composited PNG tile and its metadata.
	 *
	 * Prevents redundant fetching and Cairo alpha-blending when a `tile_stat()` call is
	 * immediately followed by `tile_read()` for the same `(xmlname, x, y, z)` tile.
	 */
	struct store_ro_composite_tile_cache {
		/** @brief Combined metadata and expiry status for the cached composited tile. */
		struct stat_info st_stat;
		/** @brief Heap-allocated PNG byte buffer (`MAX_TILE_SIZE` bytes) holding the composited image. */
		char *tile;
		/** @brief Tile X, Y, and zoom coordinates of the cached tile (`-1` when invalid). */
		int x, y, z;
		/** @brief Logical map style name (`xmlname`) corresponding to the cached tile. */
		char xmlname[XMLCONFIG_MAX];
	};

	/**
	 * @brief Per-backend state for the read-only composite storage engine.
	 */
	struct ro_composite_ctx {
		/** @brief Foreground (top) storage backend layered over `store_secondary`. */
		struct storage_backend *store_primary;
		/** @brief Map style name (`xmlconfig`) passed to `store_primary`. */
		char xmlconfig_primary[XMLCONFIG_MAX];
		/** @brief Background (base) storage backend composited beneath `store_primary`. */
		struct storage_backend *store_secondary;
		/** @brief Map style name (`xmlconfig`) passed to `store_secondary`. */
		char xmlconfig_secondary[XMLCONFIG_MAX];
		/** @brief Single-entry cache storing the last composited tile payload and stat metadata. */
		struct store_ro_composite_tile_cache cache;
		/** @brief Target tile width and height in pixels (e.g., `256` or `512`). */
		int render_size;
	};

	/**
	 * @brief Closure state passed to Cairo's `cairo_surface_write_to_png_stream()` callback.
	 */
	typedef struct {
		/** @brief Destination byte buffer receiving encoded PNG chunks. */
		char *data;
		/** @brief Maximum writable byte capacity of `data`. */
		unsigned int max_size;
		/** @brief Current write cursor offset (and total bytes written) in `data`. */
		unsigned int pos;
	} png_stream_to_byte_array_closure_t;
#endif /* HAVE_CAIRO */

	struct storage_backend *init_storage_ro_composite(const char *connection_string);

#ifdef __cplusplus
}
#endif

#endif /* STORE_RO_COMPOSITE_H */
