#ifndef METATILE_H
#define METATILE_H

#include "config.h"
#include <stdlib.h>
#include "render_config.h"

#ifdef __cplusplus
#include <sstream>
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

class metaTile {
 public:
    metaTile(const std::string &xmlconfig, const std::string &options, int x, int y, int z);
    void clear();
    void set(int x, int y, const std::string &data);
    const std::string get(int x, int y);
    int xyz_to_meta_offset(int x, int y, int z);
    void save(struct storage_backend * store);
    void expire_tiles(int sock, char * host, char * uri);
 private:
    int x_, y_, z_;
    std::string xmlconfig_;
    std::string options_;
    std::string tile[METATILE][METATILE];
    static const int header_size = sizeof(struct meta_layout) + (sizeof(struct entry) * (METATILE * METATILE));
    
};

#endif
#endif

