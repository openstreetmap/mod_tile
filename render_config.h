#ifndef RENDER_CONFIG_H
#define RENDER_CONFIG_H

#define MAX_ZOOM 18
// MAX_SIZE is the biggest file which we will return to the user
#define MAX_SIZE (1 * 1024 * 1024)
// IMG_PATH must have blank.png etc.
#define WWW_ROOT "/var/www/html"
#define IMG_PATH "/img"
// TILE_PATH is where Openlayers with try to fetch the "z/x/y.png" tiles from
#define TILE_PATH "/osm_tiles2"
// With directory hashing enabled we rewrite the path so that tiles are really stored here instead
#define DIRECTORY_HASH
#define HASH_PATH "/direct"
// MAX_LOAD_OLD: if tile is out of date, don't re-render it if past this load threshold (users gets old tile)
#define MAX_LOAD_OLD 5
// MAX_LOAD_OLD: if tile is missing, don't render it if past this load threshold (user gets 404 error)
#define MAX_LOAD_MISSING 10
// MAX_LOAD_ANY: give up serving any data if beyond this load (user gets 404 error)
#define MAX_LOAD_ANY 100
// Maximum tile age in seconds
// TODO: this mechanism should really be a hard cutoff on planet update time.
#define MAX_AGE (48 * 60 * 60)

// Typical interval between planet imports, used as basis for tile expiry times
#define PLANET_INTERVAL (7 * 24 * 60 * 60)

// Planet import should touch this file when complete
#define PLANET_TIMESTAMP "/tmp/planet-import-complete"

// Timeout before giving for a tile to be rendered
#define REQUEST_TIMEOUT (5)
#define FD_INVALID (-1)


#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))

#define QUEUE_MAX (32)
#define MAX_CONNECTIONS (256)

#define REQ_LIMIT (32)
#define DIRTY_LIMIT (1000 * 1000)
#define NUM_THREADS (4)

// Use this to enable meta-tiles which will render NxN tiles at once
// Note: This should be a power of 2 (2, 4, 8, 16 ...)
#define METATILE (8)
//#undef METATILE

#endif
