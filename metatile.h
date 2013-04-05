/*
Copyright © 2013 mod_tile contributors

This file is part of mod_tile.

mod_tile is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

mod_tile is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

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

