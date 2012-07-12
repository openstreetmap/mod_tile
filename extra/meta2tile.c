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
#include <math.h>
#include <getopt.h>
#include <string.h>

#include "store.h"

#define MIN(x,y) ((x)<(y)?(x):(y))
#define META_MAGIC "META"

static int verbose = 0;
static int num_render = 0;
static struct timeval start, end;

const char *source;
const char *target;

int path_to_xyz(const char *path, int *px, int *py, int *pz)
{
    int i, n, hash[5], x, y, z;
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

int expand_meta(const char *name)
{
    int fd;
    char header[4096];
    int x, y, z;
    size_t pos;
    void *buf;

    if (path_to_xyz(name, &x, &y, &z)) return -1;

    int limit = (1 << z);
    limit = MIN(limit, METATILE);

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

    char path[PATH_MAX];
    sprintf(path, "%s/%d", target, z);
    if (mkdir(path, 0755) && (errno != EEXIST))
    {
        fprintf(stderr, "cannot create directory %s: %s\n", path, strerror(errno));
        close(fd);            
        return -1;
    }

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

    munmap(buf, st.st_size);
    close(fd);
    num_render++;
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
        if (p && !strcmp(p, ".meta")) expand_meta(path);
    }
    closedir(tiles);
}

void usage()
{
    fprintf(stderr, "Usage: m2t sourcedir targetdir\n");
    fprintf(stderr, "Convert .meta files found in source dir to .png in target dir,\n");
    fprintf(stderr, "using the standard \"hash\" type directory (5-level) for meta\n");
    fprintf(stderr, "tiles and the x/y/z.png structure (3-level) for output.\n");
}

int main(int argc, char **argv)
{
    int c;
    while (1) {
        int option_index = 0;
        static struct option long_options[] = 
        {
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "vh", long_options, &option_index);
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
            default:
                fprintf(stderr, "unhandled char '%c'\n", c);
                break;
        }
    }

    if (optind >= argc-1)
    {
        usage();
        return -1;
    }

    source=argv[optind++];
    target=argv[optind++];

    fprintf(stderr, "Converting tiles from directory %s to directory %s\n", source, target);

    gettimeofday(&start, NULL);

    descend(source);

    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles converted\n");
    printf("Meta tiles converted: ");
    display_rate(start, end, num_render);
    printf("Total tiles converted: ");
    display_rate(start, end, num_render * METATILE * METATILE);

    return 0;
}
