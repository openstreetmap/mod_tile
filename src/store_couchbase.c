#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <openssl/md5.h>

#ifdef HAVE_LIBMEMCACHED
#include <libmemcached/memcached.h>
#endif

#include "store.h"
#include "store_memcached.h"
#include "metatile.h"
#include "render_config.h"
#include "protocol.h"


#ifdef HAVE_LIBMEMCACHED

struct couchbase_ctx {
    struct storage_backend * hashes;
    struct storage_backend * tiles;
};

static char * md5_to_ascii(const unsigned char hash[MD5_DIGEST_LENGTH]) {
    const char *hex = "0123456789abcdef";
    char *r, result[MD5_DIGEST_LENGTH * 2 + 1];
    int i;

    for (i = 0, r = result; i < MD5_DIGEST_LENGTH; i++) {
        *r++ = hex[hash[i] >> 4];
        *r++ = hex[hash[i] & 0xF];
    }
    *r = '\0';

    return strndup(result, MD5_DIGEST_LENGTH*2);
}

static void md5_bin(const unsigned char *buf, int length, unsigned char * result) {
    MD5_CTX my_md5;

    MD5_Init(&my_md5);
    MD5_Update(&my_md5, buf, (unsigned int)length);
    MD5_Final(result, &my_md5);
}

static struct metahash_layout * meta_to_hashes(int x, int y, const char *buf) {
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    struct metahash_layout *mh = (struct metahash_layout *)malloc(metahash_len);
    struct meta_layout *m = (struct meta_layout *)(buf);

    if (mh == NULL) {
        return NULL;
    }

    mh->count = METATILE * METATILE;

    int tile_index;
    for (tile_index = 0; tile_index < mh->count; tile_index++)
    {
        const size_t tile_offset = m->index[tile_index].offset;
        const size_t tile_size   = m->index[tile_index].size;
        md5_bin((unsigned char *)buf+tile_offset, tile_size, mh->hash_entry[tile_index]);
    }

    return mh;
}

static char * couchbase_xyz_to_storagekey(const char *xmlconfig, int x, int y, int z, char * key) {
    int mask;

    mask = METATILE - 1;
    x &= ~mask;
    y &= ~mask;

    snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d", xmlconfig, x, y, z);

    return key;
}

static int couchbase_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    char meta_path[PATH_MAX];
    uint32_t flags;
    size_t len;
    size_t md5_len;
    memcached_return_t rc;
    char * buf_raw;
    char * md5;
    char * md5_raw;

    int mask = METATILE - 1;
    size_t tile_index = (x & mask) * METATILE + (y & mask);
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    struct metahash_layout *mh = (struct metahash_layout *)malloc(metahash_len);

    couchbase_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

    if (mh == NULL) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: failed to allocate memory for metahash: %s", meta_path);
        return -2;
    }

    md5_raw = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &md5_len, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
        if (rc != MEMCACHED_NOTFOUND) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: failed read meta %s from cocuhbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        }
        free(mh);
        return -1;
    }
    if (md5_len != (metahash_len + sizeof(struct stat_info))) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: %s meta size %d doesn't equal %d", meta_path, md5_len, metahash_len + sizeof(struct stat_info));
        free(md5_raw);
        free(mh);
        return -1;
    }

    memcpy(mh, md5_raw + sizeof(struct stat_info), metahash_len);
    md5 = md5_to_ascii(mh->hash_entry[tile_index]);
    buf_raw = memcached_get(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, &len, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: failed read tile %s from cocuhbase %s", meta_path, memcached_last_error_message(ctx->tiles->storage_ctx));
        free(md5_raw);
        free(md5);
        free(mh);
        return -1;
    }

    *compressed = 0;

    memcpy(buf, buf_raw, len);
    free(md5_raw);
    free(md5);
    free(buf_raw);
    free(mh);
    return len;
}

static struct stat_info couchbase_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    struct stat_info tile_stat;
    char meta_path[PATH_MAX];
    size_t len;
    size_t md5_len;
    uint32_t flags;
    memcached_return_t rc;
    char * md5;
    char * md5_raw;
    char * buf_raw;
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    struct metahash_layout *mh = (struct metahash_layout *)malloc(metahash_len);

    couchbase_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

    tile_stat.size = -1;
    tile_stat.expired = 0;
    tile_stat.mtime = 0;
    tile_stat.atime = 0;
    tile_stat.ctime = 0;

    if (mh == NULL) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: failed to allocate memory for metahash: %s", meta_path);
        return tile_stat;
    }

    md5_raw = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &md5_len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        if (rc != MEMCACHED_NOTFOUND) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: failed to get meta stat %s from cocuhbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        }
        free(mh);
        return tile_stat;
    }

    if (md5_len != (metahash_len + sizeof(struct stat_info))) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: failed get meta stat %s from cocuhbase %s, %d != %d", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx), md5_len, metahash_len+ sizeof(struct stat_info));
        free(mh);
        free(md5_raw);
        return tile_stat;
    }

    int tile_index;
    for (tile_index = 0; tile_index < mh->count; tile_index++)
    {
        int tx = x + (tile_index / METATILE);
        int ty = y + (tile_index % METATILE);
        md5 = md5_to_ascii(mh->hash_entry[tile_index]);
        buf_raw = memcached_get(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, &len, &flags, &rc);
        if (rc != MEMCACHED_SUCCESS) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: failed read tile %d/%d/%d from cocuhbase %s", tx, ty, z, memcached_last_error_message(ctx->tiles->storage_ctx));
            free(md5_raw);
            free(buf_raw);
            free(md5);
            free(mh);
            return tile_stat;
        }
        free(buf_raw);
        free(md5);
    }

    memcpy(&tile_stat,md5_raw, sizeof(struct stat_info));

    free(mh);
    free(md5_raw);
    return tile_stat;
}


static char * couchbase_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {
    snprintf(string,PATH_MAX - 1, "couchbase:///%s/%d/%d/%d", xmlconfig, x, y, z);
    return string;
}

static int couchbase_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    struct meta_layout *m = (struct meta_layout *)(buf);
    struct metahash_layout *mh;
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    char meta_path[PATH_MAX];
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
//    char tmp[PATH_MAX];
    struct stat_info tile_stat;
    int sz2 = metahash_len + sizeof(tile_stat);
    char * buf2 = malloc(sz2);
    char * md5;
    memcached_return_t rc;

    if (buf2 == NULL) {
        return -2;
    }

    mh = meta_to_hashes(x,y,buf);

    if (mh == NULL) {
        free(buf2);
        return -2;
    }

    tile_stat.expired = 0;
    tile_stat.size = sz-header_len;
    tile_stat.mtime = time(NULL);
    tile_stat.atime = tile_stat.mtime;
    tile_stat.ctime = tile_stat.mtime;

    memcpy(buf2, &tile_stat, sizeof(tile_stat));
    memcpy(buf2 + sizeof(tile_stat), mh, metahash_len);

//    log_message(STORE_LOGLVL_DEBUG, "Trying to create and write a metatile to %s", couchbase_tile_storage_id(store, xmlconfig, x, y, z, tmp));
 
    snprintf(meta_path,PATH_MAX - 1, "%s/%d/%d/%d", xmlconfig, x, y, z);

    rc = memcached_set(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), buf2, metahash_len+sizeof(tile_stat), (time_t)0, (uint32_t)0);
    if (rc != MEMCACHED_SUCCESS) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed write meta %s to cocuhbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        free(mh);
        free(buf2);
        return -1;
    }

//    log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: write meta %s to cocuhbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));

    int tile_index;
    for (tile_index = 0; tile_index < mh->count; tile_index++)
    {
        int counter = 0;
        int tx = x + (tile_index / METATILE);
        int ty = y + (tile_index % METATILE);
        md5 = md5_to_ascii(mh->hash_entry[tile_index]);

        do {
            if (counter > 0) sleep(1);
            rc = memcached_set(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, buf+m->index[tile_index].offset, m->index[tile_index].size, (time_t)0, (uint32_t)0);
            counter++;
        } while (rc != MEMCACHED_SUCCESS && counter < 3);

        if (rc != MEMCACHED_SUCCESS || counter > 1) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed write tile %d/%d/%d to cocuhbase %s in %d iterations", tx, ty, z, memcached_last_error_message(ctx->tiles->storage_ctx), counter);
        }

        free(md5);
    }

    free(mh);
    free(buf2);

    memcached_flush_buffers(ctx->hashes->storage_ctx);
    memcached_flush_buffers(ctx->tiles->storage_ctx);
    return sz;
}

static int couchbase_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    char meta_path[PATH_MAX];
    memcached_return_t rc;

    couchbase_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

    rc = memcached_delete(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), 0);

    if (rc != MEMCACHED_SUCCESS) {
        return -1;
    }

    return 0;
}

static int couchbase_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    char meta_path[PATH_MAX];
    char * buf;
    size_t len;
    uint32_t flags;
    uint64_t cas;
    memcached_return_t rc;

    couchbase_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);
    buf = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
        return -1;
    }

    //cas = memcached_result_cas(&rc);

    ((struct stat_info *)buf)->expired = 1;

    rc = memcached_cas(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), buf, len, 0, flags, cas);

    if (rc != MEMCACHED_SUCCESS) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

static int couchbase_close_storage(struct storage_backend * store) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);

    ctx->hashes->close_storage(ctx->hashes);
    ctx->tiles->close_storage(ctx->tiles);

    free(ctx);
    free(store);
    return 0;
}
#endif //Have memcached

struct storage_backend * init_storage_couchbase(const char * connection_string) {
    
#ifndef HAVE_LIBMEMCACHED
    log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: Support for memcached has not been compiled into this program");
    return NULL;
#else
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    struct couchbase_ctx * ctx = malloc(sizeof(struct couchbase_ctx));
    char * connection_string_hashes;
    char * connection_string_tiles;
    int len;

    log_message(STORE_LOGLVL_DEBUG,"init_storage_couchbase: initialising couchbase storage backend for %s", connection_string);

    if (!store || !ctx) {
        log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: failed to allocate memory for context");
        if (store) free(store);
        if (ctx) free(ctx);
        return NULL;
    }

    connection_string_tiles = strstr(connection_string,",");
    if (connection_string_tiles == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: failed to parse configuration string");
        free(ctx);
        free(store);
        return NULL;
    }

    len = strlen(connection_string) - strlen("couchbase:{") - strlen(connection_string_tiles);
    connection_string_hashes = malloc(len + 1);
    memcpy(connection_string_hashes,connection_string + strlen("couchbase:{"), len);
    connection_string_hashes[len] = 0;
    connection_string_tiles = strdup(connection_string_tiles + 1);
    connection_string_tiles[strlen(connection_string_tiles) - 1] = 0;

    log_message(STORE_LOGLVL_DEBUG,"init_storage_couchbase: Hashes memcached storage backend: %s", connection_string_hashes);
    log_message(STORE_LOGLVL_DEBUG,"init_storage_couchbase: Tiles memcached storage backend: %s", connection_string_tiles);

    if (strstr(connection_string_hashes,"memcached://") == NULL || strstr(connection_string_tiles,"memcached://") == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: failed to parse configuration string");
        free(connection_string_hashes);
        free(connection_string_tiles);
        free(ctx);
        free(store);
        return NULL;
    }

    ctx->hashes = init_storage_memcached(connection_string_hashes);
    if (ctx->hashes == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: failed to initialise hashes storage backend");
        free(connection_string_hashes);
        free(connection_string_tiles);
        free(ctx);
        free(store);
        return NULL;
    }

    ctx->tiles = init_storage_memcached(connection_string_tiles);
    if (ctx->tiles == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: failed to initialise tiles storage backend");
        ctx->hashes->close_storage(ctx->hashes);
        free(connection_string_hashes);
        free(connection_string_tiles);
        free(ctx);
        free(store);
        return NULL;
    }

    store->storage_ctx = ctx;

    store->tile_read = &couchbase_tile_read;
    store->tile_stat = &couchbase_tile_stat;
    store->metatile_write = &couchbase_metatile_write;
    store->metatile_delete = &couchbase_metatile_delete;
    store->metatile_expire = &couchbase_metatile_expire;
    store->tile_storage_id = &couchbase_tile_storage_id;
    store->close_storage = &couchbase_close_storage;

    free(connection_string_hashes);
    free(connection_string_tiles);

    return store;
#endif
}
