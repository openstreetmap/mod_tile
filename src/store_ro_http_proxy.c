#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#include <curl/easy.h>
#endif

#include "store.h"
#include "store_ro_http_proxy.h"
#include "render_config.h"
#include "protocol.h"


#ifdef HAVE_LIBCURL

static pthread_mutex_t qLock;
static int done_global_init = 0;

struct tile_cache {
    struct stat_info st_stat;
    char * tile;
    int x,y,z;
    char xmlname[XMLCONFIG_MAX];
};

struct ro_http_proxy_ctx {
    CURL * ctx;
    char * baseurl;
    struct tile_cache cache;
};

struct MemoryStruct {
    char *memory;
    size_t size;
};


static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct * chunk = userp;
  if (chunk->memory) {
      chunk->memory = realloc(chunk->memory, chunk->size + realsize);
  } else {
      chunk->memory = malloc(realsize);
  }
  //log_message(STORE_LOGLVL_DEBUG, "ro_http_proxy_tile_read: writing a chunk: Position %i, size %i", chunk->size, realsize);

  memcpy(&(chunk->memory[chunk->size]), contents, realsize);
  chunk->size += realsize;

  return realsize;
}

static char * ro_http_proxy_xyz_to_storagekey(struct storage_backend * store, int x, int y, int z, char * key) {
    snprintf(key,PATH_MAX - 1, "http://%s/%i/%i/%i.png", ((struct ro_http_proxy_ctx *) (store->storage_ctx))->baseurl, z, x, y);
    return key;
}

static int ro_http_proxy_tile_retrieve(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct ro_http_proxy_ctx * ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
    char * path;
    CURLcode res;
    struct MemoryStruct chunk;
    long httpCode;

    //TODO: Deal with options
    if ((ctx->cache.x == x) && (ctx->cache.y == y) && (ctx->cache.z == z) && (strcmp(ctx->cache.xmlname, xmlconfig) == 0)) {
        log_message(STORE_LOGLVL_DEBUG, "ro_http_proxy_tile_fetch: Got a cached tile");
        return 1;
    } else {
        log_message(STORE_LOGLVL_DEBUG, "ro_http_proxy_tile_fetch: Fetching tile");

        chunk.memory = NULL;
        chunk.size = 0;
        path = malloc(PATH_MAX);

        ro_http_proxy_xyz_to_storagekey(store, x, y, z, path);
        log_message(STORE_LOGLVL_DEBUG, "ro_http_proxy_tile_fetch: proxing file %s", path);
        curl_easy_setopt(ctx->ctx, CURLOPT_URL, path);

        curl_easy_setopt(ctx->ctx, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(ctx->ctx, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(ctx->ctx);
        free(path);

        if(res != CURLE_OK) {
            log_message(STORE_LOGLVL_ERR, "ro_http_proxy_tile_fetch: failed to retrieve file: %s", curl_easy_strerror(res));
            ctx->cache.x = -1;  ctx->cache.y = -1;  ctx->cache.z = -1;
            return -1;
        }

        res = curl_easy_getinfo(ctx->ctx, CURLINFO_RESPONSE_CODE, &httpCode );
        if (res != CURLE_OK) {
            log_message(STORE_LOGLVL_ERR, "ro_http_proxy_tile_fetch: failed to retrieve HTTP code: %s", curl_easy_strerror(res));
            ctx->cache.x = -1;  ctx->cache.y = -1;  ctx->cache.z = -1;
            return -1;
        }

        switch (httpCode) {
        case 200: {
            if (ctx->cache.tile != NULL) free(ctx->cache.tile);
            ctx->cache.tile = chunk.memory;
            ctx->cache.st_stat.size = chunk.size;
            ctx->cache.st_stat.expired = 0;
            res = curl_easy_getinfo(ctx->ctx, CURLINFO_FILETIME, &(ctx->cache.st_stat.mtime));
            ctx->cache.st_stat.atime = 0;
            log_message(STORE_LOGLVL_DEBUG, "ro_http_proxy_tile_read: Read file of size %i", chunk.size);
            break;
        }
        case 404: {
            if (ctx->cache.tile != NULL) free(ctx->cache.tile);
            ctx->cache.st_stat.size = -1;
            ctx->cache.st_stat.expired = 0;
            break;
        }
        }


        ctx->cache.x = x;  ctx->cache.y = y;  ctx->cache.z = z;
        strcpy(ctx->cache.xmlname,xmlconfig);
        return 1;
    }
}

static int ro_http_proxy_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {
    struct ro_http_proxy_ctx * ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

    if (ro_http_proxy_tile_retrieve(store, xmlconfig, options, x, y, z) > 0) {
        if (ctx->cache.st_stat.size > sz) {
            log_message(STORE_LOGLVL_ERR, "ro_http_proxy_tile_read: size was too big, overrun %i %i", sz, ctx->cache.st_stat.size);
            return -1;
        }
        memcpy(buf, ctx->cache.tile, ctx->cache.st_stat.size);
        return ctx->cache.st_stat.size;
    } else {
        log_message(STORE_LOGLVL_ERR, "ro_http_proxy_tile_read: Fetching didn't work");
        return -1;
    }
}

static struct stat_info ro_http_proxy_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct stat_info tile_stat;
    struct ro_http_proxy_ctx * ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

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


static char * ro_http_proxy_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {

    return ro_http_proxy_xyz_to_storagekey(store, x, y, z, string);
}

static int ro_http_proxy_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    log_message(STORE_LOGLVL_ERR, "ro_http_proxy_metatile_write: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}


static int ro_http_proxy_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    log_message(STORE_LOGLVL_ERR, "ro_http_proxy_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}

static int ro_http_proxy_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {

    log_message(STORE_LOGLVL_ERR, "ro_http_proxy_metatile_expire: This is a readonly storage backend. Write functionality isn't implemented");
    return -1;
}


static int ro_http_proxy_close_storage(struct storage_backend * store) {
    struct ro_http_proxy_ctx * ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

    free(ctx->baseurl);
    if (ctx->cache.tile) free(ctx->cache.tile);
    curl_easy_cleanup(ctx->ctx);
    free(ctx);
    free(store);

    return 0;
}


#endif //Have curl



struct storage_backend * init_storage_ro_http_proxy(const char * connection_string) {
    
#ifndef HAVE_LIBCURL
    log_message(STORE_LOGLVL_ERR,"init_storage_ro_http_proxy: Support for curl and therefore the http proxy storage has not been compiled into this program");
    return NULL;
#else
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    struct ro_http_proxy_ctx * ctx = malloc(sizeof(struct ro_http_proxy_ctx));
    CURLcode res;

    log_message(STORE_LOGLVL_DEBUG,"init_storage_ro_http_proxy: initialising proxy storage backend for %s", connection_string);

    if (!store || !ctx) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_http_proxy: failed to allocate memory for context");
        if (store) free(store); 
        if (ctx) free(ctx); 
        return NULL;
    }

    ctx->cache.x = -1; ctx->cache.y = -1; ctx->cache.z = -1;
    ctx->cache.tile = NULL;
    ctx->cache.xmlname[0] = 0;

    ctx->baseurl = strdup(&(connection_string[strlen("ro_http_proxy://")]));
    pthread_mutex_lock(&qLock);
    if (!done_global_init) {
        log_message(STORE_LOGLVL_DEBUG,"init_storage_ro_http_proxy: Global init of curl", connection_string);
        res = curl_global_init(CURL_GLOBAL_DEFAULT);
        done_global_init = 1;
    } else {
        res = CURLE_OK;
    }
    pthread_mutex_unlock(&qLock);
    if (res != CURLE_OK) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_http_proxy: failed to initialise global curl: %s", curl_easy_strerror(res));
        free(ctx);
        free(store);
        return NULL;
    }

    ctx->ctx = curl_easy_init();
    if (!ctx->ctx) {
        log_message(STORE_LOGLVL_ERR,"init_storage_ro_http_proxy: failed to initialise curl");
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
#endif
}
