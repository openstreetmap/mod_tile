/*
 * Copyright © 2013 mod_tile contributors
 * Copyright © 2013 Kai Krueger
 *
 *This file is part of renderd, a project to render OpenStreetMap tiles
 *with Mapnik.
 *
 * renderd is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * mod_tile is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include "gen_tile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HASHIDX_SIZE 2213

typedef struct {
    long noDirtyRender;
    long noReqRender;
    long noReqPrioRender;
    long noReqLowRender;
    long noReqBulkRender;
    long noReqDroped;
    long noZoomRender[MAX_ZOOM + 1];
    long timeReqRender;
    long timeReqPrioRender;
    long timeReqLowRender;
    long timeReqBulkRender;
    long timeReqDirty;
    long timeZoomRender[MAX_ZOOM + 1];
} stats_struct;

struct item_idx {
    struct item_idx *next;
    struct item *item;
};

struct request_queue {
    int hashidxSize;
    struct item reqHead, reqPrioHead, reqLowHead, reqBulkHead, dirtyHead, renderHead;
    struct item_idx * item_hashidx;
    int reqNum, reqPrioNum, reqLowNum, reqBulkNum, dirtyNum;
    pthread_mutex_t qLock;
    pthread_cond_t qCond;
    stats_struct stats;
};


struct request_queue *request_queue_init();
void request_queue_close(struct request_queue * queue);

struct item *request_queue_fetch_request(struct request_queue * queue);
enum protoCmd request_queue_add_request(struct request_queue * queue, struct item * request);

void request_queue_remove_request(struct request_queue * queue, struct item * request, int render_time);
void request_queue_clear_requests_by_fd(struct request_queue * queue, int fd);

int request_queue_no_requests_queued(struct request_queue * queue, enum protoCmd);
void request_queue_copy_stats(struct request_queue * queue, stats_struct * stats);


#ifdef __cplusplus
}
#endif
#endif


