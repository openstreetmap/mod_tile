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

#define INILINE_MAX 256
typedef struct {
    char xmlname[XMLCONFIG_MAX];
    char baseuri[PATH_MAX];
    int mincachetime[MAX_ZOOM];
    int minzoom;
    int maxzoom;
} tile_config_rec;

typedef struct {
    apr_array_header_t *configs;
    int request_timeout;
    int max_load_old;
    int max_load_missing;
    int cache_duration_dirty;
    int cache_duration_max;
    int cache_duration_minimum;
    int cache_duration_low_zoom;
    int cache_level_low_zoom;
    int cache_duration_medium_zoom;
    int cache_level_medium_zoom;
    double cache_duration_last_modified_factor;
    char renderd_socket_name[PATH_MAX];
    char tile_dir[PATH_MAX];
} tile_server_conf;

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
    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    int fd;
    struct sockaddr_un addr;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "failed to create unix socket");
        return FD_INVALID;
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, scfg->renderd_socket_name, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "socket connect failed for: %s", scfg->renderd_socket_name);
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

int request_tile(request_rec *r, struct protocol *cmd, int dirtyOnly)
{
    int *pfd;
    int ret = 0;
    int retry = 1;
    struct protocol resp;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

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
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Failed to connected to renderer");
            return 0;
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Connected to renderer");
        }
    }

    // cmd has already been partial filled, fill in the rest
    cmd->ver = PROTO_VER;
    cmd->cmd = dirtyOnly ? cmdDirty : cmdRender;

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Requesting xml(%s) z(%d) x(%d) y(%d)", cmd->xmlname, cmd->z, cmd->x, cmd->y);
    do {
        ret = send(*pfd, cmd, sizeof(struct protocol), 0);

        if (ret == sizeof(struct protocol))
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
        struct timeval tv = {scfg->request_timeout, 0 };
        fd_set rx;
        int s;

        while (1) {
            FD_ZERO(&rx);
            FD_SET(*pfd, &rx);
            s = select((*pfd)+1, &rx, NULL, NULL, &tv);
            if (s == 1) {
                bzero(&resp, sizeof(struct protocol));
                ret = recv(*pfd, &resp, sizeof(struct protocol), 0);
                if (ret != sizeof(struct protocol)) {
                    if (errno == EPIPE) {
                        close(*pfd);
                        *pfd = FD_INVALID;
                    }
                    //perror("recv error");
                    break;
                }

                if (cmd->x == resp.x && cmd->y == resp.y && cmd->z == resp.z && !strcmp(cmd->xmlname, resp.xmlname)) {
                    if (resp.cmd == cmdDone)
                        return 1;
                    else
                        return 0;
                } else {
                    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                       "Response does not match request: xml(%s,%s) z(%d,%d) x(%d,%d) y(%d,%d)", cmd->xmlname,
                       resp.xmlname, cmd->z, resp.z, cmd->x, resp.x, cmd->y, resp.y);
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

static enum tileState tile_state(request_rec *r, struct protocol *cmd)
{
    enum tileState state = tile_state_once(r);
#ifdef METATILEFALLBACK
    if (state == tileMissing) {
        ap_conf_vector_t *sconf = r->server->module_config;
        tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

        // Try fallback to plain PNG
        char path[PATH_MAX];
        xyz_to_path(path, sizeof(path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
        r->filename = apr_pstrdup(r->pool, path);
        state = tile_state_once(r);
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "png fallback %d/%d/%d",x,y,z);

        if (state == tileMissing) {
            // PNG not available either, if it gets rendered, it'll now be a .meta
            xyz_to_meta(path, sizeof(path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
            r->filename = apr_pstrdup(r->pool, path);
        }
    }
#endif
    return state;
}

static void add_expiry(request_rec *r, struct protocol * cmd)
{
    apr_time_t holdoff;
    apr_table_t *t = r->headers_out;
    enum tileState state = tile_state(r, cmd);
    apr_finfo_t *finfo = &r->finfo;
    char *timestr;
    long int planetTimestamp, maxAge, minCache, lastModified;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "expires(%s), uri(%s), filename(%s), path_info(%s)\n",
                  r->handler, r->uri, r->filename, r->path_info);

    /* Test if the tile we are serving is out of date, then set a low maxAge*/
    if (state == tileOld) {
        holdoff = (scfg->cache_duration_dirty /2) * (rand() / (RAND_MAX + 1.0));
        maxAge = scfg->cache_duration_dirty + holdoff;
    } else {
        // cache heuristic based on zoom level
        minCache = tile_configs->mincachetime[cmd->z];
        // Time to the next known complete rerender
        planetTimestamp = apr_time_sec( + getPlanetTime(r) + apr_time_from_sec(PLANET_INTERVAL) - r->request_time) ;
        // Time since the last render of this tile
        lastModified = (int)(((double)apr_time_sec(r->request_time - finfo->mtime)) * scfg->cache_duration_last_modified_factor);
        // Add a random jitter of 3 hours to space out cache expiry
        holdoff = (3 * 60 * 60) * (rand() / (RAND_MAX + 1.0));

        maxAge = MAX(minCache, planetTimestamp);
        maxAge = MAX(maxAge,lastModified);
        maxAge += holdoff;

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                        "caching heuristics: next planet render %ld; zoom level based %ld; last modified %ld\n",
                        planetTimestamp, minCache, lastModified);
    }

    maxAge = MIN(maxAge, scfg->cache_duration_max);

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Setting tiles maxAge to %ld\n", maxAge);

    apr_table_mergen(t, "Cache-Control",
                     apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                     maxAge));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, (apr_time_from_sec(maxAge) + r->request_time));
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

    struct protocol * cmd = (struct protocol *)ap_get_module_config(r->request_config, &tile_module);
    if (cmd == NULL)
        return DECLINED;

    request_tile(r, cmd, 1);
    return error_message(r, "Tile submitted for rendering\n");
}

static int tile_storage_hook(request_rec *r)
{
//    char abs_path[PATH_MAX];
    int avg;
    enum tileState state;

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_storage_hook: handler(%s), uri(%s), filename(%s), path_info(%s)",
                  r->handler, r->uri, r->filename, r->path_info);

    if (!r->handler)
        return DECLINED;

    // Any status request is OK
    if (!strcmp(r->handler, "tile_status"))
        return OK;

    if (strcmp(r->handler, "tile_serve") && strcmp(r->handler, "tile_dirty"))
        return DECLINED;

    struct protocol * cmd = (struct protocol *)ap_get_module_config(r->request_config, &tile_module);
    if (cmd == NULL)
        return DECLINED;
/*
should already be done
    // Generate the tile filename
#ifdef METATILE
    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    xyz_to_meta(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#else
    xyz_to_path(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#endif
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "abs_path(%s)", abs_path);
    r->filename = apr_pstrdup(r->pool, abs_path);
*/
    avg = get_load_avg(r);
    state = tile_state(r, cmd);

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    switch (state) {
        case tileCurrent:
            return OK;
            break;
        case tileOld:
            if (avg > scfg->max_load_old) {
               // Too much load to render it now, mark dirty but return old tile
               request_tile(r, cmd, 1);
               ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Load larger max_load_old (%d). Mark dirty and deliver from cache.", scfg->max_load_old);
               return OK;
            }
            break;
        case tileMissing:
            if (avg > scfg->max_load_missing) {
               request_tile(r, cmd, 1);
               ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Load larger max_load_missing (%d). Return HTTP_NOT_FOUND.", scfg->max_load_missing);
               return HTTP_NOT_FOUND;
            }
            break;
    }

    if (request_tile(r, cmd, 0)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Update file info abs_path(%s)", r->filename);
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

    struct protocol * cmd = (struct protocol *)ap_get_module_config(r->request_config, &tile_module);
    if (cmd == NULL){
        sleep(CLIENT_PENALTY);
        return HTTP_NOT_FOUND;
    }

    state = tile_state(r, cmd);
    if (state == tileMissing)
        return error_message(r, "Unable to find a tile at %s\n", r->filename);
    apr_ctime(time_str, r->finfo.mtime);

    return error_message(r, "Tile is %s. Last rendered at %s\n", (state == tileOld) ? "due to be rendered" : "clean", time_str);
}

static int tile_handler_serve(request_rec *r)
{
    const int tile_max = MAX_SIZE;
    unsigned char *buf;
    int len;
    apr_status_t errstatus;

    if(strcmp(r->handler, "tile_serve"))
        return DECLINED;

    struct protocol * cmd = (struct protocol *)ap_get_module_config(r->request_config, &tile_module);
    if (cmd == NULL){
        sleep(CLIENT_PENALTY);
        return HTTP_NOT_FOUND;
    }

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_handler_serve: xml(%s) z(%d) x(%d) y(%d)", cmd->xmlname, cmd->z, cmd->x, cmd->y);

    // FIXME: It is a waste to do the malloc + read if we are fulfilling a HEAD or returning a 304.
    buf = malloc(tile_max);
    if (!buf)
        return HTTP_INTERNAL_SERVER_ERROR;

    len = tile_read(cmd->xmlname, cmd->x, cmd->y, cmd->z, buf, tile_max);
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
        add_expiry(r, cmd);
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

static int tile_translate(request_rec *r)
{
    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_translate: uri(%s)", r->uri);

    int i,n,limit,oob;
    char option[11];

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;
    for (i = 0; i < scfg->configs->nelts; ++i) {
        tile_config_rec *tile_config = &tile_configs[i];

        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_translate: baseuri(%s) name(%s)", tile_config->baseuri, tile_config->xmlname);

        if (!strncmp(tile_config->baseuri, r->uri, strlen(tile_config->baseuri))) {

            struct protocol * cmd = (struct protocol *) apr_pcalloc(r->pool, sizeof(struct protocol));
            bzero(cmd, sizeof(struct protocol));

            n = sscanf(r->uri+strlen(tile_config->baseuri), "%d/%d/%d.png/%10s", &(cmd->z), &(cmd->x), &(cmd->y), option);
            if (n < 3) return DECLINED;

            oob = (cmd->z < 0 || cmd->z > MAX_ZOOM);
            if (!oob) {
                 // valid x/y for tiles are 0 ... 2^zoom-1
                 limit = (1 << cmd->z) - 1;
                 oob =  (cmd->x < 0 || cmd->x > limit || cmd->y < 0 || cmd->y > limit);
            }

            if (oob) {
                sleep(CLIENT_PENALTY);
                return HTTP_NOT_FOUND;
            }

            strcpy(cmd->xmlname, tile_config->xmlname);

            // Store a copy for later
            ap_set_module_config(r->request_config, &tile_module, cmd);

            // Generate the tile filename?
            char abs_path[PATH_MAX];
#ifdef METATILE
            xyz_to_meta(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#else
            xyz_to_path(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#endif
            r->filename = apr_pstrdup(r->pool, abs_path);

            if (n == 4) {
                if (!strcmp(option, "status")) r->handler = "tile_status";
                else if (!strcmp(option, "dirty")) r->handler = "tile_dirty";
                else return DECLINED;
            } else {
                r->handler = "tile_serve";
            }

            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_translate: op(%s) xml(%s) z(%d) x(%d) y(%d)", r->handler , cmd->xmlname, cmd->z, cmd->x, cmd->y);

            return OK;
        }
    }
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

static const char *_add_tile_config(cmd_parms *cmd, void *mconfig, const char *baseuri, const char *name, int minzoom, int maxzoom)
{
    int i;
    if (strlen(name) == 0) {
        return "ConfigName value must not be null";
    }

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    tile_config_rec *tilecfg = apr_array_push(scfg->configs);

    strncpy(tilecfg->baseuri, baseuri, PATH_MAX-1);
    tilecfg->baseuri[PATH_MAX-1] = 0;
    strncpy(tilecfg->xmlname, name, XMLCONFIG_MAX-1);
    tilecfg->xmlname[XMLCONFIG_MAX-1] = 0;
    tilecfg->minzoom = minzoom;
    tilecfg->maxzoom = maxzoom;
    for (i = tilecfg->minzoom = minzoom; i < tilecfg->maxzoom; i++) {
             if (i < scfg->cache_level_low_zoom) {
                 tilecfg->mincachetime[i] = scfg->cache_duration_low_zoom;
             } else if (i < scfg->cache_level_medium_zoom) {
                 tilecfg->mincachetime[i] = scfg->cache_duration_medium_zoom;
             } else {
                 tilecfg->mincachetime[i] = scfg->cache_duration_minimum;
             }
         }


    return NULL;
}

static const char *add_tile_config(cmd_parms *cmd, void *mconfig, const char *baseuri, const char *name)
{
    return _add_tile_config(cmd, mconfig, baseuri, name, 0, MAX_ZOOM);
}

static const char *load_tile_config(cmd_parms *cmd, void *mconfig, const char *conffile)
{
    FILE * hini ;
    char filename[PATH_MAX];
    char xmlname[XMLCONFIG_MAX];
    char line[INILINE_MAX];
    char key[INILINE_MAX];
    char value[INILINE_MAX];
    const char * result;

    if (strlen(conffile) == 0) {
        strcpy(filename, RENDERD_CONFIG);
    } else {
        strcpy(filename, conffile);
    }

    // Load the config
    if ((hini=fopen(filename, "r"))==NULL) {
        return "Unable to open config file";
    }

    while (fgets(line, INILINE_MAX, hini)!=NULL) {
        if (line[0] == '#') continue;
        if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
        if (line[0] == '[') {
            if (strlen(line) >= XMLCONFIG_MAX){
                return "XML name too long";
            }
            sscanf(line, "[%[^]]", xmlname);
        } else if (sscanf(line, "%[^=]=%[^;#]", key, value) == 2
               ||  sscanf(line, "%[^=]=\"%[^\"]\"", key, value) == 2) {

            if (!strcmp(key, "URI")){
                if (strlen(value) >= PATH_MAX){
                    return "URI too long";
                }
                result = add_tile_config(cmd, mconfig, value, xmlname);
                if (result != NULL) return result;
            }
        }
    }
    fclose(hini);
    return NULL;
}

static const char *mod_tile_request_timeout_config(cmd_parms *cmd, void *mconfig, const char *request_timeout_string)
{
    int request_timeout;

    if (sscanf(request_timeout_string, "%d", &request_timeout) != 1) {
        return "ModTileRequestTimeout needs integer argument";
    }

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->request_timeout = request_timeout;
    return NULL;
}

static const char *mod_tile_max_load_old_config(cmd_parms *cmd, void *mconfig, const char *max_load_old_string)
{
    int max_load_old;

    if (sscanf(max_load_old_string, "%d", &max_load_old) != 1) {
        return "ModTileMaxLoadOld needs integer argument";
    }

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->max_load_old = max_load_old;
    return NULL;
}

static const char *mod_tile_max_load_missing_config(cmd_parms *cmd, void *mconfig, const char *max_load_missing_string)
{
    int max_load_missing;

    if (sscanf(max_load_missing_string, "%d", &max_load_missing) != 1) {
        return "ModTileMaxLoadMissing needs integer argument";
    }

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->max_load_missing = max_load_missing;
    return NULL;
}

static const char *mod_tile_renderd_socket_name_config(cmd_parms *cmd, void *mconfig, const char *renderd_socket_name_string)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    strncpy(scfg->renderd_socket_name, renderd_socket_name_string, PATH_MAX-1);
    scfg->renderd_socket_name[PATH_MAX-1] = 0;
    return NULL;
}

static const char *mod_tile_tile_dir_config(cmd_parms *cmd, void *mconfig, const char *tile_dir_string)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    strncpy(scfg->tile_dir, tile_dir_string, PATH_MAX-1);
    scfg->tile_dir[PATH_MAX-1] = 0;
    return NULL;
}

static const char *mod_tile_cache_lastmod_factor_config(cmd_parms *cmd, void *mconfig, const char *modified_factor_string)
{
    float modified_factor;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config,
            &tile_module);
    if (sscanf(modified_factor_string, "%f", &modified_factor) != 1) {
        return "ModTileCacheLastModifiedFactor needs float argument";
    }
    scfg->cache_duration_last_modified_factor = modified_factor;
    return NULL;
}

static const char *mod_tile_cache_duration_max_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_string)
{
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config,
            &tile_module);
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
        return "ModTileCacheDurationMax needs integer argument";
    }
    scfg->cache_duration_max = cache_duration;
    return NULL;
}

static const char *mod_tile_cache_duration_dirty_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_string)
{
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config,
            &tile_module);
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
        return "ModTileCacheDurationDirty needs integer argument";
    }
    scfg->cache_duration_dirty = cache_duration;
    return NULL;
}

static const char *mod_tile_cache_duration_minimum_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_string)
{
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config,
            &tile_module);
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
        return "ModTileCacheDurationMinimum needs integer argument";
    }
    scfg->cache_duration_minimum = cache_duration;
    return NULL;
}

static const char *mod_tile_cache_duration_low_config(cmd_parms *cmd, void *mconfig, const char *zoom_level_string, const char *cache_duration_string)
{
    int zoom_level;
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    if (sscanf(zoom_level_string, "%d", &zoom_level) != 1) {
            return "ModTileCacheDurationLowZoom needs integer argument";
    }
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
            return "ModTileCacheDurationLowZoom needs integer argument";
    }
    scfg->cache_level_low_zoom = zoom_level;
    scfg->cache_duration_low_zoom = cache_duration;

    return NULL;
}
static const char *mod_tile_cache_duration_medium_config(cmd_parms *cmd, void *mconfig, const char *zoom_level_string, const char *cache_duration_string)
{
    int zoom_level;
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    if (sscanf(zoom_level_string, "%d", &zoom_level) != 1) {
            return "ModTileCacheDurationMediumZoom needs integer argument";
    }
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
            return "ModTileCacheDurationMediumZoom needs integer argument";
    }
    scfg->cache_level_medium_zoom = zoom_level;
    scfg->cache_duration_medium_zoom = cache_duration;

    return NULL;
}


static void *create_tile_config(apr_pool_t *p, server_rec *s)
{
    tile_server_conf * scfg = (tile_server_conf *) apr_pcalloc(p, sizeof(tile_server_conf));

    scfg->configs = apr_array_make(p, 4, sizeof(tile_config_rec));
    scfg->request_timeout = REQUEST_TIMEOUT;
    scfg->max_load_old = MAX_LOAD_OLD;
    scfg->max_load_missing = MAX_LOAD_MISSING;
    strncpy(scfg->renderd_socket_name, RENDER_SOCKET, PATH_MAX-1);
    scfg->renderd_socket_name[PATH_MAX-1] = 0;
    strncpy(scfg->tile_dir, HASH_PATH, PATH_MAX-1);
    scfg->tile_dir[PATH_MAX-1] = 0;
    scfg->cache_duration_dirty = 15*60;
    scfg->cache_duration_last_modified_factor = 0.0;
    scfg->cache_duration_max = 7*24*60*60;
    scfg->cache_duration_minimum = 3*60*60;
    scfg->cache_duration_low_zoom = 6*24*60*60;
    scfg->cache_duration_medium_zoom = 1*24*60*60;
    scfg->cache_level_low_zoom = 0;
    scfg->cache_level_medium_zoom = 0;

    return scfg;
}

static void *merge_tile_config(apr_pool_t *p, void *basev, void *overridesv)
{
    tile_server_conf * scfg = (tile_server_conf *) apr_pcalloc(p, sizeof(tile_server_conf));
    tile_server_conf * scfg_base = (tile_server_conf *) basev;
    tile_server_conf * scfg_over = (tile_server_conf *) overridesv;

    scfg->configs = apr_array_append(p, scfg_base->configs, scfg_over->configs);
    scfg->request_timeout = scfg_over->request_timeout;
    scfg->max_load_old = scfg_over->max_load_old;
    scfg->max_load_missing = scfg_over->max_load_missing;
    strncpy(scfg->renderd_socket_name, scfg_over->renderd_socket_name, PATH_MAX-1);
    scfg->renderd_socket_name[PATH_MAX-1] = 0;
    strncpy(scfg->tile_dir, scfg_over->tile_dir, PATH_MAX-1);
    scfg->tile_dir[PATH_MAX-1] = 0;
    scfg->cache_duration_dirty = scfg_over->cache_duration_dirty;
    scfg->cache_duration_last_modified_factor = scfg_over->cache_duration_last_modified_factor;
    scfg->cache_duration_max = scfg_over->cache_duration_max;
    scfg->cache_duration_minimum = scfg_over->cache_duration_minimum;
    scfg->cache_duration_low_zoom = scfg_over->cache_duration_low_zoom;
    scfg->cache_duration_medium_zoom = scfg_over->cache_duration_medium_zoom;
    scfg->cache_level_low_zoom = scfg_over->cache_level_low_zoom;
    scfg->cache_level_medium_zoom = scfg_over->cache_level_medium_zoom;

    return scfg;
}

static const command_rec tile_cmds[] =
{
    AP_INIT_TAKE1(
        "LoadTileConfigFile",            /* directive name */
        load_tile_config,                /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "load an entire renderd config file"  /* directive description */
    ),
    AP_INIT_TAKE2(
        "AddTileConfig",                 /* directive name */
        add_tile_config,                 /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "path and name of renderd config to use"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileRequestTimeout",         /* directive name */
        mod_tile_request_timeout_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set timeout in seconds on mod_tile requests"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileMaxLoadOld",             /* directive name */
        mod_tile_max_load_old_config,    /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set max load for rendering old tiles"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileMaxLoadMissing",         /* directive name */
        mod_tile_max_load_missing_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set max load for rendering missing tiles"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileRenderdSocketName",      /* directive name */
        mod_tile_renderd_socket_name_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set name of unix domain socket for connecting to rendering daemon"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileTileDir",                /* directive name */
        mod_tile_tile_dir_config,        /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set name of tile cache directory"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileCacheDurationMax",                /* directive name */
        mod_tile_cache_duration_max_config,        /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the maximum cache expiry in seconds"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileCacheDurationDirty",                    /* directive name */
        mod_tile_cache_duration_dirty_config,           /* config action routine */
        NULL,                                           /* argument to include in call */
        OR_OPTIONS,                                     /* where available */
        "Set the cache expiry for serving dirty tiles"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileCacheDurationMinimum",          /* directive name */
        mod_tile_cache_duration_minimum_config, /* config action routine */
        NULL,                                   /* argument to include in call */
        OR_OPTIONS,                             /* where available */
        "Set the minimum cache expiry"          /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileCacheLastModifiedFactor",       /* directive name */
        mod_tile_cache_lastmod_factor_config,   /* config action routine */
        NULL,                                   /* argument to include in call */
        OR_OPTIONS,                             /* where available */
        "Set the factor by which the last modified determins cache expiry" /* directive description */
    ),
    AP_INIT_TAKE2(
        "ModTileCacheDurationLowZoom",       /* directive name */
        mod_tile_cache_duration_low_config,                 /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the minimum cache duration and zoom level for low zoom tiles"  /* directive description */
    ),
    AP_INIT_TAKE2(
        "ModTileCacheDurationMediumZoom",       /* directive name */
        mod_tile_cache_duration_medium_config,                 /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the minimum cache duration and zoom level for medium zoom tiles"  /* directive description */
    ),
    {NULL}
};

module AP_MODULE_DECLARE_DATA tile_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,                                /* dir config creater */
    NULL,                                /* dir merger --- default is to override */
    create_tile_config,                  /* server config */
    merge_tile_config,                   /* merge server config */
    tile_cmds,                           /* command apr_table_t */
    register_hooks                       /* register hooks */
};

