#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <getopt.h>

#include "render_config.h"
#include "daemon.h"
#include "gen_tile.h"
#include "protocol.h"

#define PIDFILE "/var/run/renderd/renderd.pid"

// extern "C" {
#include "iniparser3.0b/src/iniparser.h"
// }

static pthread_t *render_threads;
static pthread_t *slave_threads;
static struct sigaction sigPipeAction;

static struct item reqHead, reqPrioHead, reqBulkHead, dirtyHead, renderHead;
static struct item_idx * item_hashidx;
static int reqNum, reqPrioNum, reqBulkNum, dirtyNum;
static pthread_mutex_t qLock;
static pthread_cond_t qCond;
static int exit_pipe_fd;

static stats_struct stats;
static pthread_t stats_thread;

static renderd_config config;

int noSlaveRenders;
int hashidxSize;

void statsRenderFinish(int z, long time) {
    pthread_mutex_lock(&qLock);
    if ((z >= 0) && (z <= MAX_ZOOM)) {
        stats.noZoomRender[z]++;
        stats.timeZoomRender[z] += time;
    }
    pthread_mutex_unlock(&qLock);
}

struct item *fetch_request(void)
{
    struct item *item = NULL;

    pthread_mutex_lock(&qLock);

    while ((reqNum == 0) && (dirtyNum == 0) && (reqPrioNum == 0) && (reqBulkNum == 0)) {
        pthread_cond_wait(&qCond, &qLock);
    }
    if (reqPrioNum) {
        item = reqPrioHead.next;
        reqPrioNum--;
        stats.noReqPrioRender++;
    } else if (reqNum) {
        item = reqHead.next;
        reqNum--;
        stats.noReqRender++;
    } else if (dirtyNum) {
        item = dirtyHead.next;
        dirtyNum--;
        stats.noDirtyRender++;
    } else if (reqBulkNum) {
        item = reqBulkHead.next;
        reqBulkNum--;
        stats.noReqBulkRender++;
    }
    if (item) {
        item->next->prev = item->prev;
        item->prev->next = item->next;

        item->prev = &renderHead;
        item->next = renderHead.next;
        renderHead.next->prev = item;
        renderHead.next = item;
        item->inQueue = queueRender;
    }


    pthread_mutex_unlock(&qLock);

    return item;
}

void clear_requests(int fd)
{
    struct item *item, *dupes, *queueHead;

    /**Only need to look up on the shorter request and render queue
      * so using the linear list shouldn't be a problem
      */
    pthread_mutex_lock(&qLock);
    for (int i = 0; i < 4; i++) {
        switch (i) {
        case 0: { queueHead = &reqHead; break;}
        case 1: { queueHead = &renderHead; break;}
        case 2: { queueHead = &reqPrioHead; break;}
        case 3: { queueHead = &reqBulkHead; break;}
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

    pthread_mutex_unlock(&qLock);
}


static int calcHashKey(struct item *item) {
    uint64_t xmlnameHash = 0;
    uint64_t key;
    for (int i = 0; item->req.xmlname[i] != 0; i++) {
        xmlnameHash += item->req.xmlname[i];
    }
    key = ((uint64_t)(xmlnameHash & 0x1FF) << 52) + ((uint64_t)(item->req.z) << 48) + ((uint64_t)(item->mx & 0xFFFFFF) << 24) + (item->my & 0xFFFFFF);
    return key % hashidxSize;
}

void insert_item_idx(struct item *item) {
    struct item_idx * nextItem;
    struct item_idx * prevItem;

    int key = calcHashKey(item);

    if (item_hashidx[key].item == NULL) {
        item_hashidx[key].item = item;
    } else {
        prevItem = &(item_hashidx[key]);
        nextItem = item_hashidx[key].next;
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

void remove_item_idx(struct item * item) {
    int key = calcHashKey(item);
    struct item_idx * nextItem;
    struct item_idx * prevItem;
    struct item * test;
    if (item_hashidx[key].item == NULL) {
        //item not in index;
        return;
    }
    prevItem = &(item_hashidx[key]);
    nextItem = &(item_hashidx[key]);

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
                if (nextItem == &(item_hashidx[key])) {
                    prevItem = nextItem->next;
                    memcpy(&(item_hashidx[key]), nextItem->next,
                            sizeof(struct item_idx));
                    free(prevItem);
                } else {
                    prevItem->next = nextItem->next;
                }
            } else {
                prevItem->next = NULL;
            }

            if (nextItem != &(item_hashidx[key])) {
                free(nextItem);
            }
            return;
        } else {
            prevItem = nextItem;
            nextItem = nextItem->next;
        }
    }
}

struct item * lookup_item_idx(struct item * item) {
    struct item_idx * nextItem;
    struct item * test;

    int key = calcHashKey(item);

    if (item_hashidx[key].item == NULL) {
        return NULL;
    } else {
        nextItem = &(item_hashidx[key]);
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

static const char *cmdStr(enum protoCmd c)
{
    switch (c) {
        case cmdIgnore:  return "Ignore";
        case cmdRender:  return "Render";
        case cmdRenderPrio:  return "RenderPrio";
        case cmdRenderBulk:  return "RenderBulk";
        case cmdDirty:   return "Dirty";
        case cmdDone:    return "Done";
        case cmdNotDone: return "NotDone";
        default:         return "unknown";
    }
}

void send_response(struct item *item, enum protoCmd rsp)
{
    struct protocol *req = &item->req;
    int ret;

    pthread_mutex_lock(&qLock);
    item->next->prev = item->prev;
    item->prev->next = item->next;
    remove_item_idx(item);
    pthread_mutex_unlock(&qLock);

    while (item) {
        struct item *prev = item;
        req = &item->req;
        if ((item->fd != FD_INVALID) && ((req->cmd == cmdRender) || (req->cmd == cmdRenderPrio) || (req->cmd == cmdRenderBulk))) {
            req->cmd = rsp;
            //fprintf(stderr, "Sending message %s to %d\n", cmdStr(rsp), item->fd);
            ret = send(item->fd, req, sizeof(*req), 0);
            if (ret != sizeof(*req))
                perror("send error during send_done");
        }
        item = item->duplicates;
        free(prev);
    }
}


enum protoCmd pending(struct item *test)
{
    // check all queues and render list to see if this request already queued
    // If so, add this new request as a duplicate
    // call with qLock held
    struct item *item;

    item = lookup_item_idx(test);
    if (item != NULL) {
        if ((item->inQueue == queueRender) || (item->inQueue == queueRequest) || (item->inQueue == queueRequestPrio)) {
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


enum protoCmd rx_request(const struct protocol *req, int fd)
{
    struct protocol *reqnew;
    struct item *list = NULL, *item;
    enum protoCmd pend;

    // Upgrade version 1 to version 2
    if (req->ver == 1) {
        reqnew = (struct protocol *)malloc(sizeof(struct protocol));
        memcpy(reqnew, req, sizeof(struct protocol_v1));
        reqnew->xmlname[0] = 0;
        req = reqnew;
    }
    else if (req->ver != 2) {
        syslog(LOG_ERR, "Bad protocol version %d", req->ver);
        return cmdIgnore;
    }

    syslog(LOG_DEBUG, "DEBUG: Got command %s fd(%d) xml(%s), z(%d), x(%d), y(%d)",
            cmdStr(req->cmd), fd, req->xmlname, req->z, req->x, req->y);

    if ((req->cmd != cmdRender) && (req->cmd != cmdRenderPrio) && (req->cmd != cmdDirty) && (req->cmd != cmdRenderBulk))
        return cmdIgnore;

    item = (struct item *)malloc(sizeof(*item));
    if (!item) {
            syslog(LOG_ERR, "malloc failed");
            return cmdNotDone;
    }

    item->req = *req;
    item->duplicates = NULL;
    item->fd = (req->cmd == cmdDirty) ? FD_INVALID : fd;

#ifdef METATILE
    /* Round down request co-ordinates to the neareast N (should be a power of 2)
     * Note: request path is no longer consistent but this will be recalculated
     * when the metatile is being rendered.
     */
    item->mx = item->req.x & ~(METATILE-1);
    item->my = item->req.y & ~(METATILE-1);
#else
    item->mx = item->req.x;
    item->my = item->req.y;
#endif

    pthread_mutex_lock(&qLock);

    // Check for a matching request in the current rendering or dirty queues
    pend = pending(item);
    if (pend == cmdNotDone) {
        // We found a match in the dirty queue, can not wait for it
        pthread_mutex_unlock(&qLock);
        free(item);
        return cmdNotDone;
    }
    if (pend == cmdIgnore) {
        // Found a match in render queue, item added as duplicate
        pthread_mutex_unlock(&qLock);
        return cmdIgnore;
    }

    // New request, add it to render or dirty queue
    if ((req->cmd == cmdRender) && (reqNum < REQ_LIMIT)) {
        list = &reqHead;
        item->inQueue = queueRequest;
        reqNum++;
    } else if ((req->cmd == cmdRenderPrio) && (reqPrioNum < REQ_LIMIT)) {
        list = &reqPrioHead;
        item->inQueue = queueRequestPrio;
        reqPrioNum++;
    } else if ((req->cmd == cmdRenderBulk) && (reqBulkNum < REQ_LIMIT)) {
        list = &reqBulkHead;
        item->inQueue = queueRequestBulk;
        reqBulkNum++;
    } else if (dirtyNum < DIRTY_LIMIT) {
        list = &dirtyHead;
        item->inQueue = queueDirty;
        dirtyNum++;
        item->fd = FD_INVALID; // No response after render
    } else {
        // The queue is severely backlogged. Drop request
        stats.noReqDroped++;
        pthread_mutex_unlock(&qLock);
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
        insert_item_idx(item);

        pthread_cond_signal(&qCond);
    } else
        free(item);

    pthread_mutex_unlock(&qLock);

    return (list == &reqHead)?cmdIgnore:cmdNotDone;
}

void request_exit(void)
{
  // Any write to the exit pipe will trigger a graceful exit
  char c=0;
  if (write(exit_pipe_fd, &c, sizeof(c)) < 0) {
      fprintf(stderr, "Failed to write to the exit pipe: %s\n", strerror(errno));
  }
}

void process_loop(int listen_fd)
{
    int num_connections = 0;
    int connections[MAX_CONNECTIONS];
    int pipefds[2];
    int exit_pipe_read;

    bzero(connections, sizeof(connections));

    // A pipe is used to allow the render threads to request an exit by the main process
    if (pipe(pipefds)) {
      fprintf(stderr, "Failed to create pipe\n");
      return;
    }
    exit_pipe_fd = pipefds[1];
    exit_pipe_read = pipefds[0];

    while (1) {
        struct sockaddr_un in_addr;
        socklen_t in_addrlen = sizeof(in_addr);
        fd_set rd;
        int incoming, num, nfds, i;

        FD_ZERO(&rd);
        FD_SET(listen_fd, &rd);
        nfds = listen_fd+1;

        for (i=0; i<num_connections; i++) {
            FD_SET(connections[i], &rd);
            nfds = MAX(nfds, connections[i]+1);
        }

	FD_SET(exit_pipe_read, &rd);
	nfds = MAX(nfds, exit_pipe_read+1);

        num = select(nfds, &rd, NULL, NULL, NULL);
        if (num == -1)
            perror("select()");
        else if (num) {
	    if (FD_ISSET(exit_pipe_read, &rd)) {
	      // A render thread wants us to exit
	      break;
	    }

            //printf("Data is available now on %d fds\n", num);
            if (FD_ISSET(listen_fd, &rd)) {
                num--;
                incoming = accept(listen_fd, (struct sockaddr *) &in_addr, &in_addrlen);
                if (incoming < 0) {
                    perror("accept()");
                } else {
                    if (num_connections == MAX_CONNECTIONS) {
                        syslog(LOG_WARNING, "Connection limit(%d) reached. Dropping connection\n", MAX_CONNECTIONS);
                        close(incoming);
                    } else {
                        connections[num_connections++] = incoming;
                        syslog(LOG_DEBUG, "DEBUG: Got incoming connection, fd %d, number %d\n", incoming, num_connections);
                    }
                }
            }
            for (i=0; num && (i<num_connections); i++) {
                int fd = connections[i];
                if (FD_ISSET(fd, &rd)) {
                    struct protocol cmd;
                    int ret;

                    // TODO: to get highest performance we should loop here until we get EAGAIN
                    ret = recv(fd, &cmd, sizeof(cmd), MSG_DONTWAIT);
                    if (ret == sizeof(cmd)) {
                        enum protoCmd rsp = rx_request(&cmd, fd);

                        if ((cmd.cmd == cmdRender) && (rsp == cmdNotDone)) {
                            cmd.cmd = rsp;
                            syslog(LOG_DEBUG, "DEBUG: Sending NotDone response(%d)\n", rsp);
                            ret = send(fd, &cmd, sizeof(cmd), 0);
                            if (ret != sizeof(cmd))
                                perror("response send error");
                        }
                    } else if (!ret) {
                        int j;

                        num_connections--;
                        syslog(LOG_DEBUG, "DEBUG: Connection %d, fd %d closed, now %d left\n", i, fd, num_connections);
                        for (j=i; j < num_connections; j++)
                            connections[j] = connections[j+1];
                        clear_requests(fd);
                        close(fd);
                    } else {
                        syslog(LOG_ERR, "Recv Error on fd %d", fd);
                        break;
                    }
                }
            }
        } else {
            syslog(LOG_ERR, "Select timeout");
        }
    }
}

/**
 * Periodically write out current stats to a stats file. This information
 * can then be used to monitor performance of renderd e.g. with a munin plugin
 */
void *stats_writeout_thread(void * arg) {
    stats_struct lStats;
    int dirtQueueLength;
    int reqQueueLength;
    int reqPrioQueueLength;
    int reqBulkQueueLength;
	int i;

    int noFailedAttempts = 0;
    char tmpName[PATH_MAX];

    snprintf(tmpName, sizeof(tmpName), "%s.tmp", config.stats_filename);

    syslog(LOG_DEBUG, "Starting stats thread");
    while (1) {
        pthread_mutex_lock(&qLock);
        memcpy(&lStats, &stats, sizeof(stats_struct));
        dirtQueueLength = dirtyNum;
        reqQueueLength = reqNum;
        reqPrioQueueLength = reqPrioNum;
        reqBulkQueueLength = reqBulkNum;
        pthread_mutex_unlock(&qLock);

        FILE * statfile = fopen(tmpName, "w");
        if (statfile == NULL) {
            syslog(LOG_WARNING, "Failed to open stats file: %i", errno);
            noFailedAttempts++;
            if (noFailedAttempts > 3) {
                syslog(LOG_ERR, "ERROR: Failed repeatedly to write stats, giving up");
                break;
            }
            continue;
        } else {
            noFailedAttempts = 0;
            fprintf(statfile, "ReqQueueLength: %i\n", reqQueueLength);
            fprintf(statfile, "ReqPrioQueueLength: %i\n", reqPrioQueueLength);
            fprintf(statfile, "ReqBulkQueueLength: %i\n", reqBulkQueueLength);
            fprintf(statfile, "DirtQueueLength: %i\n", dirtQueueLength);
            fprintf(statfile, "DropedRequest: %li\n", lStats.noReqDroped);
            fprintf(statfile, "ReqRendered: %li\n", lStats.noReqRender);
            fprintf(statfile, "ReqPrioRendered: %li\n", lStats.noReqPrioRender);
            fprintf(statfile, "ReqBulkRendered: %li\n", lStats.noReqBulkRender);
            fprintf(statfile, "DirtyRendered: %li\n", lStats.noDirtyRender);
            for (i = 0; i <= MAX_ZOOM; i++) {
                fprintf(statfile,"ZoomRendered%02i: %li\n", i, lStats.noZoomRender[i]);
            }
            for (i = 0; i <= MAX_ZOOM; i++) {
                fprintf(statfile,"TimeRenderedZoom%02i: %li\n", i, lStats.timeZoomRender[i]);
            }
            fclose(statfile);
            if (rename(tmpName, config.stats_filename)) {
                syslog(LOG_WARNING, "Failed to overwrite stats file: %i", errno);
                noFailedAttempts++;
                if (noFailedAttempts > 3) {
                    syslog(LOG_ERR, "ERROR: Failed repeatedly to overwrite stats, giving up");
                    break;
                }
                continue;
            }
        }
        sleep(10);
    }
    return NULL;
}

int client_socket_init(renderd_config * sConfig) {
    int fd, s;
    struct sockaddr_un * addrU;
    struct addrinfo hints; 
    struct addrinfo *result, *rp;
    char portnum[16]; 
    char ipstring[INET6_ADDRSTRLEN];

    if (sConfig->ipport > 0) {
        syslog(LOG_INFO, "Initialising TCP/IP client socket to %s:%i", sConfig->iphostname, sConfig->ipport);
        
        memset(&hints, 0, sizeof(struct addrinfo)); 
        hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */ 
        hints.ai_socktype = SOCK_STREAM; /* TCP socket */ 
        hints.ai_flags = 0; 
        hints.ai_protocol = 0;          /* Any protocol */ 
        hints.ai_canonname = NULL; 
        hints.ai_addr = NULL; 
        hints.ai_next = NULL; 
        sprintf(portnum,"%i", sConfig->ipport);

        s = getaddrinfo(sConfig->iphostname, portnum, &hints, &result); 
        if (s != 0) { 
            syslog(LOG_INFO, "failed to resolve hostname of rendering slave"); 
            return FD_INVALID; 
        }
        
        /* getaddrinfo() returns a list of address structures. 
           Try each address until we successfully connect. */ 
        for (rp = result; rp != NULL; rp = rp->ai_next) { 
            inet_ntop(rp->ai_family, rp->ai_family == AF_INET ? &(((struct sockaddr_in *)rp->ai_addr)->sin_addr) : 
                      &(((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr) , ipstring, rp->ai_addrlen); 
            syslog(LOG_DEBUG, "Connecting TCP socket to rendering daemon at %s", ipstring); 
            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol); 
            if (fd < 0) 
                continue; 
            if (connect(fd, rp->ai_addr, rp->ai_addrlen) != 0) { 
                syslog(LOG_INFO, "failed to connect to rendering daemon (%s), trying next ip", ipstring); 
                close(fd);
                fd = -1;
                continue;
            } else { 
                break; 
            } 
        }
        freeaddrinfo(result);
        
        if (fd < 0) { 
            syslog(LOG_WARNING, "failed to connect to %s:%i", sConfig->iphostname, sConfig->ipport); 
            return FD_INVALID; 
        }

        syslog(LOG_INFO, "socket %s:%i initialised to fd %i", sConfig->iphostname, sConfig->ipport, fd);
    } else {
        syslog(LOG_INFO, "Initialising unix client socket on %s",
                sConfig->socketname);
        addrU = (struct sockaddr_un *)malloc(sizeof(struct sockaddr_un));
        fd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            syslog(LOG_WARNING, "Could not obtain socket: %i", fd);
            free(addrU);
            return FD_INVALID;
        }

        bzero(addrU, sizeof(struct sockaddr_un));
        addrU->sun_family = AF_UNIX;
        strncpy(addrU->sun_path, sConfig->socketname, sizeof(addrU->sun_path) - 1);

        if (connect(fd, (struct sockaddr *) addrU, sizeof(struct sockaddr_un)) < 0) {
            syslog(LOG_WARNING, "socket connect failed for: %s",
                    sConfig->socketname);
            close(fd);
            free(addrU);
            return FD_INVALID;
        }
        free(addrU);
        syslog(LOG_INFO, "socket %s initialised to fd %i", sConfig->socketname,
                fd);
    }
    return fd;
}

int server_socket_init(renderd_config *sConfig) {
    struct sockaddr_un addrU;
    struct sockaddr_in6 addrI;
    mode_t old;
    int fd;

    if (sConfig->ipport > 0) {
        syslog(LOG_INFO, "Initialising TCP/IP server socket on %s:%i",
                sConfig->iphostname, sConfig->ipport);
        fd = socket(PF_INET6, SOCK_STREAM, 0);
        if (fd < 0) {
            fprintf(stderr, "failed to create IP socket\n");
            exit(2);
        }
        bzero(&addrI, sizeof(addrI));
        addrI.sin6_family = AF_INET6;
        addrI.sin6_addr = in6addr_any;
        addrI.sin6_port = htons(sConfig->ipport);
        if (bind(fd, (struct sockaddr *) &addrI, sizeof(addrI)) < 0) {
            fprintf(stderr, "socket bind failed for: %s:%i\n",
                    sConfig->iphostname, sConfig->ipport);
            close(fd);
            exit(3);
        }
    } else {
        syslog(LOG_INFO, "Initialising unix server socket on %s",
                sConfig->socketname);

        fd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            fprintf(stderr, "failed to create unix socket\n");
            exit(2);
        }

        bzero(&addrU, sizeof(addrU));
        addrU.sun_family = AF_UNIX;
        strncpy(addrU.sun_path, sConfig->socketname, sizeof(addrU.sun_path) - 1);

        unlink(addrU.sun_path);

        old = umask(0); // Need daemon socket to be writeable by apache
        if (bind(fd, (struct sockaddr *) &addrU, sizeof(addrU)) < 0) {
            fprintf(stderr, "socket bind failed for: %s\n", sConfig->socketname);
            close(fd);
            exit(3);
        }
        umask(old);
    }

    if (listen(fd, QUEUE_MAX) < 0) {
        fprintf(stderr, "socket listen failed for %d\n", QUEUE_MAX);
        close(fd);
        exit(4);
    }

    syslog(LOG_DEBUG, "Created server socket %i", fd);

    return fd;

}

/**
 * This function is used as a the start function for the slave renderer thread.
 * It pulls a request from the central queue of requests and dispatches it to
 * the slave renderer. It then blocks and waits for the response with no timeout.
 * As it only sends one request at a time (there are as many slave_thread threads as there
 * are rendering threads on the slaves) nothing gets queued on the slave and should get
 * rendererd immediately. Thus overall, requests should be nicely load balanced between
 * all the rendering threads available both locally and in the slaves.
 */
void *slave_thread(void * arg) {
    renderd_config * sConfig = (renderd_config *) arg;

    int pfd = FD_INVALID;
    int retry;
    size_t ret_size;

    struct protocol * resp;
    struct protocol * req_slave;

    req_slave = (struct protocol *)malloc(sizeof(struct protocol));
    resp = (struct protocol *)malloc(sizeof(struct protocol));
    bzero(req_slave, sizeof(struct protocol));
    bzero(resp, sizeof(struct protocol));

    while (1) {
        if (pfd == FD_INVALID) {
            pfd = client_socket_init(sConfig);
            if (pfd == FD_INVALID) {
                if (sConfig->ipport > 0) {
                    syslog(LOG_ERR,
                        "Failed to connect to render slave %s:%i, trying again in 30 seconds",
                        sConfig->iphostname, sConfig->ipport);
                } else {
                    syslog( LOG_ERR,
                            "Failed to connect to render slave %s, trying again in 30 seconds",
                            sConfig->socketname);
                }
                sleep(30);
                continue;
            }
        }

        enum protoCmd ret;
        struct item *item = fetch_request();
        if (item) {
            struct protocol *req = &item->req;
            req_slave->ver = PROTO_VER;
            req_slave->cmd = cmdRender;
            strcpy(req_slave->xmlname, req->xmlname);
            req_slave->x = req->x;
            req_slave->y = req->y;
            req_slave->z = req->z;

            /*Dispatch request to slave renderd*/
            retry = 2;
            syslog(LOG_INFO,
                    "Dispatching request to slave thread on fd %i", pfd);
            do {
                ret_size = send(pfd, req_slave, sizeof(struct protocol), 0);

                if (ret_size == sizeof(struct protocol)) {
                    //correctly sent command to slave
                    break;
                }

                if (errno != EPIPE) {
                    syslog(LOG_ERR,
                            "Failed to send cmd to render slave, shutting down thread");
                    free(resp);
                    free(req_slave);
                    close(pfd);
                    return NULL;
                }

                syslog(LOG_WARNING, "Failed to send cmd to render slave, retrying");
                close(pfd);
                pfd = client_socket_init(sConfig);
                if (pfd == FD_INVALID) {
                    syslog(LOG_ERR,
                            "Failed to re-connect to render slave, dropping request");
                    ret = cmdNotDone;
                    send_response(item, ret);
                    break;
                }
            } while (retry--);
            if (pfd == FD_INVALID || ret_size != sizeof(struct protocol)) {
                continue;
            }

            ret_size = 0;
            retry = 10;
            while ((ret_size < sizeof(struct protocol)) && (retry > 0)) {
                ret_size = recv(pfd, resp + ret_size, (sizeof(struct protocol)
                        - ret_size), 0);
                if ((errno == EPIPE) || ret_size == 0) {
                    close(pfd);
                    pfd = FD_INVALID;
                    ret_size = 0;
                    syslog(LOG_ERR, "Pipe to render slave closed");
                    break;
                }
                retry--;
            }
            if (ret_size < sizeof(struct protocol)) {
                if (sConfig->ipport > 0) {
                    syslog( LOG_ERR,
                            "Invalid reply from render slave %s:%i, trying again in 30 seconds",
                            sConfig->iphostname, sConfig->ipport);
                } else {
                    syslog( LOG_ERR,
                            "Invalid reply render slave %s, trying again in 30 seconds",
                            sConfig->socketname);
                }

                ret = cmdNotDone;
                send_response(item, ret);
                sleep(30);
            } else {
                ret = resp->cmd;
                send_response(item, ret);
                if (resp->cmd != cmdDone) {
                    if (sConfig->ipport > 0) {
                        syslog( LOG_ERR,
                                "Request from render slave %s:%i did not complete correctly",
                                sConfig->iphostname, sConfig->ipport);
                    } else {
                        syslog( LOG_ERR,
                                "Request from render slave %s did not complete correctly",
                                sConfig->socketname);
                    }
                    //Sleep for a while to make sure we don't overload the renderer
                    //This only happens if it didn't correctly block on the rendering
                    //request
                    sleep(30);
                }
            }

        } else {
            sleep(1); // TODO: Use an event to indicate there are new requests
        }
    }
    free(resp);
    free(req_slave);
    return NULL;
}


int main(int argc, char **argv)
{
    int fd, i, j, k;

    int c;
    int foreground=0;
    int active_slave=0;
    int log_options;
    char config_file_name[PATH_MAX] = RENDERD_CONFIG;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"config", 1, 0, 'c'},
            {"foreground", 1, 0, 'f'},
            {"slave", 1, 0, 's'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "hfc:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'f':
                foreground=1;
                break;
            case 'c':
                strncpy(config_file_name, optarg, PATH_MAX-1);
                config_file_name[PATH_MAX-1] = 0;
                break;
            case 's':
                if (sscanf(optarg, "%i", &active_slave) != 1) {
                    fprintf(stderr, "--slave needs to be nummeric (%s)\n", optarg);
                    active_slave = 0;
                }
                break;
            case 'h':
                fprintf(stderr, "Usage: renderd [OPTION] ...\n");
                fprintf(stderr, "Mapnik rendering daemon\n");
                fprintf(stderr, "  -f, --foreground     run in foreground\n");
                fprintf(stderr, "  -h, --help           display this help and exit\n");
                fprintf(stderr, "  -c, --config=CONFIG  set location of config file (default /etc/renderd.conf)\n");
                fprintf(stderr, "  -s, --slave=CONFIG_NR set which render slave this is (default 0)\n");
                exit(0);
            default:
                fprintf(stderr, "unknown config option '%c'\n", c);
                exit(1);
        }
    }

    log_options = LOG_PID;
#ifdef LOG_PERROR
    if (foreground)
        log_options |= LOG_PERROR;
#endif
    openlog("renderd", log_options, LOG_DAEMON);

    syslog(LOG_INFO, "Rendering daemon started");

    pthread_mutex_init(&qLock, NULL);
    pthread_cond_init(&qCond, NULL);
    reqHead.next = reqHead.prev = &reqHead;
    reqPrioHead.next = reqPrioHead.prev = &reqPrioHead;
    reqBulkHead.next = reqBulkHead.prev = &reqBulkHead;
    dirtyHead.next = dirtyHead.prev = &dirtyHead;
    renderHead.next = renderHead.prev = &renderHead;
    hashidxSize = HASHIDX_SIZE;
    item_hashidx = (struct item_idx *) malloc(sizeof(struct item_idx) * hashidxSize);
    bzero(item_hashidx, sizeof(struct item_idx) * hashidxSize);

    stats.noDirtyRender = 0;
    stats.noReqDroped = 0;
    stats.noReqRender = 0;
    stats.noReqPrioRender = 0;
    stats.noReqBulkRender = 0;

    xmlconfigitem maps[XMLCONFIGS_MAX];
    bzero(maps, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

    renderd_config config_slaves[MAX_SLAVES];
    bzero(config_slaves, sizeof(renderd_config) * MAX_SLAVES);
    bzero(&config, sizeof(renderd_config));

    dictionary *ini = iniparser_load(config_file_name);
    if (! ini) {
        exit(1);
    }

    noSlaveRenders = 0;

    int iconf = -1;
    char buffer[PATH_MAX];
    for (int section=0; section < iniparser_getnsec(ini); section++) {
        char *name = iniparser_getsecname(ini, section);
        syslog(LOG_INFO, "Parsing section %s\n", name);
        if (strncmp(name, "renderd", 7) && strcmp(name, "mapnik")) {
            if (config.tile_dir == NULL) {
                fprintf(stderr, "No valid (active) renderd config section available\n");
                exit(7);
            }
            /* this is a map section */
            iconf++;
            if (iconf >= XMLCONFIGS_MAX) {
                fprintf(stderr, "Config: more than %d configurations found\n", XMLCONFIGS_MAX);
                exit(7);
            }

            if (strlen(name) >= (XMLCONFIG_MAX - 1)) {
                fprintf(stderr, "XML name too long: %s\n", name);
                exit(7);
            }

            strcpy(maps[iconf].xmlname, name);
            
            sprintf(buffer, "%s:uri", name);
            char *ini_uri = iniparser_getstring(ini, buffer, (char *)"");
            if (strlen(ini_uri) >= (PATH_MAX - 1)) {
                fprintf(stderr, "URI too long: %s\n", ini_uri);
                exit(7);
            }
            strcpy(maps[iconf].xmluri, ini_uri);

            sprintf(buffer, "%s:xml", name);
            char *ini_xmlpath = iniparser_getstring(ini, buffer, (char *)"");
            if (strlen(ini_xmlpath) >= (PATH_MAX - 1)){
                fprintf(stderr, "XML path too long: %s\n", ini_xmlpath);
                exit(7);
            }
            strcpy(maps[iconf].xmlfile, ini_xmlpath);

            sprintf(buffer, "%s:host", name);
            char *ini_hostname = iniparser_getstring(ini, buffer, (char *) "");
            if (strlen(ini_hostname) >= (PATH_MAX - 1)) {
                fprintf(stderr, "Host name too long: %s\n", ini_hostname);
                exit(7);
            }
            strcpy(maps[iconf].host, ini_hostname);

            sprintf(buffer, "%s:htcphost", name);
            char *ini_htcpip = iniparser_getstring(ini, buffer, (char *) "");
            if (strlen(ini_htcpip) >= (PATH_MAX - 1)) {
                fprintf(stderr, "HTCP host name too long: %s\n", ini_htcpip);
                exit(7);
            }
            strcpy(maps[iconf].htcpip, ini_htcpip);

            sprintf(buffer, "%s:tilesize", name);
            char *ini_tilesize = iniparser_getstring(ini, buffer, (char *) "256");
            maps[iconf].tile_px_size = atoi(ini_tilesize);
            if (maps[iconf].tile_px_size < 1) {
                fprintf(stderr, "Tile size is invalid: %s\n", ini_tilesize);
                exit(7);
            }

            sprintf(buffer, "%s:tiledir", name);
            char *ini_tiledir = iniparser_getstring(ini, buffer, (char *) config.tile_dir);
            if (strlen(ini_tiledir) >= (PATH_MAX - 1)) {
                fprintf(stderr, "Tiledir too long: %s\n", ini_tiledir);
                exit(7);
            }
            strcpy(maps[iconf].tile_dir, ini_tiledir);
        } else if (strncmp(name, "renderd", 7) == 0) {
            int render_sec = 0;
            if (sscanf(name, "renderd%i", &render_sec) != 1) {
                render_sec = 0;
            }
            syslog(LOG_INFO, "Parsing render section %i\n", render_sec);
            if (render_sec >= MAX_SLAVES) {
                syslog(LOG_ERR, "Can't handle more than %i render sections\n",
                        MAX_SLAVES);
                exit(7);
            }
            sprintf(buffer, "%s:socketname", name);
            config_slaves[render_sec].socketname = iniparser_getstring(ini,
                    buffer, (char *) RENDER_SOCKET);
            sprintf(buffer, "%s:iphostname", name);
            config_slaves[render_sec].iphostname = iniparser_getstring(ini,
                    buffer, "");
            sprintf(buffer, "%s:ipport", name);
            config_slaves[render_sec].ipport = iniparser_getint(ini, buffer, 0);
            sprintf(buffer, "%s:num_threads", name);
            config_slaves[render_sec].num_threads = iniparser_getint(ini,
                    buffer, NUM_THREADS);
            sprintf(buffer, "%s:tile_dir", name);
            config_slaves[render_sec].tile_dir = iniparser_getstring(ini,
                    buffer, (char *) HASH_PATH);
            sprintf(buffer, "%s:stats_file", name);
            config_slaves[render_sec].stats_filename = iniparser_getstring(ini,
                    buffer, NULL);

            if (render_sec == active_slave) {
                config.socketname = config_slaves[render_sec].socketname;
                config.iphostname = config_slaves[render_sec].iphostname;
                config.ipport = config_slaves[render_sec].ipport;
                config.num_threads = config_slaves[render_sec].num_threads;
                config.tile_dir = config_slaves[render_sec].tile_dir;
                config.stats_filename
                        = config_slaves[render_sec].stats_filename;
                config.mapnik_plugins_dir = iniparser_getstring(ini,
                        "mapnik:plugins_dir", (char *) MAPNIK_PLUGINS);
                config.mapnik_font_dir = iniparser_getstring(ini,
                        "mapnik:font_dir", (char *) FONT_DIR);
                config.mapnik_font_dir_recurse = iniparser_getboolean(ini,
                        "mapnik:font_dir_recurse", FONT_RECURSE);
            } else {
                noSlaveRenders += config_slaves[render_sec].num_threads;
            }
        }
    }

    if (config.ipport > 0) {
        syslog(LOG_INFO, "config renderd: ip socket=%s:%i\n", config.iphostname, config.ipport);
    } else {
        syslog(LOG_INFO, "config renderd: unix socketname=%s\n", config.socketname);
    }
    syslog(LOG_INFO, "config renderd: num_threads=%d\n", config.num_threads);
    if (active_slave == 0) {
        syslog(LOG_INFO, "config renderd: num_slaves=%d\n", noSlaveRenders);
    }
    syslog(LOG_INFO, "config renderd: tile_dir=%s\n", config.tile_dir);
    syslog(LOG_INFO, "config renderd: stats_file=%s\n", config.stats_filename);
    syslog(LOG_INFO, "config mapnik:  plugins_dir=%s\n", config.mapnik_plugins_dir);
    syslog(LOG_INFO, "config mapnik:  font_dir=%s\n", config.mapnik_font_dir);
    syslog(LOG_INFO, "config mapnik:  font_dir_recurse=%d\n", config.mapnik_font_dir_recurse);
    for (i = 0; i < MAX_SLAVES; i++) {
        if (config_slaves[i].num_threads == 0) {
            continue;
        }
        if (i == active_slave) {
            syslog(LOG_INFO, "config renderd(%i): Active\n", i);
        }
        if (config_slaves[i].ipport > 0) {
                syslog(LOG_INFO, "config renderd(%i): ip socket=%s:%i\n", i,
                        config_slaves[i].iphostname, config_slaves[i].ipport);
            } else {
                syslog(LOG_INFO, "config renderd(%i): unix socketname=%s\n", i,
                        config_slaves[i].socketname);
            }
        syslog(LOG_INFO, "config renderd(%i): num_threads=%d\n", i,
                config_slaves[i].num_threads);
        syslog(LOG_INFO, "config renderd(%i): tile_dir=%s\n", i,
                config_slaves[i].tile_dir);
        syslog(LOG_INFO, "config renderd(%i): stats_file=%s\n", i,
                config_slaves[i].stats_filename);
    }

    for(iconf = 0; iconf < XMLCONFIGS_MAX; ++iconf) {
        if (maps[iconf].xmlname[0] != 0) {
         syslog(LOG_INFO, "config map %d:   name(%s) file(%s) uri(%s) htcp(%s) host(%s)",
                 iconf, maps[iconf].xmlname, maps[iconf].xmlfile, maps[iconf].xmluri,
                 maps[iconf].htcpip, maps[iconf].host);
        }
    }

    fd = server_socket_init(&config);
#if 0
    if (fcntl(fd, F_SETFD, O_RDWR | O_NONBLOCK) < 0) {
        fprintf(stderr, "setting socket non-block failed\n");
        close(fd);
        exit(5);
    }
#endif

    //sigPipeAction.sa_handler = pipe_handler;
    sigPipeAction.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sigPipeAction, NULL) < 0) {
        fprintf(stderr, "failed to register signal handler\n");
        close(fd);
        exit(6);
    }

    render_init(config.mapnik_plugins_dir, config.mapnik_font_dir, config.mapnik_font_dir_recurse);

    /* unless the command line said to run in foreground mode, fork and detach from terminal */
    if (foreground) {
        fprintf(stderr, "Running in foreground mode...\n");
    } else {
        if (daemon(0, 0) != 0) {
            fprintf(stderr, "can't daemonize: %s\n", strerror(errno));
        }
        /* write pid file */
        FILE *pidfile = fopen(PIDFILE, "w");
        if (pidfile) {
            (void) fprintf(pidfile, "%d\n", getpid());
            (void) fclose(pidfile);
        }
    }

    if (config.stats_filename != NULL) {
        if (pthread_create(&stats_thread, NULL, stats_writeout_thread, NULL)) {
            syslog(LOG_WARNING, "Could not create stats writeout thread");
        }
    } else {
        syslog(LOG_INFO, "No stats file specified in config. Stats reporting disabled");
    }

    render_threads = (pthread_t *) malloc(sizeof(pthread_t) * config.num_threads);

    for(i=0; i<config.num_threads; i++) {
        if (pthread_create(&render_threads[i], NULL, render_thread, (void *)maps)) {
            fprintf(stderr, "error spawning render thread\n");
            close(fd);
            exit(7);
        }
    }

    if (active_slave == 0) {
        //Only the master renderd opens connections to its slaves
        k = 0;
        slave_threads
                = (pthread_t *) malloc(sizeof(pthread_t) * noSlaveRenders);
        for (i = 1; i < MAX_SLAVES; i++) {
            for (j = 0; j < config_slaves[i].num_threads; j++) {
                if (pthread_create(&slave_threads[k++], NULL, slave_thread,
                        (void *) &config_slaves[i])) {
                    fprintf(stderr, "error spawning render thread\n");
                    close(fd);
                    exit(7);
                }
            }
        }
    }

    process_loop(fd);

    unlink(config.socketname);
    close(fd);
    return 0;
}
