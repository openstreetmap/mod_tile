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
#include <strings.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include "gen_tile.h"
#include "protocol.h"
#include "render_config.h"
#include "store.h"
#include "dir_utils.h"
#include "mod_tile.h"
#include "sys_utils.h"


#if !defined(OS2) && !defined(WIN32) && !defined(BEOS) && !defined(NETWARE)
#include "unixd.h"
#define MOD_TILE_SET_MUTEX_PERMS /* XXX Apache should define something */
#endif

apr_time_t *last_check = 0;
apr_time_t *planet_timestamp = 0;

apr_shm_t *stats_shm;
apr_shm_t *delaypool_shm;
char *shmfilename;
char *shmfilename_delaypool;
apr_global_mutex_t *stats_mutex;
apr_global_mutex_t *delay_mutex;
char *mutexfilename;
int layerCount = 0;
int global_max_zoom = 0;

static int error_message(request_rec *r, const char *format, ...)
                 __attribute__ ((format (printf, 2, 3)));

static int error_message(request_rec *r, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len;
    char *msg = malloc(1000*sizeof(char));

    if (msg) {
        len = vsnprintf(msg, 1000, format, ap);
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
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "socket connect failed for: %s with reason: %s", scfg->renderd_socket_name, strerror(errno));
        close(fd);
        return FD_INVALID;
    }
    return fd;
}

int request_tile(request_rec *r, struct protocol *cmd, int renderImmediately)
{
    int fd;
    int ret = 0;
    int retry = 1;
    struct protocol resp;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    fd = socket_init(r);

    if (fd == FD_INVALID) {
        ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, "Failed to connect to renderer");
        return 0;
    }

    // cmd has already been partial filled, fill in the rest
    cmd->ver = PROTO_VER;
    switch (renderImmediately) {
    case 0: { cmd->cmd = cmdDirty; break;}
    case 1: { cmd->cmd = cmdRender; break;}
    case 2: { cmd->cmd = cmdRenderPrio; break;}
    }

    if (scfg->bulkMode) cmd->cmd = cmdRenderBulk; 

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Requesting style(%s) z(%d) x(%d) y(%d) from renderer with priority %d", cmd->xmlname, cmd->z, cmd->x, cmd->y, cmd->cmd);
    do {
        ret = send(fd, cmd, sizeof(struct protocol), 0);

        if (ret == sizeof(struct protocol))
            break;
        
        if (errno != EPIPE) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "request_tile: Failed to send request to renderer: %s", strerror(errno));
            close(fd);
            return 0;
        }
        close(fd);

        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "request_tile: Reconnecting to rendering socket after failed request due to sigpipe");

        fd = socket_init(r);
        if (fd == FD_INVALID)
            return 0;
    } while (retry--);

    if (renderImmediately) {
        struct timeval tv = {(renderImmediately > 1?scfg->request_timeout_priority:scfg->request_timeout), 0 };
        fd_set rx;
        int s;

        while (1) {
            FD_ZERO(&rx);
            FD_SET(fd, &rx);
            s = select(fd+1, &rx, NULL, NULL, &tv);
            if (s == 1) {
                bzero(&resp, sizeof(struct protocol));
                ret = recv(fd, &resp, sizeof(struct protocol), 0);
                if (ret != sizeof(struct protocol)) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "request_tile: Failed to read response from rendering socket %s",
                                  strerror(errno));
                    break;
                }

                if (cmd->x == resp.x && cmd->y == resp.y && cmd->z == resp.z && !strcmp(cmd->xmlname, resp.xmlname)) {
                    close(fd);
                    if (resp.cmd == cmdDone)
                        return 1;
                    else
                        return 0;
                } else {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                       "Response does not match request: xml(%s,%s) z(%d,%d) x(%d,%d) y(%d,%d)", cmd->xmlname,
                       resp.xmlname, cmd->z, resp.z, cmd->x, resp.x, cmd->y, resp.y);
                }
            } else {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                              "request_tile: Request xml(%s) z(%d) x(%d) y(%d) could not be rendered in %i seconds",
                              cmd->xmlname, cmd->z, cmd->x, cmd->y,
                              (renderImmediately > 1?scfg->request_timeout_priority:scfg->request_timeout));
                break;
            }
        }
    }

    close(fd);
    return 0;
}

static apr_time_t getPlanetTime(request_rec *r)
{
    static pthread_mutex_t planet_lock = PTHREAD_MUTEX_INITIALIZER;
    apr_time_t now = r->request_time;
    struct apr_finfo_t s;

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    if (rdata == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "No per request configuration data");
        return planet_timestamp[0];
    }
    struct protocol * cmd = rdata->cmd;


    pthread_mutex_lock(&planet_lock);
    // Only check for updates periodically
    if (now < last_check[rdata->layerNumber] + apr_time_from_sec(300)) {
        pthread_mutex_unlock(&planet_lock);
        return planet_timestamp[rdata->layerNumber];
    }

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    char filename[PATH_MAX];
    snprintf(filename, PATH_MAX-1, "%s/%s%s", scfg->tile_dir, cmd->xmlname, PLANET_TIMESTAMP);

    last_check[rdata->layerNumber] = now;
    if (apr_stat(&s, filename, APR_FINFO_MIN, r->pool) != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "per tile style planet time stamp (%s) missing, trying global one", filename);
        snprintf(filename, PATH_MAX-1, "%s/%s", scfg->tile_dir, PLANET_TIMESTAMP);
        if (apr_stat(&s, filename, APR_FINFO_MIN, r->pool) != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Global planet time stamp file (%s) is missing. Assuming 3 days old.", filename);
            // Make something up
            planet_timestamp[rdata->layerNumber] = now - apr_time_from_sec(3 * 24 * 60 * 60);
        } else {
            if (s.mtime != planet_timestamp[rdata->layerNumber]) {
                planet_timestamp[rdata->layerNumber] = s.mtime;
                char * timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
                    apr_rfc822_date(timestr, (planet_timestamp[rdata->layerNumber]));
                ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Global planet file time stamp (%s) updated to %s", filename, timestr);
            }
        }
    } else {
        if (s.mtime != planet_timestamp[rdata->layerNumber]) {
            planet_timestamp[rdata->layerNumber] = s.mtime;
            char * timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
            apr_rfc822_date(timestr, (planet_timestamp[rdata->layerNumber]));
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Per style planet file time stamp (%s) updated to %s", filename, timestr);
        }
    }
    pthread_mutex_unlock(&planet_lock);
    return planet_timestamp[rdata->layerNumber];
}

static enum tileState tile_state_once(request_rec *r)
{
    apr_status_t rv;
    apr_finfo_t *finfo = &r->finfo;

    if (!(finfo->valid & APR_FINFO_MTIME)) {
        rv = apr_stat(finfo, r->filename, APR_FINFO_MIN, r->pool);
        if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_state_once: File %s is missing", r->filename);
            return tileMissing;
        }
    }

    if (finfo->mtime < getPlanetTime(r)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_state_once: File %s is old", r->filename);
        return tileOld;
    }

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

/**
 * Add CORS ( Cross-origin resource sharing ) headers. http://www.w3.org/TR/cors/
 * CORS allows requests that would otherwise be forbidden under the same origin policy.
 */
static int add_cors(request_rec *r, const char * cors) {
    const char* origin = apr_table_get(r->headers_in,"Origin");
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking if CORS headers need to be added: Origin: %s Policy: %s", origin, cors);        
    if (!origin) return DONE;
    else {
        if ((strcmp(cors,"*") == 0) || strstr(cors, origin)) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Origin header is allowed under the CORS policy. Adding Access-Control-Allow-Origin");
            if (strcmp(cors,"*") == 0) {
                apr_table_setn(r->headers_out, "Access-Control-Allow-Origin",
                               apr_psprintf(r->pool, "%s", cors));
            } else {
                apr_table_setn(r->headers_out, "Access-Control-Allow-Origin",
                               apr_psprintf(r->pool, "%s", origin));
                apr_table_setn(r->headers_out, "Vary",
                               apr_psprintf(r->pool, "%s", "Origin"));
                
            }
            const char* headers = apr_table_get(r->headers_in,"Access-Control-Request-Headers");
            apr_table_setn(r->headers_out, "Access-Control-Allow-Headers",
                           apr_psprintf(r->pool, "%s", headers));
            if (headers) {
                apr_table_setn(r->headers_out, "Access-Control-Max-Age",
                               apr_psprintf(r->pool, "%i", 604800));
            }
            //If this is an OPTIONS cors pre-flight request, no need to return the body as the actual request will follow
            if (strcmp(r->method, "OPTIONS") == 0)
                return OK;
            else return DONE;
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Origin header (%s)is NOT allowed under the CORS policy(%s). Rejecting request", origin, cors);
            return HTTP_FORBIDDEN;
        }
    }
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
    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;
    tile_config_rec *tile_config = &tile_configs[rdata->layerNumber];

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "expires(%s), uri(%s), filename(%s), path_info(%s)\n",
                  r->handler, r->uri, r->filename, r->path_info);

    /* If the hostname matches the "extended caching hostname" then set the cache age accordingly */
    if ((scfg->cache_extended_hostname[0] != 0) && (strstr(r->hostname,
            scfg->cache_extended_hostname) != NULL)) {
        maxAge = scfg->cache_extended_duration;
    } else {

        /* Test if the tile we are serving is out of date, then set a low maxAge*/
        if (state == tileOld) {
            holdoff = (scfg->cache_duration_dirty / 2) * (rand() / (RAND_MAX
                    + 1.0));
            maxAge = scfg->cache_duration_dirty + holdoff;
        } else {
            // cache heuristic based on zoom level
            if (cmd->z > tile_config->maxzoom) {
                minCache = 0;
                ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                        "z (%i) is larger than MAXZOOM %i\n", cmd->z, tile_config->maxzoom);
            } else {
                minCache = scfg->mincachetime[cmd->z];
            }
            // Time to the next known complete rerender
            planetTimestamp = apr_time_sec(getPlanetTime(r)
                    + apr_time_from_sec(PLANET_INTERVAL) - r->request_time);
            // Time since the last render of this tile
            lastModified = (int) (((double) apr_time_sec(r->request_time
                    - finfo->mtime))
                    * scfg->cache_duration_last_modified_factor);
            // Add a random jitter of 3 hours to space out cache expiry
            holdoff = (3 * 60 * 60) * (rand() / (RAND_MAX + 1.0));

            maxAge = MAX(minCache, planetTimestamp);
            maxAge = MAX(maxAge, lastModified);
            maxAge += holdoff;

            ap_log_rerror(
                    APLOG_MARK,
                    APLOG_DEBUG,
                    0,
                    r,
                    "caching heuristics: next planet render %ld; zoom level based %ld; last modified %ld\n",
                    planetTimestamp, minCache, lastModified);
        }

        maxAge = MIN(maxAge, scfg->cache_duration_max);
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Setting tiles maxAge to %ld\n", maxAge);

    apr_table_mergen(t, "Cache-Control",
                     apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                     maxAge));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, (apr_time_from_sec(maxAge) + r->request_time));
    apr_table_setn(t, "Expires", timestr);
}



static int get_global_lock(request_rec *r, apr_global_mutex_t * mutex) {
    apr_status_t rs;
    int camped;

    for (camped = 0; camped < MAXCAMP; camped++) {
        rs = apr_global_mutex_trylock(mutex);
        if (APR_STATUS_IS_EBUSY(rs)) {
            apr_sleep(CAMPOUT);
        } else if (rs == APR_SUCCESS) {
            return 1;
        } else if (APR_STATUS_IS_ENOTIMPL(rs)) {
            /* If it's not implemented, just hang in the mutex. */
            rs = apr_global_mutex_lock(mutex);
            if (rs == APR_SUCCESS) {
                return 1;
            } else {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Could not get hardlock");
                return 0;
            }
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, "Unknown return status from trylock");
            return 0;
        }
    }
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Timedout trylock");
    return 0;
}

static int incRespCounter(int resp, request_rec *r, struct protocol * cmd, int layerNumber) {
    stats_data *stats;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    if (!scfg->enableGlobalStats) {
        /* If tile stats reporting is not enable
         * pretend we correctly updated the counter to
         * not fill the logs with warnings about failed
         * stats
         */
        return 1;
    }

    if (get_global_lock(r, stats_mutex) != 0) {
        stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
        switch (resp) {
        case OK: {
            stats->noResp200++;
            if (cmd != NULL) {
                stats->noRespZoom[cmd->z]++;
                stats->noResp200Layer[layerNumber]++;
            }
            break;
        }
        case HTTP_NOT_MODIFIED: {
            stats->noResp304++;
            if (cmd != NULL) {
                stats->noRespZoom[cmd->z]++;
                stats->noResp200Layer[layerNumber]++;
            }
            break;
        }
        case HTTP_NOT_FOUND: {
            stats->noResp404++;
            stats->noResp404Layer[layerNumber]++;
            break;
        }
        case HTTP_SERVICE_UNAVAILABLE: {
            stats->noResp503++;
            break;
        }
        case HTTP_INTERNAL_SERVER_ERROR: {
            stats->noResp5XX++;
            break;
        }
        default: {
            stats->noRespOther++;
        }

        }
        apr_global_mutex_unlock(stats_mutex);
        /* Swallowing the result because what are we going to do with it at
         * this stage?
         */
        return 1;
    } else {
        return 0;
    }
}

static int incFreshCounter(int status, request_rec *r) {
    stats_data *stats;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    if (!scfg->enableGlobalStats) {
        /* If tile stats reporting is not enable
         * pretend we correctly updated the counter to
         * not fill the logs with warnings about failed
         * stats
         */
        return 1;
    }

    if (get_global_lock(r, stats_mutex) != 0) {
        stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
        switch (status) {
        case FRESH: {
            stats->noFreshCache++;
            break;
        }
        case FRESH_RENDER: {
            stats->noFreshRender++;
            break;
        }
        case OLD: {
            stats->noOldCache++;
            break;
        }
        case OLD_RENDER: {
            stats->noOldRender++;
            break;
        }
        }
        apr_global_mutex_unlock(stats_mutex);
        /* Swallowing the result because what are we going to do with it at
         * this stage?
         */
        return 1;
    } else {
        return 0;
    }
}

static int delay_allowed(request_rec *r, enum tileState state) {
    delaypool * delayp;
    int delay = 0;
    int i,j;
    int hashkey;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    delayp = (delaypool *)apr_shm_baseaddr_get(delaypool_shm);

    struct in_addr sin_addr;
    struct in6_addr ip;
    if (inet_pton(AF_INET,r->connection->remote_ip,&sin_addr) > 0) {
        //ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking delays: for IP %s appears to be an IPv4 address", r->connection->remote_ip);
        memset(ip.s6_addr,0,16);
        memcpy(&(ip.s6_addr[12]), &(sin_addr.s_addr), 4);
        uint32_t hashkey = sin_addr.s_addr % DELAY_HASHTABLE_WHITELIST_SIZE;
        if (delayp->whitelist[hashkey] == sin_addr.s_addr) {
            return 1;
        }
    } else {
        if (inet_pton(AF_INET6,r->connection->remote_ip,&ip) <= 0) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Checking delays: for IP %s. Don't know what it is", r->connection->remote_ip);
            return 0;
        }
    }

    hashkey = (*((uint32_t *)(&ip.s6_addr[0])) ^ *((uint32_t *)(&ip.s6_addr[4])) ^ *((uint32_t *)(&ip.s6_addr[8])) ^ *((uint32_t *)(&ip.s6_addr[12]))) % DELAY_HASHTABLE_SIZE;
    
    /* If a delaypool fillup is ongoing, just skip accounting to not block on a lock */
    if (delayp->locked) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "skipping delay pool accounting, during fillup procedure\n");
        return 1;
    }
    
    if (get_global_lock(r,delay_mutex) == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Could not acquire lock, skipping delay pool accounting\n");
        return 1;
    };

    if (memcmp(&(delayp->users[hashkey].ip_addr), &ip, sizeof(struct in6_addr)) == 0) {
        /* Repeat the process to determine if we have tockens in the bucket, as the fillup only runs once a client hits an empty bucket,
           so in the mean time, the bucket might have been filled */
        for (j = 0; j < 3; j++) {
            //ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checking delays: Current poolsize: %i tiles and %i renders\n", delayp->users[hashkey].available_tiles, delayp->users[hashkey].available_render_req);
            delay = 0;
            if (delayp->users[hashkey].available_tiles > 0) {
                delayp->users[hashkey].available_tiles--;
            } else {
                delay = 1;
            }
            if (state == tileMissing) {
                if (delayp->users[hashkey].available_render_req > 0) {
                    delayp->users[hashkey].available_render_req--;
                } else {
                    delay = 2;
                }
            }

            if (delay > 0) {
                /* If we are on the second round, we really  hit an empty delaypool, timeout for a while to slow down clients */
                if (j > 0) {
                    apr_global_mutex_unlock(delay_mutex);
                    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Delaypool: Client %s has hit its limits, throttling (%i)\n", r->connection->remote_ip, delay);
                    sleep(CLIENT_PENALTY);
                    if (get_global_lock(r,delay_mutex) == 0) {
                        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Could not acquire lock, but had to delay\n");
                        return 0;
                    };
                }
                /* We hit an empty bucket, so run the bucket fillup procedure to check if new tokens should have arrived in the mean time. */
                apr_time_t now = apr_time_now();
                int tiles_topup = (now - delayp->last_tile_fillup) / scfg->delaypoolTileRate;
                int render_topup = (now - delayp->last_render_fillup) / scfg->delaypoolRenderRate;
                //ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Filling up pools with %i tiles and %i renders\n", tiles_topup, render_topup);
                if ((tiles_topup > 0) || (render_topup > 0)) {
                    delayp->locked = 1;
                    for (i = 0; i < DELAY_HASHTABLE_SIZE; i++) {
                        delayp->users[i].available_tiles += tiles_topup;
                        delayp->users[i].available_render_req += render_topup;
                        if (delayp->users[i].available_tiles > scfg->delaypoolTileSize) {
                            delayp->users[i].available_tiles = scfg->delaypoolTileSize;
                        }
                        if (delayp->users[i].available_render_req > scfg->delaypoolRenderSize) {
                            delayp->users[i].available_render_req = scfg->delaypoolRenderSize;
                        }
                    }
                    delayp->locked = 0;
                }
                delayp->last_tile_fillup += scfg->delaypoolTileRate*tiles_topup;
                delayp->last_render_fillup += scfg->delaypoolRenderRate*render_topup;                
                
            } else {
                break;
            }
        }
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Creating a new delaypool for ip %s\n", r->connection->remote_ip);
        memcpy(&(delayp->users[hashkey].ip_addr), &ip, sizeof(struct in6_addr));
        delayp->users[hashkey].available_tiles = scfg->delaypoolTileSize;
        delayp->users[hashkey].available_render_req = scfg->delaypoolRenderSize;
        delay = 0;
    }
    apr_global_mutex_unlock(delay_mutex);

    if (delay > 0) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Delaypool: Client %s has hit its limits, rejecting (%i)\n", r->connection->remote_ip, delay);
        return 0;
    } else {
        return 1;
    }
}

static int tile_handler_dirty(request_rec *r)
{
    if(strcmp(r->handler, "tile_dirty"))
        return DECLINED;

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    struct protocol * cmd = rdata->cmd;
    if (cmd == NULL)
        return DECLINED;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    if (scfg->bulkMode) return OK;

    request_tile(r, cmd, 0);
    return error_message(r, "Tile submitted for rendering\n");
}

static int tile_storage_hook(request_rec *r)
{
//    char abs_path[PATH_MAX];
    double avg;
    int renderPrio = 0;
    enum tileState state;

    if (!r->handler)
        return DECLINED;

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tile_storage_hook: handler(%s), uri(%s), filename(%s), path_info(%s)",
                  r->handler, r->uri, r->filename, r->path_info);

    // Any status request is OK. tile_dirty also doesn't need to be handled, as tile_handler_dirty will take care of it
    if (!strcmp(r->handler, "tile_status") || !strcmp(r->handler, "tile_dirty") || !strcmp(r->handler, "tile_mod_stats")
            || !(strcmp(r->handler, "tile_json")))
        return OK;

    if (strcmp(r->handler, "tile_serve"))
        return DECLINED;

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    struct protocol * cmd = rdata->cmd;
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
    avg = get_load_avg();
    state = tile_state(r, cmd);

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    if (scfg->enableTileThrottling && !delay_allowed(r, state)) {
        if (!incRespCounter(HTTP_SERVICE_UNAVAILABLE, r, cmd, rdata->layerNumber)) {
                   ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                        "Failed to increase response stats counter");
        }
        return HTTP_SERVICE_UNAVAILABLE;        
    }

    switch (state) {
        case tileCurrent:
            if (!incFreshCounter(FRESH, r)) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "Failed to increase fresh stats counter");
            }
            return OK;
            break;
        case tileOld:
            if (scfg->bulkMode) {
                return OK;
            } else if (avg > scfg->max_load_old) {
               // Too much load to render it now, mark dirty but return old tile
               request_tile(r, cmd, 0);
               ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Load (%f) larger max_load_old (%d). Mark dirty and deliver from cache.", avg, scfg->max_load_old);
               if (!incFreshCounter(OLD, r)) {
                   ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                        "Failed to increase fresh stats counter");
               }
               return OK;
            }
            renderPrio = 1;
            break;
        case tileMissing:
            if (avg > scfg->max_load_missing) {
               request_tile(r, cmd, 0);
               ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Load (%f) larger max_load_missing (%d). Return HTTP_NOT_FOUND.", avg, scfg->max_load_missing);
               if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
                   ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                        "Failed to increase response stats counter");
               }
               return HTTP_NOT_FOUND;
            }
            renderPrio = 2;
            break;
    }

    if (request_tile(r, cmd, renderPrio)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Update file info abs_path(%s)", r->filename);
        // Need to update fileinfo for new rendered tile
        apr_stat(&r->finfo, r->filename, APR_FINFO_MIN, r->pool);
        if (!incFreshCounter(FRESH_RENDER, r)) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "Failed to increase fresh stats counter");
        }
        return OK;
    }

    if (state == tileOld) {
        if (!incFreshCounter(OLD_RENDER, r)) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "Failed to increase fresh stats counter");
        }
        return OK;
    }
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_storage_hook: Missing tile was not rendered in time. Returning File Not Found");
    if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                "Failed to increase response stats counter");
    }

    return HTTP_NOT_FOUND;
}

static int tile_handler_status(request_rec *r)
{
    enum tileState state;
    char time_str[APR_CTIME_LEN];

    if(strcmp(r->handler, "tile_status"))
        return DECLINED;

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    struct protocol * cmd = rdata->cmd;
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

/**
 * Implement a tilejson description page for the tile layer.
 * This follows the tilejson specification of mapbox ( https://github.com/mapbox/tilejson-spec/tree/master/2.0.0 )
 */
static int tile_handler_json(request_rec *r)
{
    unsigned char *buf;
    int len;
    enum tileState state;
    char *timestr;
    long int maxAge = 7*24*60*60;
    apr_table_t *t = r->headers_out;
    int i;

    if(strcmp(r->handler, "tile_json"))
        return DECLINED;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Handling tile json request\n");

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;
    tile_config_rec *tile_config = &tile_configs[rdata->layerNumber];
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Handling tile json request for layer %s\n", tile_config->xmlname);

    if (tile_config->cors) {
        int resp = add_cors(r, tile_config->cors);
        if (resp != DONE) return resp;
    }

    buf = malloc(8*1024);

    snprintf(buf, 8*1024,
            "{\n"
            "\t\"tilejson\": \"2.0.0\",\n"
            "\t\"schema\": \"xyz\",\n"
            "\t\"name\": \"%s\"\n"
            "\t\"description\": \"%s\",\n"
            "\t\"attribution\": \"%s\",\n"
            "\t\"minzoom\": %i,\n"
            "\t\"maxzoom\": %i,\n"
            "\t\"tiles\": [\n",
            tile_config->xmlname, (tile_config->description?tile_config->description:""), tile_config->attribution, tile_config->minzoom, tile_config->maxzoom);
    for (i = 0; i < tile_config->noHostnames; i++) {
        strncat(buf,"\t\t\"", 8*1024-strlen(buf)-1);
        strncat(buf,tile_config->hostnames[i], 8*1024-strlen(buf)-1);
        strncat(buf,tile_config->baseuri,8*1024-strlen(buf)-1);
        strncat(buf,"{z}/{x}/{y}.",8*1024-strlen(buf)-1);
        strncat(buf,tile_config->fileExtension, 8*1024-strlen(buf)-1);
        strncat(buf,"\"\n", 8*1024-strlen(buf)-1);
    }
    strncat(buf,"\t]\n}\n", 8*1024-strlen(buf)-1);
    len = strlen(buf);

    /*
     * Add HTTP headers. Make this file cachable for 1 week
     */
    char *md5 = ap_md5_binary(r->pool, buf, len);
    apr_table_setn(r->headers_out, "ETag",
            apr_psprintf(r->pool, "\"%s\"", md5));
    ap_set_content_type(r, "application/json");
    ap_set_content_length(r, len);
    apr_table_mergen(t, "Cache-Control",
            apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                    maxAge));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, (apr_time_from_sec(maxAge) + r->request_time));
    apr_table_setn(t, "Expires", timestr);
    ap_rwrite(buf, len, r);
    free(buf);

    return OK;
}

static int tile_handler_mod_stats(request_rec *r)
{
    stats_data * stats;
    stats_data local_stats;
    int i;

    if (strcmp(r->handler, "tile_mod_stats"))
        return DECLINED;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);
    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;

    if (!scfg->enableGlobalStats) {
        return error_message(r, "Stats are not enabled for this server");
    }

    if (get_global_lock(r,stats_mutex) != 0) {
        //Copy over the global counter variable into
        //local variables, that we can immediately
        //release the lock again
        stats = (stats_data *) apr_shm_baseaddr_get(stats_shm);
        memcpy(&local_stats, stats, sizeof(stats_data));
        local_stats.noResp200Layer = malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
        memcpy(local_stats.noResp200Layer, stats->noResp200Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
        local_stats.noResp404Layer = malloc(sizeof(apr_uint64_t) * scfg->configs->nelts);
        memcpy(local_stats.noResp404Layer, stats->noResp404Layer, sizeof(apr_uint64_t) * scfg->configs->nelts);
        apr_global_mutex_unlock(stats_mutex);
    } else {
        return error_message(r, "Failed to acquire lock, can't display stats");
    }

    ap_rprintf(r, "NoResp200: %li\n", local_stats.noResp200);
    ap_rprintf(r, "NoResp304: %li\n", local_stats.noResp304);
    ap_rprintf(r, "NoResp404: %li\n", local_stats.noResp404);
    ap_rprintf(r, "NoResp503: %li\n", local_stats.noResp503);
    ap_rprintf(r, "NoResp5XX: %li\n", local_stats.noResp5XX);
    ap_rprintf(r, "NoRespOther: %li\n", local_stats.noRespOther);
    ap_rprintf(r, "NoFreshCache: %li\n", local_stats.noFreshCache);
    ap_rprintf(r, "NoOldCache: %li\n", local_stats.noOldCache);
    ap_rprintf(r, "NoFreshRender: %li\n", local_stats.noFreshRender);
    ap_rprintf(r, "NoOldRender: %li\n", local_stats.noOldRender);
    for (i = 0; i <= global_max_zoom; i++) {
        ap_rprintf(r, "NoRespZoom%02i: %li\n", i, local_stats.noRespZoom[i]);
    }
    for (i = 0; i < scfg->configs->nelts; ++i) {
        tile_config_rec *tile_config = &tile_configs[i];
        ap_rprintf(r,"NoRes200Layer%s: %li\n", tile_config->baseuri, local_stats.noResp200Layer[i]);
        ap_rprintf(r,"NoRes404Layer%s: %li\n", tile_config->baseuri, local_stats.noResp404Layer[i]);
    }
    free(local_stats.noResp200Layer);
    free(local_stats.noResp404Layer);
    return OK;
}

static int tile_handler_serve(request_rec *r)
{
    const int tile_max = MAX_SIZE;
    unsigned char err_msg[4096];
    unsigned char *buf;
    int len;
    int compressed;
    apr_status_t errstatus;

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    if(strcmp(r->handler, "tile_serve"))
        return DECLINED;

    struct tile_request_data * rdata = (struct tile_request_data *)ap_get_module_config(r->request_config, &tile_module);
    struct protocol * cmd = rdata->cmd;
    if (cmd == NULL){
        sleep(CLIENT_PENALTY);
        if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "Failed to increase response stats counter");
        }
        return HTTP_NOT_FOUND;
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_handler_serve: xml(%s) z(%d) x(%d) y(%d)", cmd->xmlname, cmd->z, cmd->x, cmd->y);

    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;

    if (tile_configs[rdata->layerNumber].cors) {
        int resp = add_cors(r, tile_configs[rdata->layerNumber].cors);
        if (resp != DONE) return resp;
    }

    // FIXME: It is a waste to do the malloc + read if we are fulfilling a HEAD or returning a 304.
    buf = malloc(tile_max);
    if (!buf) {
        if (!incRespCounter(HTTP_INTERNAL_SERVER_ERROR, r, cmd, rdata->layerNumber)) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "Failed to increase response stats counter");
        }
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    err_msg[0] = 0;
    len = tile_read(scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z, buf, tile_max, &compressed, err_msg);
    if (len > 0) {
        if (compressed) {
            const char* accept_encoding = apr_table_get(r->headers_in,"Accept-Encoding");
            if (accept_encoding && strstr(accept_encoding,"gzip")) {
                r->content_encoding = "gzip";
            } else {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                              "Tile data is compressed, but user agent doesn't support Content-Encoding and we don't know how to decompress it server side");
                //TODO: decompress the output stream before sending it to client
            }
        }
        
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
        ap_set_content_type(r, tile_configs[rdata->layerNumber].mimeType);
        ap_set_content_length(r, len);
        add_expiry(r, cmd);
        if ((errstatus = ap_meets_conditions(r)) != OK) {
            free(buf);
            if (!incRespCounter(errstatus, r, cmd, rdata->layerNumber)) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                        "Failed to increase response stats counter");
            }
            return errstatus;
        } else {
            ap_rwrite(buf, len, r);
            free(buf);
            if (!incRespCounter(errstatus, r, cmd, rdata->layerNumber)) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                        "Failed to increase response stats counter");
            }
            return OK;
        }
    }
    free(buf);
    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Failed to read tile from disk: %s", err_msg);
    if (!incRespCounter(HTTP_NOT_FOUND, r, cmd, rdata->layerNumber)) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                "Failed to increase response stats counter");
    }
    return DECLINED;
}

static int tile_translate(request_rec *r)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: uri(%s)", r->uri);

    int i,n,limit,oob;
    char option[11];

    ap_conf_vector_t *sconf = r->server->module_config;
    tile_server_conf *scfg = ap_get_module_config(sconf, &tile_module);

    tile_config_rec *tile_configs = (tile_config_rec *) scfg->configs->elts;

    /*
     * The page /mod_tile returns global stats about the number of tiles
     * handled and in what state those tiles were.
     * This should probably not be hard coded
     */
    if (!strncmp("/mod_tile", r->uri, strlen("/mod_tile"))) {
        r->handler = "tile_mod_stats";
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                "tile_translate: retrieving global mod_tile stats");
        return OK;
    }

    for (i = 0; i < scfg->configs->nelts; ++i) {
        tile_config_rec *tile_config = &tile_configs[i];

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: testing baseuri(%s) name(%s) extension(%s)",
                tile_config->baseuri, tile_config->xmlname, tile_config->fileExtension );


        if (!strncmp(tile_config->baseuri, r->uri, strlen(tile_config->baseuri))) {

            struct tile_request_data * rdata = (struct tile_request_data *) apr_pcalloc(r->pool, sizeof(struct tile_request_data));
            struct protocol * cmd = (struct protocol *) apr_pcalloc(r->pool, sizeof(struct protocol));
            bzero(cmd, sizeof(struct protocol));
            bzero(rdata, sizeof(struct tile_request_data));
            if (!strncmp(r->uri + strlen(tile_config->baseuri),"tile-layer.json", strlen("tile-layer.json"))) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Requesting tileJSON for tilelayer %s", tile_config->xmlname);
                r->handler = "tile_json";
                rdata->layerNumber = i;
                ap_set_module_config(r->request_config, &tile_module, rdata);
                return OK;
            }
            char extension[256];
            n = sscanf(r->uri+strlen(tile_config->baseuri),"%d/%d/%d.%[a-z]/%10s", &(cmd->z), &(cmd->x), &(cmd->y), extension, option);
            if (n < 4) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Invalid URL for tilelayer %s", tile_config->xmlname);
                return DECLINED;
            }
            if (strcmp(extension, tile_config->fileExtension) != 0) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: Invalid file extension (%s) for tilelayer %s, required %s",
                        extension, tile_config->xmlname, tile_config->fileExtension);
                return DECLINED;
            }

            oob = (cmd->z < tile_config->minzoom || cmd->z > tile_config->maxzoom);
            if (!oob) {
                 // valid x/y for tiles are 0 ... 2^zoom-1
                 limit = (1 << cmd->z) - 1;
                 oob =  (cmd->x < 0 || cmd->x > limit || cmd->y < 0 || cmd->y > limit);
            }

            if (oob) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: request for %s was outside of allowed bounds", tile_config->xmlname);
                sleep(CLIENT_PENALTY);
                //Don't increase stats counter here,
                //As we are interested in valid tiles only
                return HTTP_NOT_FOUND;
            }

            strcpy(cmd->xmlname, tile_config->xmlname);

            // Store a copy for later
            rdata->cmd = cmd;
            rdata->layerNumber = i;
            ap_set_module_config(r->request_config, &tile_module, rdata);

            // Generate the tile filename?
            char abs_path[PATH_MAX];
#ifdef METATILE
            xyz_to_meta(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#else
            xyz_to_path(abs_path, sizeof(abs_path), scfg->tile_dir, cmd->xmlname, cmd->x, cmd->y, cmd->z);
#endif
            r->filename = apr_pstrdup(r->pool, abs_path);

            if (n == 5) {
                if (!strcmp(option, "status")) r->handler = "tile_status";
                else if (!strcmp(option, "dirty")) r->handler = "tile_dirty";
                else return DECLINED;
            } else {
                r->handler = "tile_serve";
            }

            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: op(%s) xml(%s) mime(%s) z(%d) x(%d) y(%d)",
                    r->handler , cmd->xmlname, tile_config->mimeType, cmd->z, cmd->x, cmd->y);

            return OK;
        }
    }
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "tile_translate: No suitable tile layer found");
    return DECLINED;
}

/*
 * This routine is called in the parent, so we'll set up the shared
 * memory segment and mutex here.
 */

static int mod_tile_post_config(apr_pool_t *pconf, apr_pool_t *plog,
    apr_pool_t *ptemp, server_rec *s)
{
    void *data; /* These two help ensure that we only init once. */
    const char *userdata_key = "mod_tile_init_module";
    apr_status_t rs;
    stats_data *stats;
    delaypool *delayp;
    int i;

    /*
     * The following checks if this routine has been called before.
     * This is necessary because the parent process gets initialized
     * a couple of times as the server starts up, and we don't want
     * to create any more mutexes and shared memory segments than
     * we're actually going to use.
     */
    apr_pool_userdata_get(&data, userdata_key, s->process->pool);
    if (!data) {
        apr_pool_userdata_set((const void *) 1, userdata_key,
                              apr_pool_cleanup_null, s->process->pool);
        return OK;
    } /* Kilroy was here */

    /* Create the shared memory segment */

    /*
     * Create a unique filename using our pid. This information is
     * stashed in the global variable so the children inherit it.
     * TODO get the location from the environment $TMPDIR or somesuch.
     */
    shmfilename = apr_psprintf(pconf, "/tmp/httpd_shm.%ld", (long int)getpid());
    shmfilename_delaypool = apr_psprintf(pconf, "/tmp/httpd_shm_delay.%ld", (long int)getpid());

    /* Now create that segment 
     * would prefer to use scfg->configs->nelts here but that does
     * not seem to be set at this stage, so rely on previously set layerCount */

    rs = apr_shm_create(&stats_shm, sizeof(stats_data) + layerCount * 2 * sizeof(apr_uint64_t),
                        (const char *) shmfilename, pconf);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to create shared memory segment on file %s",
                     shmfilename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    rs = apr_shm_create(&delaypool_shm, sizeof(delaypool),
                        (const char *) shmfilename_delaypool, pconf);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to create shared memory segment on file %s",
                     shmfilename_delaypool);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Created it, now let's zero it out */
    stats = (stats_data *)apr_shm_baseaddr_get(stats_shm);
    stats->noResp200 = 0;
    stats->noResp304 = 0;
    stats->noResp404 = 0;
    stats->noResp503 = 0;
    stats->noResp5XX = 0;
    for (i = 0; i <= global_max_zoom; i++) {
        stats->noRespZoom[i] = 0;
    }
    stats->noRespOther = 0;
    stats->noFreshCache = 0;
    stats->noFreshRender = 0;
    stats->noOldCache = 0;
    stats->noOldRender = 0;

    /* the "stats" block does not have a fixed size; it is a fixed-size struct
     * followed by two arrays with one element each per layer. All of this sits
     * in one shared memory block, and for ease of use, pointers from inside the
     * struct point to the arrays. */
    stats->noResp404Layer = (apr_uint64_t *) ((char *) stats + sizeof(stats_data));
    stats->noResp200Layer = (apr_uint64_t *) ((char *) stats + sizeof(stats_data) + sizeof(apr_uint64_t) * layerCount);

    /* the last_check and planet_timestamp arrays have a variable size as well,
     * however they are not in shared memory. */
    last_check = (apr_time_t *) apr_pcalloc(pconf, sizeof(apr_time_t) * layerCount);
    planet_timestamp = (apr_time_t *) apr_pcalloc(pconf, sizeof(apr_time_t) * layerCount);

    /* zero out all the non-fixed-length stuff */
    for (i=0; i<layerCount; i++) {
        stats->noResp404Layer[i] = 0;
        stats->noResp200Layer[i] = 0;
        last_check[i] = 0;
        planet_timestamp[i] = 0;
    }

    delayp = (delaypool *)apr_shm_baseaddr_get(delaypool_shm);
    
    delayp->last_tile_fillup = apr_time_now();
    delayp->last_render_fillup = apr_time_now();

    for (i = 0; i < DELAY_HASHTABLE_SIZE; i++) {
        memset(&(delayp->users[i].ip_addr),0, sizeof(struct in6_addr));
        delayp->users[i].available_tiles = 0;
        delayp->users[i].available_render_req = 0;
    }
    for (i = 0; i < DELAY_HASHTABLE_WHITELIST_SIZE; i++) {
        delayp->whitelist[i] = (in_addr_t)0;
    }
    /* TODO: need a way to initialise the delaypool whitelist */


    /* Create global mutex */

    /*
     * Create another unique filename to lock upon. Note that
     * depending on OS and locking mechanism of choice, the file
     * may or may not be actually created.
     */
    mutexfilename = apr_psprintf(pconf, "/tmp/httpd_mutex.%ld",
                                 (long int) getpid());

    rs = apr_global_mutex_create(&stats_mutex, (const char *) mutexfilename,
                                 APR_LOCK_DEFAULT, pconf);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to create mutex on file %s",
                     mutexfilename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

#ifdef MOD_TILE_SET_MUTEX_PERMS
    rs = unixd_set_global_mutex_perms(stats_mutex);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
                     "Parent could not set permissions on mod_tile "
                     "mutex: check User and Group directives");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
#endif /* MOD_TILE_SET_MUTEX_PERMS */

    /*
     * Create another unique filename to lock upon. Note that
     * depending on OS and locking mechanism of choice, the file
     * may or may not be actually created.
     */
    mutexfilename = apr_psprintf(pconf, "/tmp/httpd_mutex_delay.%ld",
                                 (long int) getpid());

    rs = apr_global_mutex_create(&delay_mutex, (const char *) mutexfilename,
                                 APR_LOCK_DEFAULT, pconf);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to create mutex on file %s",
                     mutexfilename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

#ifdef MOD_TILE_SET_MUTEX_PERMS
    rs = unixd_set_global_mutex_perms(delay_mutex);
    if (rs != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
                     "Parent could not set permissions on mod_tile "
                     "mutex: check User and Group directives");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
#endif /* MOD_TILE_SET_MUTEX_PERMS */

    return OK;
}


/*
 * This routine gets called when a child inits. We use it to attach
 * to the shared memory segment, and reinitialize the mutex.
 */

static void mod_tile_child_init(apr_pool_t *p, server_rec *s)
{
    apr_status_t rs;

     /*
      * Re-open the mutex for the child. Note we're reusing
      * the mutex pointer global here.
      */
     rs = apr_global_mutex_child_init(&stats_mutex,
                                      (const char *) mutexfilename,
                                      p);
     if (rs != APR_SUCCESS) {
         ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
                     "Failed to reopen mutex on file %s",
                     shmfilename);
         /* There's really nothing else we can do here, since
          * This routine doesn't return a status. */
         exit(1); /* Ugly, but what else? */
     }
}

static void register_hooks(__attribute__((unused)) apr_pool_t *p)
{
    ap_hook_post_config(mod_tile_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_child_init(mod_tile_child_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_serve, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_dirty, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_status, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_json, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(tile_handler_mod_stats, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_translate_name(tile_translate, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_map_to_storage(tile_storage_hook, NULL, NULL, APR_HOOK_FIRST);
}

static const char *_add_tile_config(cmd_parms *cmd, void *mconfig,
                                    const char *baseuri, const char *name, int minzoom, int maxzoom,
                                    const char * fileExtension, const char *mimeType, const char *description, const char * attribution,
                                    int noHostnames, char ** hostnames, char * cors)
{
    if (strlen(name) == 0) {
        return "ConfigName value must not be null";
    }

    if (hostnames == NULL) {
        hostnames = malloc(sizeof(char *));
        /* FIXME: wouldn't be allocationg 7+len+1 bytes be enough? */
        hostnames[0] = malloc(PATH_MAX);
        strncpy(hostnames[0],"http://", PATH_MAX);
        if (cmd->server->server_hostname == NULL) {
            ap_log_error(APLOG_MARK, APLOG_WARNING, APR_SUCCESS, cmd->server,
                         "Could not determine host name of server to configure tile-json request. Using localhost instead");

            strncat(hostnames[0],"localhost",PATH_MAX-10);
        } else 
            strncat(hostnames[0],cmd->server->server_hostname,PATH_MAX-strlen(hostnames[0])-1);
        noHostnames = 1;
    }

    if ((minzoom < 0) || (maxzoom > MAX_ZOOM_SERVER)) {
        return "The configured zoom level lies outside of the range supported by this server";
    }
    if (maxzoom > global_max_zoom) global_max_zoom = maxzoom;


    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    tile_config_rec *tilecfg = apr_array_push(scfg->configs);

    // Ensure URI string ends with a trailing slash
    int urilen = strlen(baseuri);

    if (urilen==0)
      snprintf(tilecfg->baseuri, PATH_MAX, "/");
    else if (baseuri[urilen-1] != '/')
      snprintf(tilecfg->baseuri, PATH_MAX, "%s/", baseuri);
    else
      snprintf(tilecfg->baseuri, PATH_MAX, "%s", baseuri);

    strncpy(tilecfg->xmlname, name, XMLCONFIG_MAX-1);
    strncpy(tilecfg->fileExtension, fileExtension, XMLCONFIG_MAX-1);
    strncpy(tilecfg->mimeType, mimeType, XMLCONFIG_MAX-1);
    tilecfg->xmlname[XMLCONFIG_MAX-1] = 0;
    tilecfg->minzoom = minzoom;
    tilecfg->maxzoom = maxzoom;
    tilecfg->description = description;
    tilecfg->attribution = attribution;
    tilecfg->noHostnames = noHostnames;
    tilecfg->hostnames = hostnames;
    tilecfg->cors = cors;

    ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, cmd->server,
                    "Loading tile config %s at %s for zooms %i - %i from tile directory %s with extension .%s and mime type %s",
                 name, baseuri, minzoom, maxzoom, scfg->tile_dir, fileExtension, mimeType);

    layerCount++;
    return NULL;
}

static const char *add_tile_mime_config(cmd_parms *cmd, void *mconfig, const char *baseuri, const char *name, const char * fileExtension)
{
    if (strcmp(fileExtension,"png") == 0) {
        return _add_tile_config(cmd, mconfig, baseuri, name, 0, MAX_ZOOM, fileExtension, "image/png",NULL,DEFAULT_ATTRIBUTION,0,NULL,NULL);
    }
    if (strcmp(fileExtension,"js") == 0) {
        return _add_tile_config(cmd, mconfig, baseuri, name, 0, MAX_ZOOM, fileExtension, "text/javascript",NULL,DEFAULT_ATTRIBUTION,0,NULL,"*");
    }
    return _add_tile_config(cmd, mconfig, baseuri, name, 0, MAX_ZOOM, fileExtension, "image/png",NULL,DEFAULT_ATTRIBUTION,0,NULL,NULL);
}

static const char *add_tile_config(cmd_parms *cmd, void *mconfig, const char *baseuri, const char *name)
{
    return _add_tile_config(cmd, mconfig, baseuri, name, 0, MAX_ZOOM, "png", "image/png",NULL,DEFAULT_ATTRIBUTION,0,NULL,NULL);
}

static const char *load_tile_config(cmd_parms *cmd, void *mconfig, const char *conffile)
{
    FILE * hini ;
    char filename[PATH_MAX];
    char url[PATH_MAX];
    char xmlname[XMLCONFIG_MAX];
    char line[INILINE_MAX];
    char key[INILINE_MAX];
    char value[INILINE_MAX];
    const char * result;
    char fileExtension[INILINE_MAX];
    char mimeType[INILINE_MAX];
    char * description;
    char * attribution;
    char * cors = NULL;
    char **hostnames;
    char **hostnames_tmp;
    int noHostnames;
    int tilelayer = 0;
    int minzoom = 0;
    int maxzoom = MAX_ZOOM;

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
        if (line[0] == ';') continue;
        if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = 0;
        if (line[0] == '[') {
            /*Add the previous section to the configuration */
            if (tilelayer == 1) {
                result = _add_tile_config(cmd, mconfig, url, xmlname, minzoom, maxzoom, fileExtension, mimeType,
                                          description,attribution,noHostnames,hostnames, cors);
                if (result != NULL) return result;
            }
            if (strlen(line) >= XMLCONFIG_MAX){
                return "XML name too long";
            }
            sscanf(line, "[%[^]]", xmlname);
            if ((strcmp(xmlname,"mapnik") == 0) || (strcmp(xmlname,"renderd") == 0)) {
                /* These aren't tile layers but configuration sections for renderd */
                tilelayer = 0;
            } else {
                tilelayer = 1;
            }
            /* Initialise default values for tile layer */
            strcpy(url,"");
            strcpy(fileExtension,"png");
            strcpy(mimeType,"image/png");
            description = NULL;
            attribution = malloc(sizeof(char)*(strlen(DEFAULT_ATTRIBUTION) + 1));
            strcpy(attribution,DEFAULT_ATTRIBUTION);
            hostnames = NULL;
            noHostnames = 0;
            minzoom = 0;
            maxzoom = MAX_ZOOM;
        } else if (sscanf(line, "%[^=]=%[^;#]", key, value) == 2
               ||  sscanf(line, "%[^=]=\"%[^\"]\"", key, value) == 2) {

            if (!strcmp(key, "URI")){
                if (strlen(value) >= PATH_MAX){
                    return "URI too long";
                }
                strcpy(url, value);
            }
            if (!strcmp(key, "TYPE")){
                if (strlen(value) >= PATH_MAX){
                    return "TYPE too long";
                }
                if (sscanf(value, "%[^ ] %[^;#]", fileExtension, mimeType) != 2) {
                    return "TYPE is not correctly parsable";
                }
            }
            if (!strcmp(key, "DESCRIPTION")){
                description = malloc(sizeof(char) * (strlen(value) + 1));
                strcpy(description, value);
            }
            if (!strcmp(key, "ATTRIBUTION")){
                attribution = malloc(sizeof(char) * (strlen(value) + 1));
                strcpy(attribution, value);
            }
            if (!strcmp(key, "CORS")){
                cors = malloc(sizeof(char) * (strlen(value) + 1));
                strcpy(cors, value);
            }
            if (!strcmp(key, "SERVER_ALIAS")){
                if (hostnames == NULL) {
                    noHostnames = 1;
                    hostnames = malloc((noHostnames + 1) * sizeof(char *));
                } else {
                    hostnames_tmp = hostnames;
                    hostnames = malloc((noHostnames + 1) * sizeof(char *));
                    memcpy(hostnames, hostnames_tmp,sizeof(char *) * noHostnames);
                    free(hostnames_tmp);
                    noHostnames++;
                }
                hostnames[noHostnames - 1] = malloc(sizeof(char)*(strlen(value) + 1));
                strcpy(hostnames[noHostnames - 1], value);
            }
            if (!strcmp(key, "MINZOOM")){
                minzoom = atoi(value);
            }
            if (!strcmp(key, "MAXZOOM")){
                maxzoom = atoi(value);
            }
        }
    }
    if (tilelayer == 1) {
        ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, cmd->server,
                "Committing tile config %s", xmlname);
        result = _add_tile_config(cmd, mconfig, url, xmlname, minzoom, maxzoom, fileExtension, mimeType,
                                  description,attribution,noHostnames,hostnames, cors);
        if (result != NULL) return result;
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

static const char *mod_tile_request_timeout_missing_config(cmd_parms *cmd, void *mconfig, const char *request_timeout_string)
{
    int request_timeout;

    if (sscanf(request_timeout_string, "%d", &request_timeout) != 1) {
        return "ModTileMissingRequestTimeout needs integer argument";
    }

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->request_timeout_priority = request_timeout;
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

static const char *mod_tile_cache_extended_host_name_config(cmd_parms *cmd, void *mconfig, const char *cache_extended_hostname)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    strncpy(scfg->cache_extended_hostname, cache_extended_hostname, PATH_MAX-1);
    scfg->cache_extended_hostname[PATH_MAX-1] = 0;

    return NULL;
}

static const char *mod_tile_cache_extended_duration_config(cmd_parms *cmd, void *mconfig, const char *cache_duration_string)
{
    int cache_duration;
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    if (sscanf(cache_duration_string, "%d", &cache_duration) != 1) {
            return "ModTileCacheExtendedDuration needs integer argument";
    }
    scfg->cache_extended_duration = cache_duration;

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

static const char *mod_tile_enable_stats(cmd_parms *cmd, void *mconfig, int enableStats)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->enableGlobalStats = enableStats;
    return NULL;
}

static const char *mod_tile_enable_throttling(cmd_parms *cmd, void *mconfig, int enableThrottling)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->enableTileThrottling = enableThrottling;
    return NULL;
}

static const char *mod_tile_bulk_mode(cmd_parms *cmd, void *mconfig, int bulkMode)
{
    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    scfg->bulkMode = bulkMode;
    return NULL;
}

static const char *mod_tile_delaypool_tiles_config(cmd_parms *cmd, void *mconfig, const char *bucketsize_string, const char *topuprate_string)
{
    int bucketsize;
    float topuprate;

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    if (sscanf(bucketsize_string, "%d", &bucketsize) != 1) {
            return "ModTileThrottlingTiles needs two numerical arguments, the first one must be integer";
    }
    if (sscanf(topuprate_string, "%f", &topuprate) != 1) {
            return "ModTileThrottlingTiles needs two numerical arguments, the first one must be integer";
    }
    scfg->delaypoolTileSize = bucketsize;

    /*Convert topup rate into microseconds per tile */
    scfg->delaypoolTileRate = (long)(1000000.0/topuprate);

    return NULL;
}

static const char *mod_tile_delaypool_render_config(cmd_parms *cmd, void *mconfig, const char *bucketsize_string, const char *topuprate_string)
{
    int bucketsize;
    float topuprate;

    tile_server_conf *scfg = ap_get_module_config(cmd->server->module_config, &tile_module);
    if (sscanf(bucketsize_string, "%d", &bucketsize) != 1) {
            return "ModTileThrottlingRenders needs two numerical arguments, the first one must be integer";
    }
    if (sscanf(topuprate_string, "%f", &topuprate) != 1) {
            return "ModTileThrottlingRenders needs two numerical arguments, the first one must be integer";
    }
    scfg->delaypoolRenderSize = bucketsize;

    /*Convert topup rate into microseconds per tile */
    scfg->delaypoolRenderRate = (long)(1000000.0/topuprate);

    return NULL;
}

static void *create_tile_config(apr_pool_t *p, server_rec *s)
{
    tile_server_conf * scfg = (tile_server_conf *) apr_pcalloc(p, sizeof(tile_server_conf));

    scfg->configs = apr_array_make(p, 4, sizeof(tile_config_rec));
    scfg->request_timeout = REQUEST_TIMEOUT;
    scfg->request_timeout_priority = REQUEST_TIMEOUT;
    scfg->max_load_old = MAX_LOAD_OLD;
    scfg->max_load_missing = MAX_LOAD_MISSING;
    strncpy(scfg->renderd_socket_name, RENDER_SOCKET, PATH_MAX-1);
    scfg->renderd_socket_name[PATH_MAX-1] = 0;
    strncpy(scfg->tile_dir, HASH_PATH, PATH_MAX-1);
    scfg->tile_dir[PATH_MAX-1] = 0;
    memset(&(scfg->cache_extended_hostname),0,PATH_MAX);
    scfg->cache_extended_duration = 0;
    scfg->cache_duration_dirty = 15*60;
    scfg->cache_duration_last_modified_factor = 0.0;
    scfg->cache_duration_max = 7*24*60*60;
    scfg->cache_duration_minimum = 3*60*60;
    scfg->cache_duration_low_zoom = 6*24*60*60;
    scfg->cache_duration_medium_zoom = 1*24*60*60;
    scfg->cache_level_low_zoom = 0;
    scfg->cache_level_medium_zoom = 0;
    scfg->enableGlobalStats = 1;
    scfg->enableTileThrottling = 0;
    scfg->delaypoolTileSize = AVAILABLE_TILE_BUCKET_SIZE;
    scfg->delaypoolTileRate = RENDER_TOPUP_RATE;
    scfg->delaypoolRenderSize = AVAILABLE_RENDER_BUCKET_SIZE;
    scfg->delaypoolRenderRate = RENDER_TOPUP_RATE;
    scfg->bulkMode = 0;


    return scfg;
}

static void *merge_tile_config(apr_pool_t *p, void *basev, void *overridesv)
{
    int i;
    tile_server_conf * scfg = (tile_server_conf *) apr_pcalloc(p, sizeof(tile_server_conf));
    tile_server_conf * scfg_base = (tile_server_conf *) basev;
    tile_server_conf * scfg_over = (tile_server_conf *) overridesv;

    scfg->configs = apr_array_append(p, scfg_base->configs, scfg_over->configs);
    scfg->request_timeout = scfg_over->request_timeout;
    scfg->request_timeout_priority = scfg_over->request_timeout_priority;
    scfg->max_load_old = scfg_over->max_load_old;
    scfg->max_load_missing = scfg_over->max_load_missing;
    strncpy(scfg->renderd_socket_name, scfg_over->renderd_socket_name, PATH_MAX-1);
    scfg->renderd_socket_name[PATH_MAX-1] = 0;
    strncpy(scfg->tile_dir, scfg_over->tile_dir, PATH_MAX-1);
    scfg->tile_dir[PATH_MAX-1] = 0;
    strncpy(scfg->cache_extended_hostname, scfg_over->cache_extended_hostname, PATH_MAX-1);
    scfg->cache_extended_hostname[PATH_MAX-1] = 0;
    scfg->cache_extended_duration = scfg_over->cache_extended_duration;
    scfg->cache_duration_dirty = scfg_over->cache_duration_dirty;
    scfg->cache_duration_last_modified_factor = scfg_over->cache_duration_last_modified_factor;
    scfg->cache_duration_max = scfg_over->cache_duration_max;
    scfg->cache_duration_minimum = scfg_over->cache_duration_minimum;
    scfg->cache_duration_low_zoom = scfg_over->cache_duration_low_zoom;
    scfg->cache_duration_medium_zoom = scfg_over->cache_duration_medium_zoom;
    scfg->cache_level_low_zoom = scfg_over->cache_level_low_zoom;
    scfg->cache_level_medium_zoom = scfg_over->cache_level_medium_zoom;
    scfg->enableGlobalStats = scfg_over->enableGlobalStats;
    scfg->enableTileThrottling = scfg_over->enableTileThrottling;
    scfg->delaypoolTileSize = scfg_over->delaypoolTileSize;
    scfg->delaypoolTileRate = scfg_over->delaypoolTileRate;
    scfg->delaypoolRenderSize = scfg_over->delaypoolRenderSize;
    scfg->delaypoolRenderRate = scfg_over->delaypoolRenderRate;
    scfg->bulkMode = scfg_over->bulkMode;

    //Construct a table of minimum cache times per zoom level
    for (i = 0; i <= MAX_ZOOM_SERVER; i++) {
        if (i <= scfg->cache_level_low_zoom) {
            scfg->mincachetime[i] = scfg->cache_duration_low_zoom;
        } else if (i <= scfg->cache_level_medium_zoom) {
            scfg->mincachetime[i] = scfg->cache_duration_medium_zoom;
        } else {
            scfg->mincachetime[i] = scfg->cache_duration_minimum;
        }
    }

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
    AP_INIT_TAKE3(
            "AddTileMimeConfig",         /* directive name */
            add_tile_mime_config,        /* config action routine */
            NULL,                        /* argument to include in call */
            OR_OPTIONS,                  /* where available */
            "path, name and file extension of renderd config to use"  /* directive description */
        ),
    AP_INIT_TAKE1(
        "ModTileRequestTimeout",         /* directive name */
        mod_tile_request_timeout_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set timeout in seconds on mod_tile requests"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileMissingRequestTimeout",         /* directive name */
        mod_tile_request_timeout_missing_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set timeout in seconds on missing mod_tile requests"  /* directive description */
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
        "ModTileCacheExtendedHostName",                /* directive name */
        mod_tile_cache_extended_host_name_config,        /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "set hostname for extended period caching"  /* directive description */
    ),
    AP_INIT_TAKE1(
        "ModTileCacheExtendedDuration",                /* directive name */
        mod_tile_cache_extended_duration_config,        /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "set length of extended period caching"  /* directive description */
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
        "ModTileCacheDurationMediumZoom", /* directive name */
        mod_tile_cache_duration_medium_config,                 /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the minimum cache duration and zoom level for medium zoom tiles"  /* directive description */
    ),
    AP_INIT_FLAG(
        "ModTileEnableStats",            /* directive name */
        mod_tile_enable_stats,           /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "On Off - enable of keeping stats about what mod_tile is serving"  /* directive description */
    ),
    AP_INIT_FLAG(
        "ModTileEnableTileThrottling",   /* directive name */
        mod_tile_enable_throttling,      /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "On Off - enable of throttling of IPs that excessively download tiles such as scrapers"  /* directive description */
    ),
    AP_INIT_TAKE2(
        "ModTileThrottlingTiles",        /* directive name */
        mod_tile_delaypool_tiles_config, /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the initial bucket size (number of tiles) and top up rate (tiles per second) for throttling tile request per IP"  /* directive description */
    ),
    AP_INIT_TAKE2(
        "ModTileThrottlingRenders",      /* directive name */
        mod_tile_delaypool_render_config,/* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "Set the initial bucket size (number of tiles) and top up rate (tiles per second) for throttling tile request per IP"  /* directive description */
    ),
    AP_INIT_FLAG(
        "ModTileBulkMode",               /* directive name */
        mod_tile_bulk_mode,              /* config action routine */
        NULL,                            /* argument to include in call */
        OR_OPTIONS,                      /* where available */
        "On Off - make all requests to renderd with bulk render priority, never mark tiles dirty"  /* directive description */
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

