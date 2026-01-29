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

#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "g_logger.h"
#include "store.h"
#include "store_null.h"

static int tile_read(struct storage_backend * store,
		     const char *xmlconfig,
		     const char *options,
		     int x, int y, int z,
		     char *buf, size_t sz,
		     int * compressed, char * err_msg)
{
	snprintf(err_msg, PATH_MAX - 1, "Cannot read from NULL storage.");
	return -1;
}

static struct stat_info tile_stat(struct storage_backend * store,
				  const char *xmlconfig,
				  const char *options,
				  int x, int y, int z)
{
	struct stat_info tile_stat;
	tile_stat.size = -1;
	tile_stat.atime = 0;
	tile_stat.mtime = 0;
	tile_stat.ctime = 0;
	tile_stat.expired = 1;
	return tile_stat;
}

static int metatile_write(struct storage_backend * store,
			  const char *xmlconfig,
			  const char *options,
			  int x, int y, int z,
			  const char *buf, int sz)
{
	// fake like we actually wrote the tile, but we didn't...
	return sz;
}

static int metatile_delete(struct storage_backend * store,
			   const char *xmlconfig,
			   int x, int y, int z)
{
	return 0;
}

static int metatile_expire(struct storage_backend * store,
			   const char *xmlconfig,
			   int x, int y, int z)
{
	return 0;
}

static char * tile_storage_id(struct storage_backend * store,
			      const char *xmlconfig,
			      const char *options,
			      int x, int y, int z,
			      char * string)
{
	snprintf(string, PATH_MAX - 1, "null://");
	return string;
}

static int close_storage(struct storage_backend * store)
{
	return 0;
}

struct storage_backend *init_storage_null()
{
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof * store);

	if (store == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_null: Failed to allocate memory for storage backend");
		return NULL;
	}

	store->storage_ctx = NULL;
	store->tile_read = &tile_read;
	store->tile_stat = &tile_stat;
	store->metatile_write = &metatile_write;
	store->metatile_delete = &metatile_delete;
	store->metatile_expire = &metatile_expire;
	store->tile_storage_id = &tile_storage_id;
	store->close_storage = &close_storage;

	return store;
}
