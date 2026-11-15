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

/* Read-only composite tile storage backend:
 * Fetches PNG tiles from a primary (foreground) and secondary (background)
 * storage backend, alpha-composites them in memory via Cairo onto an ARGB32
 * surface, and returns the resulting PNG payload. Write, delete, and expire
 * operations are unsupported and always return an error. */

#include <glib.h>
#include <stdio.h>

#include "config.h"
#include "g_logger.h"
#include "store.h"
#include "store_ro_composite.h"

/* TODO: need to create an appropriate configure check. */
#ifdef HAVE_CAIRO
#include <cairo/cairo.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Cairo PNG stream write callback appending encoded PNG chunks to a byte buffer.
 *
 * Passed to `cairo_surface_write_to_png_stream()` to serialize a composited ARGB32
 * surface directly into `closure->data`.
 *
 * @param in_closure Pointer to a `png_stream_to_byte_array_closure_t` tracking buffer bounds and cursor.
 * @param data       Source PNG byte chunk emitted by Cairo.
 * @param length     Byte length of `data`.
 * @return `CAIRO_STATUS_SUCCESS` if copied, or `CAIRO_STATUS_WRITE_ERROR` if `closure->max_size` would overflow.
 */
static cairo_status_t write_png_stream_to_byte_array(void *in_closure, const unsigned char *data, unsigned int length)
{
	/* Stream closure tracking the output buffer, capacity, and current write offset */
	png_stream_to_byte_array_closure_t *closure = (png_stream_to_byte_array_closure_t *)in_closure;

	g_logger(G_LOG_LEVEL_DEBUG, "ro_composite_tile: writing to byte array: pos: %i, length: %i", closure->pos, length);

	if ((closure->pos + length) > (closure->max_size)) {
		return CAIRO_STATUS_WRITE_ERROR;
	}

	memcpy((closure->data + closure->pos), data, length);
	closure->pos += length;

	return CAIRO_STATUS_SUCCESS;
}

/**
 * @brief Cairo PNG stream read callback feeding encoded PNG bytes from a memory buffer.
 *
 * Passed to `cairo_image_surface_create_from_png_stream()` to decode raw tile PNG data
 * retrieved from an underlying storage backend.
 *
 * @param in_closure Pointer to a `png_stream_to_byte_array_closure_t` tracking buffer bounds and cursor.
 * @param data       Destination buffer requested by Cairo's PNG decoder.
 * @param length     Number of bytes requested by Cairo.
 * @return `CAIRO_STATUS_SUCCESS` if read, or `CAIRO_STATUS_READ_ERROR` if `closure->max_size` is exceeded.
 */
static cairo_status_t read_png_stream_from_byte_array(void *in_closure, unsigned char *data, unsigned int length)
{
	/* Stream closure tracking the input PNG buffer, capacity, and current read offset */
	png_stream_to_byte_array_closure_t *closure = (png_stream_to_byte_array_closure_t *)in_closure;

	g_logger(G_LOG_LEVEL_DEBUG, "ro_composite_tile: reading from byte array: pos: %i, length: %i", closure->pos, length);

	if ((closure->pos + length) > (closure->max_size)) {
		return CAIRO_STATUS_READ_ERROR;
	}

	memcpy(data, (closure->data + closure->pos), length);
	closure->pos += length;

	return CAIRO_STATUS_SUCCESS;
}

/**
 * @brief Read tiles from both primary and secondary backends, composite them via Cairo, and encode as PNG.
 *
 * Fetches and decodes the PNG tile from `store_primary` (`imageA`), fetches and decodes the
 * PNG tile from `store_secondary` (`imageB`), creates a `render_size x render_size` ARGB32
 * destination surface (`imageC`), paints `imageA` followed by `imageB` onto `imageC`, and
 * encodes the resulting composite surface as PNG bytes into `buf`.
 *
 * @param store      Initialized composite `storage_backend` (`storage_ctx` holds `struct ro_composite_ctx *`).
 * @param xmlconfig  Logical map style name (overridden internally by `xmlconfig_primary` / `xmlconfig_secondary`).
 * @param options    Style parameterization options string forwarded to both child backends.
 * @param x          Tile X coordinate.
 * @param y          Tile Y coordinate.
 * @param z          Tile zoom level.
 * @param buf        Caller-supplied scratch/output buffer receiving the composited PNG bytes.
 * @param sz         Maximum capacity of `buf` in bytes.
 * @param compressed Output compression flag populated by the child `tile_read()` calls.
 * @param log_msg    Caller-supplied buffer receiving an error message on failure.
 * @return Byte length of the composited PNG written into `buf` (`> 0`) on success, or `-1` on read/decode/surface error.
 */
static int ro_composite_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
	/* Composite backend context holding child backends, style names, and render dimensions */
	struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)(store->storage_ctx);
	/* Decoded Cairo image surface from the primary backend */
	cairo_surface_t *imageA;
	/* Decoded Cairo image surface from the secondary backend */
	cairo_surface_t *imageB;
	/* Destination ARGB32 Cairo image surface receiving the composited result */
	cairo_surface_t *imageC;
	/* Cairo drawing context bound to imageC */
	cairo_t *cr;
	/* Reusable byte-array stream closure for PNG decoding and encoding */
	png_stream_to_byte_array_closure_t closure;

	/* Read and decode primary layer PNG */
	if (ctx->store_primary->tile_read(ctx->store_primary, ctx->xmlconfig_primary, options, x, y, z, buf, sz, compressed, log_msg) < 0) {
		snprintf(log_msg, 1024, "ro_composite_tile_read: Failed to read tile data of primary backend\n");
		return -1;
	}

	closure.data = buf;
	closure.pos = 0;
	closure.max_size = sz;
	imageA = cairo_image_surface_create_from_png_stream(&read_png_stream_from_byte_array, &closure);

	if (!imageA || cairo_surface_status(imageA) != CAIRO_STATUS_SUCCESS) {
		snprintf(log_msg, 1024, "ro_composite_tile_read: Failed to decode png data from primary backend\n");

		if (imageA) {
			cairo_surface_destroy(imageA);
		}

		return -1;
	}

	/* Read and decode secondary layer PNG */
	if (ctx->store_secondary->tile_read(ctx->store_secondary, ctx->xmlconfig_secondary, options, x, y, z, buf, sz, compressed, log_msg) < 0) {
		snprintf(log_msg, 1024, "ro_composite_tile_read: Failed to read tile data of secondary backend\n");
		cairo_surface_destroy(imageA);
		return -1;
	}

	closure.data = buf;
	closure.pos = 0;
	closure.max_size = sz;
	imageB = cairo_image_surface_create_from_png_stream(&read_png_stream_from_byte_array, &closure);

	if (!imageB || cairo_surface_status(imageB) != CAIRO_STATUS_SUCCESS) {
		snprintf(log_msg, 1024, "ro_composite_tile_read: Failed to decode png data from secondary backend\n");
		cairo_surface_destroy(imageA);

		if (imageB) {
			cairo_surface_destroy(imageB);
		}

		return -1;
	}

	/* Allocate target ARGB32 compositing canvas */
	imageC = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ctx->render_size, ctx->render_size);

	if (!imageC || cairo_surface_status(imageC) != CAIRO_STATUS_SUCCESS) {
		snprintf(log_msg, 1024, "ro_composite_tile_read: Failed to create output png\n");
		cairo_surface_destroy(imageA);
		cairo_surface_destroy(imageB);

		if (imageC) {
			cairo_surface_destroy(imageC);
		}

		return -1;
	}

	/* Paint primary surface first, then alpha-blend secondary surface over it */
	cr = cairo_create(imageC);
	cairo_set_source_surface(cr, imageA, 0, 0);
	cairo_paint(cr);
	cairo_set_source_surface(cr, imageB, 0, 0);
	cairo_paint(cr);
	cairo_surface_flush(imageC);
	cairo_destroy(cr);

	/* Serialize composited surface back into caller's PNG output buffer */
	closure.data = buf;
	closure.pos = 0;
	closure.max_size = sz;
	cairo_surface_write_to_png_stream(imageC, &write_png_stream_to_byte_array, &closure);

	cairo_surface_destroy(imageA);
	cairo_surface_destroy(imageB);
	cairo_surface_destroy(imageC);

	return closure.pos;
}

/**
 * @brief Delegate tile metadata and expiry lookup to the primary storage backend.
 *
 * @param store     Initialized composite `storage_backend`.
 * @param xmlconfig Logical map style name (overridden by `ctx->xmlconfig_primary`).
 * @param options   Style parameterization options string.
 * @param x         Tile X coordinate.
 * @param y         Tile Y coordinate.
 * @param z         Tile zoom level.
 * @return `struct stat_info` returned by `ctx->store_primary->tile_stat()`.
 */
static struct stat_info ro_composite_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
	/* Composite backend context holding the primary child backend */
	struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)(store->storage_ctx);
	return ctx->store_primary->tile_stat(ctx->store_primary, ctx->xmlconfig_primary, options, x, y, z);
}

/**
 * @brief Return a static identifier string for composite tiles.
 *
 * @param store     Composite `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @param string    Caller-supplied buffer (unused; static string pointer is returned).
 * @return Pointer to static string `"Composite tile"`.
 */
static char *ro_composite_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
	/* Static label returned as the composite storage identifier */
	const char *id = "Composite tile";
	return (char *)id;
}

/**
 * @brief Reject metatile write attempts on the read-only composite backend.
 *
 * @param store     Composite `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param options   Style parameterization options string (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @param buf       Serialized metatile buffer (unused).
 * @param sz        Byte length of `buf` (unused).
 * @return Always returns `-1`.
 */
static int ro_composite_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_composite_metatile_write: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Reject metatile delete attempts on the read-only composite backend.
 *
 * @param store     Composite `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `-1`.
 */
static int ro_composite_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_composite_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Reject metatile expiry attempts on the read-only composite backend.
 *
 * @param store     Composite `storage_backend` (unused).
 * @param xmlconfig Logical map style name (unused).
 * @param x         Tile X coordinate (unused).
 * @param y         Tile Y coordinate (unused).
 * @param z         Tile zoom level (unused).
 * @return Always returns `-1`.
 */
static int ro_composite_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
	g_logger(G_LOG_LEVEL_ERROR, "ro_composite_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
	return -1;
}

/**
 * @brief Close and free both child storage backends, the tile cache buffer, and `ro_composite_ctx`.
 *
 * @param store Composite `storage_backend` instance to close.
 * @return Always returns `0`.
 */
static int ro_composite_close_storage(struct storage_backend *store)
{
	/* Composite backend context owning both child storage backends */
	struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)(store->storage_ctx);
	/* close_storage() no longer frees the backend struct itself (#524), so free the
	 * two inner backends here, just as callers do for the outer backend */
	ctx->store_primary->close_storage(ctx->store_primary);
	free(ctx->store_primary);
	ctx->store_secondary->close_storage(ctx->store_secondary);
	free(ctx->store_secondary);

	if (ctx->cache.tile) {
		free(ctx->cache.tile);
	}

	free(ctx);
	store->storage_ctx = NULL;
	return 0;
}

#endif /* HAVE_CAIRO */

/**
 * @brief Allocate and initialize a read-only Cairo compositing `storage_backend`.
 *
 * Parses `connection_string` of the form:
 * `"composite:{<xmlconfig_primary>,<backend_opts_primary>}{<xmlconfig_secondary>,<backend_opts_secondary>}"`,
 * initializes both child storage backends via init_storage_backend(), sets the default
 * `render_size` to `256`, and wires all backend operation function pointers. Returns `NULL`
 * if compiled without `HAVE_CAIRO` or if parsing/initialization fails.
 *
 * @param connection_string Configuration URI beginning with `"composite:{"`.
 * @return Newly allocated `struct storage_backend` pointer, or `NULL` on failure or if unsupported.
 */
struct storage_backend *init_storage_ro_composite(const char *connection_string)
{
#ifndef HAVE_CAIRO
	g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_coposite: Support for compositing storage has not been compiled into this program");
	return NULL;
#else  /* HAVE_CAIRO */
	/* Newly allocated outer storage_backend vtable */
	struct storage_backend *store = (struct storage_backend *)malloc(sizeof(struct storage_backend));
	/* Zero-initialized composite backend context holding child backends and config */
	struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)calloc(1, sizeof(struct ro_composite_ctx));
	/* Heap-allocated substring for "<xmlconfig_primary>,<backend_opts_primary>" */
	char *connection_string_primary;
	/* Pointer to "}{" delimiter, later reassigned to heap copy of "<xmlconfig_secondary>,<backend_opts_secondary>" */
	char *connection_string_secondary;
	/* Pointer to ',' delimiter separating xmlconfig name from child backend options */
	char *tmp;

	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_ro_composite: initialising compositing storage backend for %s", connection_string);

	if (!store || !ctx) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: failed to allocate memory for context");

		if (store) {
			free(store);
		}

		if (ctx) {
			free(ctx);
		}

		return NULL;
	}

	/* Locate the "}{" boundary splitting primary and secondary backend specifications */
	connection_string_secondary = (char *)strstr(connection_string, "}{");

	if (connection_string_secondary == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: malformed connection string, missing '}{' separator: %s", connection_string);
		free(ctx);
		free(store);
		return NULL;
	}

	connection_string_primary = (char *)malloc(strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary) + 1);

	if (connection_string_primary == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: failed to allocate memory for primary connection string");
		free(ctx);
		free(store);
		return NULL;
	}

	/* Extract primary "{...}" body (between "composite:{" and "}{") and secondary "{...}" body (stripping trailing '}') */
	memcpy(connection_string_primary, connection_string + strlen("composite:{"), strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary));
	connection_string_primary[strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary)] = 0;
	connection_string_secondary = strdup(connection_string_secondary + 2);
	connection_string_secondary[strlen(connection_string_secondary) - 1] = 0;

	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_ro_composite: Primary storage backend: %s", connection_string_primary);
	g_logger(G_LOG_LEVEL_DEBUG, "init_storage_ro_composite: Secondary storage backend: %s", connection_string_secondary);

	/* Split "<xmlconfig_primary>,<backend_opts_primary>" and instantiate primary backend */
	tmp = strstr(connection_string_primary, ",");

	if (tmp == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: malformed primary connection string, missing ',' separator: %s", connection_string_primary);
		free(connection_string_primary);
		free(connection_string_secondary);
		free(ctx);
		free(store);
		return NULL;
	}

	memcpy(ctx->xmlconfig_primary, connection_string_primary, tmp - connection_string_primary);
	ctx->xmlconfig_primary[tmp - connection_string_primary] = 0;
	ctx->store_primary = init_storage_backend(tmp + 1);

	if (ctx->store_primary == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: failed to initialise primary storage backend");
		free(connection_string_primary);
		free(connection_string_secondary);
		free(ctx);
		free(store);
		return NULL;
	}

	/* Split "<xmlconfig_secondary>,<backend_opts_secondary>" and instantiate secondary backend */
	tmp = strstr(connection_string_secondary, ",");

	if (tmp == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: malformed secondary connection string, missing ',' separator: %s", connection_string_secondary);
		free(connection_string_primary);
		free(connection_string_secondary);
		ctx->store_primary->close_storage(ctx->store_primary);
		free(ctx->store_primary);
		free(ctx);
		free(store);
		return NULL;
	}

	memcpy(ctx->xmlconfig_secondary, connection_string_secondary, tmp - connection_string_secondary);
	ctx->xmlconfig_secondary[tmp - connection_string_secondary] = 0;
	ctx->store_secondary = init_storage_backend(tmp + 1);

	if (ctx->store_secondary == NULL) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_ro_composite: failed to initialise secondary storage backend");
		free(connection_string_primary);
		free(connection_string_secondary);
		ctx->store_primary->close_storage(ctx->store_primary);
		free(ctx->store_primary);
		free(ctx);
		free(store);
		return NULL;
	}

	/* Child backends retain their own copies of initialized configuration */
	free(connection_string_primary);
	free(connection_string_secondary);

	ctx->render_size = 256;

	store->storage_ctx = ctx;

	store->tile_read = &ro_composite_tile_read;
	store->tile_stat = &ro_composite_tile_stat;
	store->metatile_write = &ro_composite_metatile_write;
	store->metatile_delete = &ro_composite_metatile_delete;
	store->metatile_expire = &ro_composite_metatile_expire;
	store->tile_storage_id = &ro_composite_tile_storage_id;
	store->close_storage = &ro_composite_close_storage;

	return store;
#endif /* HAVE_CAIRO */
}
