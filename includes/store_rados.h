#ifndef STORERADOS_H
#define STORERADOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_rados(const char * connection_string);
        
#ifdef __cplusplus
}
#endif
#endif
