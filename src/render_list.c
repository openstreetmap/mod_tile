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
#include "store.h"
#include "sys_utils.h"
#include "render_submit_queue.h"

const char * tile_dir_default = HASH_PATH;

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

int main(int argc, char **argv)
{
    char *spath = strdup(RENDER_SOCKET);
    const char *mapname_default = XMLCONFIG_DEFAULT;
    const char *mapname = mapname_default;
    const char *tile_dir = tile_dir_default;
    int minX=-1, maxX=-1, minY=-1, maxY=-1;
    int x, y, z;
    char name[PATH_MAX];
    struct timeval start, end;
    int num_render = 0, num_all = 0;
    int c;
    int all=0;
    int numThreads = 1;
    int force=0;
    struct storage_backend * store;
    struct stat_info s;

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
                free(spath);
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

    store = init_storage_backend(tile_dir);
    if (store == NULL) {
        fprintf(stderr, "Failed to initialise storage backend %s\n", tile_dir);
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

    gettimeofday(&start, NULL);

    spawn_workers(numThreads, spath, maxLoad);

    if (all) {
        int x, y, z;
        printf("Rendering all tiles from zoom %d to zoom %d\n", minZoom, maxZoom);
        for (z=minZoom; z <= maxZoom; z++) {
            int current_maxX = (maxX == -1) ? (1 << z)-1 : maxX;
            int current_maxY = (maxY == -1) ? (1 << z)-1 : maxY;
            printf("Rendering all tiles for zoom %d from (%d, %d) to (%d, %d)\n", z, minX, minY, current_maxX, current_maxY);
            for (x=minX; x <= current_maxX; x+=METATILE) {
                for (y=minY; y <= current_maxY; y+=METATILE) {
                    if (!force) s = store->tile_stat(store, mapname, "", x, y, z);
                    if (force || (s.size < 0) || (s.expired)) {
                        enqueue(mapname, x, y, z);
                        num_render++;
                    }
                    num_all++;

                }
            }
        }
    } else {
        while(!feof(stdin)) {
            int n = fscanf(stdin, "%d %d %d", &x, &y, &z);

            if (verbose)


            if (n != 3) {
                // Discard input line
                char tmp[1024];
                char *r = fgets(tmp, sizeof(tmp), stdin);
                if (!r)
                    continue;
                fprintf(stderr, "bad line %d: %s", num_all, tmp);
                continue;
            }

            if (verbose)
                printf("got: x(%d) y(%d) z(%d)\n", x, y, z);

            if (z < minZoom || z > maxZoom) {
                printf("Ignoring tile, zoom %d outside valid range (%d..%d)\n", z, minZoom, maxZoom);
                continue;
            }

            num_all++;

            if (!force) s = store->tile_stat(store, mapname, "", x, y, z);
            if (force || (s.size < 0) || (s.expired)) {
                // missing or old, render it
                //ret = process_loop(fd, mapname, x, y, z);
                enqueue(mapname, x, y, z);
                num_render++;
                // Attempts to adjust the stats for the QMAX tiles which are likely in the queue
                if (!(num_render % 10)) {
                    gettimeofday(&end, NULL);
                    printf("\n");
                    printf("Meta tiles rendered: ");
                    display_rate(start, end, num_render);
                    printf("Total tiles rendered: ");
                    display_rate(start, end, (num_render) * METATILE * METATILE);
                    printf("Total tiles handled from input: ");
                    display_rate(start, end, num_all);
                }
            } else {
                if (verbose)
                    printf("Tile %s is clean, ignoring\n", store->tile_storage_id(store, mapname, "", x, y, z, name));
            }
        }
    }

    store->close_storage(store);
    free(store);
    finish_workers();

    free(spath);
    if (mapname != mapname_default) {
        free((void *)mapname);
    }
    if (tile_dir != tile_dir_default) {
        free((void *)tile_dir);
    }


    gettimeofday(&end, NULL);
    printf("\n*****************************************************\n");
    printf("*****************************************************\n");
    printf("Total for all tiles rendered\n");
    printf("Meta tiles rendered: ");
    display_rate(start, end, num_render);
    printf("Total tiles rendered: ");
    display_rate(start, end, num_render * METATILE * METATILE);
    printf("Total tiles handled: ");
    display_rate(start, end, num_all);
    print_statistics();

    return 0;
}
#endif
