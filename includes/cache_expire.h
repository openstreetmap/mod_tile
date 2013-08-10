#ifndef CACHEEXPIRE_H
#define CACHEEXPIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTCP_EXPIRE_CACHE 1
#define HTCP_EXPIRE_CACHE_PORT "4827"


void cache_expire(int sock, char * host, char * uri, int x, int y, int z);
int init_cache_expire(char * htcphost);

#ifdef __cplusplus
}
#endif

#endif
