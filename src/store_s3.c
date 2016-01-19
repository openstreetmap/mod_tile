#include "config.h"
#include <ctype.h>
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
#include "store_file_utils.h"
#include "store_s3.h"
#include "metatile.h"
#include "render_config.h"
#include "protocol.h"

#ifdef HAVE_LIBS3

static pthread_mutex_t qLock;
static int store_s3_initialized = 0;

struct s3_tile_request {
    const char *path;
    size_t tile_size;
    char *tile;
    int64_t tile_mod_time;
    int tile_expired;
    size_t cur_offset;
    S3Status result;
    const S3ErrorDetails *error_details;
};

struct store_s3_ctx {
    S3BucketContext* ctx;
    const char *basepath;
};

static int store_s3_xyz_to_storagekey(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *key, size_t keylen)
{
    int offset;
    if (options) {
        offset = xyzo_to_meta(key, keylen, ((struct store_s3_ctx*) (store->storage_ctx))->basepath, xmlconfig, options, x, y, z);
    } else {
        offset = xyz_to_meta(key, keylen, ((struct store_s3_ctx*) (store->storage_ctx))->basepath, xmlconfig, x, y, z);
    }

    return offset;
}

static S3Status store_s3_properties_callback(const S3ResponseProperties *properties, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;

    rqst->tile_size = properties->contentLength;
    rqst->tile_mod_time = properties->lastModified;
    rqst->tile_expired = 0;
    const S3NameValue *respMetadata = properties->metaData;
    for (int i = 0; i < properties->metaDataCount; i++) {
        if (0 == strcmp(respMetadata[i].name, "expired")) {
            rqst->tile_expired = atoi(respMetadata[i].value);
        }
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_properties_callback: got properties for tile %s, length: %ld, content type: %s, expired: %d", rqst->path, rqst->tile_size, properties->contentType, rqst->tile_expired);

    return S3StatusOK;
}

S3Status store_s3_object_data_callback(int bufferSize, const char *buffer, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;

    if (rqst->cur_offset == 0 && rqst->tile == NULL) {
        log_message(STORE_LOGLVL_DEBUG, "store_s3_object_data_callback: allocating %ld byte buffer for tile", rqst->tile_size);
        rqst->tile = malloc(rqst->tile_size);
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_object_data_callback: appending %ld bytes to buffer, new offset %ld", bufferSize, rqst->cur_offset + bufferSize);
    memcpy(rqst->tile + rqst->cur_offset, buffer, bufferSize);
    rqst->cur_offset += bufferSize;
    return S3StatusOK;
}

int store_s3_put_object_data_callback(int bufferSize, char *buffer, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    if (rqst->cur_offset == rqst->tile_size) {
        // indicate "end of data"
        log_message(STORE_LOGLVL_DEBUG, "store_s3_put_object_data_callback: completed put");
        return 0;
    }
    size_t bytesToWrite = MAX(bufferSize, rqst->tile_size - rqst->cur_offset);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_put_object_data_callback: uploading data, writing %ld bytes to buffer, cur offset %ld, new offset %ld", bytesToWrite, rqst->cur_offset, rqst->cur_offset + bytesToWrite);
    memcpy(buffer, rqst->tile + rqst->cur_offset, bytesToWrite);
    rqst->cur_offset += bytesToWrite;
    return bytesToWrite;
}

void store_s3_complete_callback(S3Status status, const S3ErrorDetails *errorDetails, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    log_message(STORE_LOGLVL_DEBUG, "store_s3_complete_callback: request complete, status %d (%s)", status, S3_get_status_name(status));
    if (errorDetails && errorDetails->message && (strlen(errorDetails->message) > 0)) {
        log_message(STORE_LOGLVL_DEBUG, "  error details: %s", errorDetails->message);
    }
    rqst->result = status;
    rqst->error_details = errorDetails;
}

static int store_s3_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: fetching tile");

    int tile_offset = store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: retrieving object %s", path);

    struct S3GetObjectHandler getObjectHandler;
    getObjectHandler.responseHandler.propertiesCallback = &store_s3_properties_callback;
    getObjectHandler.responseHandler.completeCallback = &store_s3_complete_callback;
    getObjectHandler.getObjectDataCallback = &store_s3_object_data_callback;

    struct s3_tile_request request;
    request.path = path;
    request.cur_offset = 0;
    request.tile = NULL;
    request.tile_expired = 0;
    request.tile_mod_time = 0;
    request.tile_size = 0;

    S3_get_object(ctx->ctx, path, NULL, 0, 0, NULL, &getObjectHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_retrieve: failed to retrieve object: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_retrieve: Read object of size %i", request.tile_size);

    // extract tile from metatile

    if (request.tile_size < METATILE_HEADER_LEN) {
        snprintf(log_msg, PATH_MAX - 1, "Meta file %s too small to contain header\n", path);
        free(request.tile);
        return -3;
    }
    struct meta_layout *m = (struct meta_layout*) request.tile;

    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
        if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
            snprintf(log_msg, PATH_MAX - 1, "Meta file %s header magic mismatch\n", path);
            free(request.tile);
            return -4;
        } else {
            *compressed = 1;
        }
    } else {
        *compressed = 0;
    }

    if (m->count != (METATILE * METATILE)) {
        snprintf(log_msg, PATH_MAX - 1, "Meta file %s header bad count %d != %d\n", path, m->count, METATILE * METATILE);
        free(request.tile);
        return -5;
    }

    int buffer_offset = m->index[tile_offset].offset;
    int tile_size = m->index[tile_offset].size;

    if (tile_size > sz) {
        snprintf(log_msg, PATH_MAX - 1, "tile of length %d too big to fit buffer of length %zd\n", tile_size, sz);
        free(request.tile);
        return -6;
    }

    memcpy(buf, request.tile + buffer_offset, tile_size);

    free(request.tile);
    request.tile = NULL;

    return tile_size;
}

static struct stat_info store_s3_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = NULL;

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: Fetching tile properties");

    path = malloc(PATH_MAX);

    store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: getting properties for object %s", path);

    struct S3ResponseHandler responseHandler;
    responseHandler.propertiesCallback = &store_s3_properties_callback;
    responseHandler.completeCallback = &store_s3_complete_callback;

    struct s3_tile_request request;
    request.path = path;
    request.error_details = NULL;
    request.cur_offset = 0;
    request.result = S3StatusOK;
    request.tile = NULL;
    request.tile_expired = 0;
    request.tile_mod_time = 0;
    request.tile_size = 0;

    S3_head_object(ctx->ctx, path, NULL, &responseHandler, &request);
    free(path);

    struct stat_info tile_stat;
    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_stat: failed to retrieve object properties: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        tile_stat.size = -1;
        tile_stat.expired = 0;
        tile_stat.mtime = 0;
        tile_stat.atime = 0;
        tile_stat.ctime = 0;
        return tile_stat;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: Read properties");

    tile_stat.size = request.tile_size;
    tile_stat.expired = request.tile_expired;
    tile_stat.mtime = request.tile_mod_time;
    tile_stat.atime = 0;
    tile_stat.ctime = 0;
    return tile_stat;
}

static char* store_s3_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string)
{
    // FIXME: assumes PATH_MAX for length of provided string
    store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, string, PATH_MAX);
    return string;
}

static int store_s3_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: storing object %s, size %ld", path, sz);

    struct S3PutObjectHandler putObjectHandler;
    putObjectHandler.responseHandler.propertiesCallback = &store_s3_properties_callback;
    putObjectHandler.responseHandler.completeCallback = &store_s3_complete_callback;
    putObjectHandler.putObjectDataCallback = &store_s3_put_object_data_callback;

    struct s3_tile_request request;
    request.path = path;
    request.tile = (char*) buf;
    request.tile_size = sz;
    request.cur_offset = 0;

    S3PutProperties props;
    props.contentType = "application/octet-stream";
    props.cacheControl = NULL;
    props.cannedAcl = S3CannedAclPrivate; // results in no ACL header in POST
    props.contentDispositionFilename = NULL;
    props.contentEncoding = NULL;
    props.expires = -1;
    props.md5 = NULL;
    props.metaData = NULL;
    props.metaDataCount = 0;
    props.useServerSideEncryption = 0;

    S3_put_object(ctx->ctx, path, sz, &props, NULL, &putObjectHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        const char *msg2 = "";
        if (request.error_details) {
            if (request.error_details->message) {
                msg = request.error_details->message;
            }
            if (request.error_details->furtherDetails) {
                msg2 = request.error_details->furtherDetails;
            }
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_write: failed to write object: %d(%s)/%s%s", request.result, S3_get_status_name(request.result), msg, msg2);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: Wrote object of size %i", sz);

    return sz;
}

static int store_s3_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, xmlconfig, NULL, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_write: deleting object %s", path);

    struct S3ResponseHandler responseHandler;
    responseHandler.propertiesCallback = &store_s3_properties_callback;
    responseHandler.completeCallback = &store_s3_complete_callback;

    struct s3_tile_request request;
    request.path = path;

    S3_delete_object(ctx->ctx, path, NULL, &responseHandler, &request);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_delete: failed to delete object: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_delete: deleted object");

    return 0;
}

static int store_s3_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, xmlconfig, NULL, x, y, z, path, PATH_MAX);
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

    S3PutProperties props;
    props.contentType = "application/octet-stream";
    props.cacheControl = NULL;
    props.cannedAcl = S3CannedAclPrivate; // results in no ACL header in POST
    props.contentDispositionFilename = NULL;
    props.contentEncoding = NULL;
    props.expires = -1;
    props.md5 = NULL;
    props.metaDataCount = 1;
    props.metaData = &expireTag;

    int64_t lastModified;

    S3_copy_object(ctx->ctx, path, ctx->ctx->bucketName, path, &props, &lastModified, 0, NULL, NULL, &responseHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_expire: failed to update object: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_expire: Updated object metadata");

    return 0;
}

static int store_s3_close_storage(struct storage_backend *store)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;

    S3_deinitialize();
    free(ctx);
    free(store);
    store_s3_initialized = 0;

    return 0;
}

static char* url_decode(const char *src)
{
    if (NULL == src) {
        return NULL;
    }
    char *dst = (char*) malloc(strlen(src) + 1);
    dst[0] = '\0';
    while (*src) {
        int c = *src;
        if (c == '%' && isxdigit(*(src + 1)) && isxdigit(*(src + 2))) {
            char hexdigit[] =
            { *(src + 1), *(src + 2), '\0' };
            char decodedchar[2];
            sprintf(decodedchar, "%c", (char) strtol(hexdigit, NULL, 16));
            strncat(dst, decodedchar, 1);
            src += 2;
        } else {
            strncat(dst, src, 1);
        }
        src++;
    }
    return dst;
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

    pthread_mutex_lock(&qLock);
    if (!store_s3_initialized) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3: global init of libs3", connection_string);
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

    // advance past "s3://"
    fullurl = &fullurl[5];
    ctx->ctx->accessKeyId = strsep(&fullurl, ":");
    if (strchr(fullurl, '@')) {
        ctx->ctx->secretAccessKey = strsep(&fullurl, "@");
        ctx->ctx->hostName = strsep(&fullurl, "/");
    } else {
        ctx->ctx->secretAccessKey = strsep(&fullurl, "/");
        ctx->ctx->hostName = NULL;
    }
    ctx->ctx->bucketName = strsep(&fullurl, "/");

    ctx->basepath = fullurl;

    ctx->ctx->accessKeyId = url_decode(ctx->ctx->accessKeyId);
    ctx->ctx->secretAccessKey = url_decode(ctx->ctx->secretAccessKey);
    ctx->ctx->hostName = url_decode(ctx->ctx->hostName);
    ctx->ctx->bucketName = url_decode(ctx->ctx->bucketName);
    ctx->ctx->protocol = S3ProtocolHTTPS;
    ctx->ctx->securityToken = NULL;
    ctx->ctx->uriStyle = S3UriStyleVirtualHost;

    ctx->basepath = url_decode(ctx->basepath);

    log_message(STORE_LOGLVL_DEBUG, "init_storage_s3 completed keyid: %s, key: %s, bucket: %s, basepath: %s", ctx->ctx->accessKeyId, ctx->ctx->secretAccessKey, ctx->ctx->bucketName, ctx->basepath);

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
