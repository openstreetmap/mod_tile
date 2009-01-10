#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "daemon.h"
#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"

static pthread_t render_threads[NUM_THREADS];
static struct sigaction sigPipeAction;

static struct item reqHead, dirtyHead, renderHead;
static int reqNum, dirtyNum;
static pthread_mutex_t qLock;
static pthread_cond_t qCond;

struct item *fetch_request(void)
{
    struct item *item = NULL;

    pthread_mutex_lock(&qLock);

    while (reqNum == 0 && dirtyNum == 0) {
        pthread_cond_wait(&qCond, &qLock);
    }
    if (reqNum) {
        item = reqHead.next;
        reqNum--;
    } else if (dirtyNum) {
        item = dirtyHead.next;
        dirtyNum--;
    }
    if (item) {
        item->next->prev = item->prev;
        item->prev->next = item->next;

        item->prev = &renderHead;
        item->next = renderHead.next;
        renderHead.next->prev = item;
        renderHead.next = item;
    }

    pthread_mutex_unlock(&qLock);

    return item;
}

void clear_requests(int fd)
{
    struct item *item, *dupes;

    pthread_mutex_lock(&qLock);
    item = reqHead.next;
    while (item != &reqHead) {
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

    item = renderHead.next;
    while (item != &renderHead) {
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

    pthread_mutex_unlock(&qLock);
}

static inline const char *cmdStr(enum protoCmd c)
{
    switch (c) {
        case cmdIgnore:  return "Ignore";
        case cmdRender:  return "Render";
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
    pthread_mutex_unlock(&qLock);

    while (item) {
        struct item *prev = item;
        req = &item->req;
        if ((item->fd != FD_INVALID) && (req->cmd == cmdRender)) {
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

    item = renderHead.next;
    while (item != &renderHead) {
        if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z == test->req.z) && (!strcmp(item->req.xmlname, test->req.xmlname))) {
            // Add new test item in the list of duplicates
            test->duplicates = item->duplicates;
            item->duplicates = test;
            return cmdIgnore;
        }
        item = item->next;
    }

    item = reqHead.next;
    while (item != &reqHead) {
        if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z == test->req.z) && (!strcmp(item->req.xmlname, test->req.xmlname))) {
            // Add new test item in the list of duplicates
            test->duplicates = item->duplicates;
            item->duplicates = test;
            return cmdIgnore;
        }
        item = item->next;
    }

    item = dirtyHead.next;
    while (item != &dirtyHead) {
        if ((item->mx == test->mx) && (item->my == test->my) && (item->req.z == test->req.z) && (!strcmp(item->req.xmlname, test->req.xmlname)))
            return cmdNotDone;
        item = item->next;
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
        reqnew = (struct protocol *)malloc(sizeof(protocol));
        memcpy(reqnew, req, sizeof(protocol_v1));
        reqnew->xmlname[0] = 0;
        req = reqnew;
    }
    else if (req->ver != 2) {
        fprintf(stderr, "Bad protocol version %d\n", req->ver);
        return cmdIgnore;
    }

    fprintf(stderr, "%s fd(%d) xml(%s), z(%d), x(%d), y(%d)\n",
            cmdStr(req->cmd), fd, req->xmlname, req->z, req->x, req->y);

    if ((req->cmd != cmdRender) && (req->cmd != cmdDirty))
        return cmdIgnore;

    if (check_xyz(req->x, req->y, req->z))
        return cmdNotDone;

    item = (struct item *)malloc(sizeof(*item));
    if (!item) {
            fprintf(stderr, "malloc failed\n");
            return cmdNotDone;
    }

    item->req = *req;
    item->duplicates = NULL;
    item->fd = (req->cmd == cmdRender) ? fd : FD_INVALID;

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

    if (dirtyNum == DIRTY_LIMIT) {
        // The queue is severely backlogged. Drop request
        pthread_mutex_unlock(&qLock);
        free(item);
        return cmdNotDone;
    }

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
        reqNum++;
    } else if (dirtyNum < DIRTY_LIMIT) {
        list = &dirtyHead;
        dirtyNum++;
        item->fd = FD_INVALID; // No response after render
    }

    if (list) {
        item->next = list;
        item->prev = list->prev;
        item->prev->next = item;
        list->prev = item;

        pthread_cond_signal(&qCond);
    } else
        free(item);

    pthread_mutex_unlock(&qLock);

    return (list == &reqHead)?cmdIgnore:cmdNotDone;
}


void process_loop(int listen_fd)
{
    int num_connections = 0;
    int connections[MAX_CONNECTIONS];

    bzero(connections, sizeof(connections));

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

        num = select(nfds, &rd, NULL, NULL, NULL);
        if (num == -1)
            perror("select()");
        else if (num) {
            //printf("Data is available now on %d fds\n", num);
            if (FD_ISSET(listen_fd, &rd)) {
                num--;
                incoming = accept(listen_fd, (struct sockaddr *) &in_addr, &in_addrlen);
                if (incoming < 0) {
                    perror("accept()");
                } else {
                    if (num_connections == MAX_CONNECTIONS) {
                        fprintf(stderr, "Connection limit(%d) reached. Dropping connection\n", MAX_CONNECTIONS);
                        close(incoming);
                    } else {
                        connections[num_connections++] = incoming;
                        fprintf(stderr, "Got incoming connection, fd %d, number %d\n", incoming, num_connections);
                    }
                }
            }
            for (i=0; num && (i<num_connections); i++) {
                int fd = connections[i];
                if (FD_ISSET(fd, &rd)) {
                    struct protocol cmd;
                    int ret;

                    //fprintf(stderr, "New command from fd %d, number %d, to go %d\n", fd, i, num);
                    // TODO: to get highest performance we should loop here until we get EAGAIN
                    ret = recv(fd, &cmd, sizeof(cmd), MSG_DONTWAIT);
                    if (ret == sizeof(cmd)) {
                        enum protoCmd rsp = rx_request(&cmd, fd);

                        if ((cmd.cmd == cmdRender) && (rsp == cmdNotDone)) {
                            cmd.cmd = rsp;
                            fprintf(stderr, "Sending NotDone response(%d)\n", rsp);
                            ret = send(fd, &cmd, sizeof(cmd), 0);
                            if (ret != sizeof(cmd))
                                perror("response send error");
                        }
                    } else if (!ret) {
                        int j;

                        num_connections--;
                        fprintf(stderr, "Connection %d, fd %d closed, now %d left\n", i, fd, num_connections);
                        for (j=i; j < num_connections; j++)
                            connections[j] = connections[j+1];
                        clear_requests(fd);
                        close(fd);
                    } else {
                        fprintf(stderr, "Recv Error on fd %d\n", fd);
                        break;
                    }
                }
            }
        } else
            fprintf(stderr, "Select timeout\n");
    }
}


int main(void)
{
    const char *spath = RENDER_SOCKET;
    int fd, i;
    struct sockaddr_un addr;
    mode_t old;

    fprintf(stderr, "Rendering daemon\n");

    pthread_mutex_init(&qLock, NULL);
    pthread_cond_init(&qCond, NULL);
    reqHead.next = reqHead.prev = &reqHead;
    dirtyHead.next = dirtyHead.prev = &dirtyHead;
    renderHead.next = renderHead.prev = &renderHead;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "failed to create unix socket\n");
        exit(2);
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, spath, sizeof(addr.sun_path));

    unlink(addr.sun_path);

    old = umask(0); // Need daemon socket to be writeable by apache
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "socket bind failed for: %s\n", spath);
        close(fd);
        exit(3);
    }
    umask(old);

    if (listen(fd, QUEUE_MAX) < 0) {
        fprintf(stderr, "socket listen failed for %d\n", QUEUE_MAX);
        close(fd);
        exit(4);
    }

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

    xmlconfigitem maps[XMLCONFIGS_MAX];
    bzero(maps, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

    FILE * hini ;
    char line[INILINE_MAX];
    char key[INILINE_MAX];
    char value[INILINE_MAX];

    // Load the config
    if ((hini=fopen(RENDERD_CONFIG, "r"))==NULL) {
        fprintf(stderr, "Config: cannot open %s\n", RENDERD_CONFIG);
        exit(7);
    }

    int iconf = -1;
    while (fgets(line, INILINE_MAX, hini)!=NULL) {
        if (line[0] == '#') continue;
        if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
        if (line[0] == '[') {
            iconf++;
            if (iconf >= XMLCONFIGS_MAX) {
                fprintf(stderr, "Config: more than %d configurations found\n", XMLCONFIGS_MAX);
                exit(7);
            }
            if (strlen(line) >= XMLCONFIG_MAX){
                fprintf(stderr, "XML name too long: %s\n", line);
                exit(7);
            }
            sscanf(line, "[%[^]]", maps[iconf].xmlname);
        } else if (sscanf(line, "%[^=]=%[^;#]", key, value) == 2
               ||  sscanf(line, "%[^=]=\"%[^\"]\"", key, value) == 2) {

            if (!strcmp(key, "XML")){
                if (strlen(value) >= PATH_MAX){
                    fprintf(stderr, "XML path too long: %s\n", value);
                    exit(7);
                }
                strcpy(maps[iconf].xmlfile, value);
            }
        }
    }
    fclose(hini);

    for(iconf = 0; iconf < XMLCONFIGS_MAX; ++iconf) {
         printf("config %d: name(%s) file(%s)\n", iconf, maps[iconf].xmlname, maps[iconf].xmlfile);
    }

    render_init();

    for(i=0; i<NUM_THREADS; i++) {
        if (pthread_create(&render_threads[i], NULL, render_thread, (void *)maps)) {
            fprintf(stderr, "error spawning render thread\n");
            close(fd);
            exit(7);
        }
    }
    process_loop(fd);

    unlink(spath);
    close(fd);
    return 0;
}
