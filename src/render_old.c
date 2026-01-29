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

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "protocol.h"
#include "render_config.h"
#include "render_submit_queue.h"
#include "renderd_config.h"
#include "store_file_utils.h"
#include "sys_utils.h"

#ifndef METATILE
#warning("render_old not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
	fprintf(stderr, "render_old not implemented for non-metatile mode. Feel free to submit fix!\n");
	return -1;
}

#else

static int num_render = 0, num_all = 0;
static int max_load;
static time_t planet_timestamp = 0;
static struct timeval start, end;

void display_rate(struct timeval start, struct timeval end, int num)
{
	int d_s, d_us;
	float sec;

	d_s = end.tv_sec - start.tv_sec;
	d_us = end.tv_usec - start.tv_usec;

	sec = d_s + d_us / 1000000.0;

	g_logger(G_LOG_LEVEL_MESSAGE, "\t%d in %.2f seconds (%.2f/s)", num, sec, num / sec);
}

static time_t get_planet_time(const char *tile_dir)
{
	static time_t last_check;
	static time_t planet_time;
	time_t now = time(NULL);
	struct stat buf;
	char filename[PATH_MAX];

	snprintf(filename, PATH_MAX - 1, "%s/%s", tile_dir, PLANET_TIMESTAMP);

	// Only check for updates periodically
	if (now < last_check + 300) {
		return planet_time;
	}

	last_check = now;

	if (stat(filename, &buf)) {
		g_logger(G_LOG_LEVEL_MESSAGE, "Planet timestamp file (%s) is missing", filename);
		// Make something up
		planet_time = now - 3 * 24 * 60 * 60;
	} else {
		if (buf.st_mtime != planet_time) {
			g_logger(G_LOG_LEVEL_MESSAGE, "Planet file updated at %s", strtok(ctime(&buf.st_mtime), "\n"));
			planet_time = buf.st_mtime;
		}
	}

	return planet_time;
}

static void check_load(void)
{
	double avg = get_load_avg();

	while (avg >= max_load) {
		g_logger(G_LOG_LEVEL_MESSAGE, "Load average %f, sleeping", avg);
		sleep(5);
		avg = get_load_avg();
	}
}

static void descend(const char *tile_dir, const char *search, int verbose)
{
	DIR *tiles = opendir(search);
	struct dirent *entry;
	char path[PATH_MAX];
	char mapname[XMLCONFIG_MAX];
	int x, y, z;

	if (!tiles) {
		if (verbose) {
			g_logger(G_LOG_LEVEL_MESSAGE, "%s: %s", strerror(errno), search);
		}

		return;
	}

	while ((entry = readdir(tiles))) {
		struct stat b;
		char *p;

		check_load();

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
			continue;
		}

		snprintf(path, sizeof(path), "%s/%s", search, entry->d_name);

		if (stat(path, &b)) {
			continue;
		}

		if (S_ISDIR(b.st_mode)) {
			descend(tile_dir, path, verbose);
			continue;
		}

		p = strrchr(path, '.');

		if (p && !strcmp(p, ".meta")) {
			if (planet_timestamp > b.st_mtime) {
				// request rendering of  old tile
				path_to_xyz(tile_dir, path, mapname, &x, &y, &z);
				enqueue(mapname, x, y, z);
				num_render++;
			}

			num_all++;
		}
	}

	closedir(tiles);
}

void render_layer(const char *tile_dir, const char *mapname, int min_zoom, int max_zoom, int verbose)
{
	for (int z = min_zoom; z <= max_zoom; z++) {
		if (verbose) {
			g_logger(G_LOG_LEVEL_MESSAGE, "Rendering zoom %d", z);
		}

		char search[PATH_MAX];
		snprintf(search, PATH_MAX, "%s/%s/%d", tile_dir, mapname, z);
		descend(tile_dir, search, verbose);
	}
}

int main(int argc, char **argv)
{
	const char *config_file_name_default = RENDERD_CONFIG;
	const char *mapname_default = XMLCONFIG_DEFAULT;
	const char *socketname_default = RENDERD_SOCKET;
	const char *tile_dir_default = RENDERD_TILE_DIR;
	int max_load_default = MAX_LOAD_OLD;
	int max_zoom_default = MAX_ZOOM;
	int min_zoom_default = 0;
	int num_threads_default = 1;

	const char *config_file_name = config_file_name_default;
	const char *mapname = mapname_default;
	const char *socketname = socketname_default;
	const char *tile_dir = tile_dir_default;
	max_load = max_load_default;
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

	int map_section_num = -1;
	int dd, mm, yy;
	int verbose = 0;
	struct tm tm;

	foreground = 1;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"config",      required_argument, 0, 'c'},
			{"map",         required_argument, 0, 'm'},
			{"max-load",    required_argument, 0, 'l'},
			{"max-zoom",    required_argument, 0, 'Z'},
			{"min-zoom",    required_argument, 0, 'z'},
			{"num-threads", required_argument, 0, 'n'},
			{"socket",      required_argument, 0, 's'},
			{"tile-dir",    required_argument, 0, 't'},
			{"timestamp",   required_argument, 0, 'T'},
			{"verbose",     no_argument,       0, 'v'},

			{"help",        no_argument,       0, 'h'},
			{"version",     no_argument,       0, 'V'},
			{0, 0, 0, 0}
		};

		int c = getopt_long(argc, argv, "c:m:l:Z:z:n:s:t:T:vhV", long_options, &option_index);

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'c': /* -c, --config */
				config_file_name = strndup(optarg, PATH_MAX);
				config_file_name_passed = 1;

				struct stat buffer;

				if (stat(config_file_name, &buffer) != 0) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Config file '%s' does not exist, please specify a valid file", config_file_name);
					return 1;
				}

				break;

			case 'm': /* -m, --map */
				mapname = strndup(optarg, XMLCONFIG_MAX);
				mapname_passed = 1;
				break;

			case 'l': /* -l, --max-load */
				max_load = min_max_int_opt(optarg, "maximum load", 0, -1);
				max_load_passed = 1;
				break;

			case 'Z': /* -Z, --max-zoom */
				max_zoom = min_max_int_opt(optarg, "maximum zoom", 0, MAX_ZOOM);
				max_zoom_passed = 1;
				break;

			case 'z': /* -z, --min-zoom */
				min_zoom = min_max_int_opt(optarg, "minimum zoom", 0, MAX_ZOOM);
				min_zoom_passed = 1;
				break;

			case 'n': /* -n, --num-threads */
				num_threads = min_max_int_opt(optarg, "number of threads", 1, -1);
				num_threads_passed = 1;
				break;

			case 's': /* -s, --socket */
				socketname = strndup(optarg, PATH_MAX);
				socketname_passed = 1;
				break;

			case 't': /* -t, --tile-dir */
				tile_dir = strndup(optarg, PATH_MAX);
				tile_dir_passed = 1;
				break;

			case 'T':
				if (sscanf(optarg, "%d/%d/%d", &dd, &mm, &yy) == 3) {
					if (yy > 100) {
						yy -= 1900;
					}

					if (yy < 70) {
						yy += 100;
					}

					memset(&tm, 0, sizeof(tm));
					tm.tm_mday = dd;
					tm.tm_mon = mm - 1;
					tm.tm_year = yy;
					planet_timestamp = mktime(&tm);
				} else if (sscanf(optarg, "%d", &dd) == 1) {
					planet_timestamp = dd;
				} else {
					g_logger(G_LOG_LEVEL_CRITICAL, "Invalid planet timestamp, must be a unix timestamp or in the format dd/mm/yyyy");
					return 1;
				}

				break;

			case 'v': /* -v, --verbose */
				verbose = 1;
				break;

			case 'h': /* -h, --help */
				fprintf(stderr, "Usage: render_old [OPTION] ...\n");
				fprintf(stderr, "Search the rendered tiles and re-render tiles which are older then the last planet import\n");
				fprintf(stderr, "  -c, --config=CONFIG               specify the renderd config file (default is '%s')\n", config_file_name_default);
				fprintf(stderr, "  -l, --max-load=LOAD               sleep if load is this high (default is '%d')\n", max_load_default);
				fprintf(stderr, "  -m, --map=STYLE                   Instead of going through all styles of CONFIG, only use a specific map-style\n");
				fprintf(stderr, "  -n, --num-threads=N               the number of parallel request threads (default is '%d')\n", num_threads_default);
				fprintf(stderr, "  -s, --socket=SOCKET|HOSTNAME:PORT unix domain socket name or hostname and port for contacting renderd (default is '%s')\n", socketname_default);
				fprintf(stderr, "  -t, --tile-dir=TILE_DIR           tile cache directory (default is '%s')\n", tile_dir_default);
				fprintf(stderr, "  -T, --timestamp=DD/MM/YY          Overwrite the assumed data of the planet import\n");
				fprintf(stderr, "  -Z, --max-zoom=ZOOM               filter input to only render tiles less than or equal to this zoom level (default is '%d')\n", max_zoom_default);
				fprintf(stderr, "  -z, --min-zoom=ZOOM               filter input to only render tiles greater than or equal to this zoom level (default is '%d')\n", min_zoom_default);
				fprintf(stderr, "\n");
				fprintf(stderr, "  -h, --help                        display this help and exit\n");
				fprintf(stderr, "  -V, --version                     display the version number and exit\n");
				return 0;

			case 'V': /* -V, --version */
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

	process_config_file(config_file_name, 0, G_LOG_LEVEL_DEBUG);

	for (int i = 0; i < XMLCONFIGS_MAX; ++i) {
		if (mapname_passed && maps[i].xmlname && strcmp(maps[i].xmlname, mapname) == 0) {
			map_section_num = i;
		}
	}

	if (mapname_passed && map_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Map section '%s' does not exist in config file '%s'.", mapname, config_file_name);
		return 1;
	}

	if (!socketname_passed) {
		socketname = strndup(config->socketname, PATH_MAX);
		socketname_passed = 1;
	}

	g_logger(G_LOG_LEVEL_INFO, "Started render_old with the following options:");
	g_logger(G_LOG_LEVEL_INFO, "\t--config      = '%s' (%s)", config_file_name, config_file_name_passed ? "user-specified" : "default");

	if (mapname_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--map         = '%s' (user-specified)", mapname);
	}

	g_logger(G_LOG_LEVEL_INFO, "\t--max-load    = '%i' (%s)", max_load, max_load_passed ? "user-specified/from config" : "default");

	if (max_zoom_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--max-zoom    = '%i' (user-specified)", max_zoom);
	}

	if (min_zoom_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--min-zoom    = '%i' (user-specified)", min_zoom);
	}

	g_logger(G_LOG_LEVEL_INFO, "\t--num-threads = '%i' (%s)", num_threads, num_threads_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--socket      = '%s' (%s)", socketname, socketname_passed ? "user-specified/from config" : "default");

	if (tile_dir_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--tile-dir    = '%s' (user-specified)", tile_dir);
	}

	if (planet_timestamp == 0) {
		planet_timestamp = get_planet_time(tile_dir);
	} else {
		g_logger(G_LOG_LEVEL_MESSAGE, "Overwriting planet file update to %s", strtok(ctime(&planet_timestamp), "\n"));
	}

	gettimeofday(&start, NULL);

	spawn_workers(num_threads, socketname, max_load);

	for (int i = 0; i < XMLCONFIGS_MAX; ++i) {
		if (mapname_passed && maps[i].xmlname && strcmp(maps[i].xmlname, mapname) != 0) {
			continue;
		}

		if (maps[i].xmlname != NULL) {
			if (!max_zoom_passed) {
				max_zoom = maps[i].max_zoom;
			}

			if (!min_zoom_passed) {
				min_zoom = maps[i].min_zoom;
			}

			if (!tile_dir_passed) {
				tile_dir = strndup(maps[i].tile_dir, PATH_MAX);
			}

			if (verbose) {
				g_logger(G_LOG_LEVEL_MESSAGE, "Rendering map '%s' from zoom '%i' to zoom '%i'", maps[i].xmlname, min_zoom, max_zoom);
			}

			render_layer(tile_dir, maps[i].xmlname, min_zoom, max_zoom, verbose);
		}
	}

	finish_workers();

	gettimeofday(&end, NULL);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total for all tiles rendered");
	g_logger(G_LOG_LEVEL_MESSAGE, "Metatiles rendered:");
	display_rate(start, end, num_render);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles rendered:");
	display_rate(start, end, num_render * METATILE * METATILE);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles handled:");
	display_rate(start, end, num_all);

	return 0;
}

#endif
