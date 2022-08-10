/*
 * Copyright (c) 2007 - 2020 by mod_tile contributors (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see http://www.gnu.org/licenses/.
 */

#ifndef RENDER_CONFIG_H
#define RENDER_CONFIG_H

#define MAX_ZOOM 20

// MAX_SIZE is the biggest file which we will return to the user
#define MAX_SIZE (1 * 1024 * 1024)

// With directory hashing enabled we rewrite the path so that tiles are really stored here instead
#define DIRECTORY_HASH
#define HASH_PATH "/var/cache/renderd/tiles"

// TILE_PATH is where Openlayers with try to fetch the "z/x/y.png" tiles from
// this is now only used if DIRECTORY_HASH is undefined
//#define TILE_PATH "/var/www/html/osm_tiles2"

// MAX_LOAD_OLD: if tile is out of date, don't re-render it if past this load threshold (users gets old tile)
// (This is the default value. Can be overwritten in Apache config with ModTileMaxLoadOld.)
#define MAX_LOAD_OLD 16
// MAX_LOAD_MISSING: if tile is missing, don't render it if past this load threshold (user gets 404 error)
// (This is the default value. Can be overwritten in Apache config with ModTileMaxLoadMissing.)
#define MAX_LOAD_MISSING 50
// MAX_LOAD_ANY: give up serving any data if beyond this load (user gets 404 error)
#define MAX_LOAD_ANY 100

//VERYOLD_THRESHOLD: defines how old a tile needs to be (in microseconds) to get rendering priority rather than renderingLow priority
//1000000*3600*24*365 = 31536000000000
#define VERYOLD_THRESHOLD 31536000000000

// Location of osm.xml file
#define RENDERD_CONFIG "/etc/renderd.conf"
// The XML configuration used if one is not provided
#define XMLCONFIG_DEFAULT "default"
// Maximum number of configurations that mod tile will allow
#define XMLCONFIGS_MAX 100
// Default PID file path
#define PIDFILE "/run/renderd/renderd.pid"

// Mapnik input plugins (will need to adjust for 32 bit libs)
#define MAPNIK_PLUGINS "/usr/local/lib64/mapnik/input"

// Default directory to search for fonts. Recursion can be enabled if desired.
#define FONT_DIR "/usr/local/lib64/mapnik/fonts"
#define FONT_RECURSE 0

// Typical interval between planet imports, used as basis for tile expiry times
#define PLANET_INTERVAL (7 * 24 * 60 * 60)

// Planet import should touch this file when complete
#define PLANET_TIMESTAMP "/planet-import-complete"

// Timeout before giving for a tile to be rendered
// (This is the default value. Can be overwritten in Apache config with ModTileRequestTimeout.)
#define REQUEST_TIMEOUT (3)
#define FD_INVALID (-1)


#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))

#define MAX_CONNECTIONS (2048)

// default for number of rendering threads
#define NUM_THREADS (4)

// Use this to enable meta-tiles which will render NxN tiles at once
// Note: This should be a power of 2 (2, 4, 8, 16 ...)
#define METATILE (8)
//#undef METATILE

//Fallback to standard tiles if meta tile doesn't exist
//Legacy - not needed on new installs
//#undef METATILEFALLBACK

// Metatiles are much larger in size so we don't need big queues to handle large areas
#ifdef METATILE
#define QUEUE_MAX (64)
#define REQ_LIMIT (32)
#define DIRTY_LIMIT (1000)

#else
#define QUEUE_MAX (1024)
#define REQ_LIMIT (512)
#define DIRTY_LIMIT (10000)
#define HASHIDX_SIZE 22123
#endif

// Penalty for client making an invalid request (in seconds)
#define CLIENT_PENALTY (3)

#endif
