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

#ifdef HAVE_LIBMEMCACHED
#include <libmemcached/memcached.h>
#endif

#include "store.h"
#include "metatile.h"
#include "render_config.h"
#include "protocol.h"


#ifdef HAVE_LIBMEMCACHED
static char * memcached_xyzo_to_storagekey(const char *xmlconfig, const char *options, int x, int y, int z, char * key) {
    int mask;

    mask = METATILE - 1;
    x &= ~mask;
    y &= ~mask;

    if (strlen(options)) {
        snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.%s.meta", xmlconfig, x, y, z, options);
    } else {
        snprintf(key, PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, x, y, z);
    }

    return key;
}

static char * memcached_xyz_to_storagekey(const char *xmlconfig, int x, int y, int z, char * key) {
    return memcached_xyzo_to_storagekey(xmlconfig, "", x, y, z, key);
}

static int memcached_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {

    char meta_path[PATH_MAX];
    int meta_offset;
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
    struct meta_layout *m = (struct meta_layout *)malloc(header_len);
    size_t file_offset, tile_size;
    int mask;
    uint32_t flags;
    size_t len;
    memcached_return_t rc;
    char * buf_raw;

    mask = METATILE - 1;
    meta_offset = (x & mask) * METATILE + (y & mask);

    memcached_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
    buf_raw = memcached_get(store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        free(m);
        return -1;
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

    memcpy(buf, buf_raw + file_offset, tile_size);
    free(buf_raw);
    return tile_size;
}

static struct stat_info memcached_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct stat_info tile_stat;
    char meta_path[PATH_MAX];
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
    struct meta_layout *m = (struct meta_layout *)malloc(header_len);
    char * buf;
    size_t len;
    uint32_t flags;
    memcached_return_t rc;
    int offset, mask;

    mask = METATILE - 1;
    offset = (x & mask) * METATILE + (y & mask);

    memcached_xyzo_to_storagekey(xmlconfig, options, x, y, z, meta_path);
    buf = memcached_get(store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        tile_stat.size = -1;
        tile_stat.expired = 0;
        tile_stat.mtime = 0;
        tile_stat.atime = 0;
        tile_stat.ctime = 0;
        free(m);
        return tile_stat;
    }

    memcpy(&tile_stat,buf, sizeof(struct stat_info));
    memcpy(m, buf + sizeof(struct stat_info), header_len);
    tile_stat.size = m->index[offset].size;

    free(m);
    free(buf);
    return tile_stat;
}


static char * memcached_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {

    snprintf(string,PATH_MAX - 1, "memcached:///%s/%d/%d/%d.meta", xmlconfig, x, y, z);
    return string;
}

static int memcached_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    char meta_path[PATH_MAX];
    char tmp[PATH_MAX];
    struct stat_info tile_stat;
    int sz2 = sz + sizeof(struct stat_info);
    char * buf2 = malloc(sz2);
    memcached_return_t rc;

    if (buf2 == NULL) {
        return -2;
    }

    tile_stat.expired = 0;
    tile_stat.size = sz;
    tile_stat.mtime = time(NULL);
    tile_stat.atime = tile_stat.mtime;
    tile_stat.ctime = tile_stat.mtime;

    memcpy(buf2, &tile_stat, sizeof(tile_stat));
    memcpy(buf2 + sizeof(tile_stat), buf, sz);

    log_message(STORE_LOGLVL_DEBUG, "Trying to create and write a metatile to %s\n", memcached_tile_storage_id(store, xmlconfig, options, x, y, z, tmp));
 
    snprintf(meta_path,PATH_MAX - 1, "%s/%d/%d/%d.meta", xmlconfig, x, y, z);

    rc = memcached_set(store->storage_ctx, meta_path, strlen(meta_path), buf2, sz2, (time_t)0, (uint32_t)0);
    free(buf2);

    if (rc != MEMCACHED_SUCCESS) {
        return -1;
    }
    memcached_flush_buffers(store->storage_ctx);
    return sz;
}


static int memcached_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    char meta_path[PATH_MAX];
    memcached_return_t rc;

    //TODO: deal with options
    memcached_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);

    rc = memcached_delete(store->storage_ctx, meta_path, strlen(meta_path), 0);

    if (rc != MEMCACHED_SUCCESS) {
        return -1;
    }

    return 0;
}

static int memcached_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {

    char meta_path[PATH_MAX];
    char * buf;
    size_t len;
    uint32_t flags;
    uint64_t cas;
    memcached_return_t rc;

    //TODO: deal with options
    memcached_xyz_to_storagekey(xmlconfig, x, y, z, meta_path);
    buf = memcached_get(store->storage_ctx, meta_path, strlen(meta_path), &len, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        return -1;
    }
    //cas = memcached_result_cas(&rc);

    ((struct stat_info *)buf)->expired = 1;

    rc = memcached_cas(store->storage_ctx, meta_path, strlen(meta_path), buf, len, 0, flags, cas);

    if (rc != MEMCACHED_SUCCESS) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

static int memcached_close_storage(struct storage_backend * store) {
    memcached_free(store->storage_ctx);
    return 0;
}
#endif //Have memcached

struct storage_backend * init_storage_memcached(const char * connection_string) {
    
#ifndef HAVE_LIBMEMCACHED
    log_message(STORE_LOGLVL_ERR,"init_storage_memcached: Support for memcached has not been compiled into this program");
    return NULL;
#else
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    memcached_st * ctx;
    char * connection_str = "--server=localhost";

    if (store == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_memcached: Failed to allocate memory for storage backend");
        return NULL;
    }
    ctx = memcached(connection_str, strlen(connection_str));
    if (ctx == NULL) {
        log_message(STORE_LOGLVL_ERR,"init_storage_memcached: Failed to create memcached ctx");
        free(store);
        return NULL;
    }
    store->storage_ctx = ctx;

    store->tile_read = &memcached_tile_read;
    store->tile_stat = &memcached_tile_stat;
    store->metatile_write = &memcached_metatile_write;
    store->metatile_delete = &memcached_metatile_delete;
    store->metatile_expire = &memcached_metatile_expire;
    store->tile_storage_id = &memcached_tile_storage_id;
    store->close_storage = &memcached_close_storage;

    return store;
#endif
}
