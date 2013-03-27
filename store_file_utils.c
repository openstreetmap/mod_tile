#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "protocol.h"
#include "render_config.h"
#include "store_file_utils.h"

// Build parent directories for the specified file name
// Note: the part following the trailing / is ignored
// e.g. mkdirp("/a/b/foo.png") == shell mkdir -p /a/b
int mkdirp(const char *path) {
    struct stat s;
    char tmp[PATH_MAX];
    char *p;

    strncpy(tmp, path, sizeof(tmp) - 1);

    // Look for parent directory
    p = strrchr(tmp, '/');
    if (!p)
        return 0;

    *p = '\0';

    if (!stat(tmp, &s))
        return !S_ISDIR(s.st_mode);
    *p = '/';
    // Walk up the path making sure each element is a directory
    p = tmp;
    if (!*p)
        return 0;
    p++; // Ignore leading /
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (!stat(tmp, &s)) {
                if (!S_ISDIR(s.st_mode)) {
                    fprintf(stderr, "Error, is not a directory: %s\n", tmp);
                    return 1;
                }
            } else if (mkdir(tmp, 0777)) {
                    // Ignore multiple threads attempting to create the same directory
                    if (errno != EEXIST) { 
                       perror(tmp);
                       return 1;
                    }
                }
            *p = '/';
        }
        p++;
    }
    return 0;
}



/* File path hashing. Used by both mod_tile and render daemon
 * The two must both agree on the file layout for meta-tiling
 * to work
 */

static int check_xyz(int x, int y, int z) {
    int oob, limit;

    // Validate tile co-ordinates
    oob = (z < 0 || z > MAX_ZOOM);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z) - 1;
        oob =  (x < 0 || x > limit || y < 0 || y > limit);
    }

    if (oob)
        fprintf(stderr, "got bad co-ords: x(%d) y(%d) z(%d)\n", x, y, z);

    return oob;
}

void xyz_to_path(char *path, size_t len, const char *tile_dir, const char *xmlconfig, int x, int y, int z)
{
#ifdef DIRECTORY_HASH
    // We attempt to cluster the tiles so that a 16x16 square of tiles will be in a single directory
    // Hash stores our 40 bit result of mixing the 20 bits of the x & y co-ordinates
    // 4 bits of x & y are used per byte of output
    unsigned char i, hash[5];

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }
    snprintf(path, len, "%s/%s/%d/%u/%u/%u/%u/%u.png", tile_dir, xmlconfig, z, hash[4], hash[3], hash[2], hash[1], hash[0]);
#else
    snprintf(path, len, TILE_PATH "/%s/%d/%d/%d.png", xmlconfig, z, x, y);
#endif
    return;
}

int path_to_xyz(const char *tilepath, const char *path, char *xmlconfig, int *px, int *py, int *pz)
{
#ifdef DIRECTORY_HASH
    int i, n, hash[5], x, y, z;
    for(i = 0; tilepath[i] && tilepath[i] == path[i]; ++i)
        ;
    if(tilepath[i]) {
        fprintf(stderr, "Tile path does not match settings (%s): %s\n", tilepath, path);
        return 1;
    }

    n = sscanf(path+i, "/%40[^/]/%d/%d/%d/%d/%d/%d", xmlconfig, pz, &hash[0], &hash[1], &hash[2], &hash[3], &hash[4]);
    if (n != 7) {
        fprintf(stderr, "Failed to parse tile path: %s\n", path);
        return 1;
    } else {
        x = y = 0;
        for (i=0; i<5; i++) {
            if (hash[i] < 0 || hash[i] > 255) {
                fprintf(stderr, "Failed to parse tile path (invalid %d): %s\n", hash[i], path);
                return 2;
            }
            x <<= 4;
            y <<= 4;
            x |= (hash[i] & 0xf0) >> 4;
            y |= (hash[i] & 0x0f);
        }
        z = *pz;
        *px = x;
        *py = y;
        return check_xyz(x, y, z);
    }
#else
    int n;
    n = sscanf(path, TILE_PATH "/%40[^/]/%d/%d/%d", xmlconfig, pz, px, py);
    if (n != 4) {
        fprintf(stderr, "Failed to parse tile path: %s\n", path);
        return 1;
    } else {
        return check_xyz(*px, *py, *pz);
    }
#endif
}

#ifdef METATILE
// Returns the path to the meta-tile and the offset within the meta-tile
int xyz_to_meta(char *path, size_t len, const char *tile_dir, const char *xmlconfig, int x, int y, int z)
{
    unsigned char i, hash[5], offset, mask;

    // Each meta tile winds up in its own file, with several in each leaf directory
    // the .meta tile name is beasd on the sub-tile at (0,0)
    mask = METATILE - 1;
    offset = (x & mask) * METATILE + (y & mask);
    x &= ~mask;
    y &= ~mask;

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }
#ifdef DIRECTORY_HASH
    snprintf(path, len, "%s/%s/%d/%u/%u/%u/%u/%u.meta", tile_dir, xmlconfig, z, hash[4], hash[3], hash[2], hash[1], hash[0]);
#else
    snprintf(path, len, "%s/%s/%d/%u/%u.meta", tile_dir, xmlconfig, z, x, y);
#endif
    return offset;
}
#endif
