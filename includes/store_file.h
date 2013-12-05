#ifndef STOREFILE_H
#define STOREFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_file(const char * tile_dir);
    int xyzo_to_meta(char *path, size_t len, const char *tile_dir, const char *xmlconfig, const char *options, int x, int y, int z);

#ifdef __cplusplus
}
#endif
#endif
