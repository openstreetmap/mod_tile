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

/* Local filesystem metatile storage backend:
 * Bundles a METATILE x METATILE (typically 8x8) grid of rendered tiles into a
 * single .meta file on disk to reduce inode consumption and improve disk
 * utilization. Supports full read, write, stat, delete, and mtime-based expire
 * operations. */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "g_logger.h"
#include "metatile.h"
#include "render_config.h"
#include "store.h"
#include "store_file.h"
#include "store_file_utils.h"

/**
 * @brief Read a single sub-tile payload from an on-disk `.meta` archive file.
 *
 * Resolves `(xmlconfig, options, x, y, z)` to a `.meta` path and sub-tile slot index,
 * reads and validates the `meta_layout` header and magic bytes (`META_MAGIC` or
 * `META_MAGIC_COMPRESSED`), seeks to the sub-tile's offset, and reads its payload into `buf`.
 *
 * @param store      Initialized file `storage_backend` (`storage_ctx` holds base tile directory).
 * @param xmlconfig  Logical map style name (e.g., `"default"`).
 * @param options    Style parameterization options string (empty if unused).
 * @param x          Tile X coordinate.
 * @param y          Tile Y coordinate.
 * @param z          Tile zoom level.
 * @param buf        Caller-supplied output buffer receiving the tile image bytes.
 * @param sz         Maximum capacity of `buf` in bytes.
 * @param compressed Output flag set to `1` if the metatile uses `META_MAGIC_COMPRESSED`, `0` otherwise.
 * @param log_msg    Caller-supplied buffer (`PATH_MAX` bytes) populated with an error description on failure.
 * @return Number of tile bytes read into `buf` (`>= 0`) on success, or a negative error code:
 *         - `-1`: Header allocation failure or `.meta` file could not be opened.
 *         - `-2`: `read()` error while reading the metatile header.
 *         - `-3`: File truncated / smaller than `header_len`.
 *         - `-4`: Invalid header magic bytes.
 *         - `-5`: Unexpected tile count (`m->count != METATILE * METATILE`).
 *         - `-6`: Stored tile size exceeds output buffer capacity `sz`.
 *         - `-7`: `lseek()` failure seeking to sub-tile payload offset.
 *         - `-8`: `read()` error while reading sub-tile payload data.
 */
static int file_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
	/* Resolved filesystem path to the target .meta file */
	char path[PATH_MAX];
	/* Sub-tile index within the metatile's entry table (0 .. METATILE^2 - 1) */
	int meta_offset;
	/* Open file descriptor for the .meta file */
	int fd = FD_INVALID;
	/* Running byte offset for short-read loops */
	unsigned int pos;
	/* Combined byte size of the fixed header and sub-tile offset/size index table */
	unsigned int header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	/* Heap-allocated buffer holding the metatile header and index table */
	struct meta_layout *m = (struct meta_layout *)malloc(header_len);
	/* Byte offset and byte length of the requested sub-tile inside the .meta file */
	size_t file_offset, tile_size;

	if (m == NULL) {
		snprintf(log_msg, PATH_MAX - 1, "Failed to allocate memory for metatile header\n");
		return -1;
	}

	meta_offset = xyzo_to_meta(path, sizeof(path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);

	fd = open(path, O_RDONLY);

	if (fd < 0) {
		snprintf(log_msg, PATH_MAX - 1, "Could not open metatile %s. Reason: %s\n", path, strerror(errno));
		free(m);
		return -1;
	}

	pos = 0;

	/* Read the complete metatile header and index table, handling partial reads */
	while (pos < header_len) {
		size_t len = header_len - pos;
		int got = read(fd, ((unsigned char *)m) + pos, len);

		if (got < 0) {
			snprintf(log_msg, PATH_MAX - 1, "Failed to read complete header for metatile %s Reason: %s\n", path, strerror(errno));
			close(fd);
			free(m);
			return -2;
		} else if (got > 0) {
			pos += got;
		} else {
			break;
		}
	}

	if (pos < header_len) {
		snprintf(log_msg, PATH_MAX - 1, "Meta file %s too small to contain header\n", path);
		close(fd);
		free(m);
		return -3;
	}

	/* Verify magic signature and determine whether payload entries are compressed */
	if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
		if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
			snprintf(log_msg, PATH_MAX - 1, "Meta file %s header magic mismatch\n", path);
			close(fd);
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
		snprintf(log_msg, PATH_MAX - 1, "Meta file %s header bad count %d != %d\n", path, m->count, METATILE * METATILE);
		free(m);
		close(fd);
		return -5;
	}

	file_offset = m->index[meta_offset].offset;
	tile_size = m->index[meta_offset].size;

	free(m);

	if (tile_size > sz) {
		snprintf(log_msg, PATH_MAX - 1, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
		tile_size = sz;
		close(fd);
		return -6;
	}

	if (lseek(fd, file_offset, SEEK_SET) < 0) {
		snprintf(log_msg, PATH_MAX - 1, "Meta file %s seek error: %s\n", path, strerror(errno));
		close(fd);
		return -7;
	}

	pos = 0;

	/* Read the sub-tile payload bytes into the caller's buffer */
	while (pos < tile_size) {
		size_t len = tile_size - pos;
		int got = read(fd, buf + pos, len);

		if (got < 0) {
			snprintf(log_msg, PATH_MAX - 1, "Failed to read data from file %s. Reason: %s\n", path, strerror(errno));
			close(fd);
			return -8;
		} else if (got > 0) {
			pos += got;
		} else {
			break;
		}
	}

	close(fd);
	return pos;
}

/**
 * @brief Retrieve filesystem metadata and expiry status for the metatile containing `(x, y, z)`.
 *
 * Stats the corresponding `.meta` file on disk (setting `size = -1` if absent) and marks
 * `expired = 1` if the file's modification time is older than the planet import timestamp
 * returned by get_planet_time().
 *
 * @param store     Initialized file `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Populated `struct stat_info` (`size < 0` indicates the metatile does not exist).
 */
static struct stat_info file_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Normalized backend-agnostic stat result returned to the caller */
	struct stat_info tile_stat;
	/* POSIX stat structure populated from the underlying .meta file */
	struct stat st_stat;
	/* Resolved filesystem path to the target .meta file */
	char meta_path[PATH_MAX];

	xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);

	if (stat(meta_path, &st_stat)) {
		tile_stat.size = -1;
		tile_stat.mtime = 0;
		tile_stat.atime = 0;
		tile_stat.ctime = 0;
	} else {
		tile_stat.size = st_stat.st_size;
		tile_stat.mtime = st_stat.st_mtime;
		tile_stat.atime = st_stat.st_atime;
		tile_stat.ctime = st_stat.st_ctime;
	}

	if (tile_stat.mtime < get_planet_time((const char *)(store->storage_ctx), xmlconfig)) {
		tile_stat.expired = 1;
	} else {
		tile_stat.expired = 0;
	}

	return tile_stat;
}

/**
 * @brief Format a human-readable `file://` storage URI identifying the metatile for `(x, y, z)`.
 *
 * @param store     Initialized file `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @param string    Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted URI.
 * @return Pointer to `string`.
 */
static char *file_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
	/* Resolved filesystem path to the target .meta file */
	char meta_path[PATH_MAX];

	xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);
	snprintf(string, PATH_MAX - 1, "file://%s", meta_path);
	return string;
}

/**
 * @brief Atomically write a serialized metatile buffer to disk via a thread-specific temporary file.
 *
 * Creates any missing parent directories via mkdirp(), writes `buf` to
 * `<meta_path>.<pthread_self()>`, closes the file, and atomically replaces `meta_path`
 * using `rename()` so concurrent readers never observe partially written `.meta` files.
 *
 * @param store     Initialized file `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Top-left tile X coordinate of the metatile.
 * @param y         Top-left tile Y coordinate of the metatile.
 * @param z         Tile zoom level.
 * @param buf       Serialized metatile buffer (`meta_layout` + index table + tile payloads).
 * @param sz        Total byte length of `buf`.
 * @return `sz` on success, or `-1` on allocation, directory, I/O, or rename failure.
 */
static int file_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
	/* File descriptor for the temporary output file */
	int fd = FD_INVALID;
	/* Final destination filesystem path of the .meta file */
	char meta_path[PATH_MAX];
	/* Thread-unique temporary path "<meta_path>.<tid>" used for atomic rename */
	char *tmp;
	/* Byte count returned by write() */
	int res;

	xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);
	g_logger(G_LOG_LEVEL_DEBUG, "Creating and writing a metatile to %s", meta_path);

	tmp = (char *)malloc(sizeof(char) * strlen(meta_path) + 24);

	if (tmp == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "file_metatile_write: failed to allocate memory for temp path");
		return -1;
	}

	snprintf(tmp, strlen(meta_path) + 24, "%s.%lu", meta_path, (unsigned long)pthread_self());

	if (mkdirp(tmp)) {
		g_logger(G_LOG_LEVEL_WARNING, "Error creating directory %s: %s", tmp, strerror(errno));
		free(tmp);
		return -1;
	}

	fd = open(tmp, O_WRONLY | O_TRUNC | O_CREAT, 0666);

	if (fd < 0) {
		g_logger(G_LOG_LEVEL_WARNING, "Error creating file %s: %s", meta_path, strerror(errno));
		free(tmp);
		return -1;
	}

	res = write(fd, buf, sz);

	if (res != sz) {
		g_logger(G_LOG_LEVEL_WARNING, "Error writing file %s: %s", meta_path, strerror(errno));
		close(fd);
		free(tmp);
		return -1;
	}

	close(fd);

	if (rename(tmp, meta_path) != 0) {
		g_logger(G_LOG_LEVEL_WARNING, "Error renaming temp file %s to %s: %s", tmp, meta_path, strerror(errno));
		unlink(tmp);
		free(tmp);
		return -1;
	}

	free(tmp);

	return sz;
}

/**
 * @brief Delete the on-disk `.meta` file containing `(x, y, z)`.
 *
 * @param store     Initialized file `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `0` on success, or `-1` on `unlink()` failure (with `errno` set).
 */
static int file_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Resolved filesystem path to the target .meta file */
	char meta_path[PATH_MAX];

	/* TODO: deal with options */
	xyz_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, x, y, z);
	g_logger(G_LOG_LEVEL_DEBUG, "Deleting metatile from %s", meta_path);
	return unlink(meta_path);
}

/**
 * @brief Mark an on-disk `.meta` file as expired by backdating its modification timestamp (`mtime`).
 *
 * If the `.meta` file exists and its `mtime` is after the year 2015 (`tm_year > 115`),
 * subtracts 20 years from `mtime` (so it predates any planet import timestamp while
 * preserving relative age information) and leaves `atime` untouched so LRU cache-purging
 * tools still see accurate last-access times. Falls back to epoch `315558000` (1980-01-01)
 * if `gmtime_r()` fails.
 *
 * @param store     Initialized file `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Always returns `0`.
 */
static int file_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	/* Resolved filesystem path to the target .meta file */
	char name[PATH_MAX];
	/* Existing file metadata used to read current mtime and preserve atime */
	struct stat s;
	/* Broken-down UTC timestamp used to subtract 20 years from st_mtime */
	static struct tm touchCalendar;
	/* Updated access and modification timestamps passed to utime() */
	struct utimbuf touchTime;

	/* TODO: deal with options */
	xyz_to_meta(name, sizeof(name), (char *)(store->storage_ctx), xmlconfig, x, y, z);

	if (stat(name, &s) == 0) { /* 0 indicates file exists on disk; mark it as expired */
		if (!gmtime_r(&(s.st_mtime), &touchCalendar)) {
			/* Fallback to 1980-01-01T00:00:00Z if mtime cannot be broken down */
			touchTime.modtime = 315558000;
		} else {
			if (touchCalendar.tm_year > 115) {   /* Guard against repeatedly backdating tiles already expired (tm_year 115 = 2015) */
				touchCalendar.tm_year -= 20; /* Set back by 20 years while keeping relative creation timestamp */
				touchTime.modtime = mktime(&touchCalendar);
			} else {
				touchTime.modtime = s.st_mtime;
			}
		}

		touchTime.actime = s.st_atime; /* Preserve atime, as it is used for LRU tile cache purging */

		if (-1 == utime(name, &touchTime)) {
			perror("modifying timestamp failed");
		}
	}

	return 0;
}

/**
 * @brief Release resources associated with a filesystem `storage_backend` instance.
 *
 * Frees the duplicated `tile_dir` string stored in `store->storage_ctx`.
 *
 * @param store Storage backend instance to close.
 * @return Always returns `0`.
 */
static int file_close_storage(struct storage_backend *store)
{
	free(store->storage_ctx);
	store->storage_ctx = NULL;
	return 0;
}

/**
 * @brief Allocate and initialize a local filesystem metatile `storage_backend`.
 *
 * Duplicates `tile_dir` into `store->storage_ctx` and wires all backend operation
 * function pointers (`tile_read`, `tile_stat`, `metatile_write`, `metatile_delete`,
 * `metatile_expire`, `tile_storage_id`, and `close_storage`).
 *
 * @param tile_dir Base directory path under which `<xmlname>/<z>/.../<x>/<y>.meta` files are stored.
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on allocation failure.
 */
struct storage_backend *init_storage_file(const char *tile_dir)
{
	/* Newly allocated storage_backend vtable and context */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof(struct storage_backend));

	if (store == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_file: Failed to allocate memory for storage backend");
		return NULL;
	}

	store->storage_ctx = strdup(tile_dir);

	store->tile_read = &file_tile_read;
	store->tile_stat = &file_tile_stat;
	store->metatile_write = &file_metatile_write;
	store->metatile_delete = &file_metatile_delete;
	store->metatile_expire = &file_metatile_expire;
	store->tile_storage_id = &file_tile_storage_id;
	store->close_storage = &file_close_storage;

	return store;
}
