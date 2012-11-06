#ifndef STORE_H
#define STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "render_config.h"
    int tile_read(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, int sz, int * compressed, unsigned char * err_msg);

#define META_MAGIC "META"
#define META_MAGIC_COMPRESSED "METZ"
//static const char meta_magic[4] = { 'M', 'E', 'T', 'A' };

struct entry {
    int offset;
    int size;
};

struct meta_layout {
    char magic[4];
    int count; // METATILE ^ 2
    int x, y, z; // lowest x,y of this metatile, plus z
    struct entry index[]; // count entries
    // Followed by the tile data
    // The index offsets are measured from the start of the file
};


int read_from_file(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, size_t sz);

#ifdef METATILE
    int read_from_meta(const char *tilepath, const char *xmlconfig, int x, int y, int z, unsigned char *buf, size_t sz, int * compressed, unsigned char * log_msg);
    void process_meta(const char *tilepath, const char *xmlconfig, int x, int y, int z);
    void process_pack(const char *tilepath, const char *name);
    void process_unpack(const char *tilepath, const char *name);
#endif

#ifdef __cplusplus
}
#endif
#endif
