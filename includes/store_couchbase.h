#ifndef STORECOUCHBASE_H
#define STORECOUCHBASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_couchbase(const char * connection_string);
        
#ifdef __cplusplus
}
#endif
#endif
