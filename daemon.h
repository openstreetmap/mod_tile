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

#ifndef DAEMON_H
#define DEEMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_DAEMON
    int daemon(int nochdir, int noclose);
#endif

#include <limits.h> /* for PATH_MAX */

#include "protocol.h"

#define INILINE_MAX 256
#define MAX_SLAVES 5

typedef struct {
    char *socketname;
    char *iphostname;
    int ipport;
    int num_threads;
    char *tile_dir;
    char *mapnik_plugins_dir;
    char *mapnik_font_dir;
    int mapnik_font_dir_recurse;
    char * stats_filename;
} renderd_config;

typedef struct {
    char xmlname[XMLCONFIG_MAX];
    char xmlfile[PATH_MAX];
    char xmluri[PATH_MAX];
    char host[PATH_MAX];
    char htcpip[PATH_MAX];
    char tile_dir[PATH_MAX];
    int tile_px_size;
} xmlconfigitem;

typedef struct {
    long noDirtyRender;
    long noReqRender;
    long noReqPrioRender;
    long noReqBulkRender;
    long noReqDroped;
    long noZoomRender[MAX_ZOOM + 1];
    long timeReqRender;
    long timeReqPrioRender;
    long timeReqBulkRender;
    long timeZoomRender[MAX_ZOOM + 1];
} stats_struct;

void statsRenderFinish(int z, long time);
void request_exit(void);

#ifdef __cplusplus
}
#endif
#endif
