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
#include <syslog.h>
#include <getopt.h>

#include "daemon.h"
#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"

#define PIDFILE "/var/run/renderd/renderd.pid"

extern "C" {
#include "iniparser3.0b/src/iniparser.h"
}

static pthread_t *render_threads;
static struct sigaction sigPipeAction;

static struct item reqHead, dirtyHead, renderHead;
static int reqNum, dirtyNum;
static pthread_mutex_t qLock;
static pthread_cond_t qCond;

static renderd_config config;

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
        syslog(LOG_ERR, "Bad protocol version %d", req->ver);
        return cmdIgnore;
    }

    syslog(LOG_DEBUG, "DEBUG: Got command %s fd(%d) xml(%s), z(%d), x(%d), y(%d)",
            cmdStr(req->cmd), fd, req->xmlname, req->z, req->x, req->y);

    if ((req->cmd != cmdRender) && (req->cmd != cmdDirty))
        return cmdIgnore;

    if (check_xyz(req->x, req->y, req->z))
        return cmdNotDone;

    item = (struct item *)malloc(sizeof(*item));
    if (!item) {
            syslog(LOG_ERR, "malloc failed");
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


int main(int argc, char **argv)
{
    int fd, i;
    struct sockaddr_un addr;
    mode_t old;
    int c;
    int foreground=0;
    char config_file_name[PATH_MAX] = RENDERD_CONFIG;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"config", 1, 0, 'c'},
            {"foreground", 1, 0, 'f'},
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
            case 'h':
                fprintf(stderr, "Usage: renderd [OPTION] ...\n");
                fprintf(stderr, "Mapnik rendering daemon\n");
                fprintf(stderr, "  -f, --foreground     run in foreground\n");
                fprintf(stderr, "  -h, --help           display this help and exit\n");
                fprintf(stderr, "  -c, --config=CONFIG  set location of config file (default /etc/renderd.conf)\n");
                exit(0);
            default:
                fprintf(stderr, "unknown config option '%c'\n", c);
                exit(1);
        }
    }

    openlog("renderd", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Rendering daemon started");

    pthread_mutex_init(&qLock, NULL);
    pthread_cond_init(&qCond, NULL);
    reqHead.next = reqHead.prev = &reqHead;
    dirtyHead.next = dirtyHead.prev = &dirtyHead;
    renderHead.next = renderHead.prev = &renderHead;

    xmlconfigitem maps[XMLCONFIGS_MAX];
    bzero(maps, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

    dictionary *ini = iniparser_load(config_file_name);
    if (! ini) {
        exit(1);
    }

    config.socketname = iniparser_getstring(ini, "renderd:socketname", (char *)RENDER_SOCKET);
    config.num_threads = iniparser_getint(ini, "renderd:num_threads", NUM_THREADS);
    config.tile_dir = iniparser_getstring(ini, "renderd:tile_dir", (char *)HASH_PATH);
    config.mapnik_plugins_dir = iniparser_getstring(ini, "mapnik:plugins_dir", (char *)MAPNIK_PLUGINS);
    config.mapnik_font_dir = iniparser_getstring(ini, "mapnik:font_dir", (char *)FONT_DIR);

    int iconf = -1;
    char buffer[PATH_MAX];
    for (int section=0; section < iniparser_getnsec(ini); section++) {
        char *name = iniparser_getsecname(ini, section);
        if (strcmp(name, "renderd") && strcmp(name, "mapnik")) {
            /* this is a map section */
            iconf++;
            if (strlen(name) >= XMLCONFIG_MAX) {
                fprintf(stderr, "XML name too long: %s\n", name);
                exit(7);
            }

            strcpy(maps[iconf].xmlname, name);
            if (iconf >= XMLCONFIGS_MAX) {
                fprintf(stderr, "Config: more than %d configurations found\n", XMLCONFIGS_MAX);
                exit(7);
            }
            sprintf(buffer, "%s:uri", name);
            char *ini_uri = iniparser_getstring(ini, buffer, (char *)"");
            if (strlen(ini_uri) >= PATH_MAX) {
                fprintf(stderr, "URI too long: %s\n", ini_uri);
                exit(7);
            }
            strcpy(maps[iconf].xmluri, ini_uri);
            sprintf(buffer, "%s:xml", name);
            char *ini_xmlpath = iniparser_getstring(ini, buffer, (char *)"");
            if (strlen(ini_xmlpath) >= PATH_MAX){
                fprintf(stderr, "XML path too long: %s\n", ini_xmlpath);
                exit(7);
            }
            sprintf(buffer, "%s:host", name);
            char *ini_hostname = iniparser_getstring(ini, buffer, (char *) "");
            if (strlen(ini_hostname) >= PATH_MAX) {
                fprintf(stderr, "Host name too long: %s\n", ini_hostname);
                exit(7);
            }

            sprintf(buffer, "%s:htcphost", name);
            char *ini_htcpip = iniparser_getstring(ini, buffer, (char *) "");
            if (strlen(ini_htcpip) >= PATH_MAX) {
                fprintf(stderr, "HTCP host name too long: %s\n", ini_htcpip);
                exit(7);
            }
            strcpy(maps[iconf].xmlfile, ini_xmlpath);
            strcpy(maps[iconf].tile_dir, config.tile_dir);
            strcpy(maps[iconf].host, ini_hostname);
            strcpy(maps[iconf].htcpip, ini_htcpip);
        }
    }

    syslog(LOG_INFO, "config renderd: socketname=%s\n", config.socketname);
    syslog(LOG_INFO, "config renderd: num_threads=%d\n", config.num_threads);
    syslog(LOG_INFO, "config renderd: tile_dir=%s\n", config.tile_dir);
    syslog(LOG_INFO, "config mapnik:  plugins_dir=%s\n", config.mapnik_plugins_dir);
    syslog(LOG_INFO, "config mapnik:  font_dir=%s\n", config.mapnik_font_dir);
    syslog(LOG_INFO, "config mapnik:  font_dir_recurse=%d\n", config.mapnik_font_dir_recurse);
    for(iconf = 0; iconf < XMLCONFIGS_MAX; ++iconf) {
         syslog(LOG_INFO, "config map %d:   name(%s) file(%s) uri(%s) htcp(%s) host(%s)",
                 iconf, maps[iconf].xmlname, maps[iconf].xmlfile, maps[iconf].xmluri,
                 maps[iconf].htcpip, maps[iconf].host);
    }

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "failed to create unix socket\n");
        exit(2);
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, config.socketname, sizeof(addr.sun_path));

    unlink(addr.sun_path);

    old = umask(0); // Need daemon socket to be writeable by apache
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "socket bind failed for: %s\n", config.socketname);
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

    render_threads = (pthread_t *) malloc(sizeof(pthread_t) * config.num_threads);
    for(i=0; i<config.num_threads; i++) {
        if (pthread_create(&render_threads[i], NULL, render_thread, (void *)maps)) {
            fprintf(stderr, "error spawning render thread\n");
            close(fd);
            exit(7);
        }
    }

    process_loop(fd);

    unlink(config.socketname);
    close(fd);
    return 0;
}
