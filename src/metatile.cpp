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
#include <cstdlib>
#include <cstring>
#include <glib.h>
#include <string>
#include <sys/types.h>

#include "cache_expire.h"
#include "g_logger.h"
#include "metatile.h"
#include "render_config.h"
#include "store.h"

/**
 * @brief Construct a new metaTile container for the given map style and top-left tile coordinates.
 *
 * Initializes all sub-tile image buffers to empty strings via clear().
 *
 * @param xmlconfig Logical map style name (e.g., "default").
 * @param options   Style parameterization options string (empty string if unused).
 * @param x         Top-left tile X coordinate of the metatile (aligned to METATILE).
 * @param y         Top-left tile Y coordinate of the metatile (aligned to METATILE).
 * @param z         Tile zoom level.
 */
metaTile::metaTile(const std::string &xmlconfig, const std::string &options, int x, int y, int z) : x_(x), y_(y), z_(z), xmlconfig_(xmlconfig), options_(options)
{
	clear();
}

/**
 * @brief Reset all sub-tile image payloads in the METATILE x METATILE grid to empty strings.
 */
void metaTile::clear()
{
	for (int x = 0; x < METATILE; x++)
		for (int y = 0; y < METATILE; y++) {
			tile[x][y] = "";
		}
}

/**
 * @brief Store encoded image data for a single sub-tile within the metatile grid.
 *
 * @param x    Sub-tile X offset within the metatile (`0 <= x < METATILE`).
 * @param y    Sub-tile Y offset within the metatile (`0 <= y < METATILE`).
 * @param data Binary image payload (e.g., PNG/JPEG/WebP bytes).
 */
void metaTile::set(int x, int y, const std::string &data)
{
	tile[x][y] = data;
}

/**
 * @brief Retrieve the encoded image data for a single sub-tile within the metatile grid.
 *
 * @param x Sub-tile X offset within the metatile (`0 <= x < METATILE`).
 * @param y Sub-tile Y offset within the metatile (`0 <= y < METATILE`).
 * @return Copy of the binary image payload string (empty if unset).
 */
const std::string metaTile::get(int x, int y)
{
	return tile[x][y];
}

/**
 * @brief Compute the linear index into a metatile's offset table for tile `(x, y, z)`.
 *
 * Masks the lower bits of `x` and `y` against `METATILE - 1` (assuming `METATILE` is a
 * power of two) and arranges entries in column-major order (`dx * METATILE + dy`).
 *
 * @param x Global tile X coordinate.
 * @param y Global tile Y coordinate.
 * @param z Tile zoom level (unused; kept for signature consistency).
 * @return Zero-based index in `[0, METATILE * METATILE - 1]`.
 */
int metaTile::xyz_to_meta_offset(int x, int y, int z)
{
	/* Bitmask extracting the sub-tile offset modulo METATILE (requires power-of-two METATILE) */
	unsigned char mask = METATILE - 1;
	return (x & mask) * METATILE + (y & mask);
}

/**
 * @brief Serialize the metatile header, index table, and tile payloads, and write to storage.
 *
 * Assembles the binary metatile blob in memory:
 *   1. `struct meta_layout`: Magic header (`META_MAGIC`), tile count, and base `(x, y, z)`.
 *   2. `struct entry[METATILE * METATILE]`: Byte offset and byte size for each sub-tile.
 *   3. Concatenated raw image payloads for all sub-tiles.
 *
 * Writes the resulting buffer via `store->metatile_write()` and logs a warning if
 * memory allocation or storage persistence fails.
 *
 * @param store Storage backend instance responsible for persisting the metatile.
 */
void metaTile::save(struct storage_backend *store)
{
	/* Sub-tile grid loop iterators (ox, oy) and grid dimension bound (limit) */
	int ox, oy, limit;
	/* Running byte offset within the serialized metatile buffer (and final total byte size) */
	ssize_t offset;
	/* Binary metatile file header */
	struct meta_layout m;
	/* Index table recording each sub-tile's byte offset and byte length */
	struct entry offsets[METATILE * METATILE];
	/* Heap-allocated buffer holding the complete serialized metatile */
	char *metatilebuffer;
	/* Scratch path buffer used to format the storage identifier on write failure */
	char *tmp;

	memset(&m, 0, sizeof(m));
	memset(&offsets, 0, sizeof(offsets));

	/* Populate metatile header */
	m.count = METATILE * METATILE;
	memcpy(m.magic, META_MAGIC, strlen(META_MAGIC));
	m.x = x_;
	m.y = y_;
	m.z = z_;

	offset = header_size;
	limit = METATILE;

	/* Compute byte offset and size for every sub-tile in the index table */
	for (ox = 0; ox < limit; ox++) {
		for (oy = 0; oy < limit; oy++) {
			int mt = xyz_to_meta_offset(x_ + ox, y_ + oy, z_);
			offsets[mt].offset = offset;
			offsets[mt].size = tile[ox][oy].size();
			offset += offsets[mt].size;
		}
	}

	metatilebuffer = (char *)malloc(offset);

	if (metatilebuffer == 0) {
		g_logger(G_LOG_LEVEL_WARNING, "Failed to write metatile. Out of memory");
		return;
	}

	memset(metatilebuffer, 0, offset);
	memcpy(metatilebuffer, &m, sizeof(m));
	memcpy(metatilebuffer + sizeof(m), &offsets, sizeof(offsets));

	/* Copy each sub-tile's encoded image bytes into its designated offset in the buffer */
	for (ox = 0; ox < limit; ox++) {
		for (oy = 0; oy < limit; oy++) {
			memcpy(metatilebuffer + offsets[xyz_to_meta_offset(x_ + ox, y_ + oy, z_)].offset, (const void *)tile[ox][oy].data(), tile[ox][oy].size());
		}
	}

	if (store->metatile_write(store, xmlconfig_.c_str(), options_.c_str(), x_, y_, z_, metatilebuffer, offset) != offset) {
		tmp = (char *)malloc(sizeof(char) * PATH_MAX);
		g_logger(G_LOG_LEVEL_WARNING, "Failed to write metatile to %s", store->tile_storage_id(store, xmlconfig_.c_str(), options_.c_str(), x_, y_, z_, tmp));
		free(tmp);
	}

	free(metatilebuffer);
}

/**
 * @brief Send HTCP cache purge requests for every individual tile covered by this metatile.
 *
 * Clamps the iteration range to `min(2^z, METATILE)` so non-existent coordinates are not
 * purged at very low zoom levels (e.g., z = 0..2).
 *
 * @param sock Connected UDP socket descriptor to the HTCP cache server (no-op if `< 0`).
 * @param host Public HTTP hostname used in the purged tile URLs.
 * @param uri  Base URI path prefix for the tileset.
 */
void metaTile::expire_tiles(int sock, const char *host, const char *uri)
{
	if (sock < 0) {
		return;
	}

	g_logger(G_LOG_LEVEL_INFO, "Purging metatile via HTCP cache expiry");
	/* Sub-tile grid offsets within the metatile */
	int ox, oy;
	/* Number of tiles per axis in this metatile (clamped when world grid < METATILE) */
	int limit = (1 << z_);
	limit = MIN(limit, METATILE);

	/* Dispatch an HTCP CLR packet for each constituent tile in the metatile */
	for (ox = 0; ox < limit; ox++) {
		for (oy = 0; oy < limit; oy++) {
			cache_expire(sock, host, uri, (x_ + ox), (y_ + oy), z_);
		}
	}
}
