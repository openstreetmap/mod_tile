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
#include <limits.h>

#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"

#ifndef METATILE
#warning("render_list not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
    fprintf(stderr, "render_list not implemented for non-metatile mode. Feel free to submit fix!\n");
    return -1;
}
#else

#define DEG_TO_RAD (M_PIl/180)
#define RAD_TO_DEG (180/M_PIl)

static int minZoom = 0;
static int maxZoom = 18;
static int verbose = 0;

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

static time_t getPlanetTime(void)
{
    static time_t last_check;
    static time_t planet_timestamp;
    time_t now = time(NULL);
    struct stat buf;

    // Only check for updates periodically
    if (now < last_check + 300)
        return planet_timestamp;

    last_check = now;
    if (stat(PLANET_TIMESTAMP, &buf)) {
        fprintf(stderr, "Planet timestamp file " PLANET_TIMESTAMP " is missing");
        // Make something up
        planet_timestamp = now - 3 * 24 * 60 * 60;
    } else {
        if (buf.st_mtime != planet_timestamp) {
            fprintf(stderr, "Planet file updated at %s", ctime(&buf.st_mtime));
            planet_timestamp = buf.st_mtime;
        }
    }
    return planet_timestamp;
}

int process_loop(int fd, int x, int y, int z)
{
    struct protocol cmd, rsp;
    //struct pollfd fds[1];
    int ret = 0;

    bzero(&cmd, sizeof(cmd));

    cmd.ver = 1;
    cmd.cmd = cmdRender;
    cmd.z = z;
    cmd.x = x;
    cmd.y = y;
    //strcpy(cmd.path, "/tmp/foo.png");

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

    if (!ret)
        perror("Socket send error");
    return ret;
}


int main(int argc, char **argv)
{
    const char *spath = RENDER_SOCKET;
    int fd;
    struct sockaddr_un addr;
    int ret=0;
    int x, y, z;
    char name[PATH_MAX];
    struct timeval start, end;
    int num_render = 0, num_all = 0;
    time_t planetTime = getPlanetTime();
    int c;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"min-zoom", 1, 0, 'z'},
            {"max-zoom", 1, 0, 'Z'},
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "hvz:Z:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'z':
                minZoom=atoi(optarg);
                if (minZoom < 0 || minZoom > 18) {
                    fprintf(stderr, "Invalid minimum zoom selected, must be between 0 and 18\n");
                    return 1;
                }
                break;
            case 'Z':
                maxZoom=atoi(optarg);
                if (maxZoom < 0 || maxZoom > 18) {
                    fprintf(stderr, "Invalid maximum zoom selected, must be between 0 and 18\n");
                    return 1;
                }
                break;
            case 'v':
                verbose=1;
                break;
            case 'h':
                fprintf(stderr, "Send a list of tiles to be rendered from STDIN in the format:\n");
                fprintf(stderr, "\tX    Y    Z\n");
                fprintf(stderr, "e.g.\n");
                fprintf(stderr, "\t0    0    1\n");
                fprintf(stderr, "\t0    1    1\n");
                fprintf(stderr, "\t1    0    1\n");
                fprintf(stderr, "\t1    1    1\n");
                fprintf(stderr, "The above would cause all 4 tiles at zoom 1 to be rendered\n");
                fprintf(stderr, "\t-z|--min-zoom\tFilter input to only render tiles greater or equal this zoom level (default 0)\n");
                fprintf(stderr, "\t-Z|--max-zoom\tFilter input to only render tiles less than or equal to this zoom level (default 18)\n");
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

    fprintf(stderr, "Rendering client\n");

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
        exit(3);
    }

    gettimeofday(&start, NULL);

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

        if (z < minZoom || z > maxZoom)
            continue;

        printf("got: x(%d) y(%d) z(%d)\n", x, y, z);

        num_all++;
        xyz_to_path(name, sizeof(name), XMLCONFIG_DEFAULT, x, y, z);

        if ((stat(name, &s) < 0) || (planetTime > s.st_mtime)) {
            // missing or old, render it
            ret = process_loop(fd, x, y, z);
            num_render++;
            if (!(num_render % 10)) {
                gettimeofday(&end, NULL);
                printf("\n");
                printf("Meta tiles rendered: ");
                display_rate(start, end, num_render);
                printf("Total tiles rendered: ");
                display_rate(start, end, num_render * METATILE * METATILE);
                printf("Total tiles handled from input: ");
                display_rate(start, end, num_all);
            }
        }
    }
    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles rendered\n");
    printf("Meta tiles rendered: ");
    display_rate(start, end, num_render);
    printf("Total tiles rendered: ");
    display_rate(start, end, num_render * METATILE * METATILE);
    printf("Total tiles handled: ");
    display_rate(start, end, num_all);

    close(fd);
    return ret;
}
#endif
