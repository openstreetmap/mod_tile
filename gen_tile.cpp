#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/filter_factory.hpp>
#include <mapnik/color_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/image_util.hpp>

#include <Magick++.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include "gen_tile.h"
#include "protocol.h"


#undef USE_RENDER_OFFSET


using namespace mapnik;

#define DEG_TO_RAD (M_PIl/180)
#define RAD_TO_DEG (180/M_PIl)

static const int minZoom = 0;
static const int maxZoom = 18;
static const char *mapfile = "/home/jburgess/osm/svn.openstreetmap.org/applications/rendering/mapnik/osm-jb-merc.xml";

#if 0
static void postProcess(const char *path)
{
    // Convert the 32bit RGBA image to one with indexed colours
    // TODO: Ideally this would work on the Mapnik Image32 instead of requiring the intermediate image
    // Or have a post-process thread with queueing

    char tmp[PATH_MAX];
    Magick::Image image;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    try {
        image.read(path);

        image.matte(0);
        image.quantizeDither(0);
        image.quantizeColors(255);
        image.quantize();
        image.modulusDepth(8);
        image.write(tmp);
        rename(tmp, path);
    }
    catch( Magick::Exception &error_ ) {
        std::cerr << "Caught exception: " << error_.what() << std::endl;
    }
}
#endif


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
static projection prj("+proj=merc +datum=WGS84");

static enum protoCmd render(Map &m, Image32 &buf, int x, int y, int z, const char *filename)
{
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
#ifdef USE_RENDER_OFFSET
    agg_renderer<Image32> ren(m,buf, 128,128);
    ren.apply();
    buf.saveToFile(filename,"png256");
#else
    agg_renderer<Image32> ren(m,buf);
    ren.apply();
    image_view<ImageData32> vw(128,128,256,256, buf.data());
    save_to_file(filename,"png256", vw);
#endif
    return cmdDone; // OK
}

pthread_mutex_t map_lock;

void render_init(void)
{
    // TODO: Make these module options
    datasource_cache::instance()->register_datasources("/usr/local/lib64/mapnik/input");
    //load_fonts("/usr/share/fonts", 1);
    load_fonts("/usr/local/lib64/mapnik/fonts", 0);
    pthread_mutex_init(&map_lock, NULL);
}


void *render_thread(__attribute__((unused)) void *unused)
{
    Map m(2 * 256, 2 * 256);
#ifdef USE_RENDER_OFFSET
    Image32 buf(256, 256);
#else
    Image32 buf(512, 512);
#endif

    load_map(m,mapfile);

    while (1) {
        enum protoCmd ret;
        struct item *item = fetch_request();
        if (item) {
            struct protocol *req = &item->req;
            //pthread_mutex_lock(&map_lock);
            ret = render(m, buf, req->x, req->y, req->z, req->path);
            //pthread_mutex_unlock(&map_lock);
            //postProcess(req->path);
            send_response(item, ret);
            delete_request(item);
        } else
            sleep(1); // TODO: Use an event to indicate there are new requests
    }
    return NULL;
}
