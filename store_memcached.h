#ifndef STOREMEMCACHED_H
#define STOREMEMCACHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_memcached(const char * connection_string);
        
#ifdef __cplusplus
}
#endif
#endif
