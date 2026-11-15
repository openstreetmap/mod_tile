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

/* No-op ("blackhole") tile storage backend:
 * Discards all written metatiles while reporting success, and treats every
 * tile read or stat lookup as an expired cache miss. Useful for benchmarking
 * Mapnik rendering speed without disk or network storage overhead. */

#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "g_logger.h"
#include "store.h"
#include "store_null.h"

/**
 * @brief Simulate a failed tile read from the no-op null storage backend.
 *
 * Always writes `"Cannot read from NULL storage."` into `err_msg` and returns `-1`.
 *
 * @param store      Null `storage_backend` instance (unused).
 * @param xmlconfig  Logical map style name (unused).
 * @param options    Style parameterization options string (unused).
 * @param x          Tile X coordinate (unused).
 * @param y          Tile Y coordinate (unused).
 * @param z          Tile zoom level (unused).
 * @param buf        Output buffer (untouched).
 * @param sz         Capacity of `buf` in bytes (unused).
 * @param compressed Compression flag output pointer (untouched).
 * @param err_msg    Caller-supplied buffer (`PATH_MAX` bytes) populated with an error message.
 * @return Always returns `-1` (cache miss / unreadable).
 */
static int tile_read(struct storage_backend *store,
		     const char *xmlconfig,
		     const char *options,
		     int x, int y, int z,
		     char *buf, size_t sz,
		     int *compressed, char *err_msg)
{
	snprintf(err_msg, PATH_MAX - 1, "Cannot read from NULL storage.");
	return -1;
}

/**
 * @brief Return metadata indicating that the requested tile does not exist and is expired.
 *
 * @param store     Null `storage_backend` instance (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return `struct stat_info` with `size = -1`, zeroed timestamps, and `expired = 1`.
 */
static struct stat_info tile_stat(struct storage_backend *store,
				  const char *xmlconfig,
				  const char *options,
				  int x, int y, int z)
{
	/* Stat result signaling a missing, expired tile */
	struct stat_info tile_stat;
	tile_stat.size = -1;
	tile_stat.atime = 0;
	tile_stat.mtime = 0;
	tile_stat.ctime = 0;
	tile_stat.expired = 1;
	return tile_stat;
}

/**
 * @brief Discard a serialized metatile buffer while reporting a successful write.
 *
 * @param store     Null `storage_backend` instance (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Top-left tile X coordinate of the metatile (unused).
 * @param y         Top-left tile Y coordinate of the metatile (unused).
 * @param z         Tile zoom level (unused).
 * @param buf       Serialized metatile buffer (ignored).
 * @param sz        Byte length of `buf`.
 * @return Always returns `sz` to simulate a complete write.
 */
static int metatile_write(struct storage_backend *store,
			  const char *xmlconfig,
			  const char *options,
			  int x, int y, int z,
			  const char *buf, int sz)
{
	/* Pretend the metatile was written without persisting anything */
	return sz;
}

/**
 * @brief No-op metatile deletion handler for the null storage backend.
 *
 * @param store     Null `storage_backend` instance (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `0`.
 */
static int metatile_delete(struct storage_backend *store,
			   const char *xmlconfig,
			   int x, int y, int z)
{
	return 0;
}

/**
 * @brief No-op metatile expiry handler for the null storage backend.
 *
 * @param store     Null `storage_backend` instance (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `0`.
 */
static int metatile_expire(struct storage_backend *store,
			   const char *xmlconfig,
			   int x, int y, int z)
{
	return 0;
}

/**
 * @brief Format the static `"null://"` storage identifier URI.
 *
 * @param store     Null `storage_backend` instance (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @param string    Caller-supplied output buffer (`PATH_MAX` bytes) receiving `"null://"`.
 * @return Pointer to `string`.
 */
static char *tile_storage_id(struct storage_backend *store,
			     const char *xmlconfig,
			     const char *options,
			     int x, int y, int z,
			     char *string)
{
	snprintf(string, PATH_MAX - 1, "null://");
	return string;
}

/**
 * @brief Close the null storage backend (no resources to release).
 *
 * @param store Null `storage_backend` instance (unused).
 * @return Always returns `0`.
 */
static int close_storage(struct storage_backend *store)
{
	return 0;
}

/**
 * @brief Allocate and initialize a no-op null `storage_backend`.
 *
 * Sets `storage_ctx` to `NULL` and wires all backend operation function pointers
 * to the no-op handlers defined in this file.
 *
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on allocation failure.
 */
struct storage_backend *init_storage_null()
{
	/* Newly allocated storage_backend vtable with NULL context */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof *store);

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
