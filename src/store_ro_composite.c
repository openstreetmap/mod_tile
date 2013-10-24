#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

//TODO: need to create an appropriate configure check.
#ifdef HAVE_CAIRO
#define WANT_STORE_COMPOSITE
#endif

#ifdef WANT_STORE_COMPOSITE
#include <cairo/cairo.h>
#endif

#include "store.h"
#include "store_ro_composite.h"
#include "render_config.h"
#include "protocol.h"


#ifdef WANT_STORE_COMPOSITE

struct tile_cache {
    struct stat_info st_stat;
    char * tile;
    int x,y,z;
    char xmlname[XMLCONFIG_MAX];
};

struct ro_composite_ctx {
    struct storage_backend * store_primary;
    char xmlconfig_primary[XMLCONFIG_MAX];
    struct storage_backend * store_secondary;
    char xmlconfig_secondary[XMLCONFIG_MAX];
    struct tile_cache cache;
    int render_size;
};

typedef struct
{
    char *data;
    unsigned int max_size;
    unsigned int pos;
} png_stream_to_byte_array_closure_t;

static cairo_status_t write_png_stream_to_byte_array (void *in_closure, const unsigned char *data, unsigned int length)
{
    png_stream_to_byte_array_closure_t *closure = (png_stream_to_byte_array_closure_t *) in_closure;

    //log_message(STORE_LOGLVL_DEBUG, "ro_composite_tile: writing to byte array: pos: %i, length: %i", closure->pos, length);

    if ((closure->pos + length) > (closure->max_size))
        return CAIRO_STATUS_WRITE_ERROR;

    memcpy ((closure->data + closure->pos), data, length);
    closure->pos += length;

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t read_png_stream_from_byte_array (void *in_closure, unsigned char *data, unsigned int length)
{
    png_stream_to_byte_array_closure_t *closure =  (png_stream_to_byte_array_closure_t *) in_closure;

    //log_message(STORE_LOGLVL_DEBUG, "ro_composite_tile: reading from byte array: pos: %i, length: %i", closure->pos, length);

    if ((closure->pos + length) > (closure->max_size))
        return CAIRO_STATUS_READ_ERROR;

    memcpy (data, (closure->data + closure->pos), length);
    closure->pos += length;

    return CAIRO_STATUS_SUCCESS;
}


static int ro_composite_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {
    struct ro_composite_ctx * ctx = (struct ro_composite_ctx *)(store->storage_ctx);
    cairo_surface_t *imageA;
    cairo_surface_t *imageB;
    cairo_surface_t *imageC;
    cairo_t *cr;
    png_stream_to_byte_array_closure_t closure;

    if(ctx->store_primary->tile_read(ctx->store_primary, ctx->xmlconfig_primary, options, x, y, z, buf, sz, compressed, log_msg) < 0) {
        snprintf(log_msg,1024, "ro_composite_tile_read: Failed to read tile data of primary backend\n");
        return -1;
    }
    closure.data = buf;
    closure.pos = 0;
    closure.max_size = sz;
    imageA = cairo_image_surface_create_from_png_stream(&read_png_stream_from_byte_array, &closure);
    if (!imageA) {
        snprintf(log_msg,1024, "ro_composite_tile_read: Failed to decode png data from primary backend\n");
        return -1;
    }

    if(ctx->store_secondary->tile_read(ctx->store_secondary, ctx->xmlconfig_secondary, options, x, y, z, buf, sz, compressed, log_msg) < 0) {
        snprintf(log_msg,1024, "ro_composite_tile_read: Failed to read tile data of secondary backend\n");
        cairo_surface_destroy(imageA);
        return -1;
    }
    closure.data = buf;
    closure.pos = 0;
    closure.max_size = sz;
    imageB = cairo_image_surface_create_from_png_stream(&read_png_stream_from_byte_array, &closure);
    if (!imageB) {
        snprintf(log_msg,1024, "ro_composite_tile_read: Failed to decode png data from secondary backend\n");
        cairo_surface_destroy(imageA);
        return -1;
    }

    imageC = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ctx->render_size, ctx->render_size);
    if (!imageC) {
        snprintf(log_msg,1024, "ro_composite_tile_read: Failed to create output png\n");
        cairo_surface_destroy(imageA);
        cairo_surface_destroy(imageB);
        return -1;
    }

    //Create the cairo context
    cr = cairo_create(imageC);
    cairo_set_source_surface(cr, imageA, 0, 0);
    cairo_paint(cr);
    cairo_set_source_surface(cr, imageB, 0, 0);
    cairo_paint(cr);
    cairo_surface_flush(imageC);
    cairo_destroy(cr);

    closure.data = buf;
    closure.pos = 0;
    closure.max_size = sz;
    cairo_surface_write_to_png_stream(imageC, &write_png_stream_to_byte_array, &closure);

    cairo_surface_destroy(imageA);
    cairo_surface_destroy(imageB);
    cairo_surface_destroy(imageC);

    return closure.pos;
}

static struct stat_info ro_composite_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct ro_composite_ctx * ctx = (struct ro_composite_ctx *)(store->storage_ctx);
    return ctx->store_primary->tile_stat(ctx->store_primary,ctx->xmlconfig_primary, options, x, y, z);
}


static char * ro_composite_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {

    return "Coposite tile";
}

static int ro_composite_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    log_message(STORE_LOGLVL_ERR, "ro_composite_metatile_write: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}


static int ro_composite_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    log_message(STORE_LOGLVL_ERR, "ro_composite_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}

static int ro_composite_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {

    log_message(STORE_LOGLVL_ERR, "ro_composite_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}


static int ro_composite_close_storage(struct storage_backend * store) {
    struct ro_composite_ctx * ctx = (struct ro_composite_ctx *)(store->storage_ctx);
    ctx->store_primary->close_storage(ctx->store_primary);
    ctx->store_secondary->close_storage(ctx->store_secondary);
    if(ctx->cache.tile) free(ctx->cache.tile);
    free(ctx);
    free(store);
    return 0;
}

#endif //WANT_COMPOSITE



struct storage_backend * init_storage_ro_composite(const char * connection_string) {
    
#ifndef WANT_STORE_COMPOSITE
    log_message(STORE_LOGLVL_ERR,"init_storage_ro_coposite: Support for compositing storage has not been compiled into this program");
    return NULL;
#else
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    struct ro_composite_ctx * ctx = malloc(sizeof(struct ro_composite_ctx));
    char * connection_string_primary;
    char * connection_string_secondary;
    char * tmp;

    log_message(STORE_LOGLVL_DEBUG,"init_storage_ro_composite: initialising compositing storage backend for %s", connection_string);

    if (!store || !ctx) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_composite: failed to allocate memory for context");
        if (store) free(store);
        if (ctx) free(ctx);
        return NULL;
    }

    connection_string_secondary = strstr(connection_string,"}{");
    connection_string_primary = malloc(strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary) + 1);
    memcpy(connection_string_primary,connection_string + strlen("composite:{"), strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary));
    connection_string_primary[strlen(connection_string) - strlen("composite:{") - strlen(connection_string_secondary)] = 0;
    connection_string_secondary = strdup(connection_string_secondary + 2);
    connection_string_secondary[strlen(connection_string_secondary) - 1] = 0;

    log_message(STORE_LOGLVL_DEBUG,"init_storage_ro_composite: Primary storage backend: %s", connection_string_primary);
    log_message(STORE_LOGLVL_DEBUG,"init_storage_ro_composite: Secondary storage backend: %s", connection_string_secondary);

    tmp = strstr(connection_string_primary, ",");
    memcpy(ctx->xmlconfig_primary, connection_string_primary, tmp - connection_string_primary);
    ctx->xmlconfig_primary[tmp - connection_string_primary] = 0;
    ctx->store_primary = init_storage_backend(tmp + 1);
    if (ctx->store_primary == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_composite: failed to initialise primary storage backend");
        free(ctx);
        free(store);
        return NULL;
    }

    tmp = strstr(connection_string_secondary, ",");
    memcpy(ctx->xmlconfig_secondary, connection_string_secondary, tmp - connection_string_secondary);
    ctx->xmlconfig_secondary[tmp - connection_string_secondary] = 0;
    ctx->store_secondary = init_storage_backend(tmp + 1);
    if (ctx->store_secondary == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_composite: failed to initialise secondary storage backend");
        ctx->store_primary->close_storage(ctx->store_primary);
        free(ctx);
        free(store);
        return NULL;
    }

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
#endif
}
