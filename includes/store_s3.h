#ifndef STORES3_H
#define STORES3_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "store.h"

struct storage_backend* init_storage_s3(const char *connection_string);

#ifdef __cplusplus
}
#endif
#endif
