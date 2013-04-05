/*
Copyright © 2013 mod_tile contributors

This file is part of mod_tile.

mod_tile is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

mod_tile is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GEN_TILE_H
#define GEN_TILE_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTCP_EXPIRE_CACHE 1
#define HTCP_EXPIRE_CACHE_PORT "4827"

enum queueEnum {queueRequest, queueRequestPrio, queueRequestBulk, queueDirty, queueRender,  queueDuplicate};

struct item {
    struct item *next;
    struct item *prev;
    struct protocol req;
    int mx, my;
    int fd;
    struct item *duplicates;
    enum queueEnum inQueue;
};

struct item_idx {
    struct item_idx *next;
    struct item *item;
};

//int render(Map &m, int x, int y, int z, const char *filename);
void *render_thread(void *);
struct item *fetch_request(void);
void delete_request(struct item *item);
void send_response(struct item *item, enum protoCmd);
void render_init(const char *plugins_dir, const char* font_dir, int font_recurse);

#ifdef __cplusplus
}
#endif

#endif
