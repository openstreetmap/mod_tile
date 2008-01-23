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

#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"

#define DEG_TO_RAD (M_PIl/180)
#define RAD_TO_DEG (180/M_PIl)

static const int minZoom = 0;
static const int maxZoom = 18;


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
        int n = fscanf(stdin, "%d %d %d %*s %*s", &x, &y, &z);

        if (n != 3) {
            // Discard input line
            char tmp[1024];
            char *r = fgets(tmp, sizeof(tmp), stdin);
            if (!r)
                continue;
            // fprintf(stderr, "bad line %d: %s", num_all, tmp);
            continue;
        }
//        printf("got: x(%d) y(%d) z(%d)\n", x, y, z);

        num_all++;
        xyz_to_path(name, sizeof(name), x, y, z);

        if (stat(name, &s) < 0) {
            // Assume error is that file doesn't exist
            // so render it
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
