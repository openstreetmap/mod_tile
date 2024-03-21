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

#ifndef METATILE_H
#define METATILE_H

#include <stdlib.h>

#include "config.h"
#include "render_config.h"

#ifdef __cplusplus
#include <sstream>
extern "C" {
#endif

#define META_MAGIC "META"
#define META_MAGIC_COMPRESSED "METZ"

struct entry {
	int offset;
	int size;
};

struct meta_layout {
	char magic[4];
	int count;	      // METATILE ^ 2
	int x, y, z;	      // lowest x,y of this metatile, plus z
	struct entry index[]; // count entries
	// Followed by the tile data
	// The index offsets are measured from the start of the file
};

#ifdef __cplusplus
}

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
	int x_, y_, z_;
	std::string xmlconfig_;
	std::string options_;
	std::string tile[METATILE][METATILE];
	static const int header_size = sizeof(struct meta_layout) + (sizeof(struct entry) * (METATILE * METATILE));
};

#endif
#endif
