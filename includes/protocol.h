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
#define PROTO_VER (3)
#define RENDER_SOCKET "/var/run/renderd/renderd.sock"
#define XMLCONFIG_MAX 41

enum protoCmd { cmdIgnore, cmdRender, cmdDirty, cmdDone, cmdNotDone, cmdRenderPrio, cmdRenderBulk, cmdRenderLow };

struct protocol {
    int ver;
    enum protoCmd cmd;
    int x;
    int y;
    int z;
    char xmlname[XMLCONFIG_MAX];
    char mimetype[XMLCONFIG_MAX]; 
    char options[XMLCONFIG_MAX]; 
};

struct protocol_v1 {
    int ver;
    enum protoCmd cmd;
    int x;
    int y;
    int z;
}; 

struct protocol_v2 { 
    int ver; 
    enum protoCmd cmd; 
    int x; 
    int y; 
    int z; 
    char xmlname[XMLCONFIG_MAX]; 
};

#ifdef __cplusplus
}
#endif
#endif
