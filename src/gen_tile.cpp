#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/datasource.hpp>
#include <mapnik/feature_type_style.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/image_util.hpp>

#include <exception>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>
#include <string>
#include <stdlib.h>

#include "gen_tile.h"
#include "render_config.h"
#include "daemon.h"
#include "store.h"
#include "metatile.h"
#include "protocol.h"
#include "request_queue.h"
#include "cache_expire.h"
#include "parameterize_style.hpp"

#ifdef HTCP_EXPIRE_CACHE
#include <sys/socket.h>
#include <netdb.h>
#endif

#if MAPNIK_VERSION >= 300000
    #define image_data_32 image_rgba8
    #define image_32 image_rgba8
    #include <mapnik/image.hpp>
    #include <mapnik/image_view_any.hpp>
#else
    #include <mapnik/graphics.hpp>
    #if MAPNIK_VERSION < 200000
        #include <mapnik/envelope.hpp>
        #define image_32 Image32
        #define image_data_32 ImageData32
        #define box2d Envelope
        #define zoom_to_box zoomToBox
    #else
        #include <mapnik/box2d.hpp>
    #endif
#endif


using namespace mapnik;
#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI/180)
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG (180/M_PI)
#endif

#ifdef METATILE
#define RENDER_SIZE (256 * (METATILE + 1))
#else
#define RENDER_SIZE (512)
#endif

struct projectionconfig {
    double bound_x0;
    double bound_y0;
    double bound_x1;
    double bound_y1;
    int    aspect_x;
    int    aspect_y;
};

struct xmlmapconfig {
    char xmlname[XMLCONFIG_MAX];
    char xmlfile[PATH_MAX];
    struct storage_backend * store;
    Map map;
    struct projectionconfig * prj;
    char xmluri[PATH_MAX];
    char host[PATH_MAX];
    char htcphost[PATH_MAX];
    int htcpsock;
    int tilesize;
    double scale;
    int minzoom;
    int maxzoom;
    int ok;
    parameterize_function_ptr parameterize_function; 
    xmlmapconfig() :
        map(256,256) {}
};


struct projectionconfig * get_projection(const char * srs) {
    struct projectionconfig * prj;

    if (strstr(srs,"+proj=merc +a=6378137 +b=6378137") != NULL) {
        syslog(LOG_DEBUG, "Using web mercator projection settings");
        prj = (struct projectionconfig *)malloc(sizeof(struct projectionconfig));
        prj->bound_x0 = -20037508.3428;
        prj->bound_x1 =  20037508.3428;
        prj->bound_y0 = -20037508.3428;
        prj->bound_y1 =  20037508.3428;
        prj->aspect_x = 1;
        prj->aspect_y = 1;
    } else if (strcmp(srs, "+proj=eqc +lat_ts=0 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs") == 0) {
        syslog(LOG_DEBUG, "Using plate carree projection settings");
        prj = (struct projectionconfig *)malloc(sizeof(struct projectionconfig));
        prj->bound_x0 = -20037508.3428;
        prj->bound_x1 =  20037508.3428;
        prj->bound_y0 = -10018754.1714;
        prj->bound_y1 =  10018754.1714;
        prj->aspect_x = 2;
        prj->aspect_y = 1;
    } else if (strcmp(srs, "+proj=tmerc +lat_0=49 +lon_0=-2 +k=0.9996012717 +x_0=400000 +y_0=-100000 +ellps=airy +datum=OSGB36 +units=m +no_defs") == 0) {
        syslog(LOG_DEBUG, "Using bng projection settings");
        prj = (struct projectionconfig *)malloc(sizeof(struct projectionconfig));
        prj->bound_x0 = 0;
        prj->bound_y0 = 0;
        prj->bound_x1 = 700000;
        prj->bound_y1 = 1400000;
        prj->aspect_x = 1;
        prj->aspect_y = 2;
    } else {
        syslog(LOG_WARNING, "Unknown projection string, using web mercator as never the less. %s", srs);
        prj = (struct projectionconfig *)malloc(sizeof(struct projectionconfig));
        prj->bound_x0 = -20037508.3428;
        prj->bound_x1 =  20037508.3428;
        prj->bound_y0 = -20037508.3428;
        prj->bound_y1 =  20037508.3428;
        prj->aspect_x = 1;
        prj->aspect_y = 1;
    }

    return prj;
}

static void load_fonts(const char *font_dir, int recurse)
{
    DIR *fonts = opendir(font_dir);
    struct dirent *entry;
    char path[PATH_MAX]; // FIXME: Eats lots of stack space when recursive

    if (!fonts) {
        syslog(LOG_CRIT, "Unable to open font directory: %s", font_dir);
        return;
    }

    while ((entry = readdir(fonts))) {
        struct stat b;
        char *p;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(path, sizeof(path), "%s/%s", font_dir, entry->d_name);
        if (stat(path, &b))
            continue;
        if (S_ISDIR(b.st_mode)) {
            if (recurse)
                load_fonts(path, recurse);
            continue;
        }
        p = strrchr(path, '.');
        if (p && (!strcmp(p, ".ttf") || !strcmp(p, ".otf") || !strcmp(p, ".ttc"))) {
            syslog(LOG_DEBUG, "DEBUG: Loading font: %s", path);
            freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}

/**
 * Set the connection pool size of mapnik's datasources  to correspond to the number of
 * rendering threads used in renderd
 **/
static void parameterize_map_max_connections(Map &m, int num_threads) {
    unsigned int i;
    char * tmp = (char *)malloc(20);
    for (i = 0; i < m.layer_count(); i++) {
#if MAPNIK_VERSION >= 300000
        layer& l = m.get_layer(i);
#else
        layer& l = m.getLayer(i);
#endif
        parameters params = l.datasource()->params();
        if (params.find("max_size") == params.end()) {
            sprintf(tmp, "%i", num_threads + 2);
            params["max_size"] = std::string(tmp);
        }
#if MAPNIK_VERSION >= 200200
        l.set_datasource(datasource_cache::instance().create(params));
#else
        l.set_datasource(datasource_cache::instance()->create(params));
#endif
    }
    free(tmp);
}


static int check_xyz(int x, int y, int z, struct xmlmapconfig * map) {
    int oob, limit;

    // Validate tile co-ordinates
    oob = (z < map->minzoom || z > map->maxzoom);
    if (!oob) {
         // valid x/y for tiles are 0 ... 2^zoom-1
        limit = (1 << z);
        oob =  (x < 0 || x > (limit * map->prj->aspect_x - 1) || y < 0 || y > (limit * map->prj->aspect_y - 1));
    }

    if (oob)
        syslog(LOG_INFO, "got bad co-ords: x(%d) y(%d) z(%d)\n", x, y, z);

    return !oob;
}

#ifdef METATILE
mapnik::box2d<double> tile2prjbounds(struct projectionconfig * prj, int x, int y, int z) {

    int render_size_tx = MIN(METATILE, prj->aspect_x * (1 << z));
    int render_size_ty = MIN(METATILE, prj->aspect_y * (1 << z));

    double p0x = prj->bound_x0 + (prj->bound_x1 - prj->bound_x0)* ((double)x / (double)(prj->aspect_x * 1<<z));
    double p0y = (prj->bound_y1 - (prj->bound_y1 - prj->bound_y0)* (((double)y + render_size_ty) / (double)(prj->aspect_y * 1<<z)));
    double p1x = prj->bound_x0 + (prj->bound_x1 - prj->bound_x0)* (((double)x + render_size_tx) / (double)(prj->aspect_x * 1<<z));
    double p1y = (prj->bound_y1 - (prj->bound_y1 - prj->bound_y0)* ((double)y / (double)(prj->aspect_y * 1<<z)));

    syslog(LOG_DEBUG, "Rendering projected coordinates %i %i %i -> %f|%f %f|%f to a %i x %i tile\n", z, x, y, p0x, p0y, p1x, p1y, render_size_tx, render_size_ty);

    mapnik::box2d<double> bbox(p0x, p0y, p1x,p1y);
    return  bbox;
}

static enum protoCmd render(struct xmlmapconfig * map, int x, int y, int z, char *options, metaTile &tiles)
{
    unsigned int render_size_tx = MIN(METATILE, map->prj->aspect_x * (1 << z));
    unsigned int render_size_ty = MIN(METATILE, map->prj->aspect_y * (1 << z));

    map->map.resize(render_size_tx*map->tilesize, render_size_ty*map->tilesize);
    map->map.zoom_to_box(tile2prjbounds(map->prj, x, y, z));
    if (map->map.buffer_size() == 0) { // Only set buffer size if the buffer size isn't explicitly set in the mapnik stylesheet.
        map->map.set_buffer_size((map->tilesize >> 1) * map->scale);
    }
    //m.zoom(size+1);

    mapnik::image_32 buf(render_size_tx*map->tilesize, render_size_ty*map->tilesize);
    try {
        Map map_parameterized = map->map; 
        if (map->parameterize_function) 
            map->parameterize_function(map_parameterized, options); 
        mapnik::agg_renderer<mapnik::image_32> ren(map_parameterized,buf,map->scale);
        ren.apply();
    } catch (std::exception const& ex) {
      syslog(LOG_ERR, "ERROR: failed to render TILE %s %d %d-%d %d-%d", map->xmlname, z, x, x+render_size_tx-1, y, y+render_size_ty-1);
      syslog(LOG_ERR, "   reason: %s", ex.what());
      return cmdNotDone;
    }

    // Split the meta tile into an NxN grid of tiles
    unsigned int xx, yy;
    for (yy = 0; yy < render_size_ty; yy++) {
        for (xx = 0; xx < render_size_tx; xx++) {
#if MAPNIK_VERSION >= 300000
            mapnik::image_view<mapnik::image<mapnik::rgba8_t>> vw1(xx * map->tilesize, yy * map->tilesize, map->tilesize, map->tilesize, buf);
            struct mapnik::image_view_any vw(vw1);
#else
            mapnik::image_view<mapnik::image_data_32> vw(xx * map->tilesize, yy * map->tilesize, map->tilesize, map->tilesize, buf.data());
#endif
            tiles.set(xx, yy, save_to_string(vw, "png256"));
        }
    }
    return cmdDone; // OK
}
#else //METATILE
static enum protoCmd render(Map &m, const char *tile_dir, char *xmlname, projection &prj, int x, int y, int z)
{
    char filename[PATH_MAX];
    char tmp[PATH_MAX];
    double p0x = x * 256.0;
    double p0y = (y + 1) * 256.0;
    double p1x = (x + 1) * 256.0;
    double p1y = y * 256.0;

    tiling.fromPixelToLL(p0x, p0y, z);
    tiling.fromPixelToLL(p1x, p1y, z);

    prj.forward(p0x, p0y);
    prj.forward(p1x, p1y);

    mapnik::box2d<double> bbox(p0x, p0y, p1x,p1y);
    bbox.width(bbox.width() * 2);
    bbox.height(bbox.height() * 2);
    m.zoomToBox(bbox);

    mapnik::image_32 buf(RENDER_SIZE, RENDER_SIZE);
    mapnik::agg_renderer<mapnik::image_32> ren(m,buf);
    ren.apply();

    xyz_to_path(filename, sizeof(filename), tile_dir, xmlname, x, y, z);
    if (mkdirp(filename))
        return cmdNotDone;
    snprintf(tmp, sizeof(tmp), "%s.tmp", filename);

    mapnik::image_view<mapnik::image_data_32> vw(128, 128, 256, 256, buf.data());
    //std::cout << "Render " << z << " " << x << " " << y << " " << filename << "\n";
    mapnik::save_to_file(vw, tmp, "png256");
    if (rename(tmp, filename)) {
        perror(tmp);
        return cmdNotDone;
    }

    return cmdDone; // OK
}
#endif //METATILE


void render_init(const char *plugins_dir, const char* font_dir, int font_dir_recurse)
{
  syslog(LOG_INFO, "Renderd is using mapnik version %i.%i.%i", ((MAPNIK_VERSION) / 100000), (((MAPNIK_VERSION) / 100) % 1000), ((MAPNIK_VERSION) % 100));
#if MAPNIK_VERSION >= 200200
    mapnik::datasource_cache::instance().register_datasources(plugins_dir);
#else
    mapnik::datasource_cache::instance()->register_datasources(plugins_dir);
#endif
    load_fonts(font_dir, font_dir_recurse);
}

void *render_thread(void * arg)
{
    xmlconfigitem * parentxmlconfig = (xmlconfigitem *)arg;
    xmlmapconfig maps[XMLCONFIGS_MAX];
    int i,iMaxConfigs;
    int render_time;

    for (iMaxConfigs = 0; iMaxConfigs < XMLCONFIGS_MAX; ++iMaxConfigs) {
        if (parentxmlconfig[iMaxConfigs].xmlname[0] == 0 || parentxmlconfig[iMaxConfigs].xmlfile[0] == 0) break;
        strcpy(maps[iMaxConfigs].xmlname, parentxmlconfig[iMaxConfigs].xmlname);
        strcpy(maps[iMaxConfigs].xmlfile, parentxmlconfig[iMaxConfigs].xmlfile);
        maps[iMaxConfigs].store = init_storage_backend(parentxmlconfig[iMaxConfigs].tile_dir);
        maps[iMaxConfigs].tilesize  = parentxmlconfig[iMaxConfigs].tile_px_size;
        maps[iMaxConfigs].scale  = parentxmlconfig[iMaxConfigs].scale_factor;
        maps[iMaxConfigs].minzoom = parentxmlconfig[iMaxConfigs].min_zoom;
        maps[iMaxConfigs].maxzoom = parentxmlconfig[iMaxConfigs].max_zoom;
        maps[iMaxConfigs].parameterize_function = init_parameterization_function(parentxmlconfig[iMaxConfigs].parameterization);


        if (maps[iMaxConfigs].store) {
            maps[iMaxConfigs].ok = 1;

            maps[iMaxConfigs].map.resize(RENDER_SIZE, RENDER_SIZE);

            try {
                mapnik::load_map(maps[iMaxConfigs].map, maps[iMaxConfigs].xmlfile);
                /* If we have more than 10 rendering threads configured, we need to fix
                 * up the mapnik datasources to support larger postgres connection pools
                 */
                if (parentxmlconfig[iMaxConfigs].num_threads > 10) {
                    syslog(LOG_INFO, "Updating max_connection parameter for mapnik layers to reflect thread count");
                    parameterize_map_max_connections(maps[iMaxConfigs].map, parentxmlconfig[iMaxConfigs].num_threads);
                }
                maps[iMaxConfigs].prj = get_projection(maps[iMaxConfigs].map.srs().c_str());
            } catch (std::exception const& ex) {
                syslog(LOG_ERR, "An error occurred while loading the map layer '%s': %s", maps[iMaxConfigs].xmlname, ex.what());
                maps[iMaxConfigs].ok = 0;
            } catch (...) {
                syslog(LOG_ERR, "An unknown error occurred while loading the map layer '%s'", maps[iMaxConfigs].xmlname);
                maps[iMaxConfigs].ok = 0;
            }

#ifdef HTCP_EXPIRE_CACHE
            strcpy(maps[iMaxConfigs].xmluri, parentxmlconfig[iMaxConfigs].xmluri);
            strcpy(maps[iMaxConfigs].host, parentxmlconfig[iMaxConfigs].host);
            strcpy(maps[iMaxConfigs].htcphost, parentxmlconfig[iMaxConfigs].htcpip);
            if (strlen(maps[iMaxConfigs].htcphost) > 0) {
                maps[iMaxConfigs].htcpsock = init_cache_expire(
                        maps[iMaxConfigs].htcphost);
                if (maps[iMaxConfigs].htcpsock > 0) {
                    syslog(LOG_INFO, "Successfully opened socket for HTCP cache expiry");
                } else {
                    syslog(LOG_ERR, "Failed to opened socket for HTCP cache expiry");
                }
            } else {
                maps[iMaxConfigs].htcpsock = -1;
            }

#endif
        } else
            maps[iMaxConfigs].ok = 0;
    }

    while (1) {
        enum protoCmd ret;
        struct item *item = request_queue_fetch_request(render_request_queue);
        render_time = -1;
        if (item) {
            struct protocol *req = &item->req;
#ifdef METATILE
            // At very low zoom the whole world may be smaller than METATILE
            unsigned int size = MIN(METATILE, 1 << req->z);
            for (i = 0; i < iMaxConfigs; ++i) {
                if (!strcmp(maps[i].xmlname, req->xmlname)) {
                    if (maps[i].ok) {
                        if (check_xyz(item->mx, item->my, req->z, &(maps[i]))) {

                            metaTile tiles(req->xmlname, req->options, item->mx, item->my, req->z);

                            timeval tim;
                            gettimeofday(&tim, NULL);
                            long t1=tim.tv_sec*1000+(tim.tv_usec/1000);

                            struct stat_info sinfo = maps[i].store->tile_stat(maps[i].store, req->xmlname, req->options, item->mx, item->my, req->z);

                            if(sinfo.size > 0)
                                syslog(LOG_DEBUG, "DEBUG: START TILE %s %d %d-%d %d-%d, age %.2f days",
                                       req->xmlname, req->z, item->mx, item->mx+size-1, item->my, item->my+size-1,
                                       (tim.tv_sec-sinfo.mtime)/86400.0);
                            else
                                syslog(LOG_DEBUG, "DEBUG: START TILE %s %d %d-%d %d-%d, new metatile",
                                       req->xmlname, req->z, item->mx, item->mx+size-1, item->my, item->my+size-1);

                            ret = render(&(maps[i]), item->mx, item->my, req->z, req->options, tiles);

                            gettimeofday(&tim, NULL);
                            long t2=tim.tv_sec*1000+(tim.tv_usec/1000);

                            syslog(LOG_DEBUG, "DEBUG: DONE TILE %s %d %d-%d %d-%d in %.3lf seconds",
                                    req->xmlname, req->z, item->mx, item->mx+size-1, item->my, item->my+size-1, (t2 - t1)/1000.0);

                            render_time = t2 - t1;

                            if (ret == cmdDone) {
                                try {
                                    tiles.save(maps[i].store);
#ifdef HTCP_EXPIRE_CACHE
                                    tiles.expire_tiles(maps[i].htcpsock,maps[i].host,maps[i].xmluri);
#endif

                                } catch (std::exception const& ex) {
                                    syslog(LOG_ERR, "Received exception when writing metatile to disk: %s", ex.what());
                                    ret = cmdNotDone;
                                } catch (...) {
                                    // Treat any error as fatal and request end of processing
                                    syslog(LOG_ERR, "Failed writing metatile to disk with unknown error, requesting exit.");
                                    ret = cmdNotDone;
                                    request_exit();
                                }
                            }
#else //METATILE
                        ret = render(maps[i].map, maps[i].tile_dir, req->xmlname, maps[i].prj, req->x, req->y, req->z);
#ifdef HTCP_EXPIRE_CACHE
                        cache_expire(maps[i].htcpsock,maps[i].host, maps[i].xmluri, req->x,req->y,req->z);
#endif
#endif //METATILE
                        } else {
                            syslog(LOG_WARNING, "Received request for map layer %s is outside of acceptable bounds z(%i), x(%i), y(%i)",
                                    req->xmlname, req->z, req->x, req->y);
                            ret = cmdIgnore;
                        }
                    } else {
                        syslog(LOG_ERR, "Received request for map layer '%s' which failed to load", req->xmlname);
                        ret = cmdNotDone;
                    }
                    send_response(item, ret, render_time);
                    if ((ret != cmdDone) && (ret != cmdIgnore)) sleep(10); //Something went wrong with rendering, delay next processing to allow temporary issues to fix them selves
                    break;
               }
            }
            if (i == iMaxConfigs){
                syslog(LOG_ERR, "No map for: %s", req->xmlname);
            }
        } else {
            sleep(1); // TODO: Use an event to indicate there are new requests
        }
    }
    return NULL;
}

