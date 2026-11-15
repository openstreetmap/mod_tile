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

#ifndef STORE_H
#define STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "render_config.h"
#include <stdlib.h>
#include <sys/types.h>

	/**
	 * @brief Metadata and expiry state returned when querying a stored tile or metatile.
	 */
	struct stat_info {
		/** @brief Total tile payload size in bytes (`< 0` indicates the tile does not exist). */
		off_t size;
		/** @brief Timestamp of last tile access (seconds since Unix epoch). */
		time_t atime;
		/** @brief Timestamp of last tile modification/render (seconds since Unix epoch). */
		time_t mtime;
		/** @brief Timestamp of last status change (seconds since Unix epoch). */
		time_t ctime;
		/** @brief Non-zero if the tile is marked expired and needs re-rendering; `0` if fresh. */
		int expired;
	};

	/**
	 * @brief Virtual dispatch table and context handle for a pluggable tile/metatile storage backend.
	 */
	struct storage_backend {
		/** @brief Read a single tile's payload into `buf`, returning bytes read or `< 0` on error. */
		int (*tile_read)(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *err_msg);
		/** @brief Query size, timestamps, and expiry status of a tile without reading its full payload. */
		struct stat_info (*tile_stat)(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z);
		/** @brief Persist a serialized `METATILE x METATILE` archive buffer into the backend. */
		int (*metatile_write)(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz);
		/** @brief Remove the metatile containing coordinates `(x, y, z)` from the backend. */
		int (*metatile_delete)(struct storage_backend *store, const char *xmlconfig, int x, int y, int z);
		/** @brief Mark the metatile containing coordinates `(x, y, z)` as expired. */
		int (*metatile_expire)(struct storage_backend *store, const char *xmlconfig, int x, int y, int z);
		/** @brief Format a backend-specific storage identifier string (path, object key, or URL) for logging. */
		char *(*tile_storage_id)(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string);
		/** @brief Release backend-specific resources and free the `storage_backend` instance. */
		int (*close_storage)(struct storage_backend *store);

		/** @brief Opaque pointer to backend-specific private state/configuration context. */
		void *storage_ctx;
	};

	struct storage_backend *init_storage_backend(const char *options);

#ifdef __cplusplus
}
#endif

#endif /* STORE_H */
