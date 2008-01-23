#ifndef GEN_TILE_H
#define GEN_TILE_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

struct item {
    struct item *next;
    struct item *prev;
    struct protocol req;
    int mx, my;
    int fd;
    struct item *duplicates;
};

//int render(Map &m, int x, int y, int z, const char *filename);
void *render_thread(void *unused);
struct item *fetch_request(void);
void delete_request(struct item *item);
void send_response(struct item *item, enum protoCmd);
void render_init(void);

#ifdef __cplusplus
}
#endif

#endif
