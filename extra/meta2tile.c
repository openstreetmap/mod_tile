// meta2tile.c
// written by Frederik Ramm <frederik@remote.org>
// License: GPL because this is based on other work in mod_tile

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "store_file.h"
#include "store_couchbase.h"
#include "metatile.h"

#define MIN(x,y) ((x)<(y)?(x):(y))
#define META_MAGIC "META"
#define THREAD_USLEEP 1

enum Status
{
    THREAD_RUNNING = 0,
    THREAD_FREE,
    THREAD_COMPUTE
};

struct ThreadInfo {
    volatile int id;
    volatile enum Status status;
    volatile char isFinished;
    pthread_t pid;
    char path[PATH_MAX];
    pthread_mutex_t mutex;
    struct storage_backend * store;
};

int threadCount = 1;

static int verbose = 0;
static long int num_render = 0;
static struct timeval start, end;

char store_conf[255];
int force=0;

const char *source;
const char *target;

#define MODE_STAT 1
#define MODE_GLOB 2
#define MAXZOOM 20

int mode = MODE_GLOB;

int zoom[MAXZOOM+1];
float bbox[4] = {-180.0, -90.0, 180.0, 90.0};

void log_message(int log_lvl, const char *format, ...) {
    va_list ap;
    char *msg = malloc(1000*sizeof(char));

    va_start(ap, format);

    if (msg) {
        vsnprintf(msg, 1000, format, ap);
        switch (log_lvl) {
        case STORE_LOGLVL_DEBUG:
            fprintf(stderr, "debug: %s\n", msg);
            break;
        case STORE_LOGLVL_INFO:
            fprintf(stderr, "info: %s\n", msg);
            break;
        case STORE_LOGLVL_WARNING:
            fprintf(stderr, "WARNING: %s\n", msg);
            break;
        case STORE_LOGLVL_ERR:
            fprintf(stderr, "ERROR: %s\n", msg);
            break;
        }
        free(msg);
        fflush(stderr);
    }
    va_end(ap);
}

int path_to_xyz(const char *path, int *px, int *py, int *pz)
{
    int i, hash[5], x, y, z;
    char copy[PATH_MAX];
    strcpy(copy, path);
    char *slash = rindex(copy, '/');
    int c=5;
    while (slash && c)
    {
        *slash = 0;
        c--;
        hash[c]= atoi(slash+1);
        slash = rindex(copy, '/');
    }
    if (c != 0)
    {
        fprintf(stderr, "Failed to parse tile path: %s\n", path);
        return 1;
    }
    *slash = 0;
    *pz = atoi(slash+1);

    x = y = 0;
    for (i=0; i<5; i++)
    {
        if (hash[i] < 0 || hash[i] > 255)
        {
            fprintf(stderr, "Failed to parse tile path (invalid %d): %s\n", hash[i], path);
            return 2;
        }
        x <<= 4;
        y <<= 4;
        x |= (hash[i] & 0xf0) >> 4;
        y |= (hash[i] & 0x0f);
    }
    z = *pz;
    *px = x;
    *py = y;
    return 0;
}

int long2tilex(double lon, int z) 
{ 
    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z))); 
}
 
int lat2tiley(double lat, int z)
{ 
    return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z))); 
}
 
double tilex2long(int x, int z) 
{
    return x / pow(2.0, z) * 360.0 - 180;
}
 
double tiley2lat(int y, int z) 
{
    double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

int expand_meta(const char *name, struct storage_backend * store)
{
    int fd;
    int x, y, z;
    size_t pos = 0;
    void *buf;
    char path[PATH_MAX];
    struct stat_info tile_stat;

    if (path_to_xyz(name, &x, &y, &z)) return -1;

    int limit = (1 << z);
    limit = MIN(limit, METATILE);

    float fromlat = tiley2lat(y+8, z);
    float tolat = tiley2lat(y, z);
    float fromlon = tilex2long(x, z);
    float tolon = tilex2long(x+8, z);

    if (tolon < bbox[0] || fromlon > bbox[2] || tolat < bbox[1] || fromlat > bbox[3])
    {
        if (verbose) printf("z=%d x=%d y=%d is out of bbox\n", z, x, y);
        return -8;
    }

    fd = open(name, O_RDONLY);
    if (fd < 0) 
    {
        fprintf(stderr, "Could not open metatile %s. Reason: %s\n", name, strerror(errno));
        return -1;
    }

    struct stat st;
    fstat(fd, &st);

    buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED)
    {
        fprintf(stderr, "Cannot mmap file %s for %ld bytes: %s\n", name, st.st_size, strerror(errno));
        close(fd);
        return -3;
    }
    struct meta_layout *m = (struct meta_layout *)buf;

    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) 
    {
        fprintf(stderr, "Meta file %s header magic mismatch\n", name);
        close(fd);
        return -4;
    }

    if (m->count != (METATILE * METATILE)) 
    {
        fprintf(stderr, "Meta file %s header bad count %d != %d\n", name, m->count, METATILE * METATILE);
        close(fd);
        return -5;
    }

    if (store == NULL)
    {
        sprintf(path, "%s/%d", target, z);
        if (mkdir(path, 0755) && (errno != EEXIST))
        {
            fprintf(stderr, "cannot create directory %s: %s\n", path, strerror(errno));
            close(fd);
            return -1;
        }
    }

    if (store != NULL) {
        if (!force) tile_stat = store->tile_stat(store, target, "options", x, y, z);
        if (force || (tile_stat.size < 0) || (tile_stat.expired)) {
            if (store->metatile_write(store, target, "options", x, y, z, buf, st.st_size) == -1) {
                close(fd);
                fprintf(stderr, "Failed to write data to couchbase %s/%d/%d/%d.png\n", target, x, y, z);
                return -1;
            }
            if (verbose) printf("Produced metatile: %s/%d/%d/%d.meta\n", target, x, y, z);
        }
    } else {
        for (int meta = 0; meta < METATILE*METATILE; meta++)
        {
            int tx = x + (meta / METATILE);
            int ty = y + (meta % METATILE);
            int output;

            if (ty==y)
            {
                sprintf(path, "%s/%d/%d", target, z, tx);
                if (mkdir(path, 0755) && (errno != EEXIST))
                {
                    fprintf(stderr, "cannot create directory %s: %s\n", path, strerror(errno));
                    close(fd);
                    return -1;
                }
            }

            sprintf(path, "%s/%d/%d/%d.png", target, z, tx, ty);
            output = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
            if (output == -1)
            {
                fprintf(stderr, "cannot open %s for writing: %s\n", path, strerror(errno));
                close(fd);
                return -1;
            }

            pos = 0;
            while (pos < m->index[meta].size) 
            {
                size_t len = m->index[meta].size - pos;
                int written = write(output, buf + pos + m->index[meta].offset, len);

                if (written < 0)
                {
                    fprintf(stderr, "Failed to write data to file %s. Reason: %s\n", path, strerror(errno));
                    close(fd);
                    return -7;
                }
                else if (written > 0)
                {
                    pos += written;
                }
                else
                {
                    break;
                }
            }
            close(output);
            if (verbose) printf("Produced tile: %s\n", path);
        }
    }
    munmap(buf, st.st_size);
    close(fd);
    return pos;
}

void display_rate(struct timeval start, struct timeval end, int num) 
{
    int d_s, d_us;
    float sec;

    d_s  = end.tv_sec  - start.tv_sec;
    d_us = end.tv_usec - start.tv_usec;

    sec = d_s + d_us / 1000000.0;

    printf("Converted %d tiles in %.2f seconds (%.2f tiles/s)\n", num, sec, num / sec);
    fflush(NULL);
}

static void *thread(void *pArg)
{
    struct ThreadInfo *threadInfo = (struct ThreadInfo *) pArg;
//    printf("Started thread %d\n", threadInfo->id);

    pthread_mutex_lock(&threadInfo->mutex);

    threadInfo->status = THREAD_FREE;
//    threadInfo->store = init_storage_couchbase(store_conf);

    pthread_mutex_unlock(&threadInfo->mutex);

    while(1)
    {

        pthread_mutex_lock(&threadInfo->mutex);
        if ((threadInfo->status == THREAD_FREE) && (threadInfo->isFinished == 1))
        {
            pthread_mutex_unlock(&threadInfo->mutex);
            break;
        }

        if (threadInfo->status == THREAD_COMPUTE)
        {
            pthread_mutex_unlock(&threadInfo->mutex);

            expand_meta(threadInfo->path, threadInfo->store);

            pthread_mutex_lock(&threadInfo->mutex);
            threadInfo->status = THREAD_FREE;
            pthread_mutex_unlock(&threadInfo->mutex);
        }
        else
        {
            pthread_mutex_unlock(&threadInfo->mutex);
            usleep(THREAD_USLEEP);
        }
    }

//    printf("Finished thread %d\n", threadInfo->id);
    return NULL;
}

static void descend(const char *search, int zoomdone, struct ThreadInfo *pThreadInfo)
{
    DIR *tiles = opendir(search);
    struct dirent *entry;
    char path[PATH_MAX];
    int this_is_zoom = -1;

    if (!tiles) 
    {
        //fprintf(stderr, "Unable to open directory: %s\n", search);
        return;
    }

    while ((entry = readdir(tiles))) 
    {
        struct stat b;
        char *p;

        //check_load();

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        if (this_is_zoom == -1)
        {
            if (!zoomdone && isdigit(*(entry->d_name)) && atoi(entry->d_name) >= 0 && atoi(entry->d_name) <= MAXZOOM)
            {
                this_is_zoom = 1;
            }
            else
            {
                this_is_zoom = 0;
            }
        }

        if (this_is_zoom)
        {
            int z = atoi(entry->d_name);
            if (z<0 || z>MAXZOOM || !zoom[z]) continue;
        }

        snprintf(path, sizeof(path), "%s/%s", search, entry->d_name);
        if (stat(path, &b))
            continue;
        if (S_ISDIR(b.st_mode)) {
            descend(path, zoomdone || this_is_zoom, pThreadInfo);
            continue;
        }
        p = strrchr(path, '.');
        if (p && !strcmp(p, ".meta")) {
            while(1)
            {
                char isFind = 0;
                for (int j = 0; j < threadCount; ++j)
                {
                    pthread_mutex_lock(&pThreadInfo[j].mutex);
                    if (pThreadInfo[j].status == THREAD_FREE)
                    {
                        //printf("buff = %s", buff);
                        strcpy(pThreadInfo[j].path, path);
                        pThreadInfo[j].status = THREAD_COMPUTE;
                        isFind = 1;
                        pthread_mutex_unlock(&pThreadInfo[j].mutex);
                        break;
                    }
                    pthread_mutex_unlock(&pThreadInfo[j].mutex);
                }

                if (isFind == 1)
                    break;
                else
                    usleep(THREAD_USLEEP);
            }
            num_render++;
        }
    }
    closedir(tiles);
}


void usage()
{
    fprintf(stderr, "Usage: m2t [-m mode] [-b bbox] [-z zoom] [-f force] [-t threads] [-c \"couchbase:{memcached://localhost:11211,memcached://localhost:11212}\"] sourcedir targetdir\n");
    fprintf(stderr, "Convert .meta files found in source dir to .png in target dir,\n");
    fprintf(stderr, "using the standard \"hash\" type directory (5-level) for meta\n");
    fprintf(stderr, "tiles and the z/x/y.png structure (3-level) for output.\n");
}

int handle_bbox(char *arg)
{
    char *token = strtok(arg, ",");
    int bbi = 0;
    while(token && bbi<4)
    {
        bbox[bbi++] = atof(token);
        token = strtok(NULL, ",");
    }
    return (bbi==4 && token==NULL);
}

int handle_zoom(char *arg)
{
    char *token = strtok(arg, ",");
    while(token)
    {
        int fromz = atoi(token);
        int toz = atoi(token);
        char *minus = strchr(token, '-');
        if (minus)
        {
            toz = atoi(minus+1);
        }
        if (fromz<0 || toz<0 || fromz>MAXZOOM || toz>MAXZOOM || toz<fromz || !isdigit(*token)) return 0;
        for (int i=fromz; i<=toz; i++) zoom[i]=1;
        token = strtok(NULL, ",");
    }
    return 1;
}

int main(int argc, char **argv)
{
    int c;
    for (int i=0; i<=MAXZOOM; i++) zoom[i]=0;
    int zoomset = 0;
    while (1) {
        int option_index = 0;
        static struct option long_options[] = 
        {
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {"couchbase", 1, 0, 'c'},
            {"bbox", 1, 0, 'b'},
            {"mode", 1, 0, 'm'},
            {"zoom", 1, 0, 'z'},
            {"threads", 1, 0, 't'},
            {"force", 0, 0, 'f'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "vhb:m:z:c:t:f", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) 
        {
            case 'v':
                verbose=1;
                break;
            case 'h':
                usage();
                return -1;
            case 'b':
                if (!handle_bbox(optarg))
                {
                    fprintf(stderr, "invalid bbox argument - must be of the form east,south,west,north\n");
                    return -1;
                }
                break;
            case 'z':
                zoomset = 1;
                if (!handle_zoom(optarg))
                {
                    fprintf(stderr, "invalid zoom argument - must be of the form zoom or z0,z1,z2... or z0-z1\n");
                    return -1;
                }
                break;
            case 'c':
                sprintf(store_conf, "%s", optarg);
                if (store_conf == NULL) {
                    return -1;
                }
                break;
            case 't':
                threadCount=atoi(optarg);
                if (threadCount <= 0) {
                    fprintf(stderr, "Invalid number of threads, must be at least 1\n");
                    return 1;
                }
                break;
            case 'm': 
                if (!strcmp(optarg, "glob"))
                {
                    mode = MODE_GLOB;
                }
                else if (!strcmp(optarg, "stat"))
                {
                    mode = MODE_STAT;
                    fprintf(stderr, "mode=stat not yet implemented\n");
                    return -1;
                }
                else
                {
                    fprintf(stderr, "mode argument must be either 'glob' or 'stat'\n");
                    return -1;
                }
                break;
            case 'f':   /* -f, --force */
                force=1;
                break;
            default:
                fprintf(stderr, "unhandled char '%c'\n", c);
                break;
        }
    }

    struct ThreadInfo *pThreadInfo = (struct ThreadInfo *)malloc(sizeof(struct ThreadInfo) * threadCount);

    if (!pThreadInfo) {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    for (int i = 0; i < threadCount; ++i)
    {
        pThreadInfo[i].isFinished = 0;
        pThreadInfo[i].id = i;
        pThreadInfo[i].store = init_storage_couchbase(store_conf);
        pThreadInfo[i].status = THREAD_COMPUTE;
        pthread_mutex_init(&pThreadInfo[i].mutex, NULL);
        if (pthread_create(&pThreadInfo[i].pid, NULL, thread, (void *)&pThreadInfo[i]) != 0)
        {
            fprintf(stderr, "Failed to start thread\n");
            _exit(-1);
        }
    }

    if (!zoomset) for (int i=0; i<=MAXZOOM; i++) zoom[i]=1;

    if (optind >= argc-1)
    {
        usage();
        return -1;
    }

    source=argv[optind++];
    target=argv[optind++];

    fprintf(stderr, "Converting tiles from directory %s to directory %s\n", source, target);

    gettimeofday(&start, NULL);

    descend(source, 0, pThreadInfo);

    for (int j = 0; j < threadCount; ++j) {
        pthread_mutex_lock(&pThreadInfo[j].mutex);
        pThreadInfo[j].isFinished = 1;
        pthread_mutex_unlock(&pThreadInfo[j].mutex);
    }

    for (int i = 0; i < threadCount; ++i) {
        pthread_join(pThreadInfo[i].pid, NULL);
    }

    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles converted\n");
    printf("Meta tiles converted: ");
    display_rate(start, end, num_render);
    printf("Total tiles converted: ");
    display_rate(start, end, num_render * METATILE * METATILE);

    for (int i=0; i<threadCount; i++) {
        if (pThreadInfo[i].store != NULL) {
            pThreadInfo[i].store->close_storage(pThreadInfo[i].store);
        }
    }

    free(pThreadInfo);

    return 0;
}
