#ifndef STOREROCOMPOSITE_H
#define STOREROCOMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_ro_composite(const char * connection_string);
        
#ifdef __cplusplus
}
#endif
#endif
