/* Meta-tile optimised file storage
 *
 * Instead of storing each individual tile as a file,
 * bundle the 8x8 meta tile into a special meta-file.
 * This reduces the Inode usage and more efficient
 * utilisation of disk space.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>

#include "store.h"
#include "metatile.h"
#include "render_config.h"
#include "store_file.h"
#include "store_file_utils.h"
#include "protocol.h"


static time_t getPlanetTime(const char * tile_dir, const char * xmlname)
{
    struct stat st_stat;
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX-1, "%s/%s%s", tile_dir, xmlname, PLANET_TIMESTAMP);

    if (stat(filename, &st_stat) < 0) {
        snprintf(filename, PATH_MAX-1, "%s/%s", tile_dir, PLANET_TIMESTAMP);
        if (stat(filename, &st_stat) < 0) {
            // Make something up
            return time(NULL) - (3*24*60*60);
        }
    }
    return st_stat.st_mtime;
}

static int file_tile_read(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t sz, int * compressed, char * log_msg) {

    char path[PATH_MAX];
    int meta_offset, fd;
    unsigned int pos;
    unsigned int header_len = sizeof(struct meta_layout) + METATILE*METATILE*sizeof(struct entry);
    struct meta_layout *m = (struct meta_layout *)malloc(header_len);
    size_t file_offset, tile_size;

    meta_offset = xyzo_to_meta(path, sizeof(path), store->storage_ctx, xmlconfig, options, x, y, z);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(log_msg,PATH_MAX - 1, "Could not open metatile %s. Reason: %s\n", path, strerror(errno));
        free(m);
        return -1;
    }

    pos = 0;
    while (pos < header_len) {
        size_t len = header_len - pos;
        int got = read(fd, ((unsigned char *) m) + pos, len);
        if (got < 0) {
            snprintf(log_msg,PATH_MAX - 1, "Failed to read complete header for metatile %s Reason: %s\n", path, strerror(errno));
            close(fd);
            free(m);
            return -2;
        } else if (got > 0) {
            pos += got;
        } else {
            break;
        }
    }
    if (pos < header_len) {
        snprintf(log_msg,PATH_MAX - 1, "Meta file %s too small to contain header\n", path);
        close(fd);
        free(m);
        return -3;
    }
    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
        if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
            snprintf(log_msg,PATH_MAX - 1, "Meta file %s header magic mismatch\n", path);
            close(fd);
            free(m);
            return -4;
        } else {
            *compressed = 1;
        }
    } else *compressed = 0;

    // Currently this code only works with fixed metatile sizes (due to xyz_to_meta above)
    if (m->count != (METATILE * METATILE)) {
        snprintf(log_msg, PATH_MAX - 1, "Meta file %s header bad count %d != %d\n", path, m->count, METATILE * METATILE);
        free(m);
        close(fd);
        return -5;
    }

    file_offset = m->index[meta_offset].offset;
    tile_size   = m->index[meta_offset].size;

    free(m);

    if (tile_size > sz) {
        snprintf(log_msg, PATH_MAX - 1, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
        tile_size = sz;
        close(fd);
        return -6;
    }

    if (lseek(fd, file_offset, SEEK_SET) < 0) {
        snprintf(log_msg, PATH_MAX - 1, "Meta file %s seek error: %s\n", path, strerror(errno));
        close(fd);
        return -7;
    }
    
    pos = 0;
    while (pos < tile_size) {
        size_t len = tile_size - pos;
        int got = read(fd, buf + pos, len);
        if (got < 0) {
            snprintf(log_msg, PATH_MAX - 1, "Failed to read data from file %s. Reason: %s\n", path, strerror(errno));
            close(fd);
            return -8;
        } else if (got > 0) {
            pos += got;
        } else {
            break;
        }
    }
    close(fd);
    return pos;
}

static struct stat_info file_tile_stat(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z) {
    struct stat_info tile_stat;
    struct stat st_stat;
    char meta_path[PATH_MAX];

    xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);
    
    if (stat(meta_path, &st_stat)) {
        tile_stat.size = -1;
        tile_stat.mtime = 0;
        tile_stat.atime = 0;
        tile_stat.ctime = 0;
    } else {
        tile_stat.size = st_stat.st_size;
        tile_stat.mtime = st_stat.st_mtime;
        tile_stat.atime = st_stat.st_atime;
        tile_stat.ctime = st_stat.st_ctime;
    }

    if (tile_stat.mtime < getPlanetTime(store->storage_ctx, xmlconfig)) {
        tile_stat.expired = 1;
    } else {
        tile_stat.expired = 0;
    }

    return tile_stat;
}

static char * file_tile_storage_id(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, char * string) {
    char meta_path[PATH_MAX];

    xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);
    snprintf(string, PATH_MAX - 1, "file://%s", meta_path);
    return string;
}
    

static int file_metatile_write(struct storage_backend * store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz) {
    int fd;
    char meta_path[PATH_MAX];
    char * tmp;
    int res;
 
    xyzo_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, options, x, y, z);
    log_message(STORE_LOGLVL_DEBUG, "Creating and writing a metatile to %s\n", meta_path);

    tmp = malloc(sizeof(char) * strlen(meta_path) + 24);
    snprintf(tmp, strlen(meta_path) + 24, "%s.%lu", meta_path, pthread_self());

    if (mkdirp(tmp)) {
        free(tmp);
        return -1;
    }


    fd = open(tmp, O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd < 0) {
        log_message(STORE_LOGLVL_WARNING, "Error creating file %s: %s\n", meta_path, strerror(errno));
        free(tmp);
        return -1;
    }
    
    res = write(fd, buf, sz);
    if (res != sz) {
        log_message(STORE_LOGLVL_WARNING, "Error writing file %s: %s\n", meta_path, strerror(errno));
        close(fd);
        free(tmp);
        return -1;
    }

    close(fd);
    rename(tmp, meta_path);
    free(tmp);

    return sz;
}

static int file_metatile_delete(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {
    char meta_path[PATH_MAX];

    //TODO: deal with options
    xyz_to_meta(meta_path, sizeof(meta_path), (char *)(store->storage_ctx), xmlconfig, x, y, z);
    log_message(STORE_LOGLVL_DEBUG, "Deleting metatile from %s\n", meta_path);
    return unlink(meta_path);
}

static int file_metatile_expire(struct storage_backend * store, const char *xmlconfig, int x, int y, int z) {

    char name[PATH_MAX];
    struct stat s;
    static struct tm touchCalendar;
    struct utimbuf touchTime;

    //TODO: deal with options
    xyz_to_meta(name, sizeof(name), store->storage_ctx, xmlconfig, x, y, z);

    if (stat(name, &s) == 0) {// 0 is success
        // tile exists on disk; mark it as expired

        if (!gmtime_r(&(s.st_mtime), &touchCalendar)) {
            touchTime.modtime = 315558000;
        } else {
            if (touchCalendar.tm_year > 105) { // Tile hasn't already been marked as expired
                touchCalendar.tm_year -= 20; //Set back by 20 years, to keep the creation time as reference.
                touchTime.modtime = mktime(&touchCalendar);
            } else {
                touchTime.modtime = s.st_mtime;
            }
        }
        touchTime.actime = s.st_atime; // Don't modify atime, as that is used for tile cache purging

        if (-1 == utime(name, &touchTime))
        {
            perror("modifying timestamp failed");
        }
    }
    return 0;
}

static int file_close_storage(struct storage_backend * store) {
    free(store->storage_ctx);
    store->storage_ctx = NULL;
    return 0;
}

struct storage_backend * init_storage_file(const char * tile_dir) {
    
    struct storage_backend * store = malloc(sizeof(struct storage_backend));
    if (store == NULL) {
        log_message(STORE_LOGLVL_ERR, "init_storage_file: Failed to allocate memory for storage backend");
        return NULL;
    }
    store->storage_ctx = strdup(tile_dir);

    store->tile_read = &file_tile_read;
    store->tile_stat = &file_tile_stat;
    store->metatile_write = &file_metatile_write;
    store->metatile_delete = &file_metatile_delete;
    store->metatile_expire = &file_metatile_expire;
    store->tile_storage_id = &file_tile_storage_id;
    store->close_storage = &file_close_storage;

    return store;
}
