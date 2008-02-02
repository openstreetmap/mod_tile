#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"

// Build parent directories for the specified file name
// Note: the part following the trailing / is ignored
// e.g. mkdirp("/a/b/foo.png") == shell mkdir -p /a/b
int mkdirp(const char *path) {
    struct stat s;
    char tmp[PATH_MAX];
    char *p;

    strncpy(tmp, path, sizeof(tmp));

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
                if (!S_ISDIR(s.st_mode))
                    return 1;
            } else if (mkdir(tmp, 0777))
                return 1;
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

const char *xyz_to_path(char *path, size_t len, int x, int y, int z)
{
#ifdef DIRECTORY_HASH
    // We attempt to cluseter the tiles so that a 16x16 square of tiles will be in a single directory
    // Hash stores our 40 bit result of mixing the 20 bits of the x & y co-ordinates
    // 4 bits of x & y are used per byte of output
    unsigned char i, hash[5];

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }
    snprintf(path, len, WWW_ROOT HASH_PATH "/%d/%u/%u/%u/%u/%u.png", z, hash[4], hash[3], hash[2], hash[1], hash[0]);
#else
    snprintf(path, len, WWW_ROOT TILE_PATH "/%d/%d/%d.png", z, x, y);
#endif
    return path + strlen(WWW_ROOT);
}

int check_xyz(int x, int y, int z)
{
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


int path_to_xyz(const char *path, int *px, int *py, int *pz)
{
    int i, n, hash[5], x, y, z;

    n = sscanf(path, WWW_ROOT HASH_PATH "/%d/%d/%d/%d/%d/%d.png", pz, &hash[0], &hash[1], &hash[2], &hash[3], &hash[4]);
    if (n != 6) {
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
}
