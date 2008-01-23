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
    // Directory hashing is optimised for z18 max (2^18 tiles split into 2 levels of 2^9)
    //snprintf(path, PATH_MAX, WWW_ROOT HASH_PATH "/%d/%03d/%03d/%03d/%03d.png", z,
    //                 x/512 x%512, y/512, y%512);

    // We attempt to cluseter the tiles so that a 16x16 square of tiles will be in a single directory
    // Hash stores our 40 bit result of mixing the 20 bits of the x & y co-ordinates
    // 4 bits of x & y are used per byte of output
    unsigned char i, hash[5];

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }
    snprintf(path, PATH_MAX, WWW_ROOT HASH_PATH "/%d/%u/%u/%u/%u/%u.png", z, hash[4], hash[3], hash[2], hash[1], hash[0]);
#else
    snprintf(path, PATH_MAX, WWW_ROOT TILE_PATH "/%d/%d/%d.png", z, x, y);
#endif
    return path + strlen(WWW_ROOT);
}
