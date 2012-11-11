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
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>

#include <pthread.h>


#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"
#include "sys_utils.h"


char *tile_dir = HASH_PATH;

#ifndef METATILE
#warning("render_old not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
    fprintf(stderr, "render_old not implemented for non-metatile mode. Feel free to submit fix!\n");
    return -1;
}
#else

#define INILINE_MAX 256
static int minZoom = 0;
static int maxZoom = MAX_ZOOM;
static int verbose = 0;
static int num_render = 0, num_all = 0;
static int max_load = MAX_LOAD_OLD;
static time_t planetTime;
static struct timeval start, end;


int work_complete;

void display_rate(struct timeval start, struct timeval end, int num) 
{
    int d_s, d_us;
    float sec;

    d_s  = end.tv_sec  - start.tv_sec;
    d_us = end.tv_usec - start.tv_usec;

    sec = d_s + d_us / 1000000.0;

    printf("%d tiles in %.2f seconds (%.2f tiles/s)\n", num, sec, num / sec);
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

int connect_socket(const char *arg) {
    int fd;
    struct sockaddr_un addr;
    const char *spath = arg;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "failed to create unix socket\n");
        exit(2);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, spath, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "socket connect failed for: %s\n", spath);
        close(fd);
        return -1;
    }
    return fd;
}


int process_loop(int fd, char * xmlname, int x, int y, int z)
{
    struct protocol cmd, rsp;
    //struct pollfd fds[1];
    int ret = 0;

    memset(&cmd, 0, sizeof(cmd));

    cmd.ver = 2;
    cmd.cmd = cmdRenderBulk;
    cmd.z = z;
    cmd.x = x;
    cmd.y = y;
    strcpy(cmd.xmlname, xmlname);
    //strcpy(cmd.path, "/tmp/foo.png");
    //printf("Sending request of size %i\n", sizeof(cmd));
    ret = send(fd, &cmd, sizeof(cmd), 0);
    if (ret != sizeof(cmd)) {
        perror("send error");
    }
        //printf("Waiting for response\n");
    memset(&rsp, 0, sizeof(rsp));
    ret = recv(fd, &rsp, sizeof(rsp), MSG_WAITALL);
    if (ret != sizeof(rsp)) {
        if (ret == 0) {
            printf("rendering socket closed, pausing\n");
            sleep(10);
            return 0;
        } else {
            perror("recv error");
            return -1;
        }
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

int process(const char *tilepath, int fd, const char *name)
{
    char xmlconfig[XMLCONFIG_MAX];
    int x, y, z;
    struct stat b;

    if (stat(name, &b))
        return 1;

    if (path_to_xyz(tilepath, name, xmlconfig, &x, &y, &z))
        return 1;

    printf("Requesting xml(%s) x(%d) y(%d) z(%d) as last modified at %s\n", xmlconfig, x, y, z, ctime(&b.st_mtime));
    return process_loop(fd, xmlconfig, x, y, z);
}

static void check_load(void)
{
    double avg = get_load_avg();

    while (avg >= max_load) {
        printf("Load average %f, sleeping\n", avg);
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

char *fetch(const char * socket, int * fd)
{
    // Fetch path to render from queue or NULL on work completion
    // Must free() pointer after use
    char *path;

    pthread_mutex_lock(&qLock);

    while (qLen == 0) {
        /* If there is nothing to do, close the socket, as it may otherwise timeout */
        if (fd >= 0) {
            close(*fd);
            *fd = -1;
        }
        if (work_complete) {
            pthread_mutex_unlock(&qLock);
            return NULL;
        }
        pthread_cond_wait(&qCondNotEmpty, &qLock);
    }
    if (*fd < 0) {
        *fd = connect_socket(socket);
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
    sleep(10);
}

static void descend(const char *search)
{
    DIR *tiles = opendir(search);
    struct dirent *entry;
    char path[PATH_MAX];

    if (!tiles) {
        fprintf(stderr, "Unable to open directory: %s\n", search);
        return;
    }

    while ((entry = readdir(tiles))) {
        struct stat b;
        char *p;

        check_load();

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(path, sizeof(path), "%s/%s", search, entry->d_name);
        if (stat(path, &b))
            continue;
        if (S_ISDIR(b.st_mode)) {
            descend(path);
            continue;
        }
        p = strrchr(path, '.');
        if (p && !strcmp(p, ".meta")) {
            num_all++;
            if (planetTime > b.st_mtime) {
                // request rendering of  old tile
                enqueue(path);
            }
        }
    }
    closedir(tiles);
}



void *thread_main(void *arg)
{
    int fd;
    char * spath = (char *) arg;
    char *tile;

    fd = connect_socket(spath);

    while((tile = fetch(spath, &fd))) {
        int ret = process(tile_dir, fd, tile);
        if (ret == 0) {
            printf("Reconnecting closed socket\n");
            close(fd);
            fd = connect_socket(spath);
        }
        num_render++;
        if (!(num_render % 10)) {
            gettimeofday(&end, NULL);
            printf("\n");
            printf("Meta tiles rendered: ");
            display_rate(start, end, num_render);
            printf("Total tiles rendered: ");
            display_rate(start, end, num_render * METATILE * METATILE);
            printf("Number of Metatiles tested for expiry: ");
            display_rate(start, end, num_all);
            printf("\n");
        }
        free(tile);
    }

    close(fd);

    return NULL;
}

void render_layer(const char *tilepath, const char *name)
{
    int z;

    for (z=minZoom; z<=maxZoom; z++) {
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s/%d", tilepath, name, z);
        descend(path);
    }
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
    char spath[PATH_MAX] = RENDER_SOCKET;
    char *config_file = RENDERD_CONFIG;
    char *map = NULL;
    int c;
    int numThreads = 1;
    int dd, mm, yy;
    struct tm tm;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"config",1,0,'c'},
            {"min-zoom", 1, 0, 'z'},
            {"max-zoom", 1, 0, 'Z'},
            {"max-load", 1, 0, 'l'},
            {"socket", 1, 0, 's'},
            {"num-threads", 1, 0, 'n'},
            {"tile-dir", 1, 0, 't'},
            {"timestamp", 1, 0, 'T'},
            {"map", 1, 0, 'm'},
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "hvz:Z:s:t:n:c:l:T:m:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 's':   /* -s, --socket */
                strncpy(spath, optarg, PATH_MAX-1);
                spath[PATH_MAX-1] = 0;
                break;
            case 't':   /* -t, --tile-dir */
                tile_dir=strdup(optarg);
                break;

            case 'c':   /* -c, --config */
                config_file=strdup(optarg);
                break;
            case 'm':   /* -m, --map */
                map=strdup(optarg);
                break;
            case 'n':   /* -n, --num-threads */
                numThreads=atoi(optarg);
                if (numThreads <= 0) {
                    fprintf(stderr, "Invalid number of threads, must be at least 1\n");
                    return 1;
                }
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
             case 'l':
                 max_load = atoi(optarg);
                 if (max_load < 0) {
                     fprintf(stderr, "Invalid maximum load specified, must be greater than 0\n");
                     return 1;
                 }
                 break;
            case 'T':
                
                if (sscanf(optarg,"%d/%d/%d", &dd, &mm, &yy) < 3) {
                    fprintf(stderr, "Invalid planet time stamp, must be in the format dd/mm/yyyy\n");
                    return 1;
                }
                
                if (yy > 100) yy -= 1900;
                if (yy < 70) yy += 100;
                
                tm.tm_sec = 0; tm.tm_min = 0; tm.tm_hour = 0; tm.tm_mday = dd; tm.tm_mon = mm - 1; tm.tm_year =  yy;
                planetTime = mktime(&tm);

            case 'v':   /* -v, --verbose */
                verbose=1;
                break;
            case 'h':   /* -h, --help */
                fprintf(stderr, "Usage: render_old [OPTION] ...\n");
                fprintf(stderr, "Search the rendered tiles and re-render tiles which are older then the last planet import\n");
                fprintf(stderr, "  -c, --config=CONFIG  specify the renderd config file\n");
                fprintf(stderr, "  -n, --num-threads=N  the number of parallel request threads (default 1)\n");
                fprintf(stderr, "  -t, --tile-dir       tile cache directory (defaults to '" HASH_PATH "')\n");
                fprintf(stderr, "  -z, --min-zoom=ZOOM  filter input to only render tiles greater or equal to this zoom level (default 0)\n");
                fprintf(stderr, "  -Z, --max-zoom=ZOOM  filter input to only render tiles less than or equal to this zoom level (default %d)\n", MAX_ZOOM);
                fprintf(stderr, "  -s, --socket=SOCKET  unix domain socket name for contacting renderd\n");
                fprintf(stderr, "  -l, --max-load=LOAD  maximum system load with which requests are submitted\n");
                fprintf(stderr, "  -T, --timestamp=DD/MM/YY  Overwrite the assumed data of the planet import\n");
                fprintf(stderr, "  -m, --map=STYLE      Instead of going through all styls of CONFIG, only use a specific map-style\n");
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

    fprintf(stderr, "Rendering old tiles\n");

    if (planetTime == 0) {
        planetTime = getPlanetTime(tile_dir);
    } else {
        printf("Overwriting planet file update to %s", ctime(&planetTime));
    }

    gettimeofday(&start, NULL);

    FILE * hini ;
    char line[INILINE_MAX];
    char value[INILINE_MAX];

    // Load the config
    if ((hini=fopen(config_file, "r"))==NULL) {
        fprintf(stderr, "Config: cannot open %s\n", config_file);
        exit(7);
    }

    spawn_workers(numThreads, spath);

    if (map) {
        render_layer(tile_dir, map);
    } else {
        while (fgets(line, INILINE_MAX, hini)!=NULL) {
            if (line[0] == '[') {
                if (strlen(line) >= XMLCONFIG_MAX){
                    fprintf(stderr, "XML name too long: %s\n", line);
                    exit(7);
                }
                sscanf(line, "[%[^]]", value);
                // Skip mapnik & renderd sections which are config, not tile layers
                if (strcmp(value,"mapnik") && strncmp(value, "renderd", 7))
                    render_layer(tile_dir, value);
            }
        }
    }
    fclose(hini);

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
