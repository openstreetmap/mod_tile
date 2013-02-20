#define _GNU_SOURCE

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
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include "render_config.h"
#include "dir_utils.h"
#include "store.h"

char *tile_dir = HASH_PATH;

#ifndef METATILE
#warning("convert_meta not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
    fprintf(stderr, "convert_meta not implemented for non-metatile mode. Feel free to submit fix!\n");
    return -1;
}
#else

static int minZoom = 0;
static int maxZoom = MAX_ZOOM; 
static int verbose = 0;
static int num_render = 0, num_all = 0;
static struct timeval start, end;
static int unpack;

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

static void descend(const char *search)
{
    DIR *tiles = opendir(search);
    struct dirent *entry;
    char path[PATH_MAX];

    if (!tiles) {
        //fprintf(stderr, "Unable to open directory: %s\n", search);
        return;
    }

    while ((entry = readdir(tiles))) {
        struct stat b;
        char *p;

        //check_load();

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
        if (p) {
            if (unpack) {
                if (!strcmp(p, ".meta")) 
                    process_unpack(tile_dir, path);
            } else {
                if (!strcmp(p, ".png")) 
                  process_pack(tile_dir, path);
            }
        }
    }
    closedir(tiles);
}


int main(int argc, char **argv)
{
    int z, c;
    const char *map = "default";

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"map", 1, 0, 'm'},
            {"min-zoom", 1, 0, 'z'},
            {"max-zoom", 1, 0, 'Z'},
            {"unpack", 0, 0, 'u'},
            {"tile-dir", 1, 0, 't'},
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "uhvz:Z:m:t:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'z':
                minZoom=atoi(optarg);
                if (minZoom < 0 || minZoom > MAX_ZOOM) { 
                    fprintf(stderr, "Invalid minimum zoom selected, must be between 0 and %d\n", MAX_ZOOM);
                    return 1;
                }
                break;
            case 'Z':
                maxZoom=atoi(optarg);
                if (maxZoom < 0 || maxZoom > MAX_ZOOM) {
                    fprintf(stderr, "Invalid maximum zoom selected, must be between 0 and %d\n", MAX_ZOOM);
                    return 1;
                }
                break;
            case 'm':
                map=strdup(optarg);
                break;
            case 't':
                tile_dir=strdup(optarg);
                break;
            case 'u':
                unpack=1;
                break;
            case 'v':
                verbose=1;
                break;
            case 'h':
                fprintf(stderr, "Usage: convert_meta [OPTION] ...\n");
                fprintf(stderr, "Convert the rendered PNGs into the more efficient .meta format\n");
                fprintf(stderr, "  -m, --map       convert tiles in this map (default is 'default')\n");
                fprintf(stderr, "  -t, --tile-dir  tile cache directory (default is '" HASH_PATH "')\n");
                fprintf(stderr, "  -u, --unpack    unpack the .meta files back to PNGs\n");
                fprintf(stderr, "  -z, --min-zoom  only process tiles greater or equal to this zoom level (default is 0)\n");
                fprintf(stderr, "  -Z, --max-zoom  only process tiles less than or equal to this zoom level (default is %d)\n", MAX_ZOOM);
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

    fprintf(stderr, "Converting tiles in map %s\n", map);

    gettimeofday(&start, NULL);

    for (z=minZoom; z<=maxZoom; z++) {
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s/%d", tile_dir, map, z);
        descend(path);
    }

    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles converted\n");
    printf("Meta tiles converted: ");
    display_rate(start, end, num_render);
    printf("Total tiles converted: ");
    display_rate(start, end, num_render * METATILE * METATILE);
    printf("Total tiles handled: ");
    display_rate(start, end, num_all);

    return 0;
}
#endif
