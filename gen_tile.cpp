#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/filter_factory.hpp>
#include <mapnik/color_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/image_util.hpp>

#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include "gen_tile.h"
#include "daemon.h"
#include "protocol.h"
#include "render_config.h"
#include "dir_utils.h"
#include "store.h"


using namespace mapnik;
#define DEG_TO_RAD (M_PI/180)
#define RAD_TO_DEG (180/M_PI)

#ifdef METATILE
#define RENDER_SIZE (256 * (METATILE + 1))
#else
#define RENDER_SIZE (512)
#endif

static const int minZoom = 0;
static const int maxZoom = 18;

typedef struct {
    char xmlname[XMLCONFIG_MAX];
    char xmlfile[PATH_MAX];
    Map map;
    projection prj;
} xmlmapconfig;


class SphericalProjection
{
    double *Ac, *Bc, *Cc, *zc;

    public:
        SphericalProjection(int levels=18) {
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
        fprintf(stderr, "Unable to open font directory: %s\n", font_dir);
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
            //fprintf(stderr, "Loading font: %s\n", path);
            freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}


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

        void save()
        {
            int ox, oy, limit;
            size_t offset;
            struct meta_layout m;
            char meta_path[PATH_MAX];
            struct entry offsets[METATILE * METATILE];

            memset(&m, 0, sizeof(m));
            memset(&offsets, 0, sizeof(offsets));

            xyz_to_meta(meta_path, sizeof(meta_path), xmlconfig_.c_str(), x_, y_, z_);
            std::stringstream ss;
            ss << std::string(meta_path) << "." << pthread_self();
            std::string tmp(ss.str());

            mkdirp(tmp.c_str());

            std::ofstream file(tmp.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);

            // Create and write header
            m.count = METATILE * METATILE;
            memcpy(m.magic, META_MAGIC, strlen(META_MAGIC));
            m.x = x_;
            m.y = y_;
            m.z = z_;
            file.write((const char *)&m, sizeof(m));

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
            file.write((const char *)&offsets, sizeof(offsets));

            // Write tiles
            for (ox=0; ox < limit; ox++) {
                for (oy=0; oy < limit; oy++) {
                    file.write((const char *)tile[ox][oy].data(), tile[ox][oy].size());
                }
            }

            file.close();

            rename(tmp.c_str(), meta_path);
            //printf("Produced .meta: %s\n", meta_path);
        }

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

    Envelope<double> bbox(p0x, p0y, p1x,p1y);
    m.resize(render_size, render_size);
    m.zoomToBox(bbox);
    m.set_buffer_size(128);
    //m.zoom(size+1);

    Image32 buf(render_size, render_size);
    agg_renderer<Image32> ren(m,buf);
    ren.apply();

    // Split the meta tile into an NxN grid of tiles
    unsigned int xx, yy;
    for (yy = 0; yy < size; yy++) {
        for (xx = 0; xx < size; xx++) {
            image_view<ImageData32> vw(xx * 256, yy * 256, 256, 256, buf.data());
            tiles.set(xx, yy, save_to_string(vw, "png256"));
        }
    }
    std::cout << "DONE TILE " << xmlname << " " << z << " " << x << "-" << x+size-1 << " " << y << "-" << y+size-1 << "\n";
    return cmdDone; // OK
}
#else
static enum protoCmd render(Map &m, char *xmlname, projection &prj, int x, int y, int z)
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

    Envelope<double> bbox(p0x, p0y, p1x,p1y);
    bbox.width(bbox.width() * 2);
    bbox.height(bbox.height() * 2);
    m.zoomToBox(bbox);

    Image32 buf(RENDER_SIZE, RENDER_SIZE);
    agg_renderer<Image32> ren(m,buf);
    ren.apply();

    xyz_to_path(filename, sizeof(filename), xmlname, x, y, z);
    if (mkdirp(filename))
        return cmdNotDone;
    snprintf(tmp, sizeof(tmp), "%s.tmp", filename);

    image_view<ImageData32> vw(128,128,256,256, buf.data());
    //std::cout << "Render " << z << " " << x << " " << y << " " << filename << "\n";
    save_to_file(vw, tmp,"png256");
    if (rename(tmp, filename)) {
        perror(tmp);
        return cmdNotDone;
    }

    return cmdDone; // OK
}
#endif


void render_init(void)
{
    // TODO: Make these module options
    datasource_cache::instance()->register_datasources(MAPNIK_PLUGINS);
    load_fonts(FONT_DIR, FONT_RECURSE);
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
        maps[iMaxConfigs].map = Map(RENDER_SIZE, RENDER_SIZE);
        load_map(maps[iMaxConfigs].map, maps[iMaxConfigs].xmlfile);
        maps[iMaxConfigs].prj = projection(maps[iMaxConfigs].map.srs());
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

                    ret = render(maps[i].map, req->xmlname, maps[i].prj, item->mx, item->my, req->z, size, tiles);
                    if (ret == cmdDone)
                        tiles.save();
#else
                    ret = render(maps[i].map, req->xmlname, maps[i].prj, req->x, req->y, req->z);
#endif
                    send_response(item, ret);
                    break;
               }
            }
            if (i == iMaxConfigs){
                fprintf(stderr, "No map for: %s\n", req->xmlname);
            }
        } else
            sleep(1); // TODO: Use an event to indicate there are new requests
    }
    return NULL;
}
