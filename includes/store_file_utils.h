#ifndef DIR_UTILS_H
#define DIR_UTILS_H

#include <sys/types.h>

#ifdef __cplusplus
  extern "C" {
#endif

#include "render_config.h"

/* Build parent directories for the specified file name
 * Note: the part following the trailing / is ignored
 * e.g. mkdirp("/a/b/foo.png") == shell mkdir -p /a/b
 */
int mkdirp(const char *path);

/* File path hashing. Used by both mod_tile and render daemon
 * The two must both agree on the file layout for meta-tiling
 * to work
 */
void xyz_to_path(char *path, size_t len, const char *tile_dir, const char *xmlconfig, int x, int y, int z);

int path_to_xyz(const char *tilepath, const char *path, char *xmlconfig, int *px, int *py, int *pz);

#ifdef METATILE
/* New meta-tile storage functions */
/* Returns the path to the meta-tile and the offset within the meta-tile */
int xyz_to_meta(char *path, size_t len, const char *tile_dir, const char *xmlconfig, int x, int y, int z);
#endif

#ifdef __cplusplus
  }
#endif


#endif
