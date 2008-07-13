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
#include "util_md5.h"

module AP_MODULE_DECLARE_DATA tile_module;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
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
#include "store.h"
#include "dir_utils.h"

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

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void pfd_free(void *ptr)
{
    int *pfd = ptr;

    if (*pfd != FD_INVALID)
        close(*pfd);
    free(pfd);
}

static void make_key(void)
{
    (void) pthread_key_create(&key, pfd_free);
}


int request_tile(request_rec *r, int dirtyOnly)
{
    struct protocol cmd;
    int *pfd;
    int ret = 0;
    int retry = 1;
    int x, y, z, n, limit, oob;

    /* URI = .../<z>/<x>/<y>.png[/option] */
    n = sscanf(r->uri, TILE_PATH "/%d/%d/%d", &z, &x, &y);
    if (n != 3)
        return 0;

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "z(%d) x(%d) y(%d)", z, x, y);

    // Validate tile co-ordinates
    oob = (z < 0 || z > MAX_ZOOM);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z) - 1;
        oob =  (x < 0 || x > limit || y < 0 || y > limit);
    }

    if (oob)
        return 0;

    (void) pthread_once(&key_once, make_key);
    if ((pfd = pthread_getspecific(key)) == NULL) {
        pfd = malloc(sizeof(*pfd));
        if (!pfd)
            return 0;
        *pfd = FD_INVALID;
        (void) pthread_setspecific(key, pfd);
    }

    if (*pfd == FD_INVALID) {
        *pfd = socket_init(r);

        if (*pfd == FD_INVALID) {
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

    //fprintf(stderr, "Requesting tile(%d,%d,%d)\n", z,x,y);
    do {
        ret = send(*pfd, &cmd, sizeof(cmd), 0);

        if (ret == sizeof(cmd))
            break;

        if (errno != EPIPE)
            return 0;

         close(*pfd);
         *pfd = socket_init(r);
         if (*pfd == FD_INVALID)
             return 0;
         ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Reconnected to renderer");
    } while (retry--);

    if (!dirtyOnly) {
        struct timeval tv = { REQUEST_TIMEOUT, 0 };
        fd_set rx;
        int s;

        while (1) {
            FD_ZERO(&rx);
            FD_SET(*pfd, &rx);
            s = select((*pfd)+1, &rx, NULL, NULL, &tv);
            if (s == 1) {
                bzero(&cmd, sizeof(cmd));
                ret = recv(*pfd, &cmd, sizeof(cmd), 0);
                if (ret != sizeof(cmd)) {
                    if (errno == EPIPE) {
                        close(*pfd);
                        *pfd = FD_INVALID;
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
                    close(*pfd);
                    *pfd = FD_INVALID;
                    break;
                }
            }
        }
    }
    return 0;
}



static apr_time_t getPlanetTime(request_rec *r)
{
    static apr_time_t last_check;
    static apr_time_t planet_timestamp;
    static pthread_mutex_t planet_lock = PTHREAD_MUTEX_INITIALIZER;
    apr_time_t now = r->request_time;
    struct apr_finfo_t s;

    pthread_mutex_lock(&planet_lock);
    // Only check for updates periodically
    if (now < last_check + apr_time_from_sec(300)) {
        pthread_mutex_unlock(&planet_lock);
        return planet_timestamp;
    }

    last_check = now;
    if (apr_stat(&s, PLANET_TIMESTAMP, APR_FINFO_MIN, r->pool) != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Planet timestamp file " PLANET_TIMESTAMP " is missing");
        // Make something up
        planet_timestamp = now - apr_time_from_sec(3 * 24 * 60 * 60);
    } else {
        if (s.mtime != planet_timestamp) {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Planet file updated");
            planet_timestamp = s.mtime;
        }
    }
    pthread_mutex_unlock(&planet_lock);
    return planet_timestamp;
}

static enum tileState tile_state_once(request_rec *r)
{
    apr_status_t rv;
    apr_finfo_t *finfo = &r->finfo;

    if (!(finfo->valid & APR_FINFO_MTIME)) {
        rv = apr_stat(finfo, r->filename, APR_FINFO_MIN, r->pool);
        if (rv != APR_SUCCESS)
            return tileMissing;
    }

    if (finfo->mtime < getPlanetTime(r))
        return tileOld;

    return tileCurrent;
}

static enum tileState tile_state(request_rec *r)
{
    enum tileState state = tile_state_once(r);
#ifdef METATILE
    if (state == tileMissing) {
        // Try fallback to plain PNG
        char path[PATH_MAX];
        int x, y, z, n;
        /* URI = .../<z>/<x>/<y>.png[/option] */
        n = sscanf(r->uri, TILE_PATH "/%d/%d/%d", &z, &x, &y);
        if (n == 3) {
            xyz_to_path(path, sizeof(path), x,y,z);
            r->filename = apr_pstrdup(r->pool, path);
            state = tile_state_once(r);
            //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "png fallback %d/%d/%d",x,y,z);

            if (state == tileMissing) {
                // PNG not available either, if it gets rendered, it'll now be a .meta
                xyz_to_meta(path, sizeof(path), x,y,z);
                r->filename = apr_pstrdup(r->pool, path);
            }
        }
    }
#endif
    return state;
}

static void add_expiry(request_rec *r)
{
    apr_time_t expires, holdoff, nextPlanet;
    apr_table_t *t = r->headers_out;
    enum tileState state = tile_state(r);
    char *timestr;

    /* Append expiry headers ... */

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "expires(%s), uri(%s), filename(%s), path_info(%s)\n",
    //              r->handler, r->uri, r->filename, r->path_info);

    // current tiles will expire after next planet dump is due
    // or after 1 hour if the planet dump is late or tile is due for re-render
    nextPlanet = (state == tileCurrent) ? (getPlanetTime(r) + apr_time_from_sec(PLANET_INTERVAL)) : 0;
    holdoff = r->request_time + apr_time_from_sec(60 * 60);
    expires = MAX(holdoff, nextPlanet);

    apr_table_mergen(t, "Cache-Control",
                     apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                     apr_time_sec(expires - r->request_time)));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, expires);
    apr_table_setn(t, "Expires", timestr);
}


double get_load_avg(request_rec *r)
{
    double loadavg[1];
    int n = getloadavg(loadavg, 1);

    if (n < 1)
        return 1000;
    else
        return loadavg[0];
}

static int tile_handler_dirty(request_rec *r)
{
    if(strcmp(r->handler, "tile_dirty"))
        return DECLINED;

    request_tile(r, 1);
    return error_message(r, "Tile submitted for rendering\n");
}



static int tile_storage_hook(request_rec *r)
{
    int avg;
    enum tileState state;

    if (!r->handler)
        return DECLINED;

    // Any status request is OK
    if (!strcmp(r->handler, "tile_status"))
        return OK;

    if (strcmp(r->handler, "tile_serve") && strcmp(r->handler, "tile_dirty"))
        return DECLINED;

    avg = get_load_avg(r);
    state = tile_state(r);

    switch (state) {
        case tileCurrent:
            return OK;
            break;
        case tileOld:
            if (avg > MAX_LOAD_OLD) {
               // Too much load to render it now, mark dirty but return old tile
               request_tile(r, 1);
               return OK;
            }
            break;
        case tileMissing:
            if (avg > MAX_LOAD_MISSING) {
               request_tile(r, 1);
               return HTTP_NOT_FOUND;
            }
            break;
    }

    if (request_tile(r, 0)) {
        // Need to update fileinfo for new rendered tile
        apr_stat(&r->finfo, r->filename, APR_FINFO_MIN, r->pool);
        return OK;
    }

    if (state == tileOld)
        return OK;

    return HTTP_NOT_FOUND;
}

static int tile_handler_status(request_rec *r)
{
    enum tileState state;
    char time_str[APR_CTIME_LEN];

    if(strcmp(r->handler, "tile_status"))
        return DECLINED;

    state = tile_state(r);
    if (state == tileMissing)
        return error_message(r, "Unable to find a tile at %s\n", r->filename);
    apr_ctime(time_str, r->finfo.mtime);

    return error_message(r, "Tile is %s. Last rendered at %s\n", (state == tileOld) ? "due to be rendered" : "clean", time_str);
}

static int tile_translate(request_rec *r)
{
    int x, y, z, n, limit;
    char option[11];
    int oob;
    char abs_path[PATH_MAX];

    option[0] = '\0';

    /* URI = .../<z>/<x>/<y>.png[/option] */
    n = sscanf(r->uri, TILE_PATH "/%d/%d/%d.png/%10s", &z, &x, &y, option);
    /* The original rewrite config matched anything that ended with 3 numbers */
    //if (n < 3)
    //        n = sscanf(r->uri, TILE_PATH "%*[^0-9]%d/%d/%d.png/%10s", &z, &x, &y, option);

    if (n < 3)
        return DECLINED;

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "z(%d) x(%d) y(%d)", z, x, y);

    // Validate tile co-ordinates
    oob = (z < 0 || z > MAX_ZOOM);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z) - 1;
        oob =  (x < 0 || x > limit || y < 0 || y > limit);
    }

    if (oob) {
        sleep(CLIENT_PENALTY);
        return HTTP_NOT_FOUND;
    }


    // Generate the tile filename
#ifdef METATILE
    xyz_to_meta(abs_path, sizeof(abs_path), x, y, z);
#else
    xyz_to_path(abs_path, sizeof(abs_path), x, y, z);
#endif
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "abs_path(%s), rel_path(%s)", abs_path, rel_path);
    r->filename = apr_pstrdup(r->pool, abs_path);

    if (n == 4) {
        if (!strcmp(option, "status"))
            r->handler = "tile_status";
        else if (!strcmp(option, "dirty"))
            r->handler = "tile_dirty";
        else
            return DECLINED;
    } else 
        r->handler = "tile_serve";

    return OK;
}



static int tile_handler_serve(request_rec *r)
{
    int x, y, z, n, limit, oob;
    unsigned char *buf;
    int len;
    const int tile_max = 1024 * 1024;
    apr_status_t errstatus;

    if(strcmp(r->handler, "tile_serve"))
        return DECLINED;

    /* URI = .../<z>/<x>/<y>.png[/option] */
    n = sscanf(r->uri, TILE_PATH "/%d/%d/%d", &z, &x, &y);
    if (n != 3)
        return 0;

    // Validate tile co-ordinates
    oob = (z < 0 || z > MAX_ZOOM);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z) - 1;
        oob =  (x < 0 || x > limit || y < 0 || y > limit);
    }

    if (oob) {
        sleep(CLIENT_PENALTY);
        return HTTP_NOT_FOUND;
    }

    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "serve handler(%s), uri(%s), filename(%s), path_info(%s)",
    //              r->handler, r->uri, r->filename, r->path_info);

    // FIXME: It is a waste to do the malloc + read if we are fulfilling a HEAD or returning a 304.
    buf = malloc(tile_max);
    if (!buf)
        return HTTP_INTERNAL_SERVER_ERROR;

    len = tile_read(x, y, z, buf, tile_max);
    if (len > 0) {
#if 0
        // Set default Last-Modified and Etag headers
        ap_update_mtime(r, r->finfo.mtime);
        ap_set_last_modified(r);
        ap_set_etag(r);
#else
        // Use MD5 hash as only cache attribute.
        // If a tile is re-rendered and produces the same output
        // then we can continue to use the previous cached copy
        char *md5 = ap_md5_binary(r->pool, buf, len);
        apr_table_setn(r->headers_out, "ETag",
                        apr_psprintf(r->pool, "\"%s\"", md5));
#endif
        ap_set_content_type(r, "image/png");
        ap_set_content_length(r, len);
        add_expiry(r);
        if ((errstatus = ap_meets_conditions(r)) != OK) {
            free(buf);
            return errstatus;
        } else {
            ap_rwrite(buf, len, r);
            free(buf);
            return OK;
        }
    }
    free(buf);
    //ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "len = %d", len);

    return DECLINED;
}



static void register_hooks(__attribute__((unused)) apr_pool_t *p)
{
    ap_hook_handler(tile_handler_serve, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_dirty, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_status, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_translate_name(tile_translate, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_map_to_storage(tile_storage_hook, NULL, NULL, APR_HOOK_FIRST);
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
