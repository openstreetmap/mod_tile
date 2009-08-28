#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
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
static pthread_t *slave_threads;
static struct sigaction sigPipeAction;

static struct item reqHead, dirtyHead, renderHead;
static int reqNum, dirtyNum;
static pthread_mutex_t qLock;
static pthread_cond_t qCond;

static stats_struct stats;
static pthread_t stats_thread;

static renderd_config config;

int noSlaveRenders;

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
        stats.noReqRender++;
    } else if (dirtyNum) {
        item = dirtyHead.next;
        dirtyNum--;
        stats.noDirtyRender++;
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
        stats.noReqDroped++;
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

/**
 * Periodically write out current stats to a stats file. This information
 * can then be used to monitor performance of renderd e.g. with a munin plugin
 */
void *stats_writeout_thread(void * arg) {
    stats_struct lStats;
    int dirtQueueLength;
    int reqQueueLength;
    int noFailedAttempts = 0;
    char tmpName[PATH_MAX];

    snprintf(tmpName, sizeof(tmpName), "%s.tmp", config.stats_filename);

    syslog(LOG_DEBUG, "Starting stats thread");
    while (1) {
        pthread_mutex_lock(&qLock);
        memcpy(&lStats, &stats, sizeof(stats_struct));
        dirtQueueLength = dirtyNum;
        reqQueueLength = reqNum;
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
            fprintf(statfile, "DirtQueueLength: %i\n", dirtQueueLength);
            fprintf(statfile, "DropedRequest: %li\n", lStats.noReqDroped);
            fprintf(statfile, "ReqRendered: %li\n", lStats.noReqRender);
            fprintf(statfile, "DirtyRendered: %li\n", lStats.noDirtyRender);
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
    int fd;
    struct sockaddr_un * addrU;
    struct sockaddr_in * addrI;
    struct hostent *server;
    if (sConfig->ipport > 0) {
        syslog(LOG_INFO, "Initialising TCP/IP client socket to %s:%i",
                sConfig->iphostname, sConfig->ipport);
        addrI = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
        fd = socket(PF_INET, SOCK_STREAM, 0);
        server = gethostbyname(sConfig->iphostname);
        if (server == NULL) {
            syslog(LOG_WARNING, "Could not resolve hostname: %s",
                    sConfig->iphostname);
            return FD_INVALID;
        }
        bzero((char *) addrI, sizeof(struct sockaddr_in));
        addrI->sin_family = AF_INET;
        bcopy((char *) server->h_addr, (char *) &addrI->sin_addr.s_addr,
                server->h_length);
        addrI->sin_port = htons(sConfig->ipport);
        if (connect(fd, (struct sockaddr *) addrI, sizeof(struct sockaddr_in)) < 0) {
            syslog(LOG_WARNING, "Could not connect to %s:%i",
                    sConfig->iphostname, sConfig->ipport);
            return FD_INVALID;
        }
        free(addrI);
        syslog(LOG_INFO, "socket %s:%i initialised to fd %i", sConfig->iphostname, sConfig->ipport,
                fd);
    } else {
        syslog(LOG_INFO, "Initialising unix client socket on %s",
                sConfig->socketname);
        addrU = (struct sockaddr_un *)malloc(sizeof(struct sockaddr_un));
        fd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            syslog(LOG_WARNING, "Could not obtain socket: %i", fd);
            return FD_INVALID;
        }

        bzero(addrU, sizeof(struct sockaddr_un));
        addrU->sun_family = AF_UNIX;
        strncpy(addrU->sun_path, sConfig->socketname, sizeof(addrU->sun_path));

        if (connect(fd, (struct sockaddr *) addrU, sizeof(struct sockaddr_un)) < 0) {
            syslog(LOG_WARNING, "socket connect failed for: %s",
                    sConfig->socketname);
            close(fd);
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
    struct sockaddr_in addrI;
    mode_t old;
    int fd;

    if (sConfig->ipport > 0) {
        syslog(LOG_INFO, "Initialising TCP/IP server socket on %s:%i",
                sConfig->iphostname, sConfig->ipport);
        fd = socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            fprintf(stderr, "failed to create IP socket\n");
            exit(2);
        }
        bzero(&addrI, sizeof(addrI));
        addrI.sin_family = AF_INET;
        addrI.sin_addr.s_addr = INADDR_ANY;
        addrI.sin_port = htons(sConfig->ipport);
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
        strncpy(addrU.sun_path, sConfig->socketname, sizeof(addrU.sun_path));

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

    req_slave = (struct protocol *)malloc(sizeof(protocol));
    resp = (struct protocol *)malloc(sizeof(protocol));
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
    dirtyHead.next = dirtyHead.prev = &dirtyHead;
    renderHead.next = renderHead.prev = &renderHead;

    stats.noDirtyRender = 0;
    stats.noReqDroped = 0;
    stats.noReqRender = 0;

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
                    buffer, "NULL");

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
    for (int i = 0; i < MAX_SLAVES; i++) {
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
