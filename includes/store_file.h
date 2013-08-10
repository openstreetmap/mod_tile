#ifndef STOREFILE_H
#define STOREFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_file(const char * tile_dir);
        
#ifdef __cplusplus
}
#endif
#endif
