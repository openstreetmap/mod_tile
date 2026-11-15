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

/* Memcached metatile storage backend:
 * Stores each serialized METATILE x METATILE archive in Memcached under a key
 * derived from its map style and metatile-aligned coordinates, prefixed with a
 * `struct stat_info` header to track timestamps and expiry state. Supports
 * full read, write, stat, delete, and expire operations. */

#include <glib.h>
#include <stdio.h>

#include "config.h"
#include "g_logger.h"
#include "store.h"
#include "store_memcached.h"

#ifdef HAVE_LIBMEMCACHED
#include <libmemcached/memcached.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "metatile.h"
#include "render_config.h"

/**
 * @brief Format the Memcached object key for a metatile given `(xmlconfig, options, x, y, z)`.
 *
 * Masks `x` and `y` down to the top-left `(0, 0)` corner of their `METATILE x METATILE`
 * block and formats `key` as `"<xmlconfig>/<x>/<y>/<z>[.<options>].meta"`.
 *
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string (omitted from key if empty).
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @param key       Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted key.
 * @return Pointer to `key`.
 */
static char *memcached_xyzo_to_storagekey(const char *xmlconfig, const char *options, int x, int y, int z, char *key)
{
	/* Bitmask used to align x and y to the top-left corner of the metatile */
	int mask;

	mask = METATILE - 1;
	x &= ~mask;
	y &= ~mask;

	if (strlen(options)) {
		snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.%s.meta", xmlconfig, x, y, z, options);
	} else {
		snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, x, y, z);
	}

	return key;
}

/**
 * @brief Format the Memcached object key for an unparameterized metatile.
 *
 * Convenience wrapper around memcached_xyzo_to_storagekey() with empty `options` (`""`).
 *
 * @param xmlconfig Logical map style name.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @param key       Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted key.
 * @return Pointer to `key`.
 */
static char *memcached_xyz_to_storagekey(const char *xmlconfig, int x, int y, int z, char *key)
{
	return memcached_xyzo_to_storagekey(xmlconfig, "", x, y, z, key);
}

/**
 * @brief Fetch a metatile object from Memcached and extract a single sub-tile payload.
 *
 * Retrieves the cached blob (which begins with a `struct stat_info` prefix followed by
 * `struct meta_layout` and the sub-tile index/payloads), validates the metatile magic
 * signature and tile count, and copies the requested sub-tile bytes into `buf`.
 *
 * @param store      Initialized memcached `storage_backend` (`storage_ctx` holds `memcached_st *`).
 * @param xmlconfig  Logical map style name.
 * @param options    Style parameterization options string.
 * @param x          Global tile X coordinate.
 * @param y          Global tile Y coordinate.
 * @param z          Tile zoom level.
 * @param buf        Caller-supplied output buffer receiving the sub-tile image bytes.
 * @param sz         Maximum capacity of `buf` in bytes.
 * @param compressed Output flag set to `1` if `META_MAGIC_COMPRESSED` is present, `0` otherwise.
 * @param log_msg    Caller-supplied buffer receiving an error message on failure.
 * @return Number of sub-tile bytes copied into `buf` (`>= 0`) on success, or a negative error code:
 *         - `-1`: Header allocation failure or Memcached cache miss/error.
 *         - `-4`: Invalid metatile magic bytes.
 *         - `-5`: Unexpected tile count (`m->count != METATILE * METATILE`).
 *         - `-6`: Stored sub-tile size exceeds output buffer capacity `sz`.
 */
static int memcached_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
	/* Memcached lookup key for the enclosing metatile */
	char meta_path[PATH_MAX];
	/* Linear sub-tile index within the metatile's entry table (0 .. METATILE^2 - 1) */
	int meta_offset;
	/* Combined byte size of the metatile header and sub-tile offset/size index table */
	unsigned int header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	/* Heap-allocated copy of the metatile header and index table */
	struct meta_layout *m = (struct meta_layout *)malloc(header_len);
	/* Byte offset within buf_raw (accounting for leading stat_info) and byte length of the sub-tile */
	size_t file_offset, tile_size;
	/* Bitmask extracting sub-tile offsets modulo METATILE */
	int mask;
	/* Memcached item flags returned by memcached_get() */
	uint32_t flags;
	/* Byte length of the retrieved Memcached blob */
	size_t len;
	/* libmemcached return status code */
	memcached_return_t rc;
	/* Heap-allocated raw value blob returned by memcached_get() */
	char *buf_raw;

	if (m == NULL) {
		snprintf(log_msg, 1024, "Failed to allocate memory for metatile header\n");
		return -1;
	}

	mask = METATILE - 1;
	meta_offset = (x & mask) * METATILE + (y & mask);

	memcached_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	buf_raw = memcached_get((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

	if (rc != MEMCACHED_SUCCESS) {
		free(m);
		return -1;
	}

	/* Skip the prepended struct stat_info to copy the metatile header and index table */
	memcpy(m, buf_raw + sizeof(struct stat_info), header_len);

	/* Verify magic signature and determine whether payload entries are compressed */
	if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
		if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
			snprintf(log_msg, 1024, "Meta file header magic mismatch\n");
			free(m);
			return -4;
		} else {
			*compressed = 1;
		}
	} else {
		*compressed = 0;
	}

	/* Currently only fixed METATILE x METATILE grids are supported */
	if (m->count != (METATILE * METATILE)) {
		snprintf(log_msg, 1024, "Meta file header bad count %d != %d\n", m->count, METATILE * METATILE);
		free(m);
		return -5;
	}

	/* Offset in buf_raw includes the leading struct stat_info header */
	file_offset = m->index[meta_offset].offset + sizeof(struct stat_info);
	tile_size = m->index[meta_offset].size;

	free(m);

	if (tile_size > sz) {
		snprintf(log_msg, 1024, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
		tile_size = sz;
		return -6;
	}

	memcpy(buf, buf_raw + file_offset, tile_size);
	free(buf_raw);
	return tile_size;
}

/**
 * @brief Retrieve stored metadata, timestamps, and expiry status for a tile in Memcached.
 *
 * Reads the cached metatile blob from Memcached, unpacks the leading `struct stat_info`,
 * and sets `tile_stat.size` to the individual sub-tile's byte size (or `-1` on cache miss).
 *
 * @param store     Initialized memcached `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Populated `struct stat_info` (`size < 0` indicates cache miss or error).
 */
static struct stat_info memcached_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Normalized backend-agnostic stat result returned to the caller */
	struct stat_info tile_stat;
	/* Memcached lookup key for the enclosing metatile */
	char meta_path[PATH_MAX];
	/* Combined byte size of the metatile header and sub-tile index table */
	unsigned int header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	/* Heap-allocated copy of the metatile header and index table */
	struct meta_layout *m = (struct meta_layout *)malloc(header_len);
	/* Raw value blob returned by memcached_get() */
	char *buf;
	/* Byte length of the retrieved Memcached blob */
	size_t len;
	/* Memcached item flags returned by memcached_get() */
	uint32_t flags;
	/* libmemcached return status code */
	memcached_return_t rc;
	/* Sub-tile index within the metatile and METATILE - 1 bitmask */
	int offset, mask;

	mask = METATILE - 1;
	offset = (x & mask) * METATILE + (y & mask);

	memcached_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	buf = memcached_get((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

	if (rc != MEMCACHED_SUCCESS) {
		tile_stat.size = -1;
		tile_stat.expired = 0;
		tile_stat.mtime = 0;
		tile_stat.atime = 0;
		tile_stat.ctime = 0;
		free(m);
		return tile_stat;
	}

	memcpy(&tile_stat, buf, sizeof(struct stat_info));
	memcpy(m, buf + sizeof(struct stat_info), header_len);
	tile_stat.size = m->index[offset].size;

	free(m);
	free(buf);
	return tile_stat;
}

/**
 * @brief Format a human-readable `memcached:///` storage URI identifying `(x, y, z)`.
 *
 * @param store     Initialized memcached `storage_backend` (unused).
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string (currently unused).
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @param string    Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted URI.
 * @return Pointer to `string`.
 */
static char *memcached_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
	snprintf(string, PATH_MAX - 1, "memcached:///%s/%d/%d/%d.meta", xmlconfig, x, y, z);
	return string;
}

/**
 * @brief Store a serialized metatile buffer in Memcached prefixed with `struct stat_info`.
 *
 * Prepends a freshly initialized `struct stat_info` (with `expired = 0` and current timestamps)
 * ahead of `buf`, writes the combined blob via `memcached_set()` with no expiration (`0`),
 * and flushes libmemcached client buffers.
 *
 * @param store     Initialized memcached `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string (currently unused in key formatting).
 * @param x         Top-left tile X coordinate of the metatile.
 * @param y         Top-left tile Y coordinate of the metatile.
 * @param z         Tile zoom level.
 * @param buf       Serialized metatile buffer (`meta_layout` + index table + tile payloads).
 * @param sz        Total byte length of `buf`.
 * @return `sz` on success, `-2` on memory allocation failure, or `-1` if `memcached_set()` fails.
 */
static int memcached_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
	/* Memcached storage key "<xmlconfig>/<x>/<y>/<z>.meta" */
	char meta_path[PATH_MAX];
	/* Scratch buffer for formatting storage ID in debug logs */
	char tmp[PATH_MAX];
	/* Metadata header prepended to the serialized metatile in Memcached */
	struct stat_info tile_stat;
	/* Combined byte length of struct stat_info + serialized metatile payload */
	int sz2 = sz + sizeof(struct stat_info);
	/* Heap buffer packing tile_stat followed by buf */
	char *buf2 = (char *)malloc(sz2);
	/* libmemcached return status code */
	memcached_return_t rc;

	if (buf2 == NULL) {
		return -2;
	}

	tile_stat.expired = 0;
	tile_stat.size = sz;
	tile_stat.mtime = time(NULL);
	tile_stat.atime = tile_stat.mtime;
	tile_stat.ctime = tile_stat.mtime;

	memcpy(buf2, &tile_stat, sizeof(tile_stat));
	memcpy(buf2 + sizeof(tile_stat), buf, sz);

	g_logger(G_LOG_LEVEL_DEBUG, "Trying to create and write a metatile to %s", memcached_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));

	snprintf(meta_path, PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, x, y, z);

	rc = memcached_set((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), buf2, sz2, (time_t)0, (uint32_t)0);
	free(buf2);

	if (rc != MEMCACHED_SUCCESS) {
		return -1;
	}

	memcached_flush_buffers((memcached_st *)store->storage_ctx);
	return sz;
}

/**
 * @brief Delete the cached metatile entry containing `(x, y, z)` from Memcached.
 *
 * @param store     Initialized memcached `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `0` if deleted successfully, or `-1` if `memcached_delete()` failed.
 */
static int memcached_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Memcached storage key for the target metatile */
	char meta_path[PATH_MAX];
	/* libmemcached return status code */
	memcached_return_t rc;

	/* TODO: deal with options */
	memcached_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

	rc = memcached_delete((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), 0);

	if (rc != MEMCACHED_SUCCESS) {
		return -1;
	}

	return 0;
}

/**
 * @brief Mark a cached metatile entry in Memcached as expired without evicting it.
 *
 * Fetches the existing cached blob, sets `expired = 1` in its leading `struct stat_info`
 * header, and writes the updated blob back via `memcached_replace()` so `mod_tile` can
 * serve stale tiles while triggering background re-rendering.
 *
 * @param store     Initialized memcached `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `0` on success, or `-1` if the entry was not found or `memcached_replace()` failed.
 */
static int memcached_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Memcached storage key for the target metatile */
	char meta_path[PATH_MAX];
	/* Raw cached value blob fetched from Memcached and modified in place */
	char *buf;
	/* Byte length of the fetched Memcached blob */
	size_t len;
	/* Memcached item flags preserved across memcached_replace() */
	uint32_t flags;
	/* libmemcached return status code */
	memcached_return_t rc;

	/* TODO: deal with options */
	memcached_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);
	buf = memcached_get((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

	if (rc != MEMCACHED_SUCCESS) {
		return -1;
	}

	((struct stat_info *)buf)->expired = 1;

	/* memcached_get() does not return a CAS value, so a compare-and-swap cannot be used here
	 * (the previous code passed an uninitialised CAS token). Replace the existing item instead. */
	rc = memcached_replace((memcached_st *)store->storage_ctx, meta_path, strlen(meta_path), buf, len, 0, flags);

	if (rc != MEMCACHED_SUCCESS) {
		free(buf);
		return -1;
	}

	free(buf);
	return 0;
}

/**
 * @brief Release the `memcached_st` client handle associated with a Memcached storage backend.
 *
 * @param store Storage backend instance to close.
 * @return Always returns `0`.
 */
static int memcached_close_storage(struct storage_backend *store)
{
	memcached_free((memcached_st *)store->storage_ctx);
	store->storage_ctx = NULL;
	return 0;
}

#endif /* HAVE_LIBMEMCACHED */

/**
 * @brief Allocate and initialize a Memcached metatile `storage_backend`.
 *
 * Parses `connection_string` of the form `"memcached://"` (defaulting to
 * `"--server=localhost"`) or `"memcached://<host[:port]>"` (translated to
 * `"--server=<host[:port]>"`), constructs a `memcached_st` handle via `memcached()`,
 * and wires all backend operation function pointers. Returns `NULL` if compiled
 * without `HAVE_LIBMEMCACHED` or if initialization fails.
 *
 * @param connection_string Backend URI starting with `"memcached://"`.
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on failure or if unsupported.
 */
struct storage_backend *init_storage_memcached(const char *connection_string)
{
#ifndef HAVE_LIBMEMCACHED
	g_logger(G_LOG_LEVEL_ERROR, "init_storage_memcached: Support for memcached storage has not been compiled into this program");
	return NULL;
#else  /* HAVE_LIBMEMCACHED */
	/* Default libmemcached option string when no server is appended to "memcached://" */
	const char *const default_memcached_options = "--server=localhost";
	/* Active libmemcached option string passed to memcached() */
	const char *memcached_options = default_memcached_options;
	/* Dynamically allocated "--server=..." option buffer when custom host/options are specified */
	char *memcached_options_buf = NULL;
	/* Newly allocated storage_backend vtable and context */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof(struct storage_backend));

	if (store == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_memcached: Failed to allocate memory for storage backend");
		return NULL;
	}

	/* Check if custom server/options were provided beyond the "memcached://" scheme prefix */
	if (strcmp(connection_string, "memcached://")) {
		size_t connection_string_len = strnlen(connection_string, PATH_MAX);
		memcached_options_buf = (char *)malloc(connection_string_len);

		if (memcached_options_buf != NULL) {
			/* Strip "memcached://" scheme (12 chars) and prefix with "--server=" */
			snprintf(memcached_options_buf, connection_string_len - 2, "--server=%.*s",
				 (int)(connection_string_len - 12), connection_string + 12);
			memcached_options = memcached_options_buf;
		}
	}

	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_memcached: Creating memcached ctx with options '%s'", memcached_options);
	/* Initialized libmemcached client context */
	memcached_st *memcached_storage_ctx = memcached(memcached_options, strlen(memcached_options));

	if (memcached_options_buf != NULL) {
		free(memcached_options_buf);
	}

	if (memcached_storage_ctx == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_memcached: Failed to create memcached ctx");
		free(store);
		return NULL;
	}

	store->storage_ctx = memcached_storage_ctx;
	store->tile_read = &memcached_tile_read;
	store->tile_stat = &memcached_tile_stat;
	store->metatile_write = &memcached_metatile_write;
	store->metatile_delete = &memcached_metatile_delete;
	store->metatile_expire = &memcached_metatile_expire;
	store->tile_storage_id = &memcached_tile_storage_id;
	store->close_storage = &memcached_close_storage;

	return store;
#endif /* HAVE_LIBMEMCACHED */
}
