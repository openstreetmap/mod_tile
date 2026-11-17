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

/* Dispatcher wrapper for pluggable tile storage backends */

#include <errno.h>
#include <glib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include "g_logger.h"
#include "store.h"
#include "store_file.h"
#include "store_memcached.h"
#include "store_null.h"
#include "store_rados.h"
#include "store_ro_composite.h"
#include "store_ro_http_proxy.h"

/**
 * @brief Instantiate and initialize a storage backend from a path or URI specification.
 *
 * Dispatches to the appropriate backend constructor based on the prefix of `options`:
 *   - `"/..."`             : Local filesystem metatile directory (`init_storage_file()`)
 *   - `"rados://..."`      : Ceph RADOS object store (`init_storage_rados()`)
 *   - `"memcached://..."`  : Memcached in-memory cache (`init_storage_memcached()`)
 *   - `"ro_http_proxy://"` : Read-only upstream HTTP tile proxy (`init_storage_ro_http_proxy()`)
 *   - `"composite:{...}"`  : Read-only layered composite backend (`init_storage_ro_composite()`)
 *   - `"null://..."`       : No-op / blackhole backend (`init_storage_null()`)
 *
 * In `mod_tile` (Apache HTTPD), this function is invoked once per worker thread so
 * each thread owns an independent `storage_backend` context.
 *
 * @param options Filesystem directory path (starting with `'/'`) or backend URI string.
 * @return Initialized `struct storage_backend` handle on success, or `NULL` if `options`
 *         is empty, invalid, or backend initialization fails.
 */
struct storage_backend *init_storage_backend(const char *options)
{
	/* File status buffer used to verify that a leading-'/' path exists and is a directory */
	struct stat st;
	/* Initialized storage backend handle returned to the caller (NULL on failure) */
	struct storage_backend *store = NULL;

	/* Reject empty configuration strings before inspecting prefixes */
	if (strlen(options) == 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: Options string was empty");
		return NULL;
	}

	/* Leading '/' indicates a local filesystem directory backend */
	if (options[0] == '/') {
		if (stat(options, &st) != 0) {
			g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: Failed to stat %s with error: %s", options, strerror(errno));
			return NULL;
		}

		if (S_ISDIR(st.st_mode)) {
			g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising file storage backend at: %s", options);
			store = init_storage_file(options);
			return store;
		} else {
			g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: %s is not a directory", options);
			return NULL;
		}
	}

	/* Ceph RADOS storage backend */
	if (strstr(options, "rados://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising rados storage backend at: %s", options);
		store = init_storage_rados(options);
		return store;
	}

	/* Memcached storage backend */
	if (strstr(options, "memcached://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising memcached storage backend at: %s", options);
		store = init_storage_memcached(options);
		return store;
	}

	/* Read-only HTTP proxy storage backend */
	if (strstr(options, "ro_http_proxy://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising ro_http_proxy storage backend at: %s", options);
		store = init_storage_ro_http_proxy(options);
		return store;
	}

	/* Read-only composite multi-backend wrapper */
	if (strstr(options, "composite:{") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising ro_composite storage backend at: %s", options);
		store = init_storage_ro_composite(options);
		return store;
	}

	/* No-op null storage backend (discards writes, misses on reads) */
	if (strstr(options, "null://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising null storage backend at: %s", options);
		store = init_storage_null();
		return store;
	}

	g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: No valid storage backend found for options: %s", options);

	return store;
}
