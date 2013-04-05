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

#ifndef STORE_H
#define STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "render_config.h"

#define STORE_LOGLVL_DEBUG 0
#define STORE_LOGLVL_INFO 1
#define STORE_LOGLVL_WARNING 2
#define STORE_LOGLVL_ERR 3

    struct stat_info {
        off_t     size;    /* total size, in bytes */
        time_t    atime;   /* time of last access */
        time_t    mtime;   /* time of last modification */
        time_t    ctime;   /* time of last status change */
        int       expired;  /* has the tile expired */
    };

    struct storage_backend {
        int (*tile_read)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z, char *buf, size_t sz, int * compressed, char * err_msg);
        struct stat_info (*tile_stat)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z);
        int (*metatile_write)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z, const char *buf, int sz);
        int (*metatile_delete)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z);
        int (*metatile_expire)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z);
        char * (*tile_storage_id)(struct storage_backend * store, const char *xmlconfig, int x, int y, int z, char * string);
        int (*close_storage)(struct storage_backend * store);

        void * storage_ctx;
    };

    void log_message(int log_lvl, const char *format, ...);
    
    struct storage_backend * init_storage_backend(const char * options);
        
#ifdef __cplusplus
}
#endif
#endif
