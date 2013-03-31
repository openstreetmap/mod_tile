#ifndef STOREROHTTPPROXY_H
#define STOREROHTTPPROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "store.h"
    
    struct storage_backend * init_storage_ro_http_proxy(const char * connection_string);
        
#ifdef __cplusplus
}
#endif
#endif
