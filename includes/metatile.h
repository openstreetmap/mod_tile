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

#ifndef METATILE_H
#define METATILE_H

#include <stdlib.h>

#include "config.h"
#include "render_config.h"

#ifdef __cplusplus
#include <sstream>
extern "C" {
#endif

/** @brief 4-byte file header magic signature for uncompressed `.meta` archives. */
#define META_MAGIC "META"
/** @brief 4-byte file header magic signature for compressed `.meta` archives. */
#define META_MAGIC_COMPRESSED "METZ"

	/**
	 * @brief Index table record locating a single sub-tile payload within a `.meta` archive.
	 */
	struct entry {
		/** @brief Byte offset of the sub-tile payload measured from the start of the `.meta` archive. */
		int offset;
		/** @brief Byte length of the encoded sub-tile payload (`0` if empty). */
		int size;
	};

	/**
	 * @brief Binary on-disk and wire header layout for a serialized `METATILE x METATILE` archive.
	 *
	 * Immediately followed in memory/on disk by `count` `struct entry` records and the
	 * concatenated raw tile payloads referenced by each entry's `offset` and `size`.
	 */
	struct meta_layout {
		/** @brief 4-byte magic identifier (`META_MAGIC` or `META_MAGIC_COMPRESSED`). */
		char magic[4];
		/** @brief Total number of sub-tile slots in `index[]` (always `METATILE * METATILE`). */
		int count;
		/** @brief Lowest (top-left) tile X and Y coordinates of this metatile, plus zoom level Z. */
		int x, y, z;
		/** @brief Flexible array of `count` sub-tile offset/size descriptors. */
		struct entry index[];
	};

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/**
 * @brief In-memory builder and serializer for a `METATILE x METATILE` grid of rendered map tiles.
 *
 * Collects individual rendered tile payloads (`set()`), serializes them with a `meta_layout`
 * header and offset index table into a storage backend (`save()`), and dispatches HTCP cache
 * invalidation requests for all constituent tiles (`expire_tiles()`).
 */
class metaTile
{
      public:
	metaTile(const std::string &xmlconfig, const std::string &options, int x, int y, int z);
	void clear();
	void set(int x, int y, const std::string &data);
	const std::string get(int x, int y);
	int xyz_to_meta_offset(int x, int y, int z);
	void save(struct storage_backend *store);
	void expire_tiles(int sock, const char *host, const char *uri);

      private:
	/** @brief Top-left (minimum) tile X, Y coordinates and zoom level Z of this metatile. */
	int x_, y_, z_;
	/** @brief Logical map style name (e.g., `"default"`). */
	std::string xmlconfig_;
	/** @brief Style parameterization options string (empty if unused). */
	std::string options_;
	/** @brief 2D grid of encoded image byte strings indexed by `[dx][dy]` in `0 .. METATILE - 1`. */
	std::string tile[METATILE][METATILE];
	/** @brief Fixed byte size of `struct meta_layout` plus `METATILE * METATILE` index entries. */
	static const int header_size = sizeof(struct meta_layout) + (sizeof(struct entry) * (METATILE * METATILE));
};
#endif

#endif /* METATILE_H */
