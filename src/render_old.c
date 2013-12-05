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
#include "store_file_utils.h"
#include "render_submit_queue.h"
#include "sys_utils.h"

const char * tile_dir_default = HASH_PATH;

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

static time_t getPlanetTime(const char *tile_dir)
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

static void check_load(void)
{
    double avg = get_load_avg();

    while (avg >= max_load) {
        printf("Load average %f, sleeping\n", avg);
        sleep(5);
        avg = get_load_avg();
    }
}

static void descend(const char *tile_dir, const char *search)
{
    DIR *tiles = opendir(search);
    struct dirent *entry;
    char path[PATH_MAX];
    char mapname[XMLCONFIG_MAX];
    int x, y, z;

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
            descend(tile_dir, path);
            continue;
        }
        p = strrchr(path, '.');
        if (p && !strcmp(p, ".meta")) {
            num_all++;
            if (planetTime > b.st_mtime) {
                // request rendering of  old tile
                path_to_xyz(tile_dir, path, mapname, &x, &y, &z);
                enqueue(mapname, x, y, z);
            }
        }
    }
    closedir(tiles);
}

void render_layer(const char *tilepath, const char *name)
{
    int z;

    for (z=minZoom; z<=maxZoom; z++) {
		if (verbose)
			printf("Rendering zoom %d\n", z);				
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s/%d", tilepath, name, z);
        descend(tilepath, path);
    }
}

int main(int argc, char **argv)
{
    char spath[PATH_MAX] = RENDER_SOCKET;
    char *config_file = RENDERD_CONFIG;
    const char *tile_dir = tile_dir_default;
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
                
                if (sscanf(optarg,"%d/%d/%d", &dd, &mm, &yy) == 3) {
                    if (yy > 100) yy -= 1900;
                    if (yy < 70) yy += 100;
                
                    memset(&tm,0,sizeof(tm));
                    tm.tm_mday = dd; tm.tm_mon = mm - 1; tm.tm_year =  yy;
                    planetTime = mktime(&tm);
                } else if (sscanf(optarg,"%d", &dd) == 1) {
                    planetTime = dd;
                } else {
                    fprintf(stderr, "Invalid planet time stamp, must be a unix timestamp or in the format dd/mm/yyyy\n");
                    return 1;
                }
                break;

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

    spawn_workers(numThreads, spath, max_load);

    if (map) {
        render_layer(tile_dir, map);
    } else {
        while (fgets(line, INILINE_MAX, hini)!=NULL) {
            if (line[0] == '[') {
                if (strlen(line) >= XMLCONFIG_MAX){
                    fprintf(stderr, "XML name too long: %s\n", line);
                    exit(7);
                }
                if (sscanf(line, "[%[^]]", value) != 1) {
                    fprintf(stderr, "Config: malformed config file on line %s\n", line);
                    exit(7);
                };
                // Skip mapnik & renderd sections which are config, not tile layers
                if (strcmp(value,"mapnik") && strncmp(value, "renderd", 7))
                    render_layer(tile_dir, value);
            }
        }
    }
    fclose(hini);
    free(map);

    if (tile_dir != tile_dir_default) {
        free((void *)tile_dir);
    }
	
    finish_workers();

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
