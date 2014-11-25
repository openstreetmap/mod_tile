// meta2tile.c
// written by Frederik Ramm <frederik@remote.org>
// License: GPL because this is based on other work in mod_tile

// if you define WITH_SHAPE, you need GEOS and OGR libraries.
// WITH_SHAPE lets you sort tiles into various target directories
// or target mbtiles files depending on their geometry.

// if you define WITH_MBTILES, you need sqlite.
// WITH_MBTILES lets you generate mbtiles files instead of simple
// tile directories.

#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>

#ifdef WITH_MBTILES
#include "sqlite3.h"
#endif
#include "store_file.h"
#include "metatile.h"

#ifdef WITH_SHAPE
#include "ogr_api.h"
#include "geos_c.h"
#endif

#define MIN(x,y) ((x)<(y)?(x):(y))
#define META_MAGIC "META"

static int verbose = 0;
static int mbtiles = 0;
static int shape = 0;
static int num_render = 0;
static struct timeval start, end;

const char *source;
const char *target;

// linked list of targets. targets are directories
// or mbtiles files, and in WITH_SHAPE mode may be 
// associated with a polygon area.
struct target
{
    const char *pathname;
#ifdef WITH_SHAPE
    const GEOSPreparedGeometry *prepgeom;
#endif
    struct target *next;
#ifdef WITH_MBTILES
    sqlite3 *sqlite_db;
    sqlite3_stmt *sqlite_tile_insert;
#endif
} *target_list = NULL;

struct target *matched_target;

// in WITH_SHAPE mode, we select the right target geometry
// with the help of an R-Tree.
#ifdef WITH_SHAPE
GEOSSTRtree *target_tree;
#endif

#ifdef WITH_MBTILES
// linked list of mbtiles metadata items
struct metadata 
{
    char *key;
    char *value;
    struct metadata *next;
} *metadata = NULL;
#endif

#define MODE_STAT 1
#define MODE_GLOB 2
#define MAXZOOM 20

int mode = MODE_GLOB;

int zoom[MAXZOOM+1];
float bbox[4] = {-180.0, -90.0, 180.0, 90.0};

int path_to_xyz(const char *path, int *px, int *py, int *pz)
{
    int i, n, hash[5], x, y, z;
    char copy[PATH_MAX];
    strcpy(copy, path);
    char *slash = rindex(copy, '/');
    int c=5;
    while (slash && c)
    {
        *slash = 0;
        c--;
        hash[c]= atoi(slash+1);
        slash = rindex(copy, '/');
    }
    if (c != 0)
    {
        fprintf(stderr, "Failed to parse tile path: %s\n", path);
        return 1;
    }
    *slash = 0;
    *pz = atoi(slash+1);

    x = y = 0;
    for (i=0; i<5; i++)
    {
        if (hash[i] < 0 || hash[i] > 255)
        {
            fprintf(stderr, "Failed to parse tile path (invalid %d): %s\n", hash[i], path);
            return 2;
        }
        x <<= 4;
        y <<= 4;
        x |= (hash[i] & 0xf0) >> 4;
        y |= (hash[i] & 0x0f);
    }
    z = *pz;
    *px = x;
    *py = y;
    return 0;
}

#ifdef WITH_MBTILES
// this creates the mbtiles file(s) and sets them up for inserting.
void setup_mbtiles()
{

    struct target *t = target_list;
    char *errmsg;

    while(t)
    {
        if (sqlite3_open(t->pathname, &(t->sqlite_db)) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot open '%s': %s\n", t->pathname, sqlite3_errmsg(t->sqlite_db));
            exit(1);
        }
        if (verbose) fprintf(stderr, "opened '%s' for mbtiles output\n", t->pathname);

        if (sqlite3_exec(t->sqlite_db, "create table tiles ("
            "zoom_level integer, tile_column integer, tile_row integer, "
            "tile_data blob)", NULL, NULL, &errmsg) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot create tile table: %s\n", errmsg);
            exit(1);
        }

        if (sqlite3_prepare_v2(t->sqlite_db, 
            "insert into tiles (zoom_level, tile_row, tile_column, tile_data) "
            "values (?, ?, ?, ?)", -1, &(t->sqlite_tile_insert), NULL) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(t->sqlite_db));
            exit(1);
        }

        if (sqlite3_exec(t->sqlite_db, "create table metadata ("
            "name text, value text)", NULL, NULL, &errmsg) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot create metadata table: %s\n", errmsg);
            exit(1);
        }

        sqlite3_stmt *sqlite_meta_insert;
        if (sqlite3_prepare_v2(t->sqlite_db, 
            "insert into metadata (name, value) values (?, ?)",
                -1, &sqlite_meta_insert, NULL) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(t->sqlite_db));
            exit(1);
        }

        sqlite3_exec(t->sqlite_db, "begin transaction", NULL, NULL, &errmsg);
        sqlite3_exec(t->sqlite_db, "pragma synchronous=off", NULL, NULL, &errmsg);
        sqlite3_exec(t->sqlite_db, "pragma journal_mode=memory", NULL, NULL, &errmsg);

        struct metadata *md = metadata;
        while (md)
        {
            sqlite3_reset(sqlite_meta_insert);
            sqlite3_bind_text(sqlite_meta_insert, 1, md->key, -1, NULL);
            sqlite3_bind_text(sqlite_meta_insert, 2, md->value, -1, NULL);
            if (sqlite3_step(sqlite_meta_insert) != SQLITE_DONE)
            {
                fprintf(stderr, "Cannot insert metadata %s=%s: %s\n", md->key, md->value, sqlite3_errmsg(t->sqlite_db));
                exit(1);
            }
            md = md->next;
        }

        if (sqlite3_exec(t->sqlite_db, "create unique index name on metadata(name)", NULL, NULL, &errmsg))
        {
            fprintf(stderr, "Cannot create metadata index: %s\n", errmsg);
            exit(1);
        }

        // process next target
        t = t->next;
    }
}

// closes mbtiles files.
void shutdown_mbtiles()
{
    char *errmsg;
    struct target *t = target_list;

    while(t)
    {
        if (sqlite3_exec(t->sqlite_db, "end transaction", NULL, NULL, &errmsg) != SQLITE_OK)
        {
            fprintf(stderr, "Cannot end transaction: %s\n", errmsg);
            exit(1);
        }
        if (sqlite3_exec(t->sqlite_db, "create unique index tile_index on tiles(zoom_level,tile_column,tile_row)", NULL, NULL, &errmsg)) 
        {
            fprintf(stderr, "Cannot create tile index: %s\n", errmsg);
            exit(1);
        }
        if (verbose) fprintf(stderr, "finalised '%s'\n", t->pathname);
        // process next target
        t = t->next;
    }
}
#endif

int long2tilex(double lon, int z) 
{ 
    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z))); 
}
 
int lat2tiley(double lat, int z)
{ 
    return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z))); 
}
 
double tilex2long(int x, int z) 
{
    return x / pow(2.0, z) * 360.0 - 180;
}
 
double tiley2lat(int y, int z) 
{
    double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

#ifdef WITH_SHAPE
// callback function for tree search. We don't currently support multiple
// matches - last one wins.
void tree_query_callback(void *item, void *userdata)
{
    // this is called on a bbox match but is it a *real* match?
    struct target *t = (struct target *) item;
    GEOSGeometry *g = (GEOSGeometry *) userdata;
    if (GEOSPreparedIntersects(t->prepgeom, g))
    {
        matched_target = (struct target *) item;
    }
}
#endif

// main workhorse - opens meta tile and does something with it.
int expand_meta(const char *name)
{
    int fd;
    char header[4096];
    int x, y, z;
    size_t pos;
    void *buf;

    if (verbose>1) fprintf(stderr, "expand_meta %s\n", name);

    if (path_to_xyz(name, &x, &y, &z)) return -1;

    int limit = (1 << z);
    limit = MIN(limit, METATILE);

    float fromlat = tiley2lat(y+8, z);
    float tolat = tiley2lat(y, z);
    float fromlon = tilex2long(x, z);
    float tolon = tilex2long(x+8, z);

    if (tolon < bbox[0] || fromlon > bbox[2] || tolat < bbox[1] || fromlat > bbox[3])
    {
        if (verbose>1) printf("z=%d x=%d y=%d is out of bbox\n", z, x, y);
        return -8;
    }

    fd = open(name, O_RDONLY);
    if (fd < 0) 
    {
        fprintf(stderr, "Could not open metatile %s. Reason: %s\n", name, strerror(errno));
        return -1;
    }

    struct stat st;
    fstat(fd, &st);

    buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED)
    {
        fprintf(stderr, "Cannot mmap file %s for %ld bytes: %s\n", name, st.st_size, strerror(errno));
        close(fd);
        return -3;
    }
    struct meta_layout *m = (struct meta_layout *)buf;

    if (memcmp(m->magic, META_MAGIC, strlen(META_MAGIC))) 
    {
        fprintf(stderr, "Meta file %s header magic mismatch\n", name);
        munmap(buf, st.st_size);
        close(fd);
        return -4;
    }

    if (m->count != (METATILE * METATILE)) 
    {
        fprintf(stderr, "Meta file %s header bad count %d != %d\n", name, m->count, METATILE * METATILE);
        munmap(buf, st.st_size);
        close(fd);
        return -5;
    }

    char path[PATH_MAX];
    if (!mbtiles)
    {
        sprintf(path, "%s/%d", target, z);
        if (mkdir(path, 0755) && (errno != EEXIST))
        {
            fprintf(stderr, "cannot create directory %s: %s\n", path, strerror(errno));
            munmap(buf, st.st_size);
            close(fd);            
            return -1;
        }
    }

    int create_dir = 0;

    for (int meta = 0; meta < METATILE*METATILE; meta++)
    {
        int tx = x + (meta / METATILE);
        if (tx >= 1<<z) continue;
        int ty = y + (meta % METATILE);
        if (ty >= 1<<z) continue;
        int output;
        if (ty==y) create_dir = 1;

        struct target *t;
#ifdef WITH_SHAPE
        if (shape)
        {
            matched_target = NULL;

            double x0 = tilex2long(tx,z);
            double x1 = tilex2long(tx+1,z);
            double y0 = tiley2lat(ty,z);
            double y1 = tiley2lat(ty+1,z);

            // build a geometry object for the tile
            GEOSCoordSequence *seq = GEOSCoordSeq_create(5, 2);
            GEOSCoordSeq_setX(seq, 0, x0);
            GEOSCoordSeq_setX(seq, 1, x0);
            GEOSCoordSeq_setX(seq, 2, x1);
            GEOSCoordSeq_setX(seq, 3, x1);
            GEOSCoordSeq_setX(seq, 4, x0);
            GEOSCoordSeq_setY(seq, 0, y0);
            GEOSCoordSeq_setY(seq, 1, y1);
            GEOSCoordSeq_setY(seq, 2, y1);
            GEOSCoordSeq_setY(seq, 3, y0);
            GEOSCoordSeq_setY(seq, 4, y0);
            GEOSGeometry *ring = GEOSGeom_createLinearRing(seq);
            if (!ring) continue; // really shouldn't happen
            GEOSGeometry *poly = GEOSGeom_createPolygon(ring, NULL, 0);
            if (!poly) continue; // really shouldn't happen
            assert(poly);

            GEOSSTRtree_query(target_tree, poly, tree_query_callback, (void *) poly);
            GEOSGeom_destroy(poly);
            if (!matched_target) 
            {
                if (verbose > 1) fprintf(stderr, "no matching polygon for tile %d/%d/%d\n", z, tx, ty);
                continue;
            }
            t = matched_target;
        }
        else
        {
#endif
        // if not in SHAPE mode, simply use the one existing target
        t = target_list;
#ifdef WITH_SHAPE
        }
#endif

        if (!mbtiles)
        {
            // this is a small optimisation intended to reduce the amount of 
            // mkdir calls
            if (create_dir)
            {
                sprintf(path, "%s/%d/%d", t->pathname, z, tx);
                if (mkdir(path, 0755) && (errno != EEXIST))
                {
                    fprintf(stderr, "cannot create directory %s: %s\n", path, strerror(errno));
                    munmap(buf, st.st_size);
                    close(fd);            
                    return -1;
                }
                create_dir = 0;
            }

            sprintf(path, "%s/%d/%d/%d.png", t->pathname, z, tx, ty);
            output = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
            if (output == -1)
            {
                fprintf(stderr, "cannot open %s for writing: %s\n", path, strerror(errno));
                munmap(buf, st.st_size);
                close(fd);            
                return -1;
            }

            pos = 0;
            while (pos < m->index[meta].size) 
            {
                size_t len = m->index[meta].size - pos;
                int written = write(output, buf + pos + m->index[meta].offset, len);
                if (written < 0) 
                {
                    fprintf(stderr, "Failed to write data to file %s. Reason: %s\n", path, strerror(errno));
                    munmap(buf, st.st_size);
                    close(fd);
                    close(output);
                    return -7;
                } 
                else if (written > 0) 
                {
                    pos += written;
                } 
                else 
                {
                    break;
                }
            }
            close(output);
            if (verbose) printf("Produced tile: %s\n", path);
        }
        else
        {
#ifdef WITH_MBTILES
            ty = (1<<z)-ty-1;
            sqlite3_reset(t->sqlite_tile_insert);
            sqlite3_bind_int(t->sqlite_tile_insert, 1, z);
            sqlite3_bind_int(t->sqlite_tile_insert, 3, tx);
            sqlite3_bind_int(t->sqlite_tile_insert, 2, ty);
            sqlite3_bind_blob(t->sqlite_tile_insert, 4, buf + m->index[meta].offset, m->index[meta].size, SQLITE_STATIC);
            if (sqlite3_step(t->sqlite_tile_insert) != SQLITE_DONE)
            {
                fprintf(stderr, "Failed to insert tile z=%d x=%d y=%d: %s\n", z, tx, ty, sqlite3_errmsg(t->sqlite_db));
                munmap(buf, st.st_size);
                close(fd);
                return -7;
            }
            if (verbose) printf("Inserted tile %d/%d/%d into %s\n", z, tx, ty, t->pathname);
#endif
        }
    }

    munmap(buf, st.st_size);
    close(fd);
    num_render++;
    return pos;
}

void display_rate(struct timeval start, struct timeval end, int num) 
{
    int d_s, d_us;
    float sec;

    d_s  = end.tv_sec  - start.tv_sec;
    d_us = end.tv_usec - start.tv_usec;

    sec = d_s + d_us / 1000000.0;

    printf("Converted %d tiles in %.2f seconds (%.2f tiles/s)\n", num, sec, num / sec);
    fflush(NULL);
}

// recurive directory processing. 
// zoomdone signals whether we might still have to 
// exclude certain directories based on zoom selection,
// or whether we're already past that.
static void descend(const char *search, int zoomdone)
{
    DIR *tiles = opendir(search);
    struct dirent *entry;
    char path[PATH_MAX];
    int this_is_zoom = -1;

    if (verbose>1) fprintf(stderr, "descend to %s\n", search);

    if (!tiles) 
    {
        fprintf(stderr, "Unable to open directory: %s\n", search);
        return;
    }

    while ((entry = readdir(tiles))) 
    {
        struct stat b;
        char *p;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        if (this_is_zoom == -1)
        {
            if (!zoomdone && isdigit(*(entry->d_name)) && atoi(entry->d_name) >= 0 && atoi(entry->d_name) <= MAXZOOM)
            {
                this_is_zoom = 1;
            }
            else
            {
                this_is_zoom = 0;
            }
        }

        if (this_is_zoom)
        {
            int z = atoi(entry->d_name);
            if (z<0 || z>MAXZOOM || !zoom[z]) 
            {
                if (verbose > 1) fprintf(stderr, "zoom %d not requested\n", z);
                continue;
            }
        }

        snprintf(path, sizeof(path), "%s/%s", search, entry->d_name);
        if (stat(path, &b))
        {
            fprintf(stderr, "cannot stat %s\n", path);
            continue;
        }
        if (S_ISDIR(b.st_mode)) 
        {
            descend(path, zoomdone || this_is_zoom);
            continue;
        }
        p = strrchr(path, '.');
        if (p && !strcmp(p, ".meta")) 
        {
            expand_meta(path);
        }
        else
        {
            fprintf(stderr, "unknown file type: %s\n", path);
        }
    }
    closedir(tiles);
}

static void process_list_from_stdin()
{
    char buffer[32767];
    char *pos = buffer + strlen(source);
    strcpy(buffer, source);
    strcpy(pos, "/");  
    pos++;
    int size = PATH_MAX - strlen(buffer) - 1;

    while (fgets(pos, size, stdin))
    {
        char *back = pos + strlen(pos) - 1;
        while (back>pos && isspace(*back)) *(back--)=0;
        expand_meta(buffer);
    }
}

void usage()
{
    fprintf(stderr, "Usage: meta2tile [options] sourcedir target\n\n");
    fprintf(stderr, "Convert .meta files found in source dir to .png in target dir,\n");
    fprintf(stderr, "using the standard \"hash\" type directory (5-level) for meta\n");
    fprintf(stderr, "tiles and the z/x/y.png structure (3-level) for output.\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "--bbox x   specify minlon,minlat,maxlon,maxlat to extract only\n");
    fprintf(stderr, "           meta tiles intersecting that bbox (default: world).\n");
    fprintf(stderr, "--list     instead of converting all meta tiles in input directory,\n");
    fprintf(stderr, "           convert only those given (one per line) on stdin.\n");
#ifdef WITH_MBTILES
    fprintf(stderr, "--mbtiles  instead of writing single tiles to output directory,\n");
    fprintf(stderr, "           write a MBTiles file (\"target\" is a file name then.)\n");
    fprintf(stderr, "--meta k=v set k=v in the MBTiles metadata table (MBTiles spec\n");
    fprintf(stderr, "           mandates use of name, type, version, description, format).\n");
    fprintf(stderr, "           Can occur multiple times.\n");
#else
    fprintf(stderr, "--mbtiles  option not available, specify WITH_MBTILES when compiling.\n");
#endif
    fprintf(stderr, "--mode x   use in conjunction with --zoom or --bbox; mode=glob\n");
    fprintf(stderr, "           is faster if you extract more than 10 percent of\n");
    fprintf(stderr, "           files, and mode=stat is faster otherwise.\n");
#ifdef WITH_SHAPE
    fprintf(stderr, "--shape    switch to shape file output mode, in which the \"target\"\n");
    fprintf(stderr, "           is the name of a polygon shape file that has one column\n");
    fprintf(stderr, "           named \"target\" specifying the real target for all tiles\n");
    fprintf(stderr, "           that lie inside the respective polygon.\n");
#else
    fprintf(stderr, "--shape    option not available, specify WITH_SHAPE when compiling.\n");
#endif
    fprintf(stderr, "--zoom x   specify a single zoomlevel, a number of comma separated\n");
    fprintf(stderr, "           zoom levels, or z0-z1 zoom ranges to convert (default: all).\n");
    fprintf(stderr, "--verbose  talk more.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "--zoom and --bbox don't make sense with --list;\n");
    fprintf(stderr, "--bbox can be used with --shape but a tile for which no target is\n");
    fprintf(stderr, "defined will not be output even when inside the --bbox range.\n");
}

int handle_bbox(char *arg)
{
    char *token = strtok(arg, ",");
    int bbi = 0;
    while(token && bbi<4)
    {
        bbox[bbi++] = atof(token);
        token = strtok(NULL, ",");
    }
    return (bbi==4 && token==NULL);
}

int handle_zoom(char *arg)
{
    char *token = strtok(arg, ",");
    while(token)
    {
        int fromz = atoi(token);
        int toz = atoi(token);
        char *minus = strchr(token, '-');
        if (minus)
        {
            toz = atoi(minus+1);
        }
        if (fromz<0 || toz<0 || fromz>MAXZOOM || toz>MAXZOOM || toz<fromz || !isdigit(*token)) return 0;
        for (int i=fromz; i<=toz; i++) zoom[i]=1;
        token = strtok(NULL, ",");
    }
    return 1;
}

#ifdef WITH_MBTILES
int handle_meta(char *arg)
{
    char *eq = strchr(arg, '=');
    // no equal sign
    if (!eq) return 0;
    // equal sign at beginning
    if (eq==arg) return 0;
    // equal sign at end
    if (!*(eq+1)) return 0;

    struct metadata *m = (struct metadata *) malloc(sizeof(struct metadata));
    m->next = metadata;
    metadata = m;
    *eq++=0;
    m->key = arg;
    m->value = eq;
    return 1;
}
#endif

#ifdef WITH_SHAPE
int load_shape(const char *file)
{
    OGRDataSourceH hDS;
    hDS = OGROpen(file, FALSE, NULL);
    if (hDS == NULL)
    {
        fprintf(stderr, "cannot open shape file %s for reading.\n", file);
        return 0;
    }
    OGRLayerH hLayer;
    hLayer = OGR_DS_GetLayer(hDS, 0);
    OGRFeatureH hFeature;

    OGRFeatureDefnH hFDefn = OGR_L_GetLayerDefn(hLayer);
    int target_index = OGR_FD_GetFieldIndex(hFDefn, "target");
    if (target_index == -1)
    {
        fprintf(stderr, "shape file has no column named 'target'.\n", file);
        return 0;
    }
    OGRFieldDefnH hFieldDefn = OGR_FD_GetFieldDefn(hFDefn, target_index);
    if (OGR_Fld_GetType(hFieldDefn) != OFTString)
    {
        fprintf(stderr, "'target' column in shape is not a string column.\n", file);
        return 0;
    }
    OGR_L_ResetReading(hLayer);
    while ((hFeature = OGR_L_GetNextFeature(hLayer)) != NULL)
    {
        OGRGeometryH hGeometry;
        hGeometry = OGR_F_GetGeometryRef(hFeature);
        const char *path = OGR_F_GetFieldAsString(hFeature, target_index);
        if (hGeometry != NULL && 
            (wkbFlatten(OGR_G_GetGeometryType(hGeometry)) == wkbPolygon ||
            wkbFlatten(OGR_G_GetGeometryType(hGeometry)) == wkbMultiPolygon))
        {
            // make a GEOS geometry from this by way of WKB
            size_t wkbsz = OGR_G_WkbSize(hGeometry);
            unsigned char *buf = (unsigned char *) malloc(wkbsz);
            OGR_G_ExportToWkb(hGeometry, wkbXDR, buf);
            GEOSGeometry *gg = GEOSGeomFromWKB_buf(buf, wkbsz);
            free(buf);
            const GEOSPreparedGeometry *gpg = GEOSPrepare(gg);
            struct target *tgt = (struct target *) malloc(sizeof(struct target));
            tgt->pathname = strdup(path);
            tgt->prepgeom = gpg;
            tgt->next = target_list;
            target_list = tgt;
            GEOSSTRtree_insert(target_tree, gg, tgt);
        }
        else
        {
            fprintf(stderr, "target '%s' in shape file has non-polygon geometry, ignored.\n", path);
        }
        OGR_F_Destroy (hFeature);
    }
    OGR_DS_Destroy( hDS );
}
#endif

int main(int argc, char **argv)
{
    int c;
    int list = 0;
    for (int i=0; i<=MAXZOOM; i++) zoom[i]=0;
    int zoomset = 0;

#ifdef WITH_SHAPE
    OGRRegisterAll();
    initGEOS(NULL, NULL);
    target_tree = GEOSSTRtree_create(10);
#endif

    while (1) 
    {
        int option_index = 0;
        static struct option long_options[] = 
        {
            {"list", 0, 0, 'l'},
#ifdef WITH_MBTILES
            {"mbtiles", 0, 0, 't'},
            {"meta", 1, 0, 'a'},
#endif
            {"verbose", 0, 0, 'v'},
            {"help", 0, 0, 'h'},
            {"bbox", 1, 0, 'b'},
            {"mode", 1, 0, 'm'},
            {"zoom", 1, 0, 'z'},
            {"shape", 0, 0, 's'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "vhlb:m:z:"
#ifdef WITH_MBTILES
        "ta:"
#endif
#ifdef WITH_SHAPE
        "s"
#endif
        , long_options, &option_index);
        if (c == -1)
            break;

        switch (c) 
        {
            case 'v':
                verbose++;
                break;
            case 'l':
                list=1;
                break;
#ifdef WITH_MBTILES
            case 't':
                mbtiles=1;
                break;
            case 'a':
                if (!handle_meta(optarg))
                {
                    fprintf(stderr, "invalid meta argument '%s' - must be of the form key=value\n", optarg);
                    return -1;
                }
                break;
#endif
            case 'h':
                usage();
                return -1;
            case 'b':
                if (!handle_bbox(optarg))
                {
                    fprintf(stderr, "invalid bbox argument - must be of the form east,south,west,north\n");
                    return -1;
                }
                break;
            case 'z':
                zoomset = 1;
                if (!handle_zoom(optarg))
                {
                    fprintf(stderr, "invalid zoom argument - must be of the form zoom or z0,z1,z2... or z0-z1\n");
                    return -1;
                }
                break;
            case 'm': 
                if (!strcmp(optarg, "glob"))
                {
                    mode = MODE_GLOB;
                }
                else if (!strcmp(optarg, "stat"))
                {
                    mode = MODE_STAT;
                    fprintf(stderr, "mode=stat not yet implemented\n");
                    return -1;
                }
                else
                {
                    fprintf(stderr, "mode argument must be either 'glob' or 'stat'\n");
                    return -1;
                }
                break;
#if defined WITH_SHAPE
            case 's': 
                shape = 1;
                break;
#endif
            default:
                break;
        }
    }

    if (!zoomset) for (int i=0; i<=MAXZOOM; i++) zoom[i]=1;

    if (optind >= argc-1)
    {
        usage();
        return -1;
    }

    source=argv[optind++];
    target=argv[optind++];

    fprintf(stderr, "Converting tiles from directory %s to %s %s\n", 
        source, 
        mbtiles ? (shape ? "mbtiles files defined in shape file" : "mbtiles file") 
                : (shape ? "directories defined in shape file" : "directoy"), 
        target);

    gettimeofday(&start, NULL);

#ifdef WITH_SHAPE
    if (shape) load_shape(target);
#endif

    // if multi-destination not used, initialize destination list
    // with single entry.
    if (!shape)
    {
        target_list = (struct target *) malloc(sizeof(struct target));
        target_list->next = NULL;
        target_list->pathname = target;
    }
    
#ifdef WITH_MBTILES
    if (mbtiles) setup_mbtiles();
#endif

    if (list)
    {
        process_list_from_stdin();
    }
    else
    {
        descend(source, 0);
    }

#ifdef WITH_MBTILES
    if (mbtiles) shutdown_mbtiles();
#endif

    gettimeofday(&end, NULL);
    printf("\nTotal for all tiles converted\n");
    printf("Meta tiles converted: ");
    display_rate(start, end, num_render);
    printf("Total tiles converted: ");
    display_rate(start, end, num_render * METATILE * METATILE);

    return 0;
}
