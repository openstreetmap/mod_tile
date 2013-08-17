/*
 * Copyright © 2013 mod_tile contributors
 * Copyright © 2013 Kai Krueger
 *
 * This file is part of renderd, a project to render OpenStreetMap tiles
 * with Mapnik.
 *
 * renderd is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * mod_tile is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <stdlib.h>

#include "render_config.h"
#include "metatile.h"
#include "store.h"
#include "cache_expire.h"
#include "request_queue.h"


metaTile::metaTile(const std::string &xmlconfig, int x, int y, int z):
    x_(x), y_(y), z_(z), xmlconfig_(xmlconfig) {
    clear();
}

void metaTile::clear() {
    for (int x = 0; x < METATILE; x++)
        for (int y = 0; y < METATILE; y++)
            tile[x][y] = "";
}

void metaTile::set(int x, int y, const std::string &data) {
    tile[x][y] = data;
}

const std::string metaTile::get(int x, int y) {
    return tile[x][y];
}

// Returns the offset within the meta-tile index table
int metaTile::xyz_to_meta_offset(int x, int y, int z) {
    unsigned char mask = METATILE - 1;
    return (x & mask) * METATILE + (y & mask);
}

void metaTile::save(struct storage_backend * store) {
    int ox, oy, limit;
    size_t offset;
    struct meta_layout m;
    struct entry offsets[METATILE * METATILE];
    char * metatilebuffer;
    char *tmp;

    memset(&m, 0, sizeof(m));
    memset(&offsets, 0, sizeof(offsets));
    
    // Create and write header
    m.count = METATILE * METATILE;
    memcpy(m.magic, META_MAGIC, strlen(META_MAGIC));
    m.x = x_;
    m.y = y_;
    m.z = z_;
    
    offset = header_size;
    limit = (1 << z_);
    limit = MIN(limit, METATILE);
    limit = METATILE;
    
    // Generate offset table
    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            int mt = xyz_to_meta_offset(x_ + ox, y_ + oy, z_);
            offsets[mt].offset = offset;
            offsets[mt].size   = tile[ox][oy].size();
            offset += offsets[mt].size;
        }
    }
    
    metatilebuffer = (char *) malloc(offset);
    if (metatilebuffer == 0) {
        syslog(LOG_WARNING, "Failed to write metatile. Out of memory");
        return;
    }
    memset(metatilebuffer, 0, offset);
    memcpy(metatilebuffer,&m,sizeof(m));
    memcpy(metatilebuffer + sizeof(m), &offsets, sizeof(offsets));
    
    // Write tiles
    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            memcpy(metatilebuffer + offsets[xyz_to_meta_offset(x_ + ox, y_ + oy, z_)].offset, (const void *)tile[ox][oy].data(), tile[ox][oy].size());
        }
    }
    
    if (store->metatile_write(store, xmlconfig_.c_str(),x_,y_,z_,metatilebuffer, offset) != offset) {
        tmp = (char *)malloc(sizeof(char) * PATH_MAX);
        syslog(LOG_WARNING, "Failed to write metatile to %s", store->tile_storage_id(store, xmlconfig_.c_str(),x_,y_,z_, tmp));
        free(tmp);
    }
    
    free(metatilebuffer);
}


void metaTile::expire_tiles(int sock, char * host, char * uri) {
    if (sock < 0) {
        return;
    }
    syslog(LOG_INFO, "Purging metatile via HTCP cache expiry");
    int ox, oy;
    int limit = (1 << z_);
    limit = MIN(limit, METATILE);
    
    // Generate offset table
    for (ox=0; ox < limit; ox++) {
        for (oy=0; oy < limit; oy++) {
            cache_expire(sock, host, uri, (x_ + ox), (y_ + oy), z_);
        }
    }
}


