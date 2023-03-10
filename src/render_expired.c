/*
 * Copyright (c) 2007 - 2023 by mod_tile contributors (see AUTHORS file)
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

#include <getopt.h>
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "render_config.h"
#include "render_submit_queue.h"
#include "renderd_config.h"
#include "store.h"

// macros handling our tile marking arrays (these are essentially bit arrays
// that have one bit for each tile on the repsective zoom level; since we only
// need them for meta tile levels, even if someone were to render level 20,
// we'd still only use 4^17 bits = 2 GB RAM (plus a little for the lower zoom
// levels) - this saves us the hassle of working with a tree structure.

#define TILE_REQUESTED(z,x,y) \
   (tile_requested[z][((x)*twopow[z]+(y))/(8*sizeof(int))]>>(((x)*twopow[z]+(y))%(8*sizeof(int))))&0x01
#define SET_TILE_REQUESTED(z,x,y) \
   tile_requested[z][((x)*twopow[z]+(y))/(8*sizeof(int))] |= (0x01 << (((x)*twopow[z]+(y))%(8*sizeof(int))));


#ifndef METATILE
#warning("render_expired not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
	fprintf(stderr, "render_expired not implemented for non-metatile mode. Feel free to submit fix!\n");
	return -1;
}
#else

// tile marking arrays
unsigned int **tile_requested;

// "two raised to the power of [...]" - don't trust pow() to be efficient
// for base 2
unsigned long long twopow[MAX_ZOOM];

void display_rate(struct timeval start, struct timeval end, int num)
{
	int d_s, d_us;
	float sec;

	d_s  = end.tv_sec  - start.tv_sec;
	d_us = end.tv_usec - start.tv_usec;

	sec = d_s + d_us / 1000000.0;

	g_logger(G_LOG_LEVEL_MESSAGE, "\tRendered %d tiles in %.2f seconds (%.2f tiles/s)", num, sec, num / sec);
}

int main(int argc, char **argv)
{
	const char *config_file_name_default = RENDERD_CONFIG;
	const char *mapname_default = XMLCONFIG_DEFAULT;
	const char *socketname_default = RENDERD_SOCKET;
	const char *tile_dir_default = RENDERD_TILE_DIR;
	int max_load_default = MAX_LOAD_OLD;
	int max_zoom_default = 18;
	int min_zoom_default = 0;
	int num_threads_default = 1;

	const char *config_file_name = config_file_name_default;
	const char *mapname = mapname_default;
	const char *socketname = socketname_default;
	const char *tile_dir = tile_dir_default;
	int max_load = max_load_default;
	int max_zoom = max_zoom_default;
	int min_zoom = min_zoom_default;
	int num_threads = num_threads_default;

	int config_file_name_passed = 0;
	int mapname_passed = 0;
	int socketname_passed = 0;
	int tile_dir_passed = 0;
	int max_load_passed = 0;
	int max_zoom_passed = 0;
	int min_zoom_passed = 0;
	int num_threads_passed = 0;

	int x, y, z;
	struct timeval start, end;
	int num_render = 0, num_all = 0, num_read = 0, num_ignore = 0, num_unlink = 0, num_touch = 0;
	int c, i, map_section_num = -1;
	int deleteFrom = -1, touchFrom = -1;
	int doRender = 0;
	int progress = 1;
	int verbose = 0;
	struct storage_backend * store;
	char name[PATH_MAX];

	// excess_zoomlevels is how many zoom levels at the large end
	// we can ignore because their tiles will share one meta tile.
	// with the default METATILE==8 this is 3.
	int excess_zoomlevels = 0;
	int mt = METATILE;

	while (mt > 1) {
		excess_zoomlevels++;
		mt >>= 1;
	}

	foreground = 1;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"config",      required_argument, 0, 'c'},
			{"delete-from", required_argument, 0, 'd'},
			{"map",         required_argument, 0, 'm'},
			{"max-load",    required_argument, 0, 'l'},
			{"max-zoom",    required_argument, 0, 'Z'},
			{"min-zoom",    required_argument, 0, 'z'},
			{"no-progress", no_argument,       0, 'N'},
			{"num-threads", required_argument, 0, 'n'},
			{"socket",      required_argument, 0, 's'},
			{"tile-dir",    required_argument, 0, 't'},
			{"touch-from",  required_argument, 0, 'T'},
			{"verbose",     no_argument,       0, 'v'},

			{"help",        no_argument,       0, 'h'},
			{"version",     no_argument,       0, 'V'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "c:d:m:l:Z:z:Nn:s:t:T:vhV", long_options, &option_index);

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'c':   /* -c, --config */
				config_file_name = strndup(optarg, PATH_MAX);
				config_file_name_passed = 1;

				if (access(config_file_name, F_OK) != 0) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Config file '%s' does not exist, please specify a valid file with -c/--config", config_file_name);
					return 1;
				}

				break;

			case 'd':   /* -d, --delete-from */
				deleteFrom = min_max_int_opt(optarg, "delete-from", 0, MAX_ZOOM);
				break;

			case 'm':   /* -m, --map */
				mapname = strndup(optarg, XMLCONFIG_MAX);
				mapname_passed = 1;
				break;

			case 'l':   /* -l, --max-load */
				max_load = min_max_int_opt(optarg, "maximum load", 0, -1);
				max_load_passed = 1;
				break;

			case 'Z':   /* -Z, --max-zoom */
				max_zoom = min_max_int_opt(optarg, "maximum zoom", 0, MAX_ZOOM);
				max_zoom_passed = 1;
				break;

			case 'z':   /* -z, --min-zoom */
				min_zoom = min_max_int_opt(optarg, "minimum zoom", 0, MAX_ZOOM);
				min_zoom_passed = 1;
				break;

			case 'N':   /* -N, --no-progress */
				progress = 0;
				break;

			case 'n':   /* -n, --num-threads */
				num_threads = min_max_int_opt(optarg, "number of threads", 1, -1);
				num_threads_passed = 1;
				break;

			case 's':   /* -s, --socket */
				socketname = strndup(optarg, PATH_MAX);
				socketname_passed = 1;
				break;

			case 't':   /* -t, --tile-dir */
				tile_dir = strndup(optarg, PATH_MAX);
				tile_dir_passed = 1;
				break;

			case 'T':   /* -T, --touch-from */
				touchFrom = min_max_int_opt(optarg, "touch-from", 0, MAX_ZOOM);
				break;

			case 'v':   /* -v, --verbose */
				verbose = 1;
				break;

			case 'h':   /* -h, --help */
				fprintf(stderr, "Usage: render_expired [OPTION] ...\n");
				fprintf(stderr, "  -c, --config=CONFIG               specify the renderd config file (default is off)\n");
				fprintf(stderr, "  -d, --delete-from=ZOOM            when expiring tiles of ZOOM or higher, delete them instead of re-rendering (default is off)\n");
				fprintf(stderr, "  -m, --map=MAP                     render tiles in this map (default is '%s')\n", mapname_default);
				fprintf(stderr, "  -l, --max-load=LOAD               sleep if load is this high (default is '%d')\n", max_load_default);
				fprintf(stderr, "  -n, --num-threads=N               the number of parallel request threads (default is '%d')\n", num_threads_default);
				fprintf(stderr, "  -s, --socket=SOCKET|HOSTNAME:PORT unix domain socket name or hostname and port for contacting renderd (default is '%s')\n", socketname_default);
				fprintf(stderr, "  -t, --tile-dir=TILE_DIR           tile cache directory (default is '%s')\n", tile_dir_default);
				fprintf(stderr, "  -T, --touch-from=ZOOM             when expiring tiles of ZOOM or higher, touch them instead of re-rendering (default is off)\n");
				fprintf(stderr, "  -Z, --max-zoom=ZOOM               filter input to only render tiles less than or equal to this zoom level (default is '%d')\n", max_zoom_default);
				fprintf(stderr, "  -z, --min-zoom=ZOOM               filter input to only render tiles greater than or equal to this zoom level (default is '%d')\n", min_zoom_default);
				fprintf(stderr, "\n");
				fprintf(stderr, "  -h, --help                        display this help and exit\n");
				fprintf(stderr, "  -V, --version                     display the version number and exit\n");
				fprintf(stderr, "\n");
				fprintf(stderr, "Send a list of tiles to be rendered from STDIN in the format:\n");
				fprintf(stderr, "  z/x/y\n");
				fprintf(stderr, "e.g.\n");
				fprintf(stderr, "  1/0/1\n");
				fprintf(stderr, "  1/1/1\n");
				fprintf(stderr, "  1/0/0\n");
				fprintf(stderr, "  1/1/0\n");
				fprintf(stderr, "The above would cause all 4 tiles at zoom 1 to be rendered\n");
				return 0;

			case 'V':   /* -V, --version */
				fprintf(stdout, "%s\n", VERSION);
				return 0;

			default:
				g_logger(G_LOG_LEVEL_CRITICAL, "unhandled char '%c'", c);
				return 1;
		}
	}

	if (max_zoom < min_zoom) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is larger than max zoom (%i).", min_zoom, max_zoom);
		return 1;
	}

	if (config_file_name_passed) {
		process_config_file(config_file_name, 0, G_LOG_LEVEL_DEBUG);

		for (i = 0; i < XMLCONFIGS_MAX; ++i) {
			if (maps[i].xmlname && strcmp(maps[i].xmlname, mapname) == 0) {
				map_section_num = i;
			}
		}

		if (map_section_num < 0) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Map section '%s' does not exist in config file '%s'.", mapname, config_file_name);
			return 1;
		}

		if (!max_zoom_passed) {
			max_zoom = maps[map_section_num].max_zoom;
			max_zoom_passed = 1;
		}

		if (!min_zoom_passed) {
			min_zoom = maps[map_section_num].min_zoom;
			min_zoom_passed = 1;
		}

		if (!num_threads_passed) {
			num_threads = maps[map_section_num].num_threads;
			num_threads_passed = 1;
		}

		if (!socketname_passed) {
			socketname = strndup(config.socketname, PATH_MAX);
			socketname_passed = 1;
		}

		if (!tile_dir_passed) {
			tile_dir = strndup(maps[map_section_num].tile_dir, PATH_MAX);
			tile_dir_passed = 1;
		}
	}

	store = init_storage_backend(tile_dir);

	if (store == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to initialise storage backend %s", tile_dir);
		return 1;
	}

	if (min_zoom < excess_zoomlevels) {
		min_zoom = excess_zoomlevels;
	}

	// initialise arrays for tile markings

	tile_requested = (unsigned int **) malloc((max_zoom - excess_zoomlevels + 1) * sizeof(unsigned int *));

	for (i = 0; i <= max_zoom - excess_zoomlevels; i++) {
		// initialize twopow array
		twopow[i] = (i == 0) ? 1 : twopow[i - 1] * 2;
		unsigned long long fourpow = twopow[i] * twopow[i];
		tile_requested[i] = (unsigned int *) calloc((fourpow / METATILE) + 1, 1);

		if (NULL == tile_requested[i]) {
			g_logger(G_LOG_LEVEL_CRITICAL, "not enough memory available");
			return 1;
		}
	}

	if ((touchFrom != -1 && min_zoom < touchFrom)
			|| (deleteFrom != -1 && min_zoom < deleteFrom)
			|| (touchFrom == -1 && deleteFrom == -1)) {
		// No need to spawn render threads, when we're not actually going to rerender tiles
		spawn_workers(num_threads, socketname, max_load);
		doRender = 1;
	}

	g_logger(G_LOG_LEVEL_INFO, "Started render_expired with the following options:");

	if (config_file_name_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--config      = '%s' (user-specified)", config_file_name);
	}

	g_logger(G_LOG_LEVEL_INFO, "\t--map         = '%s' (%s)", mapname, mapname_passed ? "user-specified" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--max-load    = '%i' (%s)", max_load, max_load_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--max-zoom    = '%i' (%s)", max_zoom, max_zoom_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--min-zoom    = '%i' (%s)", min_zoom, min_zoom_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--num-threads = '%i' (%s)", num_threads, num_threads_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--socket      = '%s' (%s)", socketname, socketname_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--tile-dir    = '%s' (%s)", tile_dir, tile_dir_passed ? "user-specified/from config" : "default");

	gettimeofday(&start, NULL);

	while (!feof(stdin)) {
		struct stat_info s;
		int n = fscanf(stdin, "%d/%d/%d", &z, &x, &y);

		if (n != 3) {
			// Discard input line
			char tmp[1024];
			char *r = fgets(tmp, sizeof(tmp), stdin);

			if (!r) {
				continue;
			}

			if (verbose) {
				g_logger(G_LOG_LEVEL_WARNING, "bad line %d: %s", num_all, tmp);
			}

			continue;
		}

		if (verbose) {
			g_logger(G_LOG_LEVEL_MESSAGE, "read: x=%d y=%d z=%d", x, y, z);
		}

		while (z > max_zoom) {
			x >>= 1;
			y >>= 1;
			z--;
		}

		while (z < max_zoom) {
			x <<= 1;
			y <<= 1;
			z++;
		}

		g_logger(G_LOG_LEVEL_DEBUG, "loop: x=%d y=%d z=%d up to z=%d", x, y, z, min_zoom);
		num_read++;

		if (progress && (num_read % 100) == 0) {
			g_logger(G_LOG_LEVEL_INFO, "Read and expanded %i tiles from list.", num_read);
		}

		for (; z >= min_zoom; z--, x >>= 1, y >>= 1) {
			if (verbose) {
				g_logger(G_LOG_LEVEL_MESSAGE, "process: x=%d y=%d z=%d", x, y, z);
			}

			// don't do anything if this tile was already requested.
			// renderd does keep a list internally to avoid enqueing the same tile
			// twice but in case it has already rendered the tile we don't want to
			// cause extra work.
			if (TILE_REQUESTED(z - excess_zoomlevels, x >> excess_zoomlevels, y >> excess_zoomlevels)) {
				if (verbose) {
					g_logger(G_LOG_LEVEL_MESSAGE, "already requested");
				}

				break;
			}

			// mark tile as requested. (do this even if, below, the tile is not
			// actually requested due to not being present on disk, to avoid
			// unnecessary later stat'ing).
			SET_TILE_REQUESTED(z - excess_zoomlevels, x >> excess_zoomlevels, y >> excess_zoomlevels);

			// commented out - seems to cause problems in MT environment,
			// trying to write to already-closed file
			//check_load();

			num_all++;
			s = store->tile_stat(store, mapname, "", x, y, z);

			if (s.size > 0) { // Tile exists
				// tile exists on disk; render it
				if (deleteFrom != -1 && z >= deleteFrom) {
					g_logger(G_LOG_LEVEL_MESSAGE, "delete: %s", store->tile_storage_id(store, mapname, "", x, y, z, name));
					store->metatile_delete(store, mapname, x, y, z);
					num_unlink++;
				} else if (touchFrom != -1 && z >= touchFrom) {
					g_logger(G_LOG_LEVEL_MESSAGE, "touch: %s", store->tile_storage_id(store, mapname, "", x, y, z, name));
					store->metatile_expire(store, mapname, x, y, z);
					num_touch++;
				} else if (doRender) {
					g_logger(G_LOG_LEVEL_MESSAGE, "render: %s", store->tile_storage_id(store, mapname, "", x, y, z, name));
					enqueue(mapname, x, y, z);
					num_render++;
				}
			} else {
				if (verbose) {
					g_logger(G_LOG_LEVEL_MESSAGE, "not on disk: %s", store->tile_storage_id(store, mapname, "", x, y, z, name));
				}

				num_ignore++;
			}
		}
	}

	if (doRender) {
		finish_workers();
	}

	if (config_file_name_passed) {
		free((void *)config_file_name);
	}

	if (mapname_passed) {
		free((void *)mapname);
	}

	if (socketname_passed) {
		free((void *)socketname);
	}

	if (tile_dir_passed) {
		free((void *)tile_dir);
	}

	store->close_storage(store);
	free(store);

	for (i = 0; i <= max_zoom - excess_zoomlevels; i++) {
		free(tile_requested[i]);
	}

	free(tile_requested);

	gettimeofday(&end, NULL);
	g_logger(G_LOG_LEVEL_MESSAGE, "Read and expanded %i tiles from list.", num_read);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total for all tiles rendered");
	g_logger(G_LOG_LEVEL_MESSAGE, "Meta tiles rendered:");
	display_rate(start, end, num_render);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles rendered:");
	display_rate(start, end, num_render * METATILE * METATILE);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles in input: %d", num_read);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles expanded from input: %d", num_all);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total meta tiles deleted: %d", num_unlink);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total meta tiles touched: %d", num_touch);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles ignored (not on disk): %d", num_ignore);

	return 0;
}
#endif
