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

#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "g_logger.h"
#include "render_config.h"
#include "store_file_utils.h"

/**
 * @brief Retrieve the planet database import timestamp used to determine tile expiry.
 *
 * Checks for a `PLANET_TIMESTAMP` marker file in the following order:
 *   1. Map-specific path: `<tile_dir>/<xmlname>/<PLANET_TIMESTAMP>` (when `xmlname` is non-empty).
 *   2. Global fallback path: `<tile_dir>/<PLANET_TIMESTAMP>`.
 *   3. Default fallback: `now - 3 days` (`259200` seconds ago) if neither file exists.
 *
 * @param tile_dir Base tile storage directory path.
 * @param xmlname  Logical map style name (may be `NULL` or empty to skip the map-specific check).
 * @return Modification timestamp (`st_mtime`) of the timestamp file, or `time(NULL) - 3 days`
 *         if no timestamp file can be stated.
 */
time_t get_planet_time(const char *tile_dir, const char *xmlname)
{
	/* File status structure used to inspect st_mtime of PLANET_TIMESTAMP */
	struct stat st_stat;
	/* Candidate filesystem path to the PLANET_TIMESTAMP marker file */
	char filename[PATH_MAX];

	/* Check map-specific timestamp file first */
	if (xmlname && xmlname[0] != '\0') {
		snprintf(filename, sizeof(filename), "%s/%s/%s", tile_dir, xmlname, PLANET_TIMESTAMP);

		if (stat(filename, &st_stat) == 0) {
			return st_stat.st_mtime;
		}
	}

	/* Fall back to global timestamp file at the root of tile_dir */
	snprintf(filename, sizeof(filename), "%s/%s", tile_dir, PLANET_TIMESTAMP);

	if (stat(filename, &st_stat) == 0) {
		return st_stat.st_mtime;
	}

	/* Default to 3 days ago if no timestamp file is present */
	return time(NULL) - (3 * 24 * 60 * 60);
}

/**
 * @brief Recursively create all parent directories required for a target file path.
 *
 * Strips the basename after the final `'/'` (so `mkdirp("/a/b/foo.meta")` behaves like
 * `mkdir -p /a/b`) and creates any missing intermediate directories with mode `0777`
 * (modified by the process `umask`). Tolerates `EEXIST` races from concurrent threads.
 *
 * @param path Full filesystem path to a target file whose parent directories should exist.
 * @return `0` if the parent directory exists or was created successfully, or `1` if a
 *         path component exists as a non-directory or `mkdir()` fails.
 */
int mkdirp(const char *path)
{
	/* File status structure used to verify existing path components are directories */
	struct stat s;
	/* Mutable scratch copy of `path` modified in place at '/' boundaries */
	char tmp[PATH_MAX];
	/* Cursor pointer traversing '/' separators within `tmp` */
	char *p;

	strncpy(tmp, path, sizeof(tmp) - 1);

	/* Locate the final '/' separating parent directory from filename */
	p = strrchr(tmp, '/');

	if (!p) {
		return 0;
	}

	*p = '\0';

	/* Fast path: parent directory already exists */
	if (!stat(tmp, &s)) {
		return !S_ISDIR(s.st_mode);
	}

	*p = '/';
	/* Walk down the path creating each missing directory component */
	p = tmp;

	if (!*p) {
		/* Unreachable defensive guard */
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	p++; /* Skip leading '/' */

	while (*p) {
		if (*p == '/') {
			*p = '\0';

			if (!stat(tmp, &s)) {
				if (!S_ISDIR(s.st_mode)) {
					g_logger(G_LOG_LEVEL_ERROR, "Error, not a directory: %s", tmp);
					return 1;
				}
			} else if (mkdir(tmp, 0777)) {
				/* Ignore EEXIST when multiple worker threads create the same directory concurrently */
				if (errno != EEXIST) {
					perror(tmp);
					return 1;
				}
			}

			*p = '/';
		}

		p++;
	}

	return 0;
}

/* File path hashing: used by both mod_tile and renderd, which must agree on
 * the directory layout for metatile lookup and persistence to work. */

/**
 * @brief Check whether tile coordinates `(x, y, z)` are out of valid bounds.
 *
 * Note: Unlike `gen_tile.cpp`'s `check_xyz()`, this helper returns `1` (non-zero) when
 * coordinates are **invalid / out of bounds** and `0` when valid, matching standard C
 * error-code convention for `path_to_xyz()`.
 *
 * @param x Tile X coordinate.
 * @param y Tile Y coordinate.
 * @param z Tile zoom level.
 * @return `1` if `(x, y, z)` is out of bounds (`z < 0`, `z > MAX_ZOOM`, or `x`/`y` outside
 *         `[0, 2^z - 1]`), or `0` if valid.
 */
static int check_xyz(int x, int y, int z)
{
	/* Out-of-bounds flag (1 = invalid, 0 = valid) */
	int oob;
	/* Maximum valid tile index (2^z - 1) at zoom level z */
	int limit;

	/* Validate zoom level range */
	oob = (z < 0 || z > MAX_ZOOM);

	if (!oob) {
		/* Valid x/y indices for tiles are 0 ... 2^zoom - 1 */
		limit = (1 << z) - 1;
		oob = (x < 0 || x > limit || y < 0 || y > limit);
	}

	if (oob) {
		g_logger(G_LOG_LEVEL_ERROR, "got bad co-ords: x(%d) y(%d) z(%d)", x, y, z);
	}

	return oob;
}

/**
 * @brief Parse a metatile or tile filesystem path back into `(xmlconfig, px, py, pz)`.
 *
 * Strips the base directory prefix (`tilepath`, or `TILE_PATH` when compiled without
 * `DIRECTORY_HASH` and `TILE_PATH` is non-empty), then parses either:
 *   - `DIRECTORY_HASH` layout: `/<xmlconfig>/<z>/<h0>/<h1>/<h2>/<h3>/<h4>...`, unpacking
 *     each 8-bit hash octet into 4 high bits of `x` and 4 low bits of `y`.
 *   - Flat layout: `/<xmlconfig>/<z>/<x>/<y>...`
 *
 * @param tilepath  Expected base tile directory prefix.
 * @param path      Full filesystem path to parse.
 * @param xmlconfig Output buffer (at least 41 bytes) receiving the parsed map style name.
 * @param px        Output pointer receiving the parsed tile X coordinate.
 * @param py        Output pointer receiving the parsed tile Y coordinate.
 * @param pz        Output pointer receiving the parsed tile zoom level.
 * @return `0` on valid parse and in-bounds coordinates, `1` on prefix/format mismatch or
 *         out-of-bounds coordinates, or `2` if a `DIRECTORY_HASH` component is outside `0..255`.
 */
int path_to_xyz(const char *tilepath, const char *path, char *xmlconfig, int *px, int *py, int *pz)
{
#ifdef DIRECTORY_HASH
	/* Loop counter, sscanf() match count, 5 nibble-interleaved path octets, and unpacked coordinates */
	int i, n, hash[5], x, y, z;

	for (i = 0; tilepath[i] && tilepath[i] == path[i]; ++i)
		;

	if (tilepath[i]) {
		g_logger(G_LOG_LEVEL_ERROR, "Tile path does not match settings (%s): %s", tilepath, path);
		return 1;
	}

	n = sscanf(path + i, "/%40[^/]/%d/%d/%d/%d/%d/%d", xmlconfig, pz, &hash[0], &hash[1], &hash[2], &hash[3], &hash[4]);

	if (n != 7) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to parse tile path: %s", path);
		return 1;
	} else {
		x = y = 0;

		/* Reconstruct 20-bit x and y coordinates from the 5 nibble-interleaved hash octets */
		for (i = 0; i < 5; i++) {
			if (hash[i] < 0 || hash[i] > 255) {
				g_logger(G_LOG_LEVEL_ERROR, "Failed to parse tile path (invalid %d): %s", hash[i], path);
				return 2;
			}

			x <<= 4;
			y <<= 4;
			x |= (hash[i] & 0xf0) >> 4;
			y |= (hash[i] & 0x0f);
		}

		z = *pz;
		*px = x;
		*py = y;
		return check_xyz(x, y, z);
	}

#else /* DIRECTORY_HASH */
	/* Effective base directory prefix to strip from `path` */
	const char *base_prefix = tilepath;
	/* Prefix comparison index and sscanf() match count */
	int i, n;

#ifdef TILE_PATH

	if (TILE_PATH && TILE_PATH[0] != '\0') {
		base_prefix = TILE_PATH;
	}

#endif /* TILE_PATH */

	for (i = 0; base_prefix[i] && base_prefix[i] == path[i]; ++i)
		;

	if (base_prefix[i]) {
		g_logger(G_LOG_LEVEL_ERROR, "Tile path does not match settings (%s): %s", base_prefix, path);
		return 1;
	}

	n = sscanf(path + i, "/%40[^/]/%d/%d/%d", xmlconfig, pz, px, py);

	if (n != 4) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to parse tile path: %s", path);
		return 1;
	} else {
		return check_xyz(*px, *py, *pz);
	}

#endif /* DIRECTORY_HASH */
}

/**
 * @brief Compute the `.meta` file path and sub-tile index for `(xmlconfig, options, x, y, z)`.
 *
 * Aligns `(x, y)` down to the top-left `(0, 0)` corner of its `METATILE x METATILE` block
 * (`x &= ~mask`, `y &= ~mask`) and computes the sub-tile's linear index within the
 * metatile header table. Formats `path` using either:
 *   - `DIRECTORY_HASH` layout: `<tile_dir>/<xmlconfig>/<z>/<h4..h0>[.<options>].meta`
 *     (interleaving 4-bit nibbles of `x` and `y` across 5 directory levels to prevent
 *     excessive directory entry counts in leaf folders).
 *   - Flat layout: `<tile_dir>/<xmlconfig>/<z>/<x>/<y>[.<options>].meta`
 *
 * @param path      Output buffer receiving the formatted `.meta` filesystem path.
 * @param len       Capacity of `path` in bytes.
 * @param tile_dir  Base tile storage directory path.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string (omitted from filename if empty).
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Linear sub-tile index within the metatile (`0 .. METATILE * METATILE - 1`).
 */
int xyzo_to_meta(char *path, size_t len, const char *tile_dir, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Loop index, 5 nibble-interleaved hash bytes (LSB in hash[0], MSB in hash[4]),
	 * linear sub-tile offset within the metatile, and METATILE - 1 bitmask */
	unsigned char i, hash[5], offset, mask;

	/* Each metatile winds up in its own file, with several in each leaf directory;
	 * the .meta file name is based on the top-left sub-tile at (0, 0) */
	mask = METATILE - 1;
	offset = (x & mask) * METATILE + (y & mask);
	x &= ~mask;
	y &= ~mask;

	/* Pack 4 bits of x (upper nibble) and 4 bits of y (lower nibble) per level from LSB to MSB */
	for (i = 0; i < 5; i++) {
		hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
		x >>= 4;
		y >>= 4;
	}

#ifdef DIRECTORY_HASH

	if (strlen(options)) {
		snprintf(path, len, "%s/%s/%d/%u/%u/%u/%u/%u.%s.meta", tile_dir, xmlconfig, z, hash[4], hash[3], hash[2], hash[1], hash[0], options);
	} else {
		snprintf(path, len, "%s/%s/%d/%u/%u/%u/%u/%u.meta", tile_dir, xmlconfig, z, hash[4], hash[3], hash[2], hash[1], hash[0]);
	}

#else /* DIRECTORY_HASH */

	if (strlen(options)) {
		snprintf(path, len, "%s/%s/%d/%u/%u.%s.meta", tile_dir, xmlconfig, z, x, y, options);
	} else {
		snprintf(path, len, "%s/%s/%d/%u/%u.meta", tile_dir, xmlconfig, z, x, y);
	}

#endif /* DIRECTORY_HASH */
	return offset;
}

/**
 * @brief Compute the `.meta` file path and sub-tile index for unparameterized `(xmlconfig, x, y, z)`.
 *
 * Convenience wrapper around xyzo_to_meta() with an empty `options` string (`""`).
 *
 * @param path      Output buffer receiving the formatted `.meta` filesystem path.
 * @param len       Capacity of `path` in bytes.
 * @param tile_dir  Base tile storage directory path.
 * @param xmlconfig Logical map style name.
 * @param x         Global tile X coordinate.
 * @param y         Global tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Linear sub-tile index within the metatile (`0 .. METATILE * METATILE - 1`).
 */
int xyz_to_meta(char *path, size_t len, const char *tile_dir, const char *xmlconfig, int x, int y, int z)
{
	return xyzo_to_meta(path, len, tile_dir, xmlconfig, "", x, y, z);
}
