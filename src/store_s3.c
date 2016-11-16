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
    char *urlcopy;
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

    //log_message(STORE_LOGLVL_DEBUG, "store_s3_properties_callback: got properties for tile %s, length: %ld, content type: %s, expired: %d", rqst->path, rqst->tile_size, properties->contentType, rqst->tile_expired);

    return S3StatusOK;
}

S3Status store_s3_object_data_callback(int bufferSize, const char *buffer, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;

    if (rqst->cur_offset == 0 && rqst->tile == NULL) {
        //log_message(STORE_LOGLVL_DEBUG, "store_s3_object_data_callback: allocating %z byte buffer for tile", rqst->tile_size);
        rqst->tile = malloc(rqst->tile_size);
        if (NULL == rqst->tile) {
            log_message(STORE_LOGLVL_ERR, "store_s3_object_data_callback: could not allocate %z byte buffer for tile!", rqst->tile_size);
            return S3StatusOutOfMemory;
        }
    }

    //log_message(STORE_LOGLVL_DEBUG, "store_s3_object_data_callback: appending %ld bytes to buffer, new offset %ld", bufferSize, rqst->cur_offset + bufferSize);
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
    size_t bytesToWrite = MIN(bufferSize, rqst->tile_size - rqst->cur_offset);
    //log_message(STORE_LOGLVL_DEBUG, "store_s3_put_object_data_callback: uploading data, writing %ld bytes to buffer, cur offset %ld, new offset %ld", bytesToWrite, rqst->cur_offset, rqst->cur_offset + bytesToWrite);
    memcpy(buffer, rqst->tile + rqst->cur_offset, bytesToWrite);
    rqst->cur_offset += bytesToWrite;
    return bytesToWrite;
}

void store_s3_complete_callback(S3Status status, const S3ErrorDetails *errorDetails, void *callbackData)
{
    struct s3_tile_request *rqst = (struct s3_tile_request*) callbackData;
    //log_message(STORE_LOGLVL_DEBUG, "store_s3_complete_callback: request complete, status %d (%s)", status, S3_get_status_name(status));
    //if (errorDetails && errorDetails->message && (strlen(errorDetails->message) > 0)) {
    //    log_message(STORE_LOGLVL_DEBUG, "  error details: %s", errorDetails->message);
    //}
    rqst->result = status;
    rqst->error_details = errorDetails;
}

static int store_s3_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int *compressed, char *log_msg)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);

    //log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_read: fetching tile");

    int tile_offset = store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, path, PATH_MAX);
    //log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_read: retrieving object %s", path);

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

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_read: failed to retrieve object: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        free(path);
        path = NULL;
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_read: retrieved metatile %s of size %i", path, request.tile_size);

    free(path);
    path = NULL;

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

    struct stat_info tile_stat;
    tile_stat.size = -1;
    tile_stat.expired = 0;
    tile_stat.mtime = 0;
    tile_stat.atime = 0;
    tile_stat.ctime = 0;

    char *path = malloc(PATH_MAX);
    if (NULL == path) {
        log_message(STORE_LOGLVL_ERR, "store_s3_tile_stat: failed to allocate memory for tile path!");
        return tile_stat;
    }

    store_s3_xyz_to_storagekey(store, xmlconfig, options, x, y, z, path, PATH_MAX);
    //log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: getting properties for object %s", path);

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

    if (request.result != S3StatusOK) {
        if (request.result == S3StatusHttpErrorNotFound) {
            // tile does not exist
            //log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: tile not found in storage");
        } else {
            const char *msg = "";
            if (request.error_details && request.error_details->message) {
                msg = request.error_details->message;
            }
            log_message(STORE_LOGLVL_ERR, "store_s3_tile_stat: failed to retrieve object properties for %s: %d (%s) %s", path, request.result, S3_get_status_name(request.result), msg);
        }
        free(path);
        return tile_stat;
    }

    //log_message(STORE_LOGLVL_DEBUG, "store_s3_tile_stat: successfully read properties of %s", path);

    tile_stat.size = request.tile_size;
    tile_stat.expired = request.tile_expired;
    tile_stat.mtime = request.tile_mod_time;
    free(path);
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
    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_write: storing object %s, size %ld", path, sz);

    struct S3PutObjectHandler putObjectHandler;
    putObjectHandler.responseHandler.propertiesCallback = &store_s3_properties_callback;
    putObjectHandler.responseHandler.completeCallback = &store_s3_complete_callback;
    putObjectHandler.putObjectDataCallback = &store_s3_put_object_data_callback;

    struct s3_tile_request request;
    request.path = path;
    request.tile = (char*) buf;
    request.tile_size = sz;
    request.cur_offset = 0;
    request.tile_expired = 0;
    request.result = S3StatusOK;
    request.error_details = NULL;

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
        log_message(STORE_LOGLVL_ERR, "store_s3_metatile_write: failed to write object: %d(%s)/%s%s", request.result, S3_get_status_name(request.result), msg, msg2);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_write: Wrote object of size %i", sz);

    return sz;
}

static int store_s3_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, xmlconfig, NULL, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_delete: deleting object %s", path);

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

    S3_delete_object(ctx->ctx, path, NULL, &responseHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_metatile_delete: failed to delete object: %d(%s)/%s", request.result, S3_get_status_name(request.result), msg);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_delete: deleted object");

    return 0;
}

static int store_s3_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;
    char *path = malloc(PATH_MAX);
    store_s3_xyz_to_storagekey(store, xmlconfig, NULL, x, y, z, path, PATH_MAX);
    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_expire: expiring object %s", path);

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
    props.useServerSideEncryption = 0;

    int64_t lastModified;

    S3_copy_object(ctx->ctx, path, ctx->ctx->bucketName, path, &props, &lastModified, 0, NULL, NULL, &responseHandler, &request);
    free(path);

    if (request.result != S3StatusOK) {
        const char *msg = "";
        if (request.error_details && request.error_details->message) {
            msg = request.error_details->message;
        }
        log_message(STORE_LOGLVL_ERR, "store_s3_metatile_expire: failed to update object: %d (%s)/%s", request.result, S3_get_status_name(request.result), msg);
        return -1;
    }

    log_message(STORE_LOGLVL_DEBUG, "store_s3_metatile_expire: updated object metadata");

    return 0;
}

static int store_s3_close_storage(struct storage_backend *store)
{
    struct store_s3_ctx *ctx = (struct store_s3_ctx*) store->storage_ctx;

    S3_deinitialize();
    if (NULL != ctx->urlcopy) {
        free(ctx->urlcopy);
        ctx->urlcopy = NULL;
    }
    free(ctx);
    store->storage_ctx = NULL;
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

static const char* env_expand(const char *src)
{
    if (strstr(src, "${") == src && strrchr(src, '}') == (src + strlen(src) - 1)) {
        char tmp[strlen(src) + 1];
        strcpy(tmp, src);
        tmp[strlen(tmp) - 1] = '\0';
        char *val = getenv(tmp + 2);
        if (NULL == val) {
            log_message(STORE_LOGLVL_ERR, "init_storage_s3: environment variable %s not defined when initializing S3 configuration!", tmp + 2);
            return NULL;
        }
        return val;
    }
    return src;
}
#endif //Have libs3

struct storage_backend* init_storage_s3(const char *connection_string)
{
#ifndef HAVE_LIBS3
    log_message(STORE_LOGLVL_ERR,
            "init_storage_s3: Support for libs3 and therefore S3 storage has not been compiled into this program");
    return NULL;
#else
    if (strstr(connection_string, "s3://") != connection_string) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: connection string invalid for S3 storage!");
        return NULL;
    }

    struct storage_backend *store = malloc(sizeof(struct storage_backend));
    struct store_s3_ctx *ctx = malloc(sizeof(struct store_s3_ctx));

    S3Status res = S3StatusErrorUnknown;

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
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3: global init of libs3");
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

    // parse out the context information from the URL:
    //   s3://<key id>:<secret key>[@<hostname>]/<bucket>[@region][/<basepath>]
    struct S3BucketContext *bctx = ctx->ctx = malloc(sizeof(struct S3BucketContext));

    ctx->urlcopy = strdup(connection_string);
    if (NULL == ctx->urlcopy) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: error allocating memory for connection string!");
        free(ctx);
        free(store);
        return NULL;
    }

    // advance past "s3://"
    char *fullurl = &ctx->urlcopy[5];
    bctx->accessKeyId = strsep(&fullurl, ":");
    char *nextSlash = strchr(fullurl, '/');
    char *nextAt = strchr(fullurl, '@');
    if ((nextAt != NULL) && (nextAt < nextSlash)) {
        // there's an S3 host name in the URL
        bctx->secretAccessKey = strsep(&fullurl, "@");
        bctx->hostName = strsep(&fullurl, "/");
        if (bctx->hostName != NULL && strlen(bctx->hostName) <= 0) {
            bctx->hostName = NULL;
        }
    } else {
        bctx->secretAccessKey = strsep(&fullurl, "/");
        bctx->hostName = NULL;
    }

    if (strchr(fullurl, '@')) {
        // there's a region name with the bucket name
        bctx->bucketName = strsep(&fullurl, "@");
        bctx->authRegion = strsep(&fullurl, "/");
    }
    else {
        bctx->bucketName = strsep(&fullurl, "/");
        bctx->authRegion = NULL;
    }

    if (bctx->accessKeyId != NULL && strlen(bctx->accessKeyId) <= 0) {
        bctx->accessKeyId = NULL;
    }
    if (bctx->secretAccessKey != NULL && strlen(bctx->secretAccessKey) <= 0) {
        bctx->secretAccessKey = NULL;
    }
    if (bctx->bucketName != NULL && strlen(bctx->bucketName) <= 0) {
        bctx->bucketName = NULL;
    }

    if (bctx->accessKeyId == NULL) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: S3 access key ID not provided in connection string!");
        free(ctx);
        free(store);
        return NULL;
    }

    if (bctx->secretAccessKey == NULL) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: S3 secret access key not provided in connection string!");
        free(ctx);
        free(store);
        return NULL;
    }

    if (bctx->bucketName == NULL) {
        log_message(STORE_LOGLVL_ERR, "init_storage_s3: S3 bucket name not provided in connection string!");
        free(ctx);
        free(store);
        return NULL;
    }

    ctx->basepath = fullurl;

    bctx->accessKeyId = env_expand(bctx->accessKeyId);
    if (bctx->accessKeyId == NULL) {
        free(ctx);
        free(store);
        return NULL;
    }
    bctx->accessKeyId = url_decode(bctx->accessKeyId);

    bctx->secretAccessKey = env_expand(bctx->secretAccessKey);
    if (bctx->secretAccessKey == NULL) {
        free(ctx);
        free(store);
        return NULL;
    }
    bctx->secretAccessKey = url_decode(bctx->secretAccessKey);

    if (bctx->hostName) {
        bctx->hostName = env_expand(bctx->hostName);
        if (bctx->hostName == NULL) {
            free(ctx);
            free(store);
            return NULL;
        }
        bctx->hostName = url_decode(bctx->hostName);
    }

    bctx->bucketName = env_expand(bctx->bucketName);
    if (bctx->bucketName == NULL) {
        free(ctx);
        free(store);
        return NULL;
    }
    bctx->bucketName = url_decode(bctx->bucketName);

    bctx->protocol = S3ProtocolHTTPS;
    bctx->securityToken = NULL;
    bctx->uriStyle = S3UriStyleVirtualHost;

    ctx->basepath = env_expand(ctx->basepath);
    if (ctx->basepath == NULL) {
        free(ctx);
        free(store);
        return NULL;
    }
    ctx->basepath = url_decode(ctx->basepath);

    if (bctx->hostName && bctx->authRegion) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3 completed keyid: %s, key: %s, host: %s, region: %s, bucket: %s, basepath: %s", ctx->ctx->accessKeyId, ctx->ctx->secretAccessKey, ctx->ctx->hostName, ctx->ctx->authRegion, ctx->ctx->bucketName, ctx->basepath);
    } else if (bctx->hostName) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3 completed keyid: %s, key: %s, host: %s, bucket: %s, basepath: %s", ctx->ctx->accessKeyId, ctx->ctx->secretAccessKey, ctx->ctx->hostName, ctx->ctx->bucketName, ctx->basepath);
    } else if (bctx->authRegion) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3 completed keyid: %s, key: %s, region: %s, bucket: %s, basepath: %s", ctx->ctx->accessKeyId, ctx->ctx->secretAccessKey, ctx->ctx->authRegion, ctx->ctx->bucketName, ctx->basepath);
    } else {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_s3 completed keyid: %s, key: %s, bucket: %s, basepath: %s", ctx->ctx->accessKeyId, ctx->ctx->secretAccessKey, ctx->ctx->bucketName, ctx->basepath);
    }
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
