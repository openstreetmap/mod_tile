#ifndef MODTILE_H 
#define MODTILE_H

#include "store.h"

/*Size of the delaypool hashtable*/
#define DELAY_HASHTABLE_SIZE 100057
#define DELAY_HASHTABLE_WHITELIST_SIZE 13
/*Number of tiles in the bucket */
#define AVAILABLE_TILE_BUCKET_SIZE 5000
/*Number of render request in the bucket */
#define AVAILABLE_RENDER_BUCKET_SIZE 65
/*Number of microseconds per render request. Currently set at no more than 1 render request per 5 seconds on average */
#define RENDER_TOPUP_RATE 5000000l
/*Number of microseconds per render request. Currently set at no more than 1 request per second on average */
#define TILE_TOPUP_RATE 1000000l

#define INILINE_MAX 256

#define MAX_ZOOM_SERVER 30

#define FRESH 1
#define OLD 2
#define FRESH_RENDER 3
#define OLD_RENDER 4
#define VERYOLD_RENDER 5
#define VERYOLD 6


/* Number of microseconds to camp out on the mutex */
#define CAMPOUT 10
/* Maximum number of times we camp out before giving up */
#define MAXCAMP 10

#define DEFAULT_ATTRIBUTION "&copy;<a href=\"http://www.openstreetmap.org/\">OpenStreetMap</a> and <a href=\"http://wiki.openstreetmap.org/wiki/Contributors\">contributors</a>, <a href=\"http://opendatacommons.org/licenses/odbl/\">(ODbL)</a>"

typedef struct delaypool_entry {
	struct in6_addr ip_addr;
	int available_tiles;
	int available_render_req;
} delaypool_entry;

typedef struct delaypool {
	delaypool_entry users[DELAY_HASHTABLE_SIZE];
	in_addr_t whitelist[DELAY_HASHTABLE_WHITELIST_SIZE];
	apr_time_t last_tile_fillup;
	apr_time_t last_render_fillup;
	int locked;
} delaypool;

typedef struct stats_data {
    apr_uint64_t noResp200;
    apr_uint64_t noResp304;
    apr_uint64_t noResp404;
	apr_uint64_t noResp503;
    apr_uint64_t noResp5XX;
    apr_uint64_t noRespOther;
    apr_uint64_t noFreshCache;
    apr_uint64_t noFreshRender;
    apr_uint64_t noOldCache;
    apr_uint64_t noOldRender;
    apr_uint64_t noVeryOldCache;
    apr_uint64_t noVeryOldRender;
	apr_uint64_t noRespZoom[MAX_ZOOM_SERVER + 1];
    apr_uint64_t totalBufferRetrievalTime;
    apr_uint64_t noTotalBufferRetrieval;
    apr_uint64_t zoomBufferRetrievalTime[MAX_ZOOM_SERVER + 1];
    apr_uint64_t noZoomBufferRetrieval[MAX_ZOOM_SERVER + 1];

    apr_uint64_t *noResp200Layer;
    apr_uint64_t *noResp404Layer;

} stats_data;

typedef struct {
    const char * store;
    char xmlname[XMLCONFIG_MAX];
    char baseuri[PATH_MAX];
    char fileExtension[PATH_MAX];
    char mimeType[PATH_MAX];
    const char * description;
    const char * attribution;
    const char * cors;
    char **hostnames;
    int noHostnames;
    int minzoom;
    int maxzoom;
    int aspect_x;
    int aspect_y;
    int enableOptions;
} tile_config_rec;

typedef struct {
    apr_array_header_t *configs;
    int request_timeout;
	int request_timeout_priority;
    int max_load_old;
    int max_load_missing;
    apr_time_t veryold_threshold;
    int cache_duration_dirty;
    int cache_duration_max;
    int cache_duration_minimum;
    int cache_duration_low_zoom;
    int cache_level_low_zoom;
    int cache_duration_medium_zoom;
    int cache_level_medium_zoom;
    double cache_duration_last_modified_factor;
    char renderd_socket_name[PATH_MAX];
    int renderd_socket_port;
    char tile_dir[PATH_MAX];
	char cache_extended_hostname[PATH_MAX];
    int  cache_extended_duration;
    int mincachetime[MAX_ZOOM_SERVER + 1];
    int enableGlobalStats;
	int enableTileThrottling;
    int enableTileThrottlingXForward;
	int delaypoolTileSize;
	long delaypoolTileRate;
	int delaypoolRenderSize;
	long delaypoolRenderRate;
    int bulkMode;
} tile_server_conf;

typedef struct tile_request_data {
	struct protocol * cmd;
    struct storage_backend * store;
	int layerNumber;
} tile_request_data;

enum tileState { tileMissing, tileOld, tileVeryOld, tileCurrent };


#endif
