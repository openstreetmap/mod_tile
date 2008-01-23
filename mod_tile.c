#include "apr.h"
#include "apr_strings.h"
#include "apr_thread_proc.h"    /* for RLIMIT stuff */
#include "apr_optional.h"
#include "apr_buckets.h"
#include "apr_lib.h"
#include "apr_poll.h"

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC
#include "apr_want.h"

#include "util_filter.h"
#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_main.h"
#include "http_log.h"
#include "util_script.h"
#include "ap_mpm.h"
#include "mod_core.h"
#include "mod_cgi.h"

module AP_MODULE_DECLARE_DATA tile_module;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"

enum tileState { tileMissing, tileOld, tileCurrent };

static int error_message(request_rec *r, const char *format, ...)
                 __attribute__ ((format (printf, 2, 3)));

static int error_message(request_rec *r, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len;
    char *msg;

    len = vasprintf(&msg, format, ap);

    if (msg) {
        //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", msg);
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "%s", msg);
        r->content_type = "text/plain";
        if (!r->header_only)
            ap_rputs(msg, r);
        free(msg);
    }

    return OK;
}


int socket_init(request_rec *r)
{
    const char *spath = RENDER_SOCKET;
    int fd;
    struct sockaddr_un addr;

    //fprintf(stderr, "Starting rendering client\n");

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "failed to create unix socket");
        return FD_INVALID;
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, spath, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "socket connect failed for: %s", spath);
        close(fd);
        return FD_INVALID;
    }
    return fd;
}


int request_tile(request_rec *r, int x, int y, int z, const char *filename, int dirtyOnly)
{
    struct protocol cmd;
    //struct pollfd fds[1];
    static int fd = FD_INVALID;
    int ret = 0;
    int retry = 1;
 
    if (fd == FD_INVALID) {
        fd = socket_init(r);

        if (fd == FD_INVALID) {
            //fprintf(stderr, "Failed to connect to renderer\n");
            return 0;
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Connected to renderer");
        }
    }

    bzero(&cmd, sizeof(cmd));

    cmd.ver = PROTO_VER;
    cmd.cmd = dirtyOnly ? cmdDirty : cmdRender;
    cmd.z = z;
    cmd.x = x;
    cmd.y = y;
    strcpy(cmd.path, filename);

    //fprintf(stderr, "Requesting tile(%d,%d,%d)\n", z,x,y);
    do {
        ret = send(fd, &cmd, sizeof(cmd), 0);

        if (ret == sizeof(cmd))
            break;

        if (errno != EPIPE)
            return 0;
 
         close(fd);
         fd = socket_init(r);
         if (fd == FD_INVALID)
             return 0;
         ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Reconnected to renderer");
    } while (retry--);

    if (!dirtyOnly) {
        struct timeval tv = { REQUEST_TIMEOUT, 0 };
        fd_set rx;
        int s;

        while (1) {
            FD_ZERO(&rx);
            FD_SET(fd, &rx);
            s = select(fd+1, &rx, NULL, NULL, &tv);
            if (s == 1) {
                bzero(&cmd, sizeof(cmd));
                ret = recv(fd, &cmd, sizeof(cmd), 0);
                if (ret != sizeof(cmd)) {
                    if (errno == EPIPE) {
                        close(fd);
                        fd = FD_INVALID;
                    }
                    //perror("recv error");
                    break;
                }
                //fprintf(stderr, "Completed tile(%d,%d,%d)\n", z,x,y);
                if (cmd.x == x && cmd.y == y && cmd.z == z) {
                    if (cmd.cmd == cmdDone)
                        return 1;
                    else
                        return 0;
                }
            } else if (s == 0) {
                break;
            } else {
                if (errno == EPIPE) {
                    close(fd);
                    fd = FD_INVALID;
                    break;
                }
            }
        }
    }
    return 0;
}

static int getPlanetTime(request_rec *r)
{
    static time_t last_check;
    static time_t planet_timestamp;
    time_t now = time(NULL);
    struct stat buf;

    // Only check for updates periodically
    if (now < last_check + 300)
        return planet_timestamp;

    last_check = now;
    if (stat(PLANET_TIMESTAMP, &buf)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Planet timestamp file " PLANET_TIMESTAMP " is missing");
        // Make something up
        planet_timestamp = now - 3 * 24 * 60 * 60;
    } else {
        if (buf.st_mtime != planet_timestamp) {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Planet file updated at %s", ctime(&buf.st_mtime));
            planet_timestamp = buf.st_mtime;
        }
    }
    return planet_timestamp;
}

enum tileState tile_state(request_rec *r, const char *filename)
{
    // FIXME: Apache already has most, if not all, this info recorded in r->fileinfo, use this instead!
    struct stat buf;

    if (stat(filename, &buf))
        return tileMissing; 

    if (buf.st_mtime < getPlanetTime(r))
        return tileOld;

    return tileCurrent;
}

static apr_status_t expires_filter(ap_filter_t *f, apr_bucket_brigade *b)
{
    request_rec *r = f->r;
    apr_time_t expires, holdoff, nextPlanet;
    apr_table_t *t = r->headers_out;
    enum tileState state = tile_state(r, r->filename);
    char *timestr;

    /* Append expiry headers ... */

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "expires(%s), uri(%s), filename(%s), path_info(%s)\n",
    //              r->handler, r->uri, r->filename, r->path_info);

    // current tiles will expire after next planet dump is due
    // or after 1 hour if the planet dump is late or tile is due for re-render
    nextPlanet = (state == tileCurrent) ? apr_time_from_sec(getPlanetTime(r) + PLANET_INTERVAL) : 0;
    holdoff = r->request_time + apr_time_from_sec(60 * 60);
    expires = MAX(holdoff, nextPlanet);

    apr_table_mergen(t, "Cache-Control",
                     apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                     apr_time_sec(expires - r->request_time)));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, expires);
    apr_table_setn(t, "Expires", timestr);

    ap_remove_output_filter(f);
    return ap_pass_brigade(f->next, b);
}


static int serve_tile(request_rec *r, const char *rel_path)
{
    // Redirect request to the tile
    r->method = apr_pstrdup(r->pool, "GET");
    r->method_number = M_GET;
    apr_table_unset(r->headers_in, "Content-Length");
    ap_internal_redirect_handler(rel_path, r);
    return OK;
}

static int serve_blank(request_rec *r)
{
    return serve_tile(r, IMG_PATH "/blank-000000.png");
}

int get_load_avg(request_rec *r)
{
    FILE *loadavg = fopen("/proc/loadavg", "r");
    int avg = 1000;

    if (!loadavg) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "failed to read /proc/loadavg");
        return 1000;
    }
    if (fscanf(loadavg, "%d", &avg) != 1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "failed to parse /proc/loadavg");
        fclose(loadavg);
        return 1000;
    }
    fclose(loadavg);

    return avg;
}

static int tile_dirty(request_rec *r, int x, int y, int z, const char *path)
{
    request_tile(r, x,y,z,path, 1);
    return OK;
}




static int get_tile(request_rec *r, int x, int y, int z, const char *abs_path, const char *rel_path)
{
    int avg = get_load_avg(r);
    enum tileState state;

    if (avg > MAX_LOAD_ANY) {
        // we're too busy to send anything now
        return error_message(r, "error: Load above MAX_LOAD_ANY threshold %d > %d", avg, MAX_LOAD_ANY);
    }

    state = tile_state(r, abs_path);

    switch (state) {
        case tileCurrent:
            return serve_tile(r, rel_path);
            break;
        case tileOld:
            if (avg > MAX_LOAD_OLD) {
               // Too much load to render it now, mark dirty but return old tile
               tile_dirty(r, x, y, z, abs_path);
               return serve_tile(r, rel_path);
            }
            break;
        case tileMissing:
            if (avg > MAX_LOAD_MISSING) {
               tile_dirty(r, x, y, z, abs_path);
               return error_message(r, "error: File missing and load above MAX_LOAD_MISSING threshold %d > %d", avg, MAX_LOAD_MISSING);
            }
            break;
    }

    if (request_tile(r, x,y,z,abs_path, 0))
        return serve_tile(r, rel_path);

    return error_message(r, "rendering failed for %s", rel_path);
}

static int tile_status(request_rec *r, int x, int y, int z, const char *path)
{
    // FIXME: Apache already has most, if not all, this info recorded in r->fileinfo, use this instead!
    struct stat buf;
    time_t now;
    int old;
    char MtimeStr[32]; // At least 26 according to man ctime_r
    char AtimeStr[32]; // At least 26 according to man ctime_r
    char *p;

    if (stat(path, &buf))
        return error_message(r, "Unable to find a tile at %s", path);

    now = time(NULL);
    old = (buf.st_mtime < now - MAX_AGE);

    MtimeStr[0] = '\0';
    ctime_r(&buf.st_mtime, MtimeStr);
    AtimeStr[0] = '\0';
    ctime_r(&buf.st_atime, AtimeStr);

    if ((p = strrchr(MtimeStr, '\n')))
        *p = '\0';
    if ((p = strrchr(AtimeStr, '\n')))
        *p = '\0';

    return error_message(r, "Tile is %s. Last rendered at %s. Last accessed at %s", old ? "due to be rendered" : "clean", MtimeStr, AtimeStr);
}

static const char *xyz_to_path(char *path, size_t len, int x, int y, int z)
{
#ifdef DIRECTORY_HASH
    // Directory hashing is optimised for z18 max (2^18 tiles split into 2 levels of 2^9)
    //snprintf(path, PATH_MAX, WWW_ROOT HASH_PATH "/%d/%03d/%03d/%03d/%03d.png", z,
    //                 x/512 x%512, y/512, y%512);

    // We attempt to cluseter the tiles so that a 16x16 square of tiles will be in a single directory
    // Hash stores our 40 bit result of mixing the 20 bits of the x & y co-ordinates
    // 4 bits of x & y are used per byte of output
    unsigned char i, hash[5];

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }
    snprintf(path, PATH_MAX, WWW_ROOT HASH_PATH "/%d/%u/%u/%u/%u/%u.png", z, hash[4], hash[3], hash[2], hash[1], hash[0]);
#else
    snprintf(path, PATH_MAX, WWW_ROOT TILE_PATH "/%d/%d/%d.png", z, x, y);
#endif
    return path + strlen(WWW_ROOT);
}


static int tile_handler(request_rec *r)
{
    int x, y, z, n, limit;
    char option[11];
    int oob;
    char abs_path[PATH_MAX];
    const char *rel_path;

    option[0] = '\0';

    if(strcmp(r->handler, "tile"))
        return DECLINED;

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "handler(%s), uri(%s), filename(%s), path_info(%s)",
    //              r->handler, r->uri, r->filename, r->path_info);

    /* URI = .../<z>/<x>/<y>.png[/option] */
    n = sscanf(r->uri, TILE_PATH "/%d/%d/%d.png/%10s", &z, &x, &y, option);
    if (n < 3) {
        //return error_message(r, "unable to process: %s", r->path_info);
        return DECLINED;
    }

    // Generate the tile filename
    rel_path = xyz_to_path(abs_path, sizeof(abs_path), x, y, z);
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "abs_path(%s), rel_path(%s)", abs_path, rel_path);

    // Validate tile co-ordinates
    oob = (z < 0 || z > MAX_ZOOM);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z) - 1;
        oob =  (x < 0 || x > limit || y < 0 || y > limit);
    }

    if (n == 3) {
        ap_add_output_filter("MOD_TILE", NULL, r, r->connection);
        return oob ? serve_blank(r) : get_tile(r, x, y, z, abs_path, rel_path);
    }

    if (n == 4) {
        if (oob)
            return error_message(r, "The tile co-ordinates that you specified are invalid");
        if (!strcmp(option, "status"))
            return tile_status(r, x, y, z, abs_path);
        if (!strcmp(option, "dirty"))
            return tile_dirty(r, x, y, z, abs_path);
        return error_message(r, "Unknown option");
    }
    return DECLINED;
}

static void register_hooks(__attribute__((unused)) apr_pool_t *p)
{
    ap_register_output_filter("MOD_TILE", expires_filter, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler, NULL, NULL, APR_HOOK_FIRST);
}

module AP_MODULE_DECLARE_DATA tile_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,           /* dir config creater */
    NULL,           /* dir merger --- default is to override */
    NULL,           /* server config */
    NULL,           /* merge server config */
    NULL,           /* command apr_table_t */
    register_hooks  /* register hooks */
};
