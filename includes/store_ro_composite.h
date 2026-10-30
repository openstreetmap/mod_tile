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

#ifndef STOREROCOMPOSITE_H
#define STOREROCOMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

#ifdef HAVE_CAIRO
#include <cairo/cairo.h>

#include "protocol.h"
#include "store.h"

struct store_ro_composite_tile_cache {
	struct stat_info st_stat;
	char * tile;
	int x, y, z;
	char xmlname[XMLCONFIG_MAX];
};

struct ro_composite_ctx {
	struct storage_backend * store_primary;
	char xmlconfig_primary[XMLCONFIG_MAX];
	struct storage_backend * store_secondary;
	char xmlconfig_secondary[XMLCONFIG_MAX];
	struct store_ro_composite_tile_cache cache;
	int render_size;
};

typedef struct {
	char *data;
	unsigned int max_size;
	unsigned int pos;
} png_stream_to_byte_array_closure_t;
#endif

struct storage_backend *init_storage_ro_composite(const char *connection_string);

#ifdef __cplusplus
}

#endif
#endif
