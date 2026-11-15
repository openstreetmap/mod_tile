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

/* Read-only HTTP proxy tile storage backend:
 * Fetches individual PNG tiles on demand from an upstream XYZ HTTP tile server
 * via libcurl and caches the most recently fetched tile (or 404 negative result)
 * per backend context so paired `tile_stat()` and `tile_read()` calls only issue
 * one HTTP GET request. Write, delete, and expire operations are unsupported
 * and always return an error. */

#include <glib.h>
#include <stdio.h>

#include "config.h"
#include "g_logger.h"
#include "store.h"
#include "store_ro_http_proxy.h"

/* TODO: need to create an appropriate configure check. */
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Mutex serializing one-time `curl_global_init()` across worker threads. */
static pthread_mutex_t ro_http_proxy_mutex;
/** @brief Process-wide flag indicating whether `curl_global_init()` has already run. */
static int done_global_init = 0;

/**
 * @brief libcurl `CURLOPT_WRITEFUNCTION` callback appending response bytes into a `MemoryStruct`.
 *
 * Dynamically grows `chunk->memory` via `realloc()` / `malloc()` as HTTP response chunks arrive.
 *
 * @param contents Pointer to the incoming data chunk delivered by libcurl.
 * @param size     Size of each element in bytes.
 * @param nmemb    Number of elements in `contents`.
 * @param userp    Pointer to a `struct MemoryStruct` accumulating the response body.
 * @return Total number of bytes handled (`size * nmemb`).
 */
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	/* Byte length of the incoming chunk delivered by libcurl */
	size_t realsize = size * nmemb;
	/* Accumulator buffer tracking heap pointer and current byte offset */
	struct MemoryStruct *chunk = (struct MemoryStruct *)userp;

	if (chunk->memory) {
		chunk->memory = (char *)realloc(chunk->memory, chunk->size + realsize);
	} else {
		chunk->memory = (char *)malloc(realsize);
	}

	g_logger(G_LOG_LEVEL_DEBUG, "ro_http_proxy_tile_read: writing a chunk: Position %lu, size %lu", chunk->size, realsize);

	memcpy(&(chunk->memory[chunk->size]), contents, realsize);
	chunk->size += realsize;

	return realsize;
}

/**
 * @brief Format the upstream HTTP URL for tile `(x, y, z)`.
 *
 * Formats `key` as `"http://<baseurl>/<z>/<x>/<y>.png"`.
 *
 * @param store Initialized HTTP proxy `storage_backend` (`storage_ctx` holds `struct ro_http_proxy_ctx *`).
 * @param x     Tile X coordinate.
 * @param y     Tile Y coordinate.
 * @param z     Tile zoom level.
 * @param key   Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted URL.
 * @return Pointer to `key`.
 */
static char *ro_http_proxy_xyz_to_storagekey(struct storage_backend *store, int x, int y, int z, char *key)
{
	snprintf(key, PATH_MAX - 1, "http://%s/%i/%i/%i.png", ((struct ro_http_proxy_ctx *)(store->storage_ctx))->baseurl, z, x, y);
	return key;
}

/**
 * @brief Fetch a tile over HTTP from the upstream server or serve it from the single-tile cache.
 *
 * Checks whether `(xmlconfig, x, y, z)` matches `ctx->cache`. On a cache hit, returns `1`
 * immediately without performing network I/O. On a cache miss, issues an HTTP GET via
 * `curl_easy_perform()` and updates `ctx->cache`:
 *   - HTTP `200`: Stores the downloaded PNG bytes, byte size, and `Last-Modified` timestamp (`st_mtime`).
 *   - HTTP `404`: Caches a negative lookup (`st_stat.size = -1`) so the subsequent `tile_read()` avoids a duplicate request.
 *   - Other status codes or curl errors: Invalidates `ctx->cache` coordinates and returns `-1`.
 *
 * @param store     Initialized HTTP proxy `storage_backend`.
 * @param xmlconfig Logical map style name used as part of the single-tile cache key.
 * @param options   Style parameterization options string (currently unused).
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `1` if the tile result (HTTP 200 or cached 404) is ready in `ctx->cache`, or `-1` on network/HTTP error.
 */
static int ro_http_proxy_tile_retrieve(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* HTTP proxy backend context holding the CURL handle, base URL, and single-tile cache */
	struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
	/* Heap-allocated buffer for formatting the upstream tile URL */
	char *path;
	/* libcurl operation status code */
	CURLcode res;
	/* Dynamic memory buffer accumulating the HTTP response body */
	struct MemoryStruct chunk;
	/* HTTP response status code retrieved via CURLINFO_RESPONSE_CODE */
	long httpCode;

	/* TODO: Deal with options */
	if ((ctx->cache.x == x) && (ctx->cache.y == y) && (ctx->cache.z == z) && (strcmp(ctx->cache.xmlname, xmlconfig) == 0)) {
		g_logger(G_LOG_LEVEL_DEBUG, "ro_http_proxy_tile_fetch: Got a cached tile");
		return 1;
	} else {
		g_logger(G_LOG_LEVEL_DEBUG, "ro_http_proxy_tile_fetch: Fetching tile");

		chunk.memory = NULL;
		chunk.size = 0;
		path = (char *)malloc(PATH_MAX);

		if (!path) {
			g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_fetch: failed to allocate memory for path");
			return -1;
		}

		ro_http_proxy_xyz_to_storagekey(store, x, y, z, path);
		g_logger(G_LOG_LEVEL_DEBUG, "ro_http_proxy_tile_fetch: proxing file %s", path);
		curl_easy_setopt(ctx->ctx, CURLOPT_URL, path);

		curl_easy_setopt(ctx->ctx, CURLOPT_WRITEFUNCTION, write_memory_callback);
		curl_easy_setopt(ctx->ctx, CURLOPT_WRITEDATA, (void *)&chunk);

		res = curl_easy_perform(ctx->ctx);
		free(path);

		if (res != CURLE_OK) {
			g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_fetch: failed to retrieve file: %s", curl_easy_strerror(res));
			free(chunk.memory);
			ctx->cache.x = -1;
			ctx->cache.y = -1;
			ctx->cache.z = -1;
			return -1;
		}

		res = curl_easy_getinfo(ctx->ctx, CURLINFO_RESPONSE_CODE, &httpCode);

		if (res != CURLE_OK) {
			g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_fetch: failed to retrieve HTTP code: %s", curl_easy_strerror(res));
			free(chunk.memory);
			ctx->cache.x = -1;
			ctx->cache.y = -1;
			ctx->cache.z = -1;
			return -1;
		}

		switch (httpCode) {
		case 200: {
			if (ctx->cache.tile != NULL) {
				free(ctx->cache.tile);
			}

			/* Remote Last-Modified timestamp reported by libcurl (-1 if absent) */
			long filemtime = -1;
			ctx->cache.tile = chunk.memory;
			ctx->cache.st_stat.size = chunk.size;
			ctx->cache.st_stat.expired = 0;
			res = curl_easy_getinfo(ctx->ctx, CURLINFO_FILETIME, &filemtime);

			if (res == CURLE_OK && filemtime >= 0) {
				ctx->cache.st_stat.mtime = (time_t)filemtime;
			}

			ctx->cache.st_stat.atime = 0;
			g_logger(G_LOG_LEVEL_DEBUG, "ro_http_proxy_tile_read: Read file of size %lu", chunk.size);
			break;
		}

		case 404: {
			if (ctx->cache.tile != NULL) {
				free(ctx->cache.tile);
				ctx->cache.tile = NULL;
			}

			free(chunk.memory);
			ctx->cache.st_stat.size = -1;
			ctx->cache.st_stat.expired = 0;
			break;
		}

		default: {
			/* Do not cache anything for unexpected responses, otherwise the previously
			 * cached tile would be served for these coordinates */
			g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_fetch: unexpected HTTP response code %li", httpCode);
			free(chunk.memory);
			ctx->cache.x = -1;
			ctx->cache.y = -1;
			ctx->cache.z = -1;
			return -1;
		}
		}

		ctx->cache.x = x;
		ctx->cache.y = y;
		ctx->cache.z = z;
		snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", xmlconfig);
		return 1;
	}
}

/**
 * @brief Retrieve a PNG tile from the upstream HTTP proxy into caller buffer `buf`.
 *
 * Ensures the tile is loaded into `ctx->cache` via ro_http_proxy_tile_retrieve() and
 * copies the cached bytes into `buf` if present and within capacity `sz`.
 *
 * @param store      Initialized HTTP proxy `storage_backend`.
 * @param xmlconfig  Logical map style name.
 * @param options    Style parameterization options string.
 * @param x          Tile X coordinate.
 * @param y          Tile Y coordinate.
 * @param z          Tile zoom level.
 * @param buf        Caller-supplied output buffer receiving the tile PNG bytes.
 * @param sz         Maximum capacity of `buf` in bytes.
 * @param compressed Compression flag output pointer (unused; raw PNG is returned).
 * @param log_msg    Caller-supplied error message buffer (unused; errors go to `g_logger`).
 * @return Number of bytes copied into `buf` (`>= 0`) on success, or `-1` on 404, buffer overrun, or HTTP error.
 */
static int ro_http_proxy_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
	/* HTTP proxy backend context holding the single-tile cache */
	struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

	if (ro_http_proxy_tile_retrieve(store, xmlconfig, options, x, y, z) > 0) {
		if (ctx->cache.st_stat.size < 0) {
			/* Cached 404 response */
			return -1;
		}

		if ((size_t)ctx->cache.st_stat.size > sz) {
			g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_read: size was too big, overrun %lu %li", sz, (long int)(ctx->cache.st_stat.size));
			return -1;
		}

		memcpy(buf, ctx->cache.tile, ctx->cache.st_stat.size);
		return ctx->cache.st_stat.size;
	} else {
		g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_tile_read: Fetching didn't work");
		return -1;
	}
}

/**
 * @brief Retrieve metadata and `Last-Modified` timestamp for a tile from the HTTP proxy.
 *
 * Fetches (or hits cache for) the tile via ro_http_proxy_tile_retrieve() and returns
 * `ctx->cache.st_stat` (`size = -1` on HTTP 404 or fetch error).
 *
 * @param store     Initialized HTTP proxy `storage_backend`.
 * @param xmlconfig Logical map style name.
 * @param options   Style parameterization options string.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return Populated `struct stat_info` (`size < 0` indicates missing tile or HTTP error).
 */
static struct stat_info ro_http_proxy_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Fallback empty stat result returned when HTTP retrieval fails */
	struct stat_info tile_stat;
	/* HTTP proxy backend context holding the single-tile cache */
	struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

	if (ro_http_proxy_tile_retrieve(store, xmlconfig, options, x, y, z) > 0) {
		return ctx->cache.st_stat;
	} else {
		tile_stat.size = -1;
		tile_stat.expired = 0;
		tile_stat.mtime = 0;
		tile_stat.atime = 0;
		tile_stat.ctime = 0;
		return tile_stat;
	}
}

/**
 * @brief Format the upstream `"http://<baseurl>/<z>/<x>/<y>.png"` URL as the tile storage ID.
 *
 * @param store     Initialized HTTP proxy `storage_backend`.
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @param string    Caller-supplied output buffer (`PATH_MAX` bytes) receiving the formatted URL.
 * @return Pointer to `string`.
 */
static char *ro_http_proxy_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
	return ro_http_proxy_xyz_to_storagekey(store, x, y, z, string);
}

/**
 * @brief Reject metatile write attempts on the read-only HTTP proxy backend.
 *
 * @param store     HTTP proxy `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @param buf       Serialized metatile buffer (unused).
 * @param sz        Byte length of `buf` (unused).
 * @return Always returns `-1`.
 */
static int ro_http_proxy_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_metatile_write: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Reject metatile delete attempts on the read-only HTTP proxy backend.
 *
 * @param store     HTTP proxy `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `-1`.
 */
static int ro_http_proxy_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_metatile_delete: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Reject metatile expiry attempts on the read-only HTTP proxy backend.
 *
 * @param store     HTTP proxy `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `-1`.
 */
static int ro_http_proxy_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_http_proxy_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Free the base URL, cached tile buffer, `CURL` easy handle, and `ro_http_proxy_ctx`.
 *
 * @param store HTTP proxy `storage_backend` instance to close.
 * @return Always returns `0`.
 */
static int ro_http_proxy_close_storage(struct storage_backend *store)
{
	/* HTTP proxy backend context owning the base URL, tile cache, and CURL easy handle */
	struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

	free(ctx->baseurl);

	if (ctx->cache.tile) {
		free(ctx->cache.tile);
	}

	curl_easy_cleanup(ctx->ctx);
	free(ctx);
	store->storage_ctx = NULL;
	return 0;
}

#endif /* HAVE_LIBCURL */

/**
 * @brief Allocate and initialize a read-only HTTP proxy `storage_backend`.
 *
 * Strips the `"ro_http_proxy://"` prefix from `connection_string` to obtain the upstream
 * base host/path, performs thread-safe one-time `curl_global_init()` under `ro_http_proxy_mutex`,
 * configures a `CURL` easy handle with `CURLOPT_NOSIGNAL`, `CURLOPT_FOLLOWLOCATION`,
 * `CURLOPT_USERAGENT`, and `CURLOPT_FILETIME`, and wires all backend operation function pointers.
 * Returns `NULL` if compiled without `HAVE_LIBCURL` or if initialization fails.
 *
 * @param connection_string Backend URI of the form `"ro_http_proxy://<host[:port][/prefix]>"`.
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on failure or if unsupported.
 */
struct storage_backend *init_storage_ro_http_proxy(const char *connection_string)
{
#ifndef HAVE_LIBCURL
	g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_http_proxy: Support for http proxy storage has not been compiled into this program");
	return NULL;
#else  /* HAVE_LIBCURL */
	/* Newly allocated storage_backend vtable and context */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof(struct storage_backend));
	/* Per-backend HTTP proxy context holding the CURL handle, base URL, and tile cache */
	struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)malloc(sizeof(struct ro_http_proxy_ctx));
	/* libcurl initialization status code */
	CURLcode res;

	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_ro_http_proxy: initialising proxy storage backend for %s", connection_string);

	if (!store || !ctx) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_http_proxy: failed to allocate memory for context");

		if (store) {
			free(store);
		}

		if (ctx) {
			free(ctx);
		}

		return NULL;
	}

	/* Mark single-tile cache invalid until first HTTP fetch */
	ctx->cache.x = -1;
	ctx->cache.y = -1;
	ctx->cache.z = -1;
	ctx->cache.tile = NULL;
	ctx->cache.xmlname[0] = 0;

	/* Strip "ro_http_proxy://" prefix to extract upstream base host/path */
	ctx->baseurl = strdup(&(connection_string[strlen("ro_http_proxy://")]));
	pthread_mutex_lock(&ro_http_proxy_mutex);

	if (!done_global_init) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_ro_http_proxy: Global init of curl");
		res = curl_global_init(CURL_GLOBAL_DEFAULT);
		done_global_init = 1;
	} else {
		res = CURLE_OK;
	}

	pthread_mutex_unlock(&ro_http_proxy_mutex);

	if (res != CURLE_OK) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_http_proxy: failed to initialise global curl: %s", curl_easy_strerror(res));
		free(ctx->baseurl);
		free(ctx);
		free(store);
		return NULL;
	}

	ctx->ctx = curl_easy_init();

	if (!ctx->ctx) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_http_proxy: failed to initialise curl");
		free(ctx->baseurl);
		free(ctx);
		free(store);
		return NULL;
	}

	curl_easy_setopt(ctx->ctx, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ctx->ctx, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(ctx->ctx, CURLOPT_USERAGENT, "mod_tile/1.0");
	curl_easy_setopt(ctx->ctx, CURLOPT_FILETIME, 1L);

	store->storage_ctx = ctx;

	store->tile_read = &ro_http_proxy_tile_read;
	store->tile_stat = &ro_http_proxy_tile_stat;
	store->metatile_write = &ro_http_proxy_metatile_write;
	store->metatile_delete = &ro_http_proxy_metatile_delete;
	store->metatile_expire = &ro_http_proxy_metatile_expire;
	store->tile_storage_id = &ro_http_proxy_tile_storage_id;
	store->close_storage = &ro_http_proxy_close_storage;

	return store;
#endif /* HAVE_LIBCURL */
}
