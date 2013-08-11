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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include "render_config.h"
#include "request_queue.h"

static int calcHashKey(struct request_queue *queue, struct item *item) {
    uint64_t xmlnameHash = 0;
    uint64_t key;
    for (int i = 0; (item->req.xmlname[i] != 0) && (i < sizeof(item->req.xmlname)); i++) {
        xmlnameHash += item->req.xmlname[i];
    }
    key = ((uint64_t)(xmlnameHash & 0x1FF) << 52) + ((uint64_t)(item->req.z) << 48) + ((uint64_t)(item->mx & 0xFFFFFF) << 24) + (item->my & 0xFFFFFF);
    return key % queue->hashidxSize;
}

static struct item * lookup_item_idx(struct request_queue * queue, struct item * item) {
    struct item_idx * nextItem;
    struct item * test;

    int key = calcHashKey(queue, item);

    if (queue->item_hashidx[key].item == NULL) {
        return NULL;
    } else {
        nextItem = &(queue->item_hashidx[key]);
        while (nextItem != NULL) {
            test = nextItem->item;
            if ((item->mx == test->mx) && (item->my == test->my)
                    && (item->req.z == test->req.z) && (!strcmp(
                            item->req.xmlname, test->req.xmlname))) {
                return test;
            } else {
                nextItem = nextItem->next;
            }
        }
    }
    return NULL;
}

static void insert_item_idx(struct request_queue * queue, struct item *item) {
    struct item_idx * nextItem;
    struct item_idx * prevItem;

    int key = calcHashKey(queue, item);

    if (queue->item_hashidx[key].item == NULL) {
        queue->item_hashidx[key].item = item;
    } else {
        prevItem = &(queue->item_hashidx[key]);
        nextItem = queue->item_hashidx[key].next;
        while(nextItem) {
            prevItem = nextItem;
            nextItem = nextItem->next;
        }
        nextItem = (struct item_idx *)malloc(sizeof(struct item_idx));
        nextItem->item = item;
        nextItem->next = NULL;
        prevItem->next = nextItem;
    }
}

static void remove_item_idx(struct request_queue * queue, struct item * item) {
    int key = calcHashKey(queue, item);
    struct item_idx * nextItem;
    struct item_idx * prevItem;
    struct item * test;
    if (queue->item_hashidx[key].item == NULL) {
        //item not in index;
        return;
    }
    prevItem = &(queue->item_hashidx[key]);
    nextItem = &(queue->item_hashidx[key]);

    while (nextItem != NULL) {
        test = nextItem->item;
        if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z
                == test->req.z) && (!strcmp(item->req.xmlname,
                        test->req.xmlname))) {
            /*
             * Found item, removing it from list
             */
            nextItem->item = NULL;
            if (nextItem->next != NULL) {
                if (nextItem == &(queue->item_hashidx[key])) {
                    prevItem = nextItem->next;
                    memcpy(&(queue->item_hashidx[key]), nextItem->next,
                            sizeof(struct item_idx));
                    free(prevItem);
                } else {
                    prevItem->next = nextItem->next;
                }
            } else {
                prevItem->next = NULL;
            }

            if (nextItem != &(queue->item_hashidx[key])) {
                free(nextItem);
            }
            return;
        } else {
            prevItem = nextItem;
            nextItem = nextItem->next;
        }
    }
}

static enum protoCmd pending(struct request_queue * queue, struct item *test) {
    // check all queues and render list to see if this request already queued
    // If so, add this new request as a duplicate
    // call with qLock held
    struct item *item;

    item = lookup_item_idx(queue, test);
    if (item != NULL) {
        if ((item->inQueue == queueRender) || (item->inQueue == queueRequest) || (item->inQueue == queueRequestPrio) || (item->inQueue == queueRequestLow)) {
            test->duplicates = item->duplicates;
            item->duplicates = test;
            test->inQueue = queueDuplicate;
            return cmdIgnore;
        } else if ((item->inQueue == queueDirty) || (item->inQueue == queueRequestBulk)){
            return cmdNotDone;
        }
    }

    return cmdRender;
}

struct item *request_queue_fetch_request(struct request_queue * queue) {
    struct item *item = NULL;

    pthread_mutex_lock(&(queue->qLock));

    while ((queue->reqNum == 0) && (queue->dirtyNum == 0) && (queue->reqLowNum == 0) && (queue->reqPrioNum == 0) && (queue->reqBulkNum == 0)) {
        pthread_cond_wait(&(queue->qCond), &(queue->qLock));
    }
    if (queue->reqPrioNum) {
        item = queue->reqPrioHead.next;
        queue->reqPrioNum--;
        queue->stats.noReqPrioRender++;
    } else if (queue->reqNum) {
        item = queue->reqHead.next;
        queue->reqNum--;
        queue->stats.noReqRender++;
    } else if (queue->reqLowNum) {
        item = queue->reqLowHead.next;
        queue->reqLowNum--;
        queue->stats.noReqLowRender++;
    } else if (queue->dirtyNum) {
        item = queue->dirtyHead.next;
        queue->dirtyNum--;
        queue->stats.noDirtyRender++;
    } else if (queue->reqBulkNum) {
        item = queue->reqBulkHead.next;
        queue->reqBulkNum--;
        queue->stats.noReqBulkRender++;
    }
    if (item) {
        item->next->prev = item->prev;
        item->prev->next = item->next;

        //Add item to render queue
        item->prev = &(queue->renderHead);
        item->next = queue->renderHead.next;
        queue->renderHead.next->prev = item;
        queue->renderHead.next = item;
        item->inQueue = queueRender;
    }

    pthread_mutex_unlock(&queue->qLock);

    return item;
}

/* If a fd becomes invalid for returning request information, remove it from all
 * requests to not send feedback to invalid FDs
 */
void request_queue_clear_requests_by_fd(struct request_queue * queue, int fd) {
    struct item *item, *dupes, *queueHead;

    /**Only need to look up on the shorter request and render queue,
     * as the all requests on the dirty queue already have a FD_INVALID
     * as a file descriptor, so using the linear list shouldn't be a problem
     */
    pthread_mutex_lock(&(queue->qLock));
    for (int i = 0; i < 4; i++) {
        switch (i) {
        case 0: { queueHead = &(queue->reqHead); break;}
        case 1: { queueHead = &(queue->renderHead); break;}
        case 2: { queueHead = &(queue->reqPrioHead); break;}
        case 3: { queueHead = &(queue->reqBulkHead); break;}
        }

        item = queueHead->next;
        while (item != queueHead) {
            if (item->fd == fd)
                item->fd = FD_INVALID;

            dupes = item->duplicates;
            while (dupes) {
                if (dupes->fd == fd)
                    dupes->fd = FD_INVALID;
                dupes = dupes->duplicates;
            }
            item = item->next;
        }
    }

    pthread_mutex_unlock(&(queue->qLock));
}

enum protoCmd request_queue_add_request(struct request_queue * queue, struct item *item) {
    enum protoCmd status;
    const struct protocol *req;
    struct item *list = NULL;
    req = &(item->req);
    if (queue == NULL) {
        printf("queue os NULL");
        exit(3);
    }

    pthread_mutex_lock(&(queue->qLock));

    // Check for a matching request in the current rendering or dirty queues
    status = pending(queue, item);
    if (status == cmdNotDone) {
        // We found a match in the dirty queue, can not wait for it
        pthread_mutex_unlock(&(queue->qLock));
        free(item);
        return cmdNotDone;
    }
    if (status == cmdIgnore) {
        // Found a match in render queue, item added as duplicate
        pthread_mutex_unlock(&(queue->qLock));
        return cmdIgnore;
    }

    // New request, add it to render or dirty queue
    if ((req->cmd == cmdRender) && (queue->reqNum < REQ_LIMIT)) {
        list = &(queue->reqHead);
        item->inQueue = queueRequest;
        item->originatedQueue = queueRequest;
        queue->reqNum++;
    } else if ((req->cmd == cmdRenderPrio) && (queue->reqPrioNum < REQ_LIMIT)) {
        list = &(queue->reqPrioHead);
        item->inQueue = queueRequestPrio;
        item->originatedQueue = queueRequestPrio;
        queue->reqPrioNum++;
    } else if ((req->cmd == cmdRenderLow) && (queue->reqLowNum < REQ_LIMIT)) {
        list = &(queue->reqLowHead);
        item->inQueue = queueRequestLow;
        item->originatedQueue = queueRequestLow;
        queue->reqLowNum++;
    } else if ((req->cmd == cmdRenderBulk) && (queue->reqBulkNum < REQ_LIMIT)) {
        list = &(queue->reqBulkHead);
        item->inQueue = queueRequestBulk;
        item->originatedQueue = queueRequestBulk;
        queue->reqBulkNum++;
    } else if (queue->dirtyNum < DIRTY_LIMIT) {
        list = &(queue->dirtyHead);
        item->inQueue = queueDirty;
        item->originatedQueue = queueDirty;
        queue->dirtyNum++;
        item->fd = FD_INVALID; // No response after render
    } else {
        // The queue is severely backlogged. Drop request
        queue->stats.noReqDroped++;
        pthread_mutex_unlock(&(queue->qLock));
        free(item);
        return cmdNotDone;
    }

    if (list) {
        item->next = list;
        item->prev = list->prev;
        item->prev->next = item;
        list->prev = item;
        /* In addition to the linked list, add item to a hash table index
         * for faster lookup of pending requests.
         */
        insert_item_idx(queue, item);

        pthread_cond_signal(&queue->qCond);
    } else
        free(item);

    pthread_mutex_unlock(&queue->qLock);

    return (list == &(queue->dirtyHead))?cmdNotDone:cmdIgnore;
}

void request_queue_remove_request(struct request_queue * queue, struct item * request, int render_time) {
    pthread_mutex_lock(&(queue->qLock));
    if (request->inQueue != queueRender) {
        syslog(LOG_WARNING, "Removing request from queue, even though not on rendering queue");
    }
    if (render_time > 0) {
        switch (request->originatedQueue) {
        case queueRequestPrio: { queue->stats.timeReqPrioRender += render_time; break;}
        case queueRequest: { queue->stats.timeReqRender += render_time; break;}
        case queueRequestLow: { queue->stats.timeReqLowRender += render_time; break;}
        case queueDirty: { queue->stats.timeReqDirty += render_time; break;}
        case queueRequestBulk: { queue->stats.timeReqBulkRender += render_time; break;}
        }
        queue->stats.noZoomRender[request->req.z]++;
        queue->stats.timeZoomRender[request->req.z] += render_time;
    }
    request->next->prev = request->prev;
    request->prev->next = request->next;
    remove_item_idx(queue, request);
    pthread_mutex_unlock(&(queue->qLock));
}

int request_queue_no_requests_queued(struct request_queue * queue, enum protoCmd priority) {
    int noReq = -1;
    pthread_mutex_lock(&(queue->qLock));
    switch(priority) {
    case cmdRenderPrio:
        noReq = queue->reqPrioNum;
        break;
    case cmdRender:
        noReq = queue->reqNum;
        break;
    case cmdRenderLow:
        noReq = queue->reqLowNum;
        break;
    case cmdDirty:
        noReq = queue->dirtyNum;
        break;
    case cmdRenderBulk:
        noReq = queue->reqBulkNum;
        break;
    }
    pthread_mutex_unlock(&queue->qLock);
    return noReq;
}

void request_queue_copy_stats(struct request_queue * queue, stats_struct * stats) {
    pthread_mutex_lock(&(queue->qLock));
    memcpy(stats, &(queue->stats), sizeof(stats_struct));
    pthread_mutex_unlock(&queue->qLock);
}

struct request_queue * request_queue_init() {
    int res;
    struct request_queue * queue = calloc(1,sizeof (struct request_queue));
    if (queue == NULL) {
        return NULL;
    }

    res = pthread_mutex_init(&(queue->qLock), NULL);
    if (res != 0) {
        syslog(LOG_ERR, "Failed to create mutex for request_queue");
        free(queue);
        return NULL;
    }
    res = pthread_cond_init(&(queue->qCond), NULL);
    if (res != 0) {
        syslog(LOG_ERR, "Failed to create condition variable for request_queue");
        pthread_mutex_destroy(&(queue->qLock));
        free(queue);
        return NULL;
    }

    queue->stats.noDirtyRender = 0;
    queue->stats.noReqDroped = 0;
    queue->stats.noReqRender = 0;
    queue->stats.noReqPrioRender = 0;
    queue->stats.noReqLowRender = 0;
    queue->stats.noReqBulkRender = 0;

    queue->reqHead.next = queue->reqHead.prev = &(queue->reqHead);
    queue->reqPrioHead.next = queue->reqPrioHead.prev = &(queue->reqPrioHead);
    queue->reqLowHead.next = queue->reqLowHead.prev = &(queue->reqLowHead);
    queue->reqBulkHead.next = queue->reqBulkHead.prev = &(queue->reqBulkHead);
    queue->dirtyHead.next = queue->dirtyHead.prev = &(queue->dirtyHead);
    queue->renderHead.next = queue->renderHead.prev = &(queue->renderHead);
    queue->hashidxSize = HASHIDX_SIZE;
    queue->item_hashidx = (struct item_idx *) malloc(sizeof(struct item_idx) * queue->hashidxSize);
    bzero(queue->item_hashidx, sizeof(struct item_idx) * queue->hashidxSize);

    return queue;
}

void request_queue_close(struct request_queue * queue) {
    //TODO: Free items if the queues are not empty at closing time
    pthread_mutex_destroy(&(queue->qLock));
    free(queue->item_hashidx);
    free(queue);
}
