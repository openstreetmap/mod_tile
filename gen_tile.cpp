#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/graphics.hpp>
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

#include "gen_tile.h"
#include "render_config.h"
#include "daemon.h"
#include "store.h"
#include "metatile.h"
#include "protocol.h"

#ifdef HTCP_EXPIRE_CACHE
#include <sys/socket.h>
#include <netdb.h>
#endif

#if MAPNIK_VERSION < 200000
#include <mapnik/envelope.hpp>
#define image_32 Image32
#define image_data_32 ImageData32
#define box2d Envelope
#define zoom_to_box zoomToBox
#else
#include <mapnik/box2d.hpp>
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

static const int minZoom = 0;
static const int maxZoom = MAX_ZOOM;

struct xmlmapconfig {
    char xmlname[XMLCONFIG_MAX];
    char xmlfile[PATH_MAX];
    struct storage_backend * store;
    Map map;
    projection prj;
    char xmluri[PATH_MAX];
    char host[PATH_MAX];
    char htcphost[PATH_MAX];
    int htcpsock;
    int ok;
    xmlmapconfig() :
        prj("+init=epsg:4326"),
        map(256,256) {}
};


class SphericalProjection
{
    double *Ac, *Bc, *Cc, *zc;

    public:
        SphericalProjection(int levels=MAX_ZOOM) {
            Ac = new double[levels];
            Bc = new double[levels];
            Cc = new double[levels];
            zc = new double[levels];
            int d, c = 256;
            for (d=0; d<levels; d++) {
                int e = c/2;
                Bc[d] = c/360.0;
                Cc[d] = c/(2 * M_PI);
                zc[d] = e;
                Ac[d] = c;
                c *=2;
            }
        }
        ~SphericalProjection(){
            printf("Delteting SphericalProjection");
            delete [] Ac;
            delete [] Bc;
            delete [] Cc;
            delete [] zc;
        }

        void fromLLtoPixel(double &x, double &y, int zoom) {
            double d = zc[zoom];
            double f = minmax(sin(DEG_TO_RAD * y),-0.9999,0.9999);
            x = round(d + x * Bc[zoom]);
            y = round(d + 0.5*log((1+f)/(1-f))*-Cc[zoom]);
        }
        void fromPixelToLL(double &x, double &y, int zoom) {
            double e = zc[zoom];
            double g = (y - e)/-Cc[zoom];
            x = (x - e)/Bc[zoom];
            y = RAD_TO_DEG * ( 2 * atan(exp(g)) - 0.5 * M_PI);
        }

    private:
        double minmax(double a, double b, double c)
        {
            #define MIN(x,y) ((x)<(y)?(x):(y))
            #define MAX(x,y) ((x)>(y)?(x):(y))
            a = MAX(a,b);
            a = MIN(a,c);
            return a;
        }
};

static SphericalProjection tiling(maxZoom+1);

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
        if (p && !strcmp(p, ".ttf")) {
            syslog(LOG_DEBUG, "DEBUG: Loading font: %s", path);
            freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}

#ifdef HTCP_EXPIRE_CACHE

/**
 * This function sends a HTCP cache clr request for a given
 * URL.
 * RFC for HTCP can be found at http://www.htcp.org/
 */
void cache_expire_url(int sock, char * url) {
    char * buf;

    if (sock < 0) {
            return;
    }

    int idx = 0;
    int url_len;

    url_len = strlen(url);
    buf = (char *) malloc(12 + 22 + url_len);
    if (!buf) {
        return;
    }

    idx = 0;

    //16 bit: Overall length of the datagram packet, including this header
    *((uint16_t *) (&buf[idx])) = htons(12 + 22 + url_len);
    idx += 2;

    //HTCP version. Currently at 0.0
    buf[idx++] = 0; //Major version
    buf[idx++] = 0; //Minor version

    //Length of HTCP data, including this field
    *((uint16_t *) (&buf[idx])) = htons(8 + 22 + url_len);
    idx += 2;

    //HTCP opcode CLR=4
    buf[idx++] = 4;
    //Reserved
    buf[idx++] = 0;

    //32 bit transaction id;
    *((uint32_t *) (&buf[idx])) = htonl(255);
    idx += 4;

    buf[idx++] = 0;
    buf[idx++] = 0; //HTCP reason

    //Length of the Method string
    *((uint16_t *) (&buf[idx])) = htons(4);
    idx += 2;

    ///Method string
    memcpy(&buf[idx], "HEAD", 4);
    idx += 4;

    //Length of the url string
    *((uint16_t *) (&buf[idx])) = htons(url_len);
    idx += 2;

    //Url string
    memcpy(&buf[idx], url, url_len);
    idx += url_len;

    //Length of version string
    *((uint16_t *) (&buf[idx])) = htons(8);
    idx += 2;

    //version string
    memcpy(&buf[idx], "HTTP/1.1", 8);
    idx += 8;

    //Length of request headers. Currently 0 as we don't have any headers to send
    *((uint16_t *) (&buf[idx])) = htons(0);

    if (send(sock, (void *) buf, (12 + 22 + url_len), 0) < (12 + 22 + url_len)) {
        syslog(LOG_ERR, "Failed to send HTCP purge for %s\n", url);
    };

    free(buf);
}

void cache_expire(int sock, char * host, char * uri, int x, int y, int z) {

    if (sock < 0) {
        return;
    }
    char * url = (char *)malloc(1024);
    sprintf(url,"http://%s%s%i/%i/%i.png", host, uri, z,x,y);
    cache_expire_url(sock, url);
    free(url);
}

int init_cache_expire(char * htcphost) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd, s;

    /* Obtain address(es) matching host/port */

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol */

    s = getaddrinfo(htcphost, HTCP_EXPIRE_CACHE_PORT, &hints, &result);
    if (s != 0) {
        syslog(LOG_ERR, "Failed to lookup HTCP cache host: %s", gai_strerror(s));
        return -1;
    }

    /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; /* Success */

        close(sfd);
    }

    if (rp == NULL) { /* No address succeeded */
        syslog(LOG_ERR, "Failed to create HTCP cache socket");
        return -1;
    }

    freeaddrinfo(result); /* No longer needed */

    return sfd;

}
#endif



#ifdef METATILE

class metaTile {
    public:
        metaTile(const std::string &xmlconfig, int x, int y, int z):
            x_(x), y_(y), z_(z), xmlconfig_(xmlconfig)
        {
            clear();
        }

        void clear()
        {
            for (int x = 0; x < METATILE; x++)
                for (int y = 0; y < METATILE; y++)
                    tile[x][y] = "";
        }

        void set(int x, int y, const std::string &data)
        {
            tile[x][y] = data;
        }

        const std::string get(int x, int y)
        {
            return tile[x][y];
        }

        // Returns the offset within the meta-tile index table
        int xyz_to_meta_offset(int x, int y, int z)
        {
            unsigned char mask = METATILE - 1;
            return (x & mask) * METATILE + (y & mask);
        }

        void save(struct storage_backend * store)
        {
            int ox, oy, limit;
            size_t offset;
            struct meta_layout m;
            struct entry offsets[METATILE * METATILE];
            char * metatilebuffer;

            memset(&m, 0, sizeof(m));
            memset(&offsets, 0, sizeof(offsets));

            // Create and write header
            m.count = METATILE * METATILE;
            memcpy(m.magic, META_MAGIC, strlen(META_MAGIC));
            m.x = x_;
            m.y = y_;
            m.z = z_;
            
            offset = header_size;
            limit = (1 << z_);
            limit = MIN(limit, METATILE);
            
            // Generate offset table
            for (ox=0; ox < limit; ox++) {
                for (oy=0; oy < limit; oy++) {
                    int mt = xyz_to_meta_offset(x_ + ox, y_ + oy, z_);
                    offsets[mt].offset = offset;
                    offsets[mt].size   = tile[ox][oy].size();
                    offset += offsets[mt].size;
                }
            }

            metatilebuffer = (char *) malloc(offset);
            memset(metatilebuffer, 0, offset);
            memcpy(metatilebuffer,&m,sizeof(m));
            memcpy(metatilebuffer + sizeof(m), &offsets, sizeof(offsets));
            
            // Write tiles
            for (ox=0; ox < limit; ox++) {
                for (oy=0; oy < limit; oy++) {
                    memcpy(metatilebuffer + offsets[xyz_to_meta_offset(x_ + ox, y_ + oy, z_)].offset, (const void *)tile[ox][oy].data(), tile[ox][oy].size());
                }
            }
            
            store->metatile_write(store, xmlconfig_.c_str(),x_,y_,z_,metatilebuffer, offset);

        }

#ifdef HTCP_EXPIRE_CACHE
    void expire_tiles(int sock, char * host, char * uri) {
        if (sock < 0) {
            return;
        }
        syslog(LOG_INFO, "Purging metatile via HTCP cache expiry");
        int ox, oy;
        int limit = (1 << z_);
        limit = MIN(limit, METATILE);
        
        // Generate offset table
        for (ox=0; ox < limit; ox++) {
            for (oy=0; oy < limit; oy++) {
                cache_expire(sock, host, uri, (x_ + ox), (y_ + oy), z_);
            }
        }
    }
#endif
        int x_, y_, z_;
        std::string xmlconfig_;
        std::string tile[METATILE][METATILE];
        static const int header_size = sizeof(struct meta_layout) + (sizeof(struct entry) * (METATILE * METATILE));
};


static enum protoCmd render(Map &m, char *xmlname, projection &prj, int x, int y, int z, unsigned int size, metaTile &tiles)
{
    int render_size = 256 * size;
    double p0x = x * 256;
    double p0y = (y + size) * 256;
    double p1x = (x + size) * 256;
    double p1y = y * 256;

    //std::cout << "META TILE " << z << " " << x << "-" << x+size-1 << " " << y << "-" << y+size-1 << "\n";

    tiling.fromPixelToLL(p0x, p0y, z);
    tiling.fromPixelToLL(p1x, p1y, z);

    prj.forward(p0x, p0y);
    prj.forward(p1x, p1y);

    mapnik::box2d<double> bbox(p0x, p0y, p1x,p1y);
    m.resize(render_size, render_size);
    m.zoom_to_box(bbox);
    m.set_buffer_size(128);
    //m.zoom(size+1);

    mapnik::image_32 buf(render_size, render_size);
    try {
      mapnik::agg_renderer<mapnik::image_32> ren(m,buf);
      ren.apply();
    } catch (std::exception const& ex) {
      syslog(LOG_ERR, "ERROR: failed to render TILE %s %d %d-%d %d-%d", xmlname, z, x, x+size-1, y, y+size-1);
      syslog(LOG_ERR, "   reason: %s", ex.what());
      return cmdNotDone;
    }

    // Split the meta tile into an NxN grid of tiles
    unsigned int xx, yy;
    for (yy = 0; yy < size; yy++) {
        for (xx = 0; xx < size; xx++) {
            mapnik::image_view<mapnik::image_data_32> vw(xx * 256, yy * 256, 256, 256, buf.data());
            tiles.set(xx, yy, save_to_string(vw, "png256"));
        }
    }
//    std::cout << "DONE TILE " << xmlname << " " << z << " " << x << "-" << x+size-1 << " " << y << "-" << y+size-1 << "\n";
//    syslog(LOG_DEBUG, "DEBUG: DONE TILE %s %d %d-%d %d-%d", xmlname, z, x, x+size-1, y, y+size-1);
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

    for (iMaxConfigs = 0; iMaxConfigs < XMLCONFIGS_MAX; ++iMaxConfigs) {
        if (parentxmlconfig[iMaxConfigs].xmlname[0] == 0 || parentxmlconfig[iMaxConfigs].xmlfile[0] == 0) break;
        strcpy(maps[iMaxConfigs].xmlname, parentxmlconfig[iMaxConfigs].xmlname);
        strcpy(maps[iMaxConfigs].xmlfile, parentxmlconfig[iMaxConfigs].xmlfile);
        maps[iMaxConfigs].store = init_storage_backend(parentxmlconfig[iMaxConfigs].tile_dir);

        if (maps[iMaxConfigs].store) {
            maps[iMaxConfigs].ok = 1;

            maps[iMaxConfigs].map = mapnik::Map(RENDER_SIZE, RENDER_SIZE);

            try {
                mapnik::load_map(maps[iMaxConfigs].map, maps[iMaxConfigs].xmlfile);
            } catch (std::exception const& ex) {
                syslog(LOG_ERR, "An error occurred while loading the map layer '%s': %s", maps[iMaxConfigs].xmlname, ex.what());
                maps[iMaxConfigs].ok = 0;
            } catch (...) {
                syslog(LOG_ERR, "An unknown error occurred while loading the map layer '%s'", maps[iMaxConfigs].xmlname);
                maps[iMaxConfigs].ok = 0;
            }
            maps[iMaxConfigs].prj = projection(maps[iMaxConfigs].map.srs());
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
        struct item *item = fetch_request();
        if (item) {
            struct protocol *req = &item->req;
#ifdef METATILE
            // At very low zoom the whole world may be smaller than METATILE
            unsigned int size = MIN(METATILE, 1 << req->z);
            for (i = 0; i < iMaxConfigs; ++i) {
                if (!strcmp(maps[i].xmlname, req->xmlname)) {
                    metaTile tiles(req->xmlname, item->mx, item->my, req->z);
                    
                    if (maps[i].ok) {
                        timeval tim;
                        gettimeofday(&tim, NULL);
                        long t1=tim.tv_sec*1000+(tim.tv_usec/1000);

                        ret = render(maps[i].map, req->xmlname, maps[i].prj, item->mx, item->my, req->z, size, tiles);

                        gettimeofday(&tim, NULL);
                        long t2=tim.tv_sec*1000+(tim.tv_usec/1000);
                        syslog(LOG_DEBUG, "DEBUG: DONE TILE %s %d %d-%d %d-%d in %.3lf seconds", 
                               req->xmlname, req->z, item->mx, item->mx+size-1, item->my, item->my+size-1, (t2 - t1)/1000.0);
                        statsRenderFinish(req->z, t2 - t1);
                    } else {
                        syslog(LOG_ERR, "Received request for map layer '%s' which failed to load", req->xmlname);
                        ret = cmdNotDone;
                    }

                    if (ret == cmdDone) {
                        try {
                            tiles.save(maps[i].store);
                        } catch (...) {
                            // Treat any error as fatal and request end of processing
                            syslog(LOG_ERR, "Received error when writing metatile to disk, requesting exit.");
                            ret = cmdNotDone;
                            request_exit();
                        }
#ifdef HTCP_EXPIRE_CACHE
                        tiles.expire_tiles(maps[i].htcpsock,maps[i].host,maps[i].xmluri);
#endif
                    }
#else
                    ret = render(maps[i].map, maps[i].tile_dir, req->xmlname, maps[i].prj, req->x, req->y, req->z);
#ifdef HTCP_EXPIRE_CACHE
                    cache_expire(maps[i].htcpsock,maps[i].host, maps[i].xmluri, req->x,req->y,req->z);
#endif
#endif
                    send_response(item, ret);
                    if (ret != cmdDone) sleep(10); //Something went wrong with rendering, delay next processing to allow temporary issues to fix them selves
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

