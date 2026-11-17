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

#ifndef STORE_RO_HTTP_PROXY_H
#define STORE_RO_HTTP_PROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>

#include "protocol.h"
#include "store.h"

	/**
	 * @brief Single-tile cache holding the most recently fetched remote tile and its metadata.
	 *
	 * Avoids performing a duplicate HTTP GET request when `tile_stat()` is immediately
	 * followed by `tile_read()` for the same `(xmlname, x, y, z)` tile.
	 */
	struct ro_http_proxy_tile_cache {
		/** @brief Metadata and expiry status derived from the upstream HTTP response headers. */
		struct stat_info st_stat;
		/** @brief Heap-allocated buffer holding the fetched tile payload bytes. */
		char *tile;
		/** @brief Tile X, Y, and zoom coordinates of the cached tile (`-1` when invalid). */
		int x, y, z;
		/** @brief Logical map style name (`xmlname`) corresponding to the cached tile. */
		char xmlname[XMLCONFIG_MAX];
	};

	/**
	 * @brief Per-backend state for the read-only HTTP proxy storage engine.
	 */
	struct ro_http_proxy_ctx {
		/** @brief Reusable libcurl easy handle configured for upstream tile requests. */
		CURL *ctx;
		/** @brief Base URL template/prefix of the upstream tile server. */
		char *baseurl;
		/** @brief Single-entry cache storing the last fetched tile payload and stat metadata. */
		struct ro_http_proxy_tile_cache cache;
	};

	/**
	 * @brief Dynamically resized memory buffer used by libcurl's write callback to accumulate HTTP response bodies.
	 */
	struct MemoryStruct {
		/** @brief Heap-allocated response payload buffer. */
		char *memory;
		/** @brief Current number of valid bytes stored in `memory`. */
		size_t size;
	};
#endif /* HAVE_LIBCURL */

	struct storage_backend *init_storage_ro_http_proxy(const char *connection_string);

#ifdef __cplusplus
}
#endif

#endif /* STORE_RO_HTTP_PROXY_H */
