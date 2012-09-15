#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

#include <pthread.h>

#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"
#include "sys_utils.h"

char *tile_dir = HASH_PATH;

#ifndef METATILE
#warning("render_list not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
    fprintf(stderr, "render_list not implemented for non-metatile mode. Feel free to submit fix!\n");
    return -1;
}
#else

static int minZoom = 0;
static int maxZoom = MAX_ZOOM;
static int verbose = 0;
static int maxLoad = MAX_LOAD_OLD;

int work_complete;

void display_rate(struct timeval start, struct timeval end, int num) 
{
    int d_s, d_us;
    float sec;

    d_s  = end.tv_sec  - start.tv_sec;
    d_us = end.tv_usec - start.tv_usec;

    sec = d_s + d_us / 1000000.0;

    printf("Rendered %d tiles in %.2f seconds (%.2f tiles/s)\n", num, sec, num / sec);
    fflush(NULL);
}

static time_t getPlanetTime(char *tile_dir)
{
    static time_t last_check;
    static time_t planet_timestamp;
    time_t now = time(NULL);
    struct stat buf;
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX-1, "%s/%s", tile_dir, PLANET_TIMESTAMP);

    // Only check for updates periodically
    if (now < last_check + 300)
        return planet_timestamp;

    last_check = now;
    if (stat(filename, &buf)) {
        fprintf(stderr, "Planet timestamp file (%s) is missing\n", filename);
        // Make something up
        planet_timestamp = now - 3 * 24 * 60 * 60;
    } else {
        if (buf.st_mtime != planet_timestamp) {
            printf("Planet file updated at %s", ctime(&buf.st_mtime));
            planet_timestamp = buf.st_mtime;
        }
    }
    return planet_timestamp;
}



int process_loop(int fd, const char *mapname, int x, int y, int z)
{
    struct protocol cmd, rsp;
    //struct pollfd fds[1];
    int ret = 0;

    bzero(&cmd, sizeof(cmd));

    cmd.ver = 2;
    cmd.cmd = cmdRenderBulk;
    cmd.z = z;
    cmd.x = x;
    cmd.y = y;
    strcpy(cmd.xmlname, mapname);

    //printf("Sending request\n");
    ret = send(fd, &cmd, sizeof(cmd), 0);
    if (ret != sizeof(cmd)) {
        perror("send error");
    }

    //printf("Waiting for response\n");
    bzero(&rsp, sizeof(rsp));
    ret = recv(fd, &rsp, sizeof(rsp), 0);
    if (ret != sizeof(rsp)) {
        perror("recv error");
        return 0;
    }
    //printf("Got response\n");

    if (rsp.cmd != cmdDone) {
        printf("rendering failed, pausing\n");
        sleep(10);
    }

    if (!ret)
        perror("Socket send error");
    return ret;
}

void process(const char *tilepath, int fd, const char *name)
{
    char xmlconfig[XMLCONFIG_MAX];
    int x, y, z;

    if (path_to_xyz(tilepath, name, xmlconfig, &x, &y, &z))
        return;

    printf("Requesting xml(%s) x(%d) y(%d) z(%d)\n", xmlconfig, x, y, z);
    process_loop(fd, xmlconfig, x, y, z);
}

static void check_load(void)
{
    double avg = get_load_avg();

    while (avg >= maxLoad) {
        /* printf("Load average %d, sleeping\n", avg); */
        sleep(5);
        avg = get_load_avg();
    }
}

#define QMAX 32

pthread_mutex_t qLock;
pthread_cond_t qCondNotEmpty;
pthread_cond_t qCondNotFull;

unsigned int qLen;
struct qItem {
    char *path;
    struct qItem *next;
};

struct qItem *qHead, *qTail;

char *fetch(void)
{
    // Fetch path to render from queue or NULL on work completion
    // Must free() pointer after use
    char *path;

    pthread_mutex_lock(&qLock);

    while (qLen == 0) {
        if (work_complete) {
            pthread_mutex_unlock(&qLock);
            return NULL;
        }
        pthread_cond_wait(&qCondNotEmpty, &qLock);
    }

    // Fetch item from queue
    if (!qHead) {
        fprintf(stderr, "Queue failure, null qHead with %d items in list\n", qLen);
        exit(1);
    }
    path = qHead->path;

    if (qHead == qTail) {
        free(qHead);
        qHead = NULL;
        qTail = NULL;
        qLen = 0;
    } else {
        struct qItem *e = qHead;
        qHead = qHead->next;
        free(e);
        if (qLen == QMAX)
            pthread_cond_signal(&qCondNotFull);
        qLen--;
    }

    pthread_mutex_unlock(&qLock);
    return path;
}

void enqueue(const char *path)
{
    // Add this path in the local render queue
    struct qItem *e = malloc(sizeof(struct qItem));

    e->path = strdup(path);
    e->next = NULL;

    if (!e->path) {
        fprintf(stderr, "Malloc failure\n");
        exit(1);
    }

    pthread_mutex_lock(&qLock);

    while (qLen == QMAX) {
        pthread_cond_wait(&qCondNotFull, &qLock);
    }

    // Append item to end of queue
    if (qTail)
        qTail->next = e;
    else
        qHead = e;
    qTail = e;
    pthread_cond_signal(&qCondNotEmpty);
    qLen++;

    pthread_mutex_unlock(&qLock);
}


void *thread_main(void *arg)
{
    const char *spath = (const char *)arg;
    int fd;
    struct sockaddr_un addr;
    char *tile;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "failed to create unix socket\n");
        exit(2);
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, spath, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "socket connect failed for: %s\n", spath);
        close(fd);
        return NULL;
    }

    while((tile = fetch())) {
        process(tile_dir, fd, tile);
        free(tile);
        check_load();
    }

    close(fd);

    return NULL;
}


pthread_t *workers;

void spawn_workers(int num, const char *spath)
{
    int i;

    // Setup request queue
    pthread_mutex_init(&qLock, NULL);
    pthread_cond_init(&qCondNotEmpty, NULL);
    pthread_cond_init(&qCondNotFull, NULL);

    printf("Starting %d rendering threads\n", num);
    workers = calloc(sizeof(pthread_t), num);
    if (!workers) {
        perror("Error allocating worker memory");
        exit(1);
    }
    for(i=0; i<num; i++) {
        if (pthread_create(&workers[i], NULL, thread_main, (void *)spath)) {
            perror("Thread creation failed");
            exit(1);
        }
    }
}

void finish_workers(int num)
{
    int i;

    printf("Waiting for rendering threads to finish\n");
    pthread_mutex_lock(&qLock);
    work_complete = 1;
    pthread_mutex_unlock(&qLock);
    pthread_cond_broadcast(&qCondNotEmpty);

    for(i=0; i<num; i++)
        pthread_join(workers[i], NULL);
    free(workers);
    workers = NULL;
}


int main(int argc, char **argv)
{
    char *spath = RENDER_SOCKET;
    char *mapname = XMLCONFIG_DEFAULT;
    int minX=-1, maxX=-1, minY=-1, maxY=-1;
    int x, y, z;
    char name[PATH_MAX];
    struct timeval start, end;
    int num_render = 0, num_all = 0;
    time_t planetTime;
    int c;
    int all=0;
    int numThreads = 1;
    int force=0;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"min-zoom", 1, 0, 'z'},
            {"max-zoom", 1, 0, 'Z'},
            {"min-x", 1, 0, 'x'},
            {"max-x", 1, 0, 'X'},
            {"min-y", 1, 0, 'y'},
            {"max-y", 1, 0, 'Y'},
            {"socket", 1, 0, 's'},
            {"num-threads", 1, 0, 'n'},
            {"max-load", 1, 0, 'l'},
            {"tile-dir", 1, 0, 't'},
            {"map", 1, 0, 'm'},
            {"verbose", 0, 0, 'v'},
            {"force", 0, 0, 'f'},
            {"all", 0, 0, 'a'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "hvaz:Z:x:X:y:Y:s:m:t:n:l:f", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'a':   /* -a, --all */
                all=1;
                break;
            case 's':   /* -s, --socket */
                spath = strdup(optarg);
                break;
            case 't':   /* -t, --tile-dir */
                tile_dir=strdup(optarg);
                break;
            case 'm':   /* -m, --map */
                mapname=strdup(optarg);
                break;
            case 'l':   /* -l, --max-load */
                maxLoad = atoi(optarg);
                break;
            case 'n':   /* -n, --num-threads */
                numThreads=atoi(optarg);
                if (numThreads <= 0) {
                    fprintf(stderr, "Invalid number of threads, must be at least 1\n");
                    return 1;
                }
                break;
            case 'x':   /* -x, --min-x */
                minX=atoi(optarg);
                break;
            case 'X':   /* -X, --max-x */
                maxX=atoi(optarg);
                break;
            case 'y':   /* -y, --min-y */
                minY=atoi(optarg);
                break;
            case 'Y':   /* -Y, --max-y */
                maxY=atoi(optarg);
                break;
            case 'z':   /* -z, --min-zoom */
                minZoom=atoi(optarg);
                if (minZoom < 0 || minZoom > MAX_ZOOM) {
                    fprintf(stderr, "Invalid minimum zoom selected, must be between 0 and %d\n", MAX_ZOOM);
                    return 1;
                }
                break;
            case 'Z':   /* -Z, --max-zoom */
                maxZoom=atoi(optarg);
                if (maxZoom < 0 || maxZoom > MAX_ZOOM) {
                    fprintf(stderr, "Invalid maximum zoom selected, must be between 0 and %d\n", MAX_ZOOM);
                    return 1;
                }
                break;
            case 'f':   /* -f, --force */
                force=1;
                break;
            case 'v':   /* -v, --verbose */
                verbose=1;
                break;
            case 'h':   /* -h, --help */
                fprintf(stderr, "Usage: render_list [OPTION] ...\n");
                fprintf(stderr, "  -a, --all            render all tiles in given zoom level range instead of reading from STDIN\n");
                fprintf(stderr, "  -f, --force          render tiles even if they seem current\n");
                fprintf(stderr, "  -m, --map=MAP        render tiles in this map (defaults to '" XMLCONFIG_DEFAULT "')\n");
                fprintf(stderr, "  -l, --max-load=LOAD  sleep if load is this high (defaults to %d)\n", MAX_LOAD_OLD);
                fprintf(stderr, "  -s, --socket=SOCKET  unix domain socket name for contacting renderd\n");
                fprintf(stderr, "  -n, --num-threads=N the number of parallel request threads (default 1)\n");
                fprintf(stderr, "  -t, --tile-dir       tile cache directory (defaults to '" HASH_PATH "')\n");
                fprintf(stderr, "  -z, --min-zoom=ZOOM  filter input to only render tiles greater or equal to this zoom level (default is 0)\n");
                fprintf(stderr, "  -Z, --max-zoom=ZOOM  filter input to only render tiles less than or equal to this zoom level (default is %d)\n", MAX_ZOOM);
                fprintf(stderr, "If you are using --all, you can restrict the tile range by adding these options:\n");
                fprintf(stderr, "  -x, --min-x=X        minimum X tile coordinate\n");
                fprintf(stderr, "  -X, --max-x=X        maximum X tile coordinate\n");
                fprintf(stderr, "  -y, --min-y=Y        minimum Y tile coordinate\n");
                fprintf(stderr, "  -Y, --max-y=Y        maximum Y tile coordinate\n");
                fprintf(stderr, "Without --all, send a list of tiles to be rendered from STDIN in the format:\n");
                fprintf(stderr, "  X Y Z\n");
                fprintf(stderr, "e.g.\n");
                fprintf(stderr, "  0 0 1\n");
                fprintf(stderr, "  0 1 1\n");
                fprintf(stderr, "  1 0 1\n");
                fprintf(stderr, "  1 1 1\n");
                fprintf(stderr, "The above would cause all 4 tiles at zoom 1 to be rendered\n");
                return -1;
            default:
                fprintf(stderr, "unhandled char '%c'\n", c);
                break;
        }
    }

    if (maxZoom < minZoom) {
        fprintf(stderr, "Invalid zoom range, max zoom must be greater or equal to minimum zoom\n");
        return 1;
    }

    if (all) {
        if ((minX != -1 || minY != -1 || maxX != -1 || maxY != -1) && minZoom != maxZoom) {
            fprintf(stderr, "min-zoom must be equal to max-zoom when using min-x, max-x, min-y, or max-y options\n");
            return 1;
        }

        if (minX == -1) { minX = 0; }
        if (minY == -1) { minY = 0; }

        int lz = (1 << minZoom) - 1;

        if (minZoom == maxZoom) {
            if (maxX == -1) { maxX = lz; }
            if (maxY == -1) { maxY = lz; }
            if (minX > lz || minY > lz || maxX > lz || maxY > lz) {
                fprintf(stderr, "Invalid range, x and y values must be <= %d (2^zoom-1)\n", lz);
                return 1;
            }
        }

        if (minX < 0 || minY < 0 || maxX < -1 || maxY < -1) {
            fprintf(stderr, "Invalid range, x and y values must be >= 0\n");
            return 1;
        }

    }

    fprintf(stderr, "Rendering client\n");

    planetTime = getPlanetTime(tile_dir);

    gettimeofday(&start, NULL);

    spawn_workers(numThreads, spath);

    if (all) {
        int x, y, z;
        printf("Rendering all tiles from zoom %d to zoom %d\n", minZoom, maxZoom);
        for (z=minZoom; z <= maxZoom; z++) {
            int current_maxX = (maxX == -1) ? (1 << z)-1 : maxX;
            int current_maxY = (maxY == -1) ? (1 << z)-1 : maxY;
            printf("Rendering all tiles for zoom %d from (%d, %d) to (%d, %d)\n", z, minX, minY, current_maxX, current_maxY);
            for (x=minX; x <= current_maxX; x+=METATILE) {
                for (y=minY; y <= current_maxY; y+=METATILE) {
                    xyz_to_meta(name, sizeof(name), tile_dir, mapname, x, y, z);
                    enqueue(name);
                    //process_loop(fd, mapname, x, y, z);
                    num_all++;
                    num_render++;
                }
            }
        }
    } else {
        while(!feof(stdin)) {
            struct stat s;
            int n = fscanf(stdin, "%d %d %d", &x, &y, &z);

            if (n != 3) {
                // Discard input line
                char tmp[1024];
                char *r = fgets(tmp, sizeof(tmp), stdin);
                if (!r)
                    continue;
                // fprintf(stderr, "bad line %d: %s", num_all, tmp);
                continue;
            }

            //printf("got: x(%d) y(%d) z(%d)\n", x, y, z);

            if (z < minZoom || z > maxZoom) {
                printf("Ignoring tile, zoom %d outside valid range (%d..%d)\n", z, minZoom, maxZoom);
                continue;
            }

            num_all++;
            xyz_to_meta(name, sizeof(name), tile_dir, mapname, x, y, z);

            if (force || (stat(name, &s) < 0) || (planetTime > s.st_mtime)) {
                // missing or old, render it
                //ret = process_loop(fd, mapname, x, y, z);
                enqueue(name);
                num_render++;
                // Attempts to adjust the stats for the QMAX tiles which are likely in the queue
                if ((num_render > QMAX) && !((num_render - QMAX) % 10)) {
                    gettimeofday(&end, NULL);
                    printf("\n");
                    printf("Meta tiles rendered: ");
                    display_rate(start, end, num_render - QMAX);
                    printf("Total tiles rendered: ");
                    display_rate(start, end, (num_render - QMAX) * METATILE * METATILE);
                    printf("Total tiles handled from input: ");
                    display_rate(start, end, num_all);
                }
            }
        }
    }

    finish_workers(numThreads);

    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles rendered\n");
    printf("Meta tiles rendered: ");
    display_rate(start, end, num_render);
    printf("Total tiles rendered: ");
    display_rate(start, end, num_render * METATILE * METATILE);
    printf("Total tiles handled: ");
    display_rate(start, end, num_all);

    return 0;
}
#endif
