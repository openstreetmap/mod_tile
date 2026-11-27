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

/* Ceph RADOS metatile storage backend:
 * Stores each serialized METATILE x METATILE archive as a single RADOS object
 * prefixed with a `struct stat_info` metadata header. Maintains a single-entry
 * per-context header cache so consecutive `tile_stat()` and `tile_read()` calls
 * for the same metatile reuse the already-fetched header without an extra
 * round-trip. Supports full read, write, stat, delete, and expire operations. */

#include <glib.h>
#include <stdio.h>

#include "config.h"
#include "g_logger.h"
#include "store.h"
#include "store_rados.h"

#ifdef HAVE_LIBRADOS
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <rados/librados.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "metatile.h"
#include "protocol.h"
#include "render_config.h"

/** @brief Mutex serializing `rados_connect()` calls across worker threads during startup. */
static pthread_mutex_t rados_mutex;

/**
 * @brief Single-entry cache holding the combined `stat_info` + `meta_layout` header
 *        of the most recently accessed metatile in a RADOS context.
 */
struct metadata_cache {
	/** @brief Heap buffer (`sizeof(stat_info) + sizeof(meta_layout) + METATILE^2 * sizeof(entry)` bytes). */
	char *data;
	/** @brief Metatile-aligned X, Y, and zoom coordinates of the cached header (`-1` when invalid). */
	int x, y, z;
	/** @brief Map style name (`xmlname`) corresponding to the cached header. */
	char xmlname[XMLCONFIG_MAX];
};

/**
 * @brief Per-backend Ceph RADOS cluster handle, pool I/O context, and metadata cache.
 */
struct rados_ctx {
	/** @brief Heap-allocated RADOS pool name parsed from the connection URI. */
	char *pool;
	/** @brief Initialized librados cluster handle. */
	rados_t cluster;
	/** @brief Open librados I/O context bound to `pool`. */
	rados_ioctx_t io;
	/** @brief Single-entry header cache avoiding redundant metadata reads on `stat` + `read`. */
	struct metadata_cache metadata_cache;
};

/**
 * @brief Format the RADOS object key for a metatile given `(xmlconfig, options, x, y, z)`.
 *
 * Masks `x` and `y` down to the top-left `(0, 0)` corner of their `METATILE x METATILE`
 * block and formats `key` as `"<xmlconfig>/<z>/<x>/<y>[.<options>].meta"`.
 *
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string (omitted from key if empty).
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @param key       Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted object key.
 * @return Pointer to `key`.
 */
static char *rados_xyzo_to_storagekey(const char *xmlconfig, const char *options, int x, int y, int z, char *key)
{
	/* Bitmask used to align x and y to the top-left corner of the metatile */
	int mask;

	mask = METATILE - 1;
	x &= ~mask;
	y &= ~mask;

	if (strlen(options)) {
		snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.%s.meta", xmlconfig, z, x, y, options);
	} else {
		snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, z, x, y);
	}

	return key;
}

/**
 * @brief Fetch (or return cached) `stat_info` + `meta_layout` header bytes for a metatile.
 *
 * Aligns `(x, y)` to the metatile origin and checks `ctx->metadata_cache`. On a cache
 * hit, returns `ctx->metadata_cache.data` immediately. On a cache miss, reads `header_len`
 * bytes from offset `0` of the RADOS object into `ctx->metadata_cache.data`, updates the
 * cache key, and returns the buffer (or invalidates the cache and returns `NULL` on error).
 *
 * @param store     Initialized RADOS `storage_backend` (`storage_ctx` holds `struct rados_ctx *`).
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Pointer to `ctx->metadata_cache.data` containing `stat_info` followed by `meta_layout`,
 *         or `NULL` if the RADOS object does not exist or cannot be read.
 */
static char *read_meta_data(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Bitmask used to align x and y to the top-left corner of the metatile */
	int mask;
	/* Byte count or negative errno returned by rados_read() */
	int err;
	/* RADOS object key for the enclosing metatile */
	char meta_path[PATH_MAX];
	/* Backend RADOS context holding cluster/pool handles and the single-entry header cache */
	struct rados_ctx *ctx = (struct rados_ctx *)store->storage_ctx;
	/* Combined byte length of struct stat_info, struct meta_layout, and the sub-tile index table */
	unsigned int header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);

	mask = METATILE - 1;
	x &= ~mask;
	y &= ~mask;

	if ((ctx->metadata_cache.x == x) && (ctx->metadata_cache.y == y) && (ctx->metadata_cache.z == z) && (strcmp(ctx->metadata_cache.xmlname, xmlconfig) == 0)) {
		g_logger(G_LOG_LEVEL_DEBUG, "Returning cached data for %s %i %i %i", ctx->metadata_cache.xmlname, ctx->metadata_cache.x, ctx->metadata_cache.y, ctx->metadata_cache.z);
		return ctx->metadata_cache.data;
	} else {
		g_logger(G_LOG_LEVEL_DEBUG, "Retrieving fresh metadata");
		rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
		err = rados_read(ctx->io, meta_path, ctx->metadata_cache.data, header_len, 0);

		if (err < 0) {
			if (-err == ENOENT) {
				g_logger(G_LOG_LEVEL_DEBUG, "cannot read data from rados pool %s: %s", ctx->pool, strerror(-err));
			} else {
				g_logger(G_LOG_LEVEL_ERROR, "cannot read data from rados pool %s: %s", ctx->pool, strerror(-err));
			}

			/* Invalidate cache coordinates on failed read */
			ctx->metadata_cache.x = -1;
			ctx->metadata_cache.y = -1;
			ctx->metadata_cache.z = -1;
			return NULL;
		}

		ctx->metadata_cache.x = x;
		ctx->metadata_cache.y = y;
		ctx->metadata_cache.z = z;
		strncpy(ctx->metadata_cache.xmlname, xmlconfig, XMLCONFIG_MAX - 1);
		return ctx->metadata_cache.data;
	}
}

/**
 * @brief Read a single sub-tile payload from a metatile object stored in Ceph RADOS.
 *
 * Retrieves the metatile header via read_meta_data(), validates the magic signature and
 * sub-tile count, and performs a ranged `rados_read()` at `file_offset` (adjusted for the
 * leading `struct stat_info`) for `tile_size` bytes into `buf`.
 *
 * @param store      Initialized RADOS `storage_backend`.
 * @param xmlconfig  Logical map style name.
 * @param options    Style parameterization options string.
 * @param x          Global tile X coordinate.
 * @param y          Global tile Y coordinate.
 * @param z          Tile zoom level.
 * @param buf        Caller-supplied output buffer receiving the sub-tile image bytes.
 * @param sz         Maximum capacity of `buf` in bytes.
 * @param compressed Output flag set to `1` if `META_MAGIC_COMPRESSED` is present, `0` otherwise.
 * @param log_msg    Caller-supplied buffer receiving an error message on failure.
 * @return Number of sub-tile bytes read into `buf` (`>= 0`) on success, or a negative error code:
 *         - `-1`: Header allocation failure or `rados_read()` error reading sub-tile payload.
 *         - `-3`: Metatile object missing or header metadata read failed.
 *         - `-4`: Invalid metatile magic bytes.
 *         - `-5`: Unexpected tile count (`m->count != METATILE * METATILE`).
 *         - `-6`: Stored sub-tile size exceeds output buffer capacity `sz`.
 */
static int rados_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
	/* RADOS object key for the enclosing metatile */
	char meta_path[PATH_MAX];
	/* Linear sub-tile index within the metatile's entry table (0 .. METATILE^2 - 1) */
	int meta_offset;
	/* Byte size of struct meta_layout plus the sub-tile offset/size index table */
	unsigned int header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	/* Heap-allocated aligned copy of the metatile header and index table */
	struct meta_layout *m = (struct meta_layout *)malloc(header_len);
	/* Byte offset within the RADOS object (including leading stat_info) and sub-tile byte length */
	size_t file_offset, tile_size;
	/* Bitmask extracting sub-tile offsets modulo METATILE */
	int mask;
	/* Byte count or negative errno returned by rados_read() */
	int err;
	/* Pointer to cached stat_info + meta_layout header returned by read_meta_data() */
	char *buf_raw;

	if (m == NULL) {
		snprintf(log_msg, 1024, "Failed to allocate memory for metatile header\n");
		return -1;
	}

	mask = METATILE - 1;
	meta_offset = (x & mask) * METATILE + (y & mask);

	rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);

	buf_raw = read_meta_data(store, xmlconfig, options, x, y, z);

	if (buf_raw == NULL) {
		snprintf(log_msg, 1024, "Failed to read metadata of tile\n");
		free(m);
		return -3;
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

	/* Offset within the RADOS object includes the leading struct stat_info header */
	file_offset = m->index[meta_offset].offset + sizeof(struct stat_info);
	tile_size = m->index[meta_offset].size;

	free(m);

	if (tile_size > sz) {
		snprintf(log_msg, 1024, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
		tile_size = sz;
		return -6;
	}

	err = rados_read(((struct rados_ctx *)store->storage_ctx)->io, meta_path, buf, tile_size, file_offset);

	if (err < 0) {
		snprintf(log_msg, 1024, "Failed to read tile data from rados %s offset: %li length: %li: %s\n", meta_path, file_offset, tile_size, strerror(-err));
		return -1;
	}

	return tile_size;
}

/**
 * @brief Retrieve stored metadata, timestamps, and expiry status for a tile in Ceph RADOS.
 *
 * Reads (or hits cache for) the metatile's leading `struct stat_info` and index table via
 * read_meta_data(), setting `tile_stat.size` to the individual sub-tile's byte size (or `-1`
 * if the RADOS object is missing).
 *
 * @param store     Initialized RADOS `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Populated `struct stat_info` (`size < 0` indicates missing object or read error).
 */
static struct stat_info rados_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Normalized backend-agnostic stat result returned to the caller */
	struct stat_info tile_stat;
	/* Pointer to cached stat_info + meta_layout header returned by read_meta_data() */
	char *buf;
	/* Sub-tile index within the metatile and METATILE - 1 bitmask */
	int offset, mask;

	mask = METATILE - 1;
	offset = (x & mask) * METATILE + (y & mask);

	buf = read_meta_data(store, xmlconfig, options, x, y, z);

	if (buf == NULL) {
		tile_stat.size = -1;
		tile_stat.expired = 0;
		tile_stat.mtime = 0;
		tile_stat.atime = 0;
		tile_stat.ctime = 0;
		return tile_stat;
	}

	memcpy(&tile_stat, buf, sizeof(struct stat_info));
	tile_stat.size = ((struct meta_layout *)(buf + sizeof(struct stat_info)))->index[offset].size;

	return tile_stat;
}

/**
 * @brief Format a human-readable `rados://<pool>/<key>` storage URI identifying `(x, y, z)`.
 *
 * @param store     Initialized RADOS `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @param string    Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted URI.
 * @return Pointer to `string`.
 */
static char *rados_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
	/* RADOS object key for the enclosing metatile */
	char meta_path[PATH_MAX];

	rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	snprintf(string, PATH_MAX - 1, "rados://%s/%s", ((struct rados_ctx *)(store->storage_ctx))->pool, meta_path);
	return string;
}

/**
 * @brief Atomically write a full serialized metatile object to Ceph RADOS prefixed with `stat_info`.
 *
 * Prepends a freshly initialized `struct stat_info` (`expired = 0`, current timestamps) ahead
 * of `buf` and overwrites the entire RADOS object in a single `rados_write_full()` operation.
 *
 * @param store     Initialized RADOS `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Top-left tile X coordinate of the metatile.
 * @param y         Top-left tile Y coordinate of the metatile.
 * @param z         Tile zoom level.
 * @param buf       Serialized metatile buffer (`meta_layout` + index table + tile payloads).
 * @param sz        Total byte length of `buf`.
 * @return `sz` on success, or `-1` on allocation, invalid I/O context, or `rados_write_full()` failure.
 */
static int rados_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
	/* RADOS object key for the target metatile */
	char meta_path[PATH_MAX];
	/* Scratch buffer for formatting storage URI in log messages */
	char tmp[PATH_MAX];
	/* Metadata header prepended to the serialized metatile object */
	struct stat_info tile_stat;
	/* Combined byte length of struct stat_info + serialized metatile payload */
	int sz2 = sz + sizeof(struct stat_info);
	/* Heap buffer packing tile_stat followed by buf */
	char *buf2 = (char *)malloc(sz2);
	/* Negative errno status returned by rados_write_full() (0 on success) */
	int err;

	if (buf2 == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "rados_metatile_write: failed to allocate memory for write buffer");
		return -1;
	}

	tile_stat.expired = 0;
	tile_stat.size = sz;
	tile_stat.mtime = time(NULL);
	tile_stat.atime = tile_stat.mtime;
	tile_stat.ctime = tile_stat.mtime;

	memcpy(buf2, &tile_stat, sizeof(tile_stat));
	memcpy(buf2 + sizeof(tile_stat), buf, sz);

	rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	g_logger(G_LOG_LEVEL_DEBUG, "Trying to create and write a tile to %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));

	/* Backend RADOS context holding the open pool I/O handle */
	struct rados_ctx *ctx = (struct rados_ctx *)store->storage_ctx;

	if (!ctx || !ctx->io) {
		g_logger(G_LOG_LEVEL_ERROR, "rados_metatile_write: cannot write %s: invalid rados io context", tmp);
		free(buf2);
		return -1;
	}

	err = rados_write_full(ctx->io, meta_path, buf2, sz2);

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "cannot write %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
		free(buf2);
		return -1;
	}

	free(buf2);

	return sz;
}

/**
 * @brief Remove the metatile object containing `(x, y, z)` from the Ceph RADOS pool.
 *
 * @param store     Initialized RADOS `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `0` on success, or `-1` on invalid I/O context or `rados_remove()` error.
 */
static int rados_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Backend RADOS context holding the open pool I/O handle */
	struct rados_ctx *ctx = (struct rados_ctx *)store->storage_ctx;
	/* RADOS object key for the target metatile */
	char meta_path[PATH_MAX];
	/* Scratch buffer for formatting storage URI in log messages */
	char tmp[PATH_MAX];
	/* Negative errno status returned by rados_remove() (0 on success) */
	int err;

	/* TODO: deal with options */
	const char *options = "";
	rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	g_logger(G_LOG_LEVEL_DEBUG, "Trying to delete a tile at %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));

	if (!ctx || !ctx->io) {
		g_logger(G_LOG_LEVEL_ERROR, "rados_metatile_delete: cannot delete %s: invalid rados io context", tmp);
		return -1;
	}

	err = rados_remove(ctx->io, meta_path);

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "failed to delete %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
		return -1;
	}

	return 0;
}

/**
 * @brief Mark a RADOS metatile object as expired by updating its leading `struct stat_info`.
 *
 * Reads the first `sizeof(struct stat_info)` bytes of the RADOS object, sets `expired = 1`,
 * and writes those bytes back in place at offset `0` via `rados_write()` without rewriting
 * the metatile image payloads.
 *
 * @param store     Initialized RADOS `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `0` on success, `-1` if invalid I/O context or object does not exist (`ENOENT`),
 *         `-2` if reading `stat_info` failed, or `-3` if writing updated `stat_info` failed.
 */
static int rados_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Leading stat_info header read from offset 0 of the RADOS object and updated in place */
	struct stat_info tile_stat;
	/* Backend RADOS context holding the open pool I/O handle */
	struct rados_ctx *ctx = (struct rados_ctx *)store->storage_ctx;
	/* RADOS object key for the target metatile */
	char meta_path[PATH_MAX];
	/* Scratch buffer for formatting storage URI in log messages */
	char tmp[PATH_MAX];
	/* Byte count or negative errno status returned by rados_read() / rados_write() */
	int err;

	/* TODO: deal with options */
	const char *options = "";
	rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
	g_logger(G_LOG_LEVEL_DEBUG, "Trying to expire a tile at %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));

	if (!ctx || !ctx->io) {
		g_logger(G_LOG_LEVEL_ERROR, "rados_metatile_expire: cannot expire %s: invalid rados io context", tmp);
		return -1;
	}

	err = rados_read(ctx->io, meta_path, (char *)&tile_stat, sizeof(struct stat_info), 0);

	if (err < 0) {
		if (-err == ENOENT) {
			g_logger(G_LOG_LEVEL_DEBUG, "Tile %s does not exist, can't expire", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));
			return -1;
		} else {
			g_logger(G_LOG_LEVEL_ERROR, "Failed to read tile metadata for %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
		}

		return -2;
	}

	tile_stat.expired = 1;

	/* Overwrite only the leading struct stat_info at byte offset 0 */
	err = rados_write(ctx->io, meta_path, (char *)&tile_stat, sizeof(struct stat_info), 0);

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "failed to write expiry data for %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
		return -3;
	}

	return 0;
}

/**
 * @brief Tear down the Ceph RADOS I/O context, cluster connection, and metadata cache.
 *
 * @param store Storage backend instance to close.
 * @return `0` on success, or `-1` if `store` or `store->storage_ctx` is `NULL`.
 */
static int rados_close_storage(struct storage_backend *store)
{
	if (!store || !store->storage_ctx) {
		g_logger(G_LOG_LEVEL_ERROR, "rados_close_storage: cannot close storage: invalid storage context");
		return -1;
	}

	/* Backend RADOS context to destroy and free */
	struct rados_ctx *ctx = (struct rados_ctx *)store->storage_ctx;

	if (ctx->io) {
		rados_ioctx_destroy(ctx->io);
	}

	if (ctx->cluster) {
		rados_shutdown(ctx->cluster);
	}

	g_logger(G_LOG_LEVEL_DEBUG, "rados_close_storage: Closed rados backend");
	free(ctx->metadata_cache.data);
	free(ctx->pool);
	free(ctx);
	store->storage_ctx = NULL;
	return 0;
}

#endif /* HAVE_LIBRADOS */

/**
 * @brief Allocate and initialize a Ceph RADOS metatile `storage_backend`.
 *
 * Parses `connection_string` of the form `"rados://<pool>/<path/to/ceph.conf>"`, creates a
 * `rados_t` cluster handle, loads the Ceph configuration file, connects to the cluster
 * (serialized via `rados_mutex`), opens an I/O context to `<pool>`, allocates the
 * single-entry `metadata_cache` buffer, and wires all backend operation function pointers.
 * Returns `NULL` if compiled without `HAVE_LIBRADOS` or if any initialization step fails.
 *
 * @param connection_string Backend URI of the form `"rados://<pool>/<ceph_conf_path>"`.
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on failure or if unsupported.
 */
struct storage_backend *init_storage_rados(const char *connection_string)
{
#ifndef HAVE_LIBRADOS
	g_logger(G_LOG_LEVEL_ERROR, "init_storage_rados: Support for rados storage has not been compiled into this program");
	return NULL;
#else  /* HAVE_LIBRADOS */
	/* Per-backend RADOS context holding cluster, pool I/O, and header cache */
	struct rados_ctx *ctx = (struct rados_ctx *)malloc(sizeof(struct rados_ctx));
	/* Newly allocated storage_backend vtable and context */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof(struct storage_backend));
	/* Heap-allocated path to the Ceph configuration file (e.g., "/etc/ceph/ceph.conf") */
	char *conf = NULL;
	/* Substring pointer positioned immediately after "rados://" */
	const char *tmp;
	/* Negative errno status returned by librados initialization calls */
	int err;
	/* Character index scanning for the '/' separating <pool> from <conf_path> */
	int i;

	if (ctx == NULL || store == NULL) {
		free(ctx);
		free(store);
		return NULL;
	}

	tmp = &(connection_string[strlen("rados://")]);
	i = 0;

	while ((tmp[i] != '/') && (tmp[i] != 0)) {
		i++;
	}

	ctx->pool = (char *)calloc(i + 1, sizeof(char));
	memcpy(ctx->pool, tmp, i * sizeof(char));
	conf = strdup(&(tmp[i]));

	err = rados_create(&(ctx->cluster), getenv("CEPH_ID"));

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_rados: cannot create a cluster handle: %s", strerror(-err));
		free(ctx);
		free(store);
		return NULL;
	}

	err = rados_conf_read_file(ctx->cluster, conf);

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_rados: failed to read rados config file %s: %s", conf, strerror(-err));
		free(ctx);
		free(store);
		return NULL;
	}

	pthread_mutex_lock(&rados_mutex);
	err = rados_connect(ctx->cluster);
	pthread_mutex_unlock(&rados_mutex);

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_rados: failed to connect to rados cluster: %s", strerror(-err));
		free(ctx);
		free(store);
		return NULL;
	}

	err = rados_ioctx_create(ctx->cluster, ctx->pool, &(ctx->io));

	if (err < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_rados: failed to initialise rados io context to pool %s: %s", ctx->pool, strerror(-err));
		rados_shutdown(ctx->cluster);
		free(ctx);
		free(store);
		return NULL;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_rados: Initialised rados backend for pool %s with config %s", ctx->pool, conf);

	/* Allocate single-entry header cache buffer (stat_info + meta_layout + METATILE^2 entries) */
	ctx->metadata_cache.data = (char *)malloc(sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry));

	if (ctx->metadata_cache.data == NULL) {
		rados_ioctx_destroy(ctx->io);
		rados_shutdown(ctx->cluster);
		free(ctx);
		free(store);
		return NULL;
	}

	free(conf);

	/* Mark metadata cache coordinates invalid until first read_meta_data() hit */
	ctx->metadata_cache.x = -1;
	ctx->metadata_cache.y = -1;
	ctx->metadata_cache.z = -1;
	ctx->metadata_cache.xmlname[0] = 0;

	store->storage_ctx = ctx;

	store->tile_read = &rados_tile_read;
	store->tile_stat = &rados_tile_stat;
	store->metatile_write = &rados_metatile_write;
	store->metatile_delete = &rados_metatile_delete;
	store->metatile_expire = &rados_metatile_expire;
	store->tile_storage_id = &rados_tile_storage_id;
	store->close_storage = &rados_close_storage;

	return store;
#endif /* HAVE_LIBRADOS */
}
