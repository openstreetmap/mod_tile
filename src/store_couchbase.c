#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#ifdef HAVE_OPENSSL_MD5_H
#include <openssl/md5.h>
#endif

#define COUCHBASE_WRITE_RETRIES 3

#ifdef HAVE_LIBMEMCACHED
#include <libmemcached/memcached.h>
#endif

#include "store.h"
#include "store_memcached.h"
#include "metatile.h"
#include "render_config.h"
#include "protocol.h"

#if defined(HAVE_LIBMEMCACHED) && defined(HAVE_OPENSSL_MD5_H)

struct couchbase_ctx {
    struct storage_backend * hashes;
    struct storage_backend * tiles;
};

struct metahash_layout {
    int count; // METATILE ^ 2
    unsigned char hash_entry[][MD5_DIGEST_LENGTH]; // md5 entries
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

static bool is_md5_in_metahash(const unsigned char *hash, struct metahash_layout *mh) {
    int i;
    for (i=0; i < mh->count; i++) {
        if (memcmp(mh->hash_entry[i], hash, MD5_DIGEST_LENGTH) == 0) {
            return true;
        }
    }
    return false;
}

static void md5_bin(const unsigned char *buf, int length, unsigned char *result) {
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
    for (tile_index = 0; tile_index < mh->count; tile_index++) {
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
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: failed to read meta %s from couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
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

    if (mh->count != METATILE*METATILE) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: %s meta count %d doesn't equal %d", meta_path, mh->count, METATILE*METATILE);
        free(md5_raw);
        free(mh);
        return -1;
    }

    md5 = md5_to_ascii(mh->hash_entry[tile_index]);
    buf_raw = memcached_get(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, &len, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_read: failed to read tile %s (%s) from couchbase %s", meta_path, md5, memcached_last_error_message(ctx->tiles->storage_ctx));
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
    size_t md5_len;
    uint32_t flags;
    memcached_return_t rc;
    char * md5_raw;
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
        free(mh);
        return tile_stat;
    }

    md5_raw = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &md5_len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        if (rc != MEMCACHED_NOTFOUND) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: failed to get meta stat %s from couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        }
        free(mh);
        return tile_stat;
    }

    if (md5_len != (metahash_len + sizeof(struct stat_info))) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: invalid %s meta stat size from couchbase %s, %d != %d", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx), md5_len, metahash_len+sizeof(struct stat_info));
        free(mh);
        free(md5_raw);
        return tile_stat;
    }

    memcpy(mh,md5_raw+sizeof(struct stat_info),metahash_len);

    if (mh->count != METATILE*METATILE) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_stat: %s meta count %d doesn't equal %d", meta_path, mh->count, METATILE*METATILE);
        free(mh);
        free(md5_raw);
        return tile_stat;
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
    struct metahash_layout *mh_dedup;
    int mh_dedup_len = sizeof(struct metahash_layout);
    int mh_dedup_item_len = sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    char meta_path[PATH_MAX];
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
//    char tmp[PATH_MAX];
    struct stat_info tile_stat;
    int sz2 = metahash_len + sizeof(tile_stat);
    char * buf2 = malloc(sz2);
    char * md5;
    char * md5_check;
    size_t md5_check_len;
    uint32_t flags;
    memcached_return_t rc;

    struct metahash_layout *mh_old;
    struct metahash_layout *mh_old_dedup;
    char * md5_old_raw;
    size_t md5_old_len;
    bool delete_old = false;

    if (buf2 == NULL) {
        return -2;
    }

    mh = meta_to_hashes(x,y,buf);
    mh_dedup = (struct metahash_layout *)malloc(mh_dedup_len);
    mh_old_dedup = (struct metahash_layout *)malloc(mh_dedup_len);

    if (mh == NULL || mh_dedup == NULL || mh_old_dedup == NULL) {
        free(buf2);
        if (mh) free(mh);
        if (mh_dedup) free(mh_dedup);
        if (mh_old_dedup) free(mh_old_dedup);
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_write: failed to allocate memory for metahash: %s", meta_path);
        return -2;
    }

    mh_dedup->count = 0;
    mh_old_dedup->count = 0;

    tile_stat.expired = 0;
    tile_stat.size = sz-header_len;
    tile_stat.mtime = time(NULL);
    tile_stat.atime = tile_stat.mtime;
    tile_stat.ctime = tile_stat.mtime;

    memcpy(buf2, &tile_stat, sizeof(tile_stat));
    memcpy(buf2 + sizeof(tile_stat), mh, metahash_len);

//    log_message(STORE_LOGLVL_DEBUG, "Trying to create and write a metatile to %s", couchbase_tile_storage_id(store, xmlconfig, x, y, z, tmp));

    snprintf(meta_path,PATH_MAX - 1, "%s/%d/%d/%d", xmlconfig, x, y, z);

    // metahash old get START
    md5_old_raw = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &md5_old_len, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
        if (rc != MEMCACHED_NOTFOUND) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_write: failed to read old meta %s from couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        }
    } else if (md5_old_len != (metahash_len + sizeof(struct stat_info))) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_write: %s old meta size %d doesn't equal %d", meta_path, md5_old_len, metahash_len + sizeof(struct stat_info));
    } else if (md5_old_raw != NULL) {
        mh_old = (struct metahash_layout *)malloc(metahash_len);
        if (mh_old == NULL) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_write: failed to allocate memory for mh_old: %s", meta_path);
        } else {
            memcpy(mh_old, md5_old_raw + sizeof(struct stat_info), metahash_len);
            if (mh_old->count != METATILE*METATILE) {
                log_message(STORE_LOGLVL_DEBUG,"couchbase_tile_write: %s old meta count %d doesn't equal %d", meta_path, mh_old->count, METATILE*METATILE);
                free(mh_old);
            } else {
                delete_old = true;
            }
        }
    }
    if (md5_old_raw) free(md5_old_raw);
    //metahash old gen END

    int counter = 0;
    do {
        if (counter > 0) sleep(1);
        rc = memcached_set(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), buf2, metahash_len+sizeof(tile_stat), (time_t)0, (uint32_t)0);
        counter++;
    } while (rc != MEMCACHED_SUCCESS && counter < COUCHBASE_WRITE_RETRIES);
    if (rc != MEMCACHED_SUCCESS || counter > 1) {
        if (rc != MEMCACHED_SUCCESS) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed to write meta %s to couchbase %s in %d iterations", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx), counter);
            free(mh);
            free(mh_dedup);
            free(mh_old_dedup);
            if (delete_old) free(mh_old);
            free(buf2);
            return -1;
        } else {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: successfully wrote meta %s to couchbase %s in %d iterations", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx), counter);
        }
    }

//    log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: write meta %s to couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));

    int tile_index;
    for (tile_index = 0; tile_index < mh->count; tile_index++) {
        struct metahash_layout *mh_tmp;

        if (delete_old && (mh_old_dedup->count == 0 || !is_md5_in_metahash(mh_old->hash_entry[tile_index],mh_old_dedup))) {
            // update dedup array for old metahash data
            mh_old_dedup->count++;
            mh_tmp = (struct metahash_layout *)realloc(mh_old_dedup,mh_dedup_len+mh_dedup_item_len*mh_old_dedup->count);
            if (mh_tmp == NULL) {
                log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed to reallocate memory for mh_old_dedup");
                free(mh);
                free(mh_dedup);
                free(mh_old_dedup);
                free(mh_old);
                free(buf2);
                return -2;
            } else {
                mh_old_dedup = mh_tmp;
                memcpy(mh_old_dedup->hash_entry[mh_old_dedup->count-1], mh_old->hash_entry[tile_index], MD5_DIGEST_LENGTH);
            }
        }

        if (mh_dedup->count > 0 && is_md5_in_metahash(mh->hash_entry[tile_index],mh_dedup)) {
            continue;
        }

        mh_dedup->count++;
        mh_tmp = (struct metahash_layout *)realloc(mh_dedup,mh_dedup_len+mh_dedup_item_len*mh_dedup->count);
        if (mh_tmp == NULL) {
            // update dedup array for metahash data
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed to reallocate memory for mh_dedup");
            free(mh);
            free(mh_dedup);
            free(mh_old_dedup);
            if (delete_old) free(mh_old);
            free(buf2);
            return -2;
        } else {
            mh_dedup = mh_tmp;
            memcpy(mh_dedup->hash_entry[mh_dedup->count-1], mh->hash_entry[tile_index], MD5_DIGEST_LENGTH);
        }

        counter = 0;
        int tx = x + (tile_index / METATILE);
        int ty = y + (tile_index % METATILE);
        md5 = md5_to_ascii(mh->hash_entry[tile_index]);

        md5_check = memcached_get(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, &md5_check_len, &flags, &rc);

        // check if tile exists in couchbase and qeuals to new one
        if (rc != MEMCACHED_SUCCESS || m->index[tile_index].size != md5_check_len || memcmp(buf+m->index[tile_index].offset, md5_check, md5_check_len) != 0) {
            do {
                if (counter > 0) sleep(1);
                rc = memcached_set(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, buf+m->index[tile_index].offset, m->index[tile_index].size, (time_t)0, (uint32_t)0);
                counter++;
            } while (rc != MEMCACHED_SUCCESS && counter < COUCHBASE_WRITE_RETRIES);
            if (rc != MEMCACHED_SUCCESS || counter > 1) {
                if (rc != MEMCACHED_SUCCESS) {
                    log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed to write tile %d/%d/%d to couchbase %s in %d iterations", tx, ty, z, memcached_last_error_message(ctx->tiles->storage_ctx), counter);
                } else {
                    log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: successfully wrote tile %d/%d/%d to couchbase %s in %d iterations", tx, ty, z, memcached_last_error_message(ctx->tiles->storage_ctx), counter);
                }
            }
        }
        if (md5_check) free(md5_check);
        free(md5);
    }

    if (delete_old) {
        // delete old tiles
        for (tile_index = 0; tile_index < mh_old_dedup->count; tile_index++) {
            if (!is_md5_in_metahash(mh_old_dedup->hash_entry[tile_index],mh_dedup)) {
                md5 = md5_to_ascii(mh_old_dedup->hash_entry[tile_index]);
                rc = memcached_delete(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, 0);
                if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND) {
                    log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_write: failed to delete old tile %s while updating %s in couchbase %s", md5, meta_path, memcached_last_error_message(ctx->tiles->storage_ctx));
                }
                free(md5);
            }
        }
        free(mh_old);
    }

    free(mh);
    free(mh_dedup);
    free(mh_old_dedup);
    free(buf2);

    memcached_flush_buffers(ctx->hashes->storage_ctx);
    memcached_flush_buffers(ctx->tiles->storage_ctx);
    return sz;
}

static int couchbase_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    struct couchbase_ctx * ctx = (struct couchbase_ctx *)(store->storage_ctx);
    char meta_path[PATH_MAX];
    int metahash_len = sizeof(struct metahash_layout) + METATILE*METATILE*sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    struct metahash_layout *mh = (struct metahash_layout *)malloc(metahash_len);
    int mh_dedup_len = sizeof(struct metahash_layout);
    int mh_dedup_item_len = sizeof(unsigned char)*MD5_DIGEST_LENGTH;
    struct metahash_layout *mh_dedup = (struct metahash_layout *)malloc(mh_dedup_len);
    char * md5;
    char * md5_raw;
    size_t md5_raw_len;
    uint32_t flags;
    memcached_return_t rc;

    couchbase_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

    if (mh == NULL || mh_dedup == NULL) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: failed to allocate memory for metahash: %s", meta_path);
        if (mh) free(mh);
        if (mh_dedup) free(mh_dedup);
        return -2;
    }

    mh_dedup->count = 0;

    md5_raw = memcached_get(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), &md5_raw_len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: failed to read meta %s from couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        free(mh);
        free(mh_dedup);
        return -1;
    }
    if (md5_raw_len != (metahash_len + sizeof(struct stat_info))) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: %s meta size %d doesn't equal %d", meta_path, md5_raw_len, metahash_len + sizeof(struct stat_info));
        free(md5_raw);
        free(mh);
        free(mh_dedup);
        return -1;
    }

    memcpy(mh, md5_raw + sizeof(struct stat_info), metahash_len);

    if (mh->count != METATILE*METATILE) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: %s meta count %d doesn't equal %d", meta_path, mh->count, METATILE*METATILE);
        free(md5_raw);
        free(mh);
        free(mh_dedup);
        return -1;
    }

    int tile_index;
    for (tile_index = 0; tile_index < mh->count; tile_index++) {
        if (mh_dedup->count > 0 && is_md5_in_metahash(mh->hash_entry[tile_index],mh_dedup)) {
            continue;
        }

        struct metahash_layout *mh_tmp;
        mh_dedup->count++;
        mh_tmp = (struct metahash_layout *)realloc(mh_dedup,mh_dedup_len+mh_dedup_item_len*mh_dedup->count);
        if (mh_tmp == NULL) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: failed to reallocate memory for mh_dedup");
            free(mh);
            free(mh_dedup);
            return -2;
        } else {
            mh_dedup = mh_tmp;
            memcpy(mh_dedup->hash_entry[mh_dedup->count-1], mh->hash_entry[tile_index], MD5_DIGEST_LENGTH);
        }

        int tx = x + (tile_index / METATILE);
        int ty = y + (tile_index % METATILE);
        md5 = md5_to_ascii(mh->hash_entry[tile_index]);
        rc = memcached_delete(ctx->tiles->storage_ctx, md5, MD5_DIGEST_LENGTH*2, 0);
        if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND) {
            log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: failed to delete tile %d/%d/%d from couchbase %s", tx, ty, z, memcached_last_error_message(ctx->tiles->storage_ctx));
            free(md5_raw);
            free(md5);
            free(mh);
            free(mh_dedup);
            return -1;
        }
        free(md5);
    }

    rc = memcached_delete(ctx->hashes->storage_ctx, meta_path, strlen(meta_path), 0);

    if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND) {
        log_message(STORE_LOGLVL_DEBUG,"couchbase_metatile_delete: failed to delete meta %s from couchbase %s", meta_path, memcached_last_error_message(ctx->hashes->storage_ctx));
        free(md5_raw);
        free(mh);
        free(mh_dedup);
        return -1;
    }

    free(md5_raw);
    free(mh);
    free(mh_dedup);

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
#endif //Have memcached / openssl_md5

struct storage_backend * init_storage_couchbase(const char * connection_string) {

#if !defined(HAVE_LIBMEMCACHED) || !defined(HAVE_OPENSSL_MD5_H)
    log_message(STORE_LOGLVL_ERR,"init_storage_couchbase: Support for memcached/openssl has not been compiled into this program");
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
