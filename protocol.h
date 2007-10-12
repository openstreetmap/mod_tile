#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol between client and render daemon
 *
 * ver = 1;
 *
 * cmdRender(z,x,y), response: {cmdDone(z,x,y), cmdBusy(z,x,y)}
 * cmdDirty(z,x,y), no response
 *
 * A client may not bother waiting for a response if the render daemon is too slow
 * causing responses to get slightly out of step with requests.
 */
#define TILE_PATH_MAX (256)
#define PROTO_VER (1)
#define RENDER_SOCKET "/tmp/osm-renderd"

enum protoCmd { cmdIgnore, cmdRender, cmdDirty, cmdDone, cmdNotDone };

struct protocol {
    int ver;
    enum protoCmd cmd;
    int x;
    int y;
    int z;
    char path[TILE_PATH_MAX]; // FIXME: this is a really bad idea since it allows wrties to arbitrrary stuff
};

#ifdef __cplusplus
}
#endif
#endif
