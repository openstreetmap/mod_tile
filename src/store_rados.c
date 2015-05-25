/* Meta-tile optimised file storage
 *
 * Instead of storing each individual tile as a file,
 * bundle the 8x8 meta tile into a special meta-file.
 * This reduces the Inode usage and more efficient
 * utilisation of disk space.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#ifdef HAVE_LIBRADOS
#include <rados/librados.h>
#endif

#include "store.h"
#include "store_rados.h"
#include "metatile.h"
#include "render_config.h"
#include "protocol.h"


#ifdef HAVE_LIBRADOS

static pthread_mutex_t qLock;

struct metadata_cache {
    char * data;
    int x,y,z;
    char xmlname[XMLCONFIG_MAX];
};

struct rados_ctx {
    char * pool;
    rados_t cluster;
    rados_ioctx_t io;
    struct metadata_cache metadata_cache;
};

static char * rados_xyzo_to_storagekey(const char *xmlconfig, const char *options, int x, int y, int z, char * key) {
    int mask;

    mask = METATILE - 1;
    x &= ~mask;
    y &= ~mask;

    if (strlen(options)) {
        snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.%s.meta", xmlconfig, z, x, y, options);
    } else {
        snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, z, x, y);
    }

    return key;
}

static char * read_meta_data(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    int mask;
    int err;
    char meta_path[PATH_MAX];
    struct rados_ctx * ctx = (struct rados_ctx *)store->storage_ctx;
    unsigned int header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);

    mask = METATILE - 1;
    x &= ~mask;
    y &= ~mask;

    if ((ctx->metadata_cache.x == x) && (ctx->metadata_cache.y == y) && (ctx->metadata_cache.z == z) && (strcmp(ctx->metadata_cache.xmlname, xmlconfig) == 0)) {
        //log_message(STORE_LOGLVL_DEBUG, "Returning cached data for %s %i %i %i", ctx->metadata_cache.xmlname, ctx->metadata_cache.x, ctx->metadata_cache.y, ctx->metadata_cache.z);
        return ctx->metadata_cache.data;
    } else {
        //log_message(STORE_LOGLVL_DEBUG, "Retrieving fresh metadata");
        rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
        err = rados_read(ctx->io, meta_path, ctx->metadata_cache.data, header_len, 0);

        if (err < 0) {
            if (-err == ENOENT) {
                log_message(STORE_LOGLVL_DEBUG, "cannot read data from rados pool %s: %s\n", ctx->pool, strerror(-err));
            } else {
                log_message(STORE_LOGLVL_ERR, "cannot read data from rados pool %s: %s\n", ctx->pool, strerror(-err));
            }
            ctx->metadata_cache.x = -1;
            ctx->metadata_cache.y = -1;
            ctx->metadata_cache.z = -1;
            return NULL;
        }
        ctx->metadata_cache.x = x;
        ctx->metadata_cache.y = y;
        ctx->metadata_cache.z = z;
        strncpy(ctx->metadata_cache.xmlname, xmlconfig, XMLCONFIG_MAX - 1);
        return ctx->metadata_cache.data;
    }
}


static int rados_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {

    char meta_path[PATH_MAX];
    int meta_offset;
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
    struct meta_layout *m = (struct meta_layout *)malloc(header_len);
    size_t file_offset, tile_size;
    int mask;
    int err;
    char * buf_raw;

    mask = METATILE - 1;
    meta_offset = (x & mask) * METATILE + (y & mask);

    rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);

    buf_raw = read_meta_data(store, xmlconfig, options, x, y, z);
    if (buf_raw == NULL) {
        snprintf(log_msg,1024, "Failed to read metadata of tile\n");
        free(m);
        return -3;
    }

    memcpy(m, buf_raw + sizeof(struct stat_info), header_len);

    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
        if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
            snprintf(log_msg,1024, "Meta file header magic mismatch\n");
            free(m);
            return -4;
        } else {
            *compressed = 1;
        }
    } else *compressed = 0;

    // Currently this code only works with fixed metatile sizes (due to xyz_to_meta above)
    if (m->count != (METATILE * METATILE)) {
        snprintf(log_msg, 1024, "Meta file header bad count %d != %d\n", m->count, METATILE * METATILE);
        free(m);
        return -5;
    }

    file_offset = m->index[meta_offset].offset + sizeof(struct stat_info);
    tile_size   = m->index[meta_offset].size;

    free(m);

    if (tile_size > sz) {
        snprintf(log_msg, 1024, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
        tile_size = sz;
        return -6;
    }

    err = rados_read(((struct rados_ctx *)store->storage_ctx)->io, meta_path, buf, tile_size, file_offset);

    if (err < 0) {
        snprintf(log_msg, 1024, "Failed to read tile data from rados %s offset: %li length: %li: %s\n", meta_path, file_offset, tile_size, strerror(-err));
        return -1;
    }

    return tile_size;
}

static struct stat_info rados_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct stat_info tile_stat;
    char * buf;
    int offset, mask;

    mask = METATILE - 1;
    offset = (x & mask) * METATILE + (y & mask);

    buf = read_meta_data(store, xmlconfig, options, x, y, z);
    if (buf == NULL) {
        tile_stat.size = -1;
        tile_stat.expired = 0;
        tile_stat.mtime = 0;
        tile_stat.atime = 0;
        tile_stat.ctime = 0;
        return tile_stat;
    }

    memcpy(&tile_stat,buf, sizeof(struct stat_info));
    tile_stat.size = ((struct meta_layout *) (buf + sizeof(struct stat_info)))->index[offset].size;

    return tile_stat;
}


static char * rados_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {
    char meta_path[PATH_MAX];

    rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
    snprintf(string,PATH_MAX - 1, "rados://%s/%s", ((struct rados_ctx *) (store->storage_ctx))->pool, meta_path);
    return string;
}

static int rados_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    char meta_path[PATH_MAX];
    char tmp[PATH_MAX];
    struct stat_info tile_stat;
    int sz2 = sz + sizeof(struct stat_info);
    char * buf2 = malloc(sz2);
    int err;

    tile_stat.expired = 0;
    tile_stat.size = sz;
    tile_stat.mtime = time(NULL);
    tile_stat.atime = tile_stat.mtime;
    tile_stat.ctime = tile_stat.mtime;

    memcpy(buf2, &tile_stat, sizeof(tile_stat));
    memcpy(buf2 + sizeof(tile_stat), buf, sz);

    rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
    log_message(STORE_LOGLVL_DEBUG, "Trying to create and write a tile to %s\n", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));

    err = rados_write_full(((struct rados_ctx *)store->storage_ctx)->io, meta_path, buf2, sz2);
    if (err < 0) {
        log_message(STORE_LOGLVL_ERR, "cannot write %s: %s\n", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
        free(buf2);
        return -1;
    }
    free(buf2);

    return sz;
}


static int rados_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    struct rados_ctx * ctx = (struct rados_ctx *)store->storage_ctx;
    char meta_path[PATH_MAX];
    char tmp[PATH_MAX];
    int err;

    //TODO: deal with options
    const char *options = "";
    rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);

    err =  rados_remove(ctx->io, meta_path);

    if (err < 0) {
        log_message(STORE_LOGLVL_ERR, "failed to delete %s: %s\n", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
        return -1;
    }

    return 0;
}

static int rados_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {

    struct stat_info tile_stat;
    struct rados_ctx * ctx = (struct rados_ctx *)store->storage_ctx;
    char meta_path[PATH_MAX];
    char tmp[PATH_MAX];
    int err;

    //TODO: deal with options
    const char *options = "";
    rados_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
    err = rados_read(ctx->io, meta_path, (char *)&tile_stat, sizeof(struct stat_info), 0);

    if (err < 0) {
        if (-err == ENOENT) {
            log_message(STORE_LOGLVL_DEBUG, "Tile %s does not exist, can't expire", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));
            return -1;
        } else {
            log_message(STORE_LOGLVL_ERR, "Failed to read tile metadata for %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
        }
        return -2;
    }

    tile_stat.expired = 1;

    err = rados_write(ctx->io, meta_path, (char *)&tile_stat, sizeof(struct stat_info), 0);

    if (err < 0) {
        log_message(STORE_LOGLVL_ERR, "failed to write expiry data for %s: %s", rados_tile_storage_id(store, xmlconfig, options, x, y, z, tmp), strerror(-err));
        return -3;
    }

    return 0;
}


static int rados_close_storage(struct storage_backend * store) {
    struct rados_ctx * ctx = (struct rados_ctx *)store->storage_ctx;

    rados_ioctx_destroy(ctx->io);
    rados_shutdown(ctx->cluster);
    log_message(STORE_LOGLVL_DEBUG,"rados_close_storage: Closed rados backend");
    free(ctx->metadata_cache.data);
    free(ctx->pool);
    free(ctx);
    return 0;
}


#endif //Have rados



struct storage_backend * init_storage_rados(const char * connection_string) {
    
#ifndef HAVE_LIBRADOS
    log_message(STORE_LOGLVL_ERR,"init_storage_rados: Support for rados has not been compiled into this program");
    return NULL;
#else
    struct rados_ctx * ctx = malloc(sizeof(struct rados_ctx));
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    char * conf = NULL;
    const char * tmp;
    int err;
    int i;

    if (ctx == NULL) {
        return NULL;
    }

    tmp = &(connection_string[strlen("rados://")]);
    i = 0;
    while ((tmp[i] != '/') && (tmp[i] != 0)) i++;
    ctx->pool = calloc(i + 1, sizeof(char));
    memcpy(ctx->pool, tmp, i*sizeof(char));
    conf = strdup(&(tmp[i]));

    err = rados_create(&(ctx->cluster), NULL);
    if (err < 0) {
        log_message(STORE_LOGLVL_ERR,"init_storage_rados: cannot create a cluster handle: %s", strerror(-err));
        free(ctx);
        free(store);
        return NULL;
    }

    err = rados_conf_read_file(ctx->cluster, conf);
    if (err < 0) {
        log_message(STORE_LOGLVL_ERR,"init_storage_rados: failed to read rados config file %s: %s", conf, strerror(-err));
        free(ctx);
        free(store);
        return NULL;
    }
    pthread_mutex_lock(&qLock);
    err = rados_connect(ctx->cluster);
    pthread_mutex_unlock(&qLock);
    if (err < 0) {
        log_message(STORE_LOGLVL_ERR,"init_storage_rados: failed to connect to rados cluster: %s", strerror(-err));
        free(ctx);
        free(store);
        return NULL;
    }

    err = rados_ioctx_create(ctx->cluster, ctx->pool, &(ctx->io));
    if (err < 0) {
        log_message(STORE_LOGLVL_ERR,"init_storage_rados: failed to initialise rados io context to pool %s: %s", ctx->pool, strerror(-err));
        rados_shutdown(ctx->cluster);
        free(ctx);
        free(store);
        return NULL;
    }

    log_message(STORE_LOGLVL_DEBUG,"init_storage_rados: Initialised rados backend for pool %s with config %s", ctx->pool, conf);

    ctx->metadata_cache.data = malloc(sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry));
    if (ctx->metadata_cache.data == NULL) {
        rados_ioctx_destroy(ctx->io);
        rados_shutdown(ctx->cluster);
        free(ctx);
        free(store);
        return NULL;
    }

    free(conf);

    ctx->metadata_cache.x = -1;
    ctx->metadata_cache.y = -1;
    ctx->metadata_cache.z = -1;
    ctx->metadata_cache.xmlname[0] = 0;


    store->storage_ctx = ctx;

    store->tile_read = &rados_tile_read;
    store->tile_stat = &rados_tile_stat;
    store->metatile_write = &rados_metatile_write;
    store->metatile_delete = &rados_metatile_delete;
    store->metatile_expire = &rados_metatile_expire;
    store->tile_storage_id = &rados_tile_storage_id;
    store->close_storage = &rados_close_storage;

    return store;
#endif
}
