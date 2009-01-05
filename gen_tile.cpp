#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/filter_factory.hpp>
#include <mapnik/color_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/image_util.hpp>

#include <iostream>
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

#define DEG_TO_RAD (M_PIl/180)
#define RAD_TO_DEG (180/M_PIl)

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
} xmlmapconfig;

// The map projection must match the one in the osm.xml file
static projection prj("+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +no_defs +over");

static double minmax(double a, double b, double c)
{
#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))
    a = MAX(a,b);
    a = MIN(a,c);
    return a;
}

class GoogleProjection
{
    double *Ac, *Bc, *Cc, *zc;

    public:
        GoogleProjection(int levels=18) {
            Ac = new double[levels];
            Bc = new double[levels];
            Cc = new double[levels];
            zc = new double[levels];
            int d, c = 256;
            for (d=0; d<levels; d++) {
                int e = c/2;
                Bc[d] = c/360.0;
                Cc[d] = c/(2 * M_PIl);
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
            y = RAD_TO_DEG * ( 2 * atan(exp(g)) - 0.5 * M_PIl);
        }
};

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

static GoogleProjection gprj(maxZoom+1);

#ifdef METATILE
static enum protoCmd render(Map &m, char *xmlname, int x, int y, int z, unsigned int size)
{
    int render_size = 256 * (size + 1);
    double p0x = x * 256;
    double p0y = (y + size) * 256;
    double p1x = (x + size) * 256;
    double p1y = y * 256;

    //std::cout << "META TILE " << z << " " << x << "-" << x+size-1 << " " << y << "-" << y+size-1 << "\n";

    gprj.fromPixelToLL(p0x, p0y, z);
    gprj.fromPixelToLL(p1x, p1y, z);

    prj.forward(p0x, p0y);
    prj.forward(p1x, p1y);

    Envelope<double> bbox(p0x, p0y, p1x,p1y);
    bbox.height(bbox.height() * (size+1.0)/size);
    bbox.width(bbox.width() * (size+1.0)/size);
    m.resize(render_size, render_size);
    m.zoomToBox(bbox);
    //m.zoom(size+1);

    Image32 buf(render_size, render_size);
    agg_renderer<Image32> ren(m,buf);
    ren.apply();


    // Split the meta tile into an NxN grid of tiles
    unsigned int xx, yy;
    for (yy = 0; yy < size; yy++) {
        for (xx = 0; xx < size; xx++) {
            int yoff = 128 + yy * 256;
            int xoff = 128 + xx * 256;
            image_view<ImageData32> vw(xoff, yoff, 256, 256, buf.data());

            char filename[PATH_MAX];
            char tmp[PATH_MAX];
            xyz_to_path(filename, sizeof(filename), xmlname, x+xx, y+yy, z);
            if (mkdirp(filename))
                return cmdNotDone;
            snprintf(tmp, sizeof(tmp), "%s.tmp", filename);
            //std::cout << "Render " << z << " " << x << "(" << xx << ") " << y << "(" << yy << ") " << filename << "\n";
            save_to_file(vw, tmp, "png256");
            if (rename(tmp, filename)) {
                perror(tmp);
                return cmdNotDone;
            }
        }
    }
    std::cout << "DONE TILE " << xmlname << " " << z << " " << x << "-" << x+size-1 << " " << y << "-" << y+size-1 << "\n";
    return cmdDone; // OK
}
#else
static enum protoCmd render(Map &m, char *xmlname, int x, int y, int z)
{
    char filename[PATH_MAX];
    char tmp[PATH_MAX];
    double p0x = x * 256.0;
    double p0y = (y + 1) * 256.0;
    double p1x = (x + 1) * 256.0;
    double p1y = y * 256.0;

    gprj.fromPixelToLL(p0x, p0y, z);
    gprj.fromPixelToLL(p1x, p1y, z);

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
                    ret = render(maps[i].map, req->xmlname, item->mx, item->my, req->z, size);
                    if (ret == cmdDone)
                        process_meta(req->xmlname, item->mx, item->my, req->z);
#else
                    ret = render(maps[i].map, req->xmlname, req->x, req->y, req->z);
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
