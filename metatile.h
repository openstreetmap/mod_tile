#ifndef METATILE_H
#define METATILE_H

#ifdef __cplusplus
extern "C" {
#endif

#define META_MAGIC "META"
#define META_MAGIC_COMPRESSED "METZ"
    
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


#ifdef __cplusplus
}
#endif
#endif

