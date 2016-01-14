#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>

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

struct s3_tile_request
{
    const char *path;
    size_t tile_size;
    int stat_only;
    char *tile;
    int64_t tile_mod_time;
    int tile_expired;
    size_t cur_offset;
    S3Status result;
    const S3ErrorDetails *error_details;
};

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

static char* store_s3_xyz_to_storagekey(struct storage_backend *store, int x, int y, int z, char *key)
{
    snprintf(key, PATH_MAX - 1, "/%s/%i/%i/%i.png", ((struct store_s3_ctx*) (store->storage_ctx))->basepath, z, x, y);
    return key;
}

static S3Status store_s3_properties_callback(const S3ResponseProperties *properties, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    log_message(STORE_LOGLVL_DEBUG, "store_s3_properties_callback: got properties for tile, length: %ld, content type: %s", properties->contentLength, properties->contentType);

    rqst->tile_size = properties->contentLength;
    rqst->tile_mod_time = properties->lastModified;
    if (!rqst->stat_only) {
        rqst->tile = malloc(properties->contentLength);
        rqst->cur_offset = 0;
    }
    rqst->tile_expired = 0;
    const S3NameValue *respMetadata = properties->metaData;
    for (int i = 0; i < properties->metaDataCount; i++) {
        if (0 == strcmp(respMetadata[i].name, "expired")) {
            rqst->tile_expired = atoi(respMetadata[i].value);
        }
    }

    return S3StatusOK;
}

S3Status store_s3_object_data_callback(int bufferSize, const char *buffer, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    log_message(STORE_LOGLVL_DEBUG, "store_s3_object_data_callback: appending %ld bytes to buffer, new offset %ld", bufferSize, rqst->cur_offset
            + bufferSize);
    memcpy(rqst->tile + rqst->cur_offset, buffer, bufferSize);
    rqst->cur_offset += bufferSize;
    return S3StatusOK;
}

int store_s3_put_object_data_callback(int bufferSize, char *buffer, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    size_t bytesToWrite = MAX(bufferSize, rqst->tile_size - rqst->cur_offset);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_properties_callback: got properties for tile, writing %ld bytes to buffer, new offset %ld", bytesToWrite, rqst->cur_offset
            + bytesToWrite);
    memcpy(buffer, rqst->tile + rqst->cur_offset, bytesToWrite);
    rqst->cur_offset += bytesToWrite;
    int written = 0;
    if (rqst->cur_offset == rqst->tile_size) {
        // indicate "end of data"
        written = 0;
    } else {
        written = bytesToWrite;
    }
    return written;
}

void store_s3_complete_callback(S3Status status, const S3ErrorDetails *errorDetails, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    log_message(STORE_LOGLVL_DEBUG, "store_s3_complete_callback: request complete, status %d", status);
    if (errorDetails) {
        log_message(STORE_LOGLVL_DEBUG, " error details: %s", errorDetails);
    }
    rqst->result = status;
    rqst->error_details = errorDetails;
}

static int store_s3_tile_retrieve(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = NULL;

    if ((ctx->cache.x == x) && (ctx->cache.y == y) && (ctx->cache.z == z)
            && (strcmp(ctx->cache.xmlname, xmlconfig) == 0)) {
        log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: Got a cached tile");
        return 1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: Fetching tile");

    path = malloc(PATH_MAX);

    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: retrieving object %s", path);

    struct S3GetObjectHandler getObjectHandler;
    getObjectHandler.responseHandler.propertiesCallback =
            &store_s3_properties_callback;
    getObjectHandler.responseHandler.completeCallback =
            &store_s3_complete_callback;
    getObjectHandler.getObjectDataCallback = &store_s3_object_data_callback;

    struct s3_tile_request request;
    request.path = path;
    request.stat_only = 0;

    S3_get_object(ctx->ctx, path, NULL, 0, 0, NULL, &getObjectHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_retrieve: failed to retrieve object: %d/%s", request.result, request.error_details);
        if (ctx->cache.tile) {
            free(ctx->cache.tile);
            ctx->cache.tile = NULL;
        }
        ctx->cache.x = -1;
        ctx->cache.y = -1;
        ctx->cache.z = -1;
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: Read object of size %i", request.tile_size);

    if (ctx->cache.tile) {
        free(ctx->cache.tile);
    }
    ctx->cache.tile = request.tile;
    ctx->cache.st_stat.size = request.tile_size;
    ctx->cache.st_stat.atime = 0;
    ctx->cache.st_stat.mtime = request.tile_mod_time;
    ctx->cache.st_stat.expired = 0;
    ctx->cache.x = x;
    ctx->cache.y = y;
    ctx->cache.z = z;
    strcpy(ctx->cache.xmlname, xmlconfig);
    return 1;
}

static int store_s3_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;

    if (store_s3_tile_retrieve(store, xmlconfig, options, x, y, z) <= 0) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_read: Fetching didn't work");
        return -1;
    }
    if (ctx->cache.st_stat.size > sz) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_read: buffer not big enough for tile (%i < %i)", sz, ctx->cache.st_stat.size);
        return -1;
    }
    memcpy(buf, ctx->cache.tile, ctx->cache.st_stat.size);
    return ctx->cache.st_stat.size;
}

static struct stat_info store_s3_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = NULL;

    if ((ctx->cache.x == x) && (ctx->cache.y == y) && (ctx->cache.z == z)
            && (strcmp(ctx->cache.xmlname, xmlconfig) == 0)) {
        log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: Got a cached tile");
        return ctx->cache.st_stat;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: Fetching tile properties");

    path = malloc(PATH_MAX);

    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: getting properties for object %s", path);

    struct S3ResponseHandler responseHandler;
    responseHandler.propertiesCallback = &store_s3_properties_callback;
    responseHandler.completeCallback = &store_s3_complete_callback;

    struct s3_tile_request request;
    request.path = path;
    request.stat_only = 1;

    S3_head_object(ctx->ctx, path, NULL, &responseHandler, &request);
    free(path);

    struct stat_info tile_stat;
    if (request.result != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_retrieve: failed to retrieve object properties: %d/%s", request.result, request.error_details);
        tile_stat.size = -1;
        tile_stat.expired = 0;
        tile_stat.mtime = 0;
        tile_stat.atime = 0;
        tile_stat.ctime = 0;
        return tile_stat;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: Read properties");

    tile_stat.size = request.tile_size;
    tile_stat.expired = request.tile_expired;
    tile_stat.mtime = request.tile_mod_time;
    tile_stat.atime = 0;
    tile_stat.ctime = 0;
    return tile_stat;
}

static char* store_s3_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char * string)
{
    return store_s3_xyz_to_storagekey(store, x, y, z, string);
}

static int store_s3_tile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: storing object %s", path);

    struct S3PutObjectHandler putObjectHandler;
    putObjectHandler.responseHandler.propertiesCallback =
            &store_s3_properties_callback;
    putObjectHandler.responseHandler.completeCallback =
            &store_s3_complete_callback;
    putObjectHandler.putObjectDataCallback = &store_s3_put_object_data_callback;

    struct s3_tile_request request;
    request.path = path;
    request.tile = buf;
    request.tile_size = sz;
    request.cur_offset = 0;

    S3_put_object(ctx->ctx, path, sz, NULL, NULL, &putObjectHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_write: failed to write object: %d/%s", request.result, request.error_details);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: Wrote object of size %i", request.tile_size);

    return sz;
}

static int store_s3_tile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: deleting object %s", path);

    struct S3ResponseHandler responseHandler;
    responseHandler.propertiesCallback = &store_s3_properties_callback;
    responseHandler.completeCallback = &store_s3_complete_callback;

    struct s3_tile_request request;
    request.path = path;
    request.stat_only = 1;

    S3_delete_object(ctx->ctx, path, NULL, &responseHandler, &request);

    if (request.result != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_delete: failed to delete object: %d/%s", request.result, request.error_details);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_delete: deleted object");

    return 0;
}

static int store_s3_tile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, x, y, z, path);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_expire: expiring object %s", path);

    struct S3ResponseHandler responseHandler;
    responseHandler.propertiesCallback = &store_s3_properties_callback;
    responseHandler.completeCallback = &store_s3_complete_callback;

    struct s3_tile_request request;
    request.path = path;
    request.cur_offset = 0;

    struct S3NameValue expireTag;
    expireTag.name = "expired";
    expireTag.value = "1";

    struct S3PutProperties putProperties;
    putProperties.metaDataCount = 1;
    putProperties.metaData = &expireTag;

    int64_t lastModified;

    S3_copy_object(ctx->ctx, path, ctx->ctx->bucketName, path, &putProperties, &lastModified, 0, NULL, NULL, &responseHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_expire: failed to update object: %d/%s", request.result, request.error_details);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_expire: Updated object metadata");

    return 0;
}

static int store_s3_close_storage(struct storage_backend *store)
{
    struct store_s3_ctx * ctx = (struct store_s3_ctx *) (store->storage_ctx);

    free(ctx->basepath);
    if (ctx->cache.tile)
        free(ctx->cache.tile);
    S3_deinitialize();
    free(ctx);
    free(store);

    return 0;
}

#endif //Have libs3

struct storage_backend* init_storage_s3(const char *connection_string)
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
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3: Global init of libs3", connection_string);
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

    // parse out the context information from the URL: s3://<key id>:<secret key>[@<hostname>]/<bucket>[/<basepath>]
    ctx->ctx = malloc(sizeof(struct S3BucketContext));

    char *fullurl = strdup(connection_string);
    fullurl = &fullurl[5]; // advance past "s3://"
    ctx->ctx->accessKeyId = strsep(&fullurl, ":");
    if (strchr(fullurl, "@")) {
        ctx->ctx->secretAccessKey = strsep(&fullurl, "@");
        ctx->ctx->hostName = strsep(&fullurl, "/");
    }
    else {
        ctx->ctx->secretAccessKey = strsep(&fullurl, "/");
    }
    ctx->ctx->bucketName = strsep(&fullurl, "/");
    ctx->basepath = fullurl;

    store->storage_ctx = ctx;

    store->tile_read = &store_s3_tile_read;
    store->tile_stat = &store_s3_tile_stat;
    store->metatile_write = &store_s3_tile_write;
    store->metatile_delete = &store_s3_tile_delete;
    store->metatile_expire = &store_s3_tile_expire;
    store->tile_storage_id = &store_s3_tile_storage_id;
    store->close_storage = &store_s3_close_storage;

    return store;
#endif
}
