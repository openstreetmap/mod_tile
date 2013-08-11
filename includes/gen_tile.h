#ifndef GEN_TILE_H
#define GEN_TILE_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif


enum queueEnum {queueRequest, queueRequestPrio, queueRequestBulk, queueDirty, queueRender,  queueDuplicate, queueRequestLow};

struct item {
    struct item *next;
    struct item *prev;
    struct protocol req;
    int mx, my;
    int fd;
    struct item *duplicates;
    enum queueEnum inQueue;
    enum queueEnum originatedQueue;
};

//int render(Map &m, int x, int y, int z, const char *filename);
void *render_thread(void *);
struct item *fetch_request(void);
void delete_request(struct item *item);
void render_init(const char *plugins_dir, const char* font_dir, int font_recurse);

#ifdef __cplusplus
}
#endif

#endif
