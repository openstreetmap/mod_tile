#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>

#define HAVE_LIBS3 1

#ifdef HAVE_LIBS3
#include <libs3.h>
#endif

#include "store.h"
#include "store_s3.h"
#include "render_config.h"
#include "protocol.h"

#ifdef HAVE_LIBS3

static pthread_mutex_t qLock;
static int store_s3_initialized = 0;

struct tile_cache
{
    struct stat_info st_stat;
    char *tile;
    int x, y, z;
    char xmlname[XMLCONFIG_MAX];
};

struct store_s3_ctx
{
    S3BucketContext* ctx;
    const char *basepath;
    struct tile_cache cache;
};

struct MemoryStruct
{
    char *memory;
    size_t size;
};

static char* store_s3_xyz_to_storagekey(struct storage_backend *store, int x, int y, int z, char *key)
{
    snprintf(key, PATH_MAX - 1, "/%s/%i/%i/%i.png", ((struct store_s3_ctx *) (store->storage_ctx))->basepath, z, x, y);
    return key;
}

static int store_s3_tile_retrieve(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z)
{
    struct store_s3_ctx * ctx = (struct store_s3_ctx *) (store->storage_ctx);
    char * path;

    if ((ctx->cache.x == x) && (ctx->cache.y == y) && (ctx->cache.z == z)
            && (strcmp(ctx->cache.xmlname, xmlconfig) == 0)) {
        log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_fetch: Got a cached tile");
        return 1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_fetch: Fetching tile");

    path = malloc(PATH_MAX);

    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_fetch: retrieving file %s", path);

    S3Status res = S3_get_object(ctx->ctx, path, NULL, 0, 0, NULL, handler, NULL);
    free(path);

    if (res != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_fetch: failed to retrieve file: %s", S3_get_status_name(res));
        ctx->cache.x = -1;
        ctx->cache.y = -1;
        ctx->cache.z = -1;
        return -1;
    }

    if (ctx->cache.tile != NULL)
        free(ctx->cache.tile);
    ctx->cache.tile = chunk.memory;
    ctx->cache.st_stat.size = chunk.size;
    ctx->cache.st_stat.expired = 0;
    res =
            curl_easy_getinfo(ctx->ctx, CURLINFO_FILETIME, &(ctx->cache.st_stat.mtime));
    ctx->cache.st_stat.atime = 0;
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_read: Read file of size %i", chunk.size);
    break;

    ctx->cache.x = x;
    ctx->cache.y = y;
    ctx->cache.z = z;
    strcpy(ctx->cache.xmlname, xmlconfig);
    return 1;
}

static int store_s3_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg)
{
    struct store_s3_ctx * ctx = (struct store_s3_ctx *) (store->storage_ctx);

    if (store_s3_tile_retrieve(store, xmlconfig, options, x, y, z) > 0) {
        if (ctx->cache.st_stat.size > sz) {
            log_message(STORE_LOGLVL_ERR, "store_s3_tile_read: size was too big, overrun %i %i", sz, ctx->cache.st_stat.size);
            return -1;
        }
        memcpy(buf, ctx->cache.tile, ctx->cache.st_stat.size);
        return ctx->cache.st_stat.size;
    } else {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_read: Fetching didn't work");
        return -1;
    }
}

static struct stat_info store_s3_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z)
{
    struct stat_info tile_stat;
    struct store_s3_ctx * ctx = (struct store_s3_ctx *) (store->storage_ctx);

    if (store_s3_tile_retrieve(store, xmlconfig, options, x, y, z) > 0) {
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

static char * store_s3_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string)
{

    return store_s3_xyz_to_storagekey(store, x, y, z, string);
}

static int store_s3_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
    log_message(STORE_LOGLVL_ERR, "store_s3_metatile_write: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}

static int store_s3_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z)
{
    log_message(STORE_LOGLVL_ERR, "store_s3_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}

static int store_s3_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{

    log_message(STORE_LOGLVL_ERR, "store_s3_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}

static int store_s3_close_storage(struct storage_backend *store)
{
    struct store_s3_ctx * ctx = (struct store_s3_ctx *) (store->storage_ctx);

    free(ctx->baseurl);
    if (ctx->cache.tile)
        free(ctx->cache.tile);
    S3_deinitialize();
    free(ctx);
    free(store);

    return 0;
}

#endif //Have curl

struct storage_backend * init_storage_s3(const char *connection_string)
{
#ifndef HAVE_LIBS3
    log_message(STORE_LOGLVL_ERR,
            "init_storage_s3: Support for libs3 and therefore S3 storage has not been compiled into this program");
    return NULL;
#else
    struct storage_backend *store = malloc(sizeof(struct storage_backend));
    struct store_s3_ctx *ctx = malloc(sizeof(struct store_s3_ctx));

    S3Status res;

    log_message(STORE_LOGLVL_DEBUG, "init_storage_s3: initializing S3 storage backend for %s", connection_string);

    if (!store || !ctx) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: failed to allocate memory for context");
        if (store)
            free(store);
        if (ctx)
            free(ctx);
        return NULL;
    }

    ctx->cache.x = -1;
    ctx->cache.y = -1;
    ctx->cache.z = -1;
    ctx->cache.tile = NULL;
    ctx->cache.xmlname[0] = 0;

    pthread_mutex_lock(&qLock);
    if (!store_s3_initialized) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3: Global init of curl", connection_string);
        res = S3_initialize(NULL, S3_INIT_ALL, NULL);
        store_s3_initialized = 1;
    } else {
        res = S3StatusOK;
    }
    pthread_mutex_unlock(&qLock);
    if (res != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: failed to initialize S3 library: %s", S3_get_status_name(res));
        free(ctx);
        free(store);
        return NULL;
    }

    // parse out the context information from the URL: s3://<key id>:<secret key>@<hostname>/<bucket>[/<basepath>]
    ctx->ctx = malloc(sizeof(struct S3BucketContext));

    char *fullurl = strdup(connection_string);
    fullurl = &fullurl[5]; // advance past "s3://"
    ctx->ctx->accessKeyId = strsep(&fullurl, ":");
    ctx->ctx->secretAccessKey = strsep(&fullurl, "@");
    ctx->ctx->hostName = strsep(&fullurl, "/");
    ctx->ctx->bucketName = strsep(&fullurl, "/");
    ctx->basepath = fullurl;

    store->storage_ctx = ctx;

    store->tile_read = &store_s3_tile_read;
    store->tile_stat = &store_s3_tile_stat;
    store->metatile_write = &store_s3_metatile_write;
    store->metatile_delete = &store_s3_metatile_delete;
    store->metatile_expire = &store_s3_metatile_expire;
    store->tile_storage_id = &store_s3_tile_storage_id;
    store->close_storage = &store_s3_close_storage;

    return store;
#endif
}
