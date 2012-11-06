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

#include "store.h"
#include "render_config.h"
#include "dir_utils.h"
#include "protocol.h"

#ifdef METATILE
int read_from_meta(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, size_t sz, int * compressed, unsigned char * log_msg)
{
    char path[PATH_MAX];
    int meta_offset, fd;
    unsigned int pos;
    char header[4096];
    struct meta_layout *m = (struct meta_layout *)header;
    size_t file_offset, tile_size;

    meta_offset = xyz_to_meta(path, sizeof(path), tilepath, xmlconfig, x, y, z);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(log_msg,1024, "Could not open metatile %s. Reason: %s\n", path, strerror(errno));
        return -1;
    }

    pos = 0;
    while (pos < sizeof(header)) {
        size_t len = sizeof(header) - pos;
        int got = read(fd, header + pos, len);
        if (got < 0) {
            snprintf(log_msg,1024, "Failed to read complete header for metatile %s Reason: %s\n", path, strerror(errno));
            close(fd);            
            return -2;
        } else if (got > 0) {
            pos += got;
        } else {
            break;
        }
    }
    if (pos < sizeof(struct meta_layout)) {
        snprintf(log_msg,1024, "Meta file %s too small to contain header\n", path);
        close(fd);
        return -3;
    }
    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) {
        if (memcmp(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED))) {
            snprintf(log_msg,1024, "Meta file %s header magic mismatch\n", path);
            close(fd);
            return -4;
        } else {
            *compressed = 1;
        }
    } else *compressed = 0;
#if 1
    // Currently this code only works with fixed metatile sizes (due to xyz_to_meta above)
    if (m->count != (METATILE * METATILE)) {
        snprintf(log_msg, 1024, "Meta file %s header bad count %d != %d\n", path, m->count, METATILE * METATILE);
        close(fd);
        return -5;
    }
#else
    if (m->count < 0 || m->count > 256) {
        fprintf(stderr, "Meta file %s header bad count %d\n", path, m->count);
        close(fd);
        return -5;
    }
#endif
    file_offset = m->index[meta_offset].offset;
    tile_size   = m->index[meta_offset].size;

    if (lseek(fd, file_offset, SEEK_SET) < 0) {
        snprintf(log_msg, 1024, "Meta file %s seek error %d\n", path, m->count);
        close(fd);
        return -6;
    }
    if (tile_size > sz) {
        snprintf(log_msg, 1024, "Truncating tile %zd to fit buffer of %zd\n", tile_size, sz);
        tile_size = sz;
    }
    pos = 0;
    while (pos < tile_size) {
        size_t len = tile_size - pos;
        int got = read(fd, buf + pos, len);
        if (got < 0) {
            snprintf(log_msg, 1024, "Failed to read data from file %s. Reason: %s\n", path, strerror(errno));
            close(fd);
            return -7;
        } else if (got > 0) {
            pos += got;
        } else {
            break;
        }
    }
    close(fd);
    return pos;
}
#endif

int read_from_file(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, size_t sz)
{
    char path[PATH_MAX];
    int fd;
    size_t pos;

    xyz_to_path(path, sizeof(path), tilepath, xmlconfig, x, y, z);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    pos = 0;
    while (pos < sz) {
        size_t len = sz - pos;
        int got = read(fd, buf + pos, len);
        if (got < 0) {
            close(fd);
            return -2;
        } else if (got > 0) {
            pos += got;
        } else {
            break;
        }
    }
    if (pos == sz) {
        fprintf(stderr, "file %s truncated at %zd bytes\n", path, sz);
    }
    close(fd);
    return pos;
}

int tile_read(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, int sz, int * compressed, unsigned char *err_msg)
{
#ifdef METATILE
    int r;

    r = read_from_meta(tilepath, xmlconfig, x, y, z, buf, sz, compressed, err_msg);
    if (r >= 0)
        return r;
#endif
    return read_from_file(tilepath, xmlconfig, x, y, z, buf, sz);
}

#ifdef METATILE
void process_meta(const char *tilepath, const char *xmlconfig, int x, int y, int z)
{
    int fd;
    int ox, oy, limit;
    size_t offset, pos;
    const int buf_len = 10 * MAX_SIZE; // To store all tiles in this .meta
    unsigned char *buf;
    struct meta_layout *m;
    char meta_path[PATH_MAX];
    char tmp[PATH_MAX];
    struct stat s;

    buf = (unsigned char *)malloc(buf_len);
    if (!buf)
        return;

    m = (struct meta_layout *)buf;
    offset = sizeof(struct meta_layout) + (sizeof(struct entry) * (METATILE * METATILE));
    memset(buf, 0, offset);

    limit = (1 << z);
    limit = MIN(limit, METATILE);

    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            //fprintf(stderr, "Process %d/%d/%d\n", num, ox, oy);
            int len = read_from_file(tilepath, xmlconfig, x + ox, y + oy, z, buf + offset, buf_len - offset);
            int mt = xyz_to_meta(meta_path, sizeof(meta_path), tilepath, xmlconfig, x + ox, y + oy, z);
            if (len <= 0) {
#if 1
                fprintf(stderr, "Problem reading sub tiles for metatile xml(%s) x(%d) y(%d) z(%d), got %d\n", xmlconfig, x, y, z, len);
                free(buf);
                return;
#else
                 m->index[mt].offset = 0;
                 m->index[mt].size = 0;
#endif
            } else {
                 m->index[mt].offset = offset;
                 m->index[mt].size = len;
                 offset += len;
            }
        }
    }
    m->count = METATILE * METATILE;
    memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
    m->x = x;
    m->y = y;
    m->z = z;

    xyz_to_meta(meta_path, sizeof(meta_path), tilepath, xmlconfig, x, y, z);
    if (mkdirp(meta_path)) {
        fprintf(stderr, "Error creating directories for: %s\n", meta_path);
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", meta_path, getpid());

    fd = open(tmp, O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd < 0) {
        fprintf(stderr, "Error creating file: %s\n", meta_path);
        free(buf);
        return;
    }

    pos = 0;
    while (pos < offset) {
        int len = write(fd, buf + pos, offset - pos);
        if (len < 0) {
            perror("Writing file");
            free(buf);
            close(fd);
            return;
        } else if (len > 0) {
            pos += len;
        } else {
            break;
        }
    }
    close(fd);
    free(buf);

    // Reset meta timestamp to match one of the original tiles
    xyz_to_path(meta_path, sizeof(meta_path), tilepath, xmlconfig, x, y, z);
    if (stat(meta_path, &s) == 0) {
        struct utimbuf b;
        b.actime = s.st_atime;
        b.modtime = s.st_mtime;
        xyz_to_meta(meta_path, sizeof(meta_path), tilepath, xmlconfig, x, y, z);
        utime(tmp, &b);
    }
    rename(tmp, meta_path);
    printf("Produced .meta: %s\n", meta_path);

    // Remove raw .png's
    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            xyz_to_path(meta_path, sizeof(meta_path), tilepath, xmlconfig, x + ox, y + oy, z);
            if (unlink(meta_path)<0)
                perror(meta_path);
        }
    }
}

void process_pack(const char *tilepath, const char *name)
{
    char meta_path[PATH_MAX];
    char xmlconfig[XMLCONFIG_MAX];
    int x, y, z;
    int meta_offset;

    if (path_to_xyz(tilepath, name, xmlconfig, &x, &y, &z))
        return;
 
    // Launch the .meta creation for only 1 tile of the whole block
    meta_offset = xyz_to_meta(meta_path, sizeof(meta_path), tilepath, xmlconfig, x, y, z);
    //fprintf(stderr,"Requesting x(%d) y(%d) z(%d) - mo(%d)\n", x, y, z, meta_offset);

    if (meta_offset == 0)
        process_meta(tilepath, xmlconfig, x, y, z);
}

static void write_tile(const char *tilepath, const char *xmlconfig, int x, int y, int z, const unsigned char *buf, size_t sz)
{
    int fd;
    char path[PATH_MAX];
    size_t pos;

    xyz_to_path(path, sizeof(path), tilepath, xmlconfig, x, y, z);
    if (mkdirp(path)) {
        fprintf(stderr, "Error creating directories for: %s\n", path);
        return;
    }
    fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd < 0) {
        fprintf(stderr, "Error creating file: %s\n", path);
        return;
    }

    pos = 0;
    while (pos < sz) {
        int len = write(fd, buf + pos, sz - pos);
        if (len < 0) {
            perror("Writing file");
            close(fd);
            return;
        } else if (len > 0) {
            pos += len;
        } else {
            break;
        }
    }
    close(fd);
    printf("Produced tile: %s\n", path);
}

void process_unpack(const char *tilepath, const char *name)
{
    char meta_path[PATH_MAX];
    char xmlconfig[XMLCONFIG_MAX];
    char err_msg[4096];
    int x, y, z;
    int ox, oy, limit;
    const int buf_len = 1024 * 1024;
    unsigned char *buf;
    struct stat s;
    int compressed;

    // path_to_xyz is valid for meta tile names as well
    if (path_to_xyz(tilepath, name, xmlconfig, &x, &y, &z))
        return;

    buf = (unsigned char *)malloc(buf_len);
    if (!buf)
        return;


    limit = (1 << z);
    limit = MIN(limit, METATILE);

    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            err_msg[0] = 0;
            int len = read_from_meta(tilepath, xmlconfig, x + ox, y + oy, z, buf, buf_len, &compressed, err_msg);

            if (len <= 0)
                fprintf(stderr, "Failed to get tile x(%d) y(%d) z(%d)\n    %s", x + ox, y + oy, z, err_msg);
            else
                write_tile(tilepath, xmlconfig, x + ox, y + oy, z, buf, len);
        }
    }

    // Grab timestamp of the meta file and update tile timestamps
    if (stat(name, &s) == 0) {
        struct utimbuf b;
        b.actime = s.st_atime;
        b.modtime = s.st_mtime;
        for (ox=0; ox < limit; ox++) {
            for (oy=0; oy < limit; oy++) {
                xyz_to_path(meta_path, sizeof(meta_path), tilepath, xmlconfig, x+ox, y+oy, z);
                utime(meta_path, &b);
            }
        }
    }

    // Remove the .meta file
    xyz_to_meta(meta_path, sizeof(meta_path), tilepath, xmlconfig, x, y, z);
    if (unlink(meta_path)<0)
        perror(meta_path);
}
#endif
