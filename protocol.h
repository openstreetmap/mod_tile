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

#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol between client and render daemon
 *
 * ver = 2;
 *
 * cmdRender(z,x,y,xmlconfig), response: {cmdDone(z,x,y), cmdBusy(z,x,y)}
 * cmdDirty(z,x,y,xmlconfig), no response
 *
 * A client may not bother waiting for a response if the render daemon is too slow
 * causing responses to get slightly out of step with requests.
 */
#define TILE_PATH_MAX (256)
#define PROTO_VER (2)
#define RENDER_SOCKET "/tmp/osm-renderd"
#define XMLCONFIG_MAX 41

enum protoCmd { cmdIgnore, cmdRender, cmdDirty, cmdDone, cmdNotDone, cmdRenderPrio, cmdRenderBulk };

struct protocol {
    int ver;
    enum protoCmd cmd;
    int x;
    int y;
    int z;
    char xmlname[XMLCONFIG_MAX];
};

struct protocol_v1 {
    int ver;
    enum protoCmd cmd;
    int x;
    int y;
    int z;
};

#ifdef __cplusplus
}
#endif
#endif
