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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"
#include "render_submit_queue.h"
#include "renderd_config.h"
#include "store.h"

#ifndef METATILE
#warning("render_list not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
	fprintf(stderr, "render_list not implemented for non-metatile mode. Feel free to submit fix!\n");
	return -1;
}
#else

int lon2tilex(double lon, int z)
{
	return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
}

int lat2tiley(double lat, int z)
{
	double latrad = lat * M_PI / 180.0;
	return (int)(floor((1.0 - log(tan(latrad) + (1.0 / cos(latrad))) / M_PI) / 2.0 * pow(2.0, z)));
}

void display_rate(struct timeval start, struct timeval end, int num)
{
	int d_s, d_us;
	float sec;

	d_s = end.tv_sec - start.tv_sec;
	d_us = end.tv_usec - start.tv_usec;

	sec = d_s + d_us / 1000000.0;

	g_logger(G_LOG_LEVEL_MESSAGE, "\t%d in %.2f seconds (%.2f/s)", num, sec, num / sec);
}

int main(int argc, char **argv)
{
	const char *config_file_name_default = RENDERD_CONFIG;
	const char *mapname_default = XMLCONFIG_DEFAULT;
	const char *socketname_default = RENDERD_SOCKET;
	const char *tile_dir_default = RENDERD_TILE_DIR;
	double max_lat_default = -1;
	double max_lon_default = -1;
	double min_lat_default = -1;
	double min_lon_default = -1;
	int max_load_default = MAX_LOAD_OLD;
	int max_x_default = -1;
	int max_y_default = -1;
	int max_zoom_default = MAX_ZOOM;
	int min_x_default = -1;
	int min_y_default = -1;
	int min_zoom_default = 0;
	int num_threads_default = 1;

	const char *config_file_name = config_file_name_default;
	const char *mapname = mapname_default;
	const char *socketname = socketname_default;
	const char *tile_dir = tile_dir_default;
	double max_lat = max_lat_default;
	double max_lon = max_lon_default;
	double min_lat = min_lat_default;
	double min_lon = min_lon_default;
	int max_load = max_load_default;
	int max_x = max_x_default;
	int max_y = max_y_default;
	int max_zoom = max_zoom_default;
	int min_x = min_x_default;
	int min_y = min_y_default;
	int min_zoom = min_zoom_default;
	int num_threads = num_threads_default;

	int config_file_name_passed = 0;
	int mapname_passed = 0;
	int socketname_passed = 0;
	int tile_dir_passed = 0;
	int max_lat_passed = 0;
	int max_lon_passed = 0;
	int min_lat_passed = 0;
	int min_lon_passed = 0;
	int max_load_passed = 0;
	int max_x_passed = 0;
	int max_y_passed = 0;
	int max_zoom_passed = 0;
	int min_x_passed = 0;
	int min_y_passed = 0;
	int min_zoom_passed = 0;
	int num_threads_passed = 0;

	int x, y, z;
	struct timeval start, end;
	int num_render = 0, num_all = 0;
	int all = 0;
	int force = 0;
	int verbose = 0;
	int stop_renderd = 0;
	struct storage_backend *store;
	struct stat_info s;

	foreground = 1;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"all",         no_argument,       0, 'a'},
			{"config",      required_argument, 0, 'c'},
			{"force",       no_argument,       0, 'f'},
			{"map",         required_argument, 0, 'm'},
			{"max-lat",     required_argument, 0, 'G'},
			{"max-load",    required_argument, 0, 'l'},
			{"max-lon",     required_argument, 0, 'W'},
			{"max-x",       required_argument, 0, 'X'},
			{"max-y",       required_argument, 0, 'Y'},
			{"max-zoom",    required_argument, 0, 'Z'},
			{"min-lat",     required_argument, 0, 'g'},
			{"min-lon",     required_argument, 0, 'w'},
			{"min-x",       required_argument, 0, 'x'},
			{"min-y",       required_argument, 0, 'y'},
			{"min-zoom",    required_argument, 0, 'z'},
			{"num-threads", required_argument, 0, 'n'},
			{"socket",      required_argument, 0, 's'},
			{"tile-dir",    required_argument, 0, 't'},
			{"verbose",     no_argument,       0, 'v'},
			{"stop",        no_argument,       0, 'S'},

			{"help",        no_argument,       0, 'h'},
			{"version",     no_argument,       0, 'V'},
			{0, 0, 0, 0}
		};

		int c = getopt_long(argc, argv, "ac:fm:G:l:W:X:Y:Z:g:w:x:y:z:n:s:t:vShV", long_options, &option_index);

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'a': /* -a, --all */
				all = 1;
				break;

			case 'c': /* -c, --config */
				config_file_name = strndup(optarg, PATH_MAX);
				config_file_name_passed = 1;

				struct stat buffer;

				if (stat(config_file_name, &buffer) != 0) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Config file '%s' does not exist, please specify a valid file", config_file_name);
					return 1;
				}

				break;

			case 'f': /* -f, --force */
				force = 1;
				break;

			case 'm': /* -m, --map */
				mapname = strndup(optarg, XMLCONFIG_MAX);
				mapname_passed = 1;
				break;

			case 'G': /* -G, --max-lat */
				max_lat = min_max_double_opt(optarg, "maximum latitute", -85.0511, 85.0511);
				max_lat_passed = 1;
				break;

			case 'l': /* -l, --max-load */
				max_load = min_max_int_opt(optarg, "maximum load", 0, -1);
				max_load_passed = 1;
				break;

			case 'W': /* -W, --max-lon */
				max_lon = min_max_double_opt(optarg, "maximum longitude", -180, 180);
				max_lon_passed = 1;
				break;

			case 'X': /* -X, --max-x */
				max_x = min_max_int_opt(optarg, "maximum X tile coordinate", 0, -1);
				max_x_passed = 1;
				break;

			case 'Y': /* -Y, --max-y */
				max_y = min_max_int_opt(optarg, "maximum Y tile coordinate", 0, -1);
				max_y_passed = 1;
				break;

			case 'Z': /* -Z, --max-zoom */
				max_zoom = min_max_int_opt(optarg, "maximum zoom", 0, MAX_ZOOM);
				max_zoom_passed = 1;
				break;

			case 'g': /* -g, --min-lat */
				min_lat = min_max_double_opt(optarg, "minimum latitute", -85.0511, 85.0511);
				min_lat_passed = 1;
				break;

			case 'w': /* -w, --min-lon */
				min_lon = min_max_double_opt(optarg, "minimum longitude", -180, 180);
				min_lon_passed = 1;
				break;

			case 'x': /* -x, --min-x */
				min_x = min_max_int_opt(optarg, "minimum X tile coordinate", 0, -1);
				min_x_passed = 1;
				break;

			case 'y': /* -y, --min-y */
				min_y = min_max_int_opt(optarg, "minimum Y tile coordinate", 0, -1);
				min_y_passed = 1;
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

			case 'v': /* -v, --verbose */
				verbose = 1;
				break;

			case 'S': /* -S, --stop */
				stop_renderd = 1;
				break;

			case 'h': /* -h, --help */
				fprintf(stderr, "Usage: render_list [OPTION] ...\n");
				fprintf(stderr, "  -a, --all                         render all tiles in given zoom level range instead of reading from STDIN\n");
				fprintf(stderr, "  -c, --config=CONFIG               specify the renderd config file (default is off)\n");
				fprintf(stderr, "  -f, --force                       render tiles even if they seem current\n");
				fprintf(stderr, "  -l, --max-load=LOAD               sleep if load is this high (default is '%d')\n", max_load_default);
				fprintf(stderr, "  -m, --map=MAP                     render tiles in this map (default is '%s')\n", mapname_default);
				fprintf(stderr, "  -n, --num-threads=N               the number of parallel request threads (default is '%i')\n", num_threads_default);
				fprintf(stderr, "  -s, --socket=SOCKET|HOSTNAME:PORT unix domain socket name or hostname and port for contacting renderd (default is '%s')\n", socketname_default);
				fprintf(stderr, "  -t, --tile-dir=TILE_DIR           tile cache directory (default is '%s')\n", tile_dir_default);
				fprintf(stderr, "  -Z, --max-zoom=ZOOM               filter input to only render tiles less than or equal to this zoom level (default is '%d')\n", max_zoom_default);
				fprintf(stderr, "  -z, --min-zoom=ZOOM               filter input to only render tiles greater than or equal to this zoom level (default is '%d')\n", min_zoom_default);
				fprintf(stderr, "\n");
				fprintf(stderr, "  -h, --help                        display this help and exit\n");
				fprintf(stderr, "  -v, --verbose                     turn on verbose output\n");
				fprintf(stderr, "  -S, --stop                        request renderd to stop and exit");
				fprintf(stderr, "  -V, --version                     display the version number and exit\n");
				fprintf(stderr, "\n");
				fprintf(stderr, "If you are using --all, you can restrict the tile range by adding these options:\n");
				fprintf(stderr, "(please note that tile coordinates must be positive integers and are not latitude and longitude values)\n");
				fprintf(stderr, "  -G, --max-lat=LATITUDE            maximum latitude\n");
				fprintf(stderr, "  -g, --min-lat=LATITUDE            minimum latitude\n");
				fprintf(stderr, "  -W, --max-lon=LONGITUDE           maximum longitude\n");
				fprintf(stderr, "  -w, --min-lon=LONGITUDE           minimum longitude\n");
				fprintf(stderr, "  -X, --max-x=X                     maximum X tile coordinate\n");
				fprintf(stderr, "  -x, --min-x=X                     minimum X tile coordinate\n");
				fprintf(stderr, "  -Y, --max-y=Y                     maximum Y tile coordinate\n");
				fprintf(stderr, "  -y, --min-y=Y                     minimum Y tile coordinate\n");
				fprintf(stderr, "\n");
				fprintf(stderr, "Without --all, send a list of tiles to be rendered from STDIN in the format:\n");
				fprintf(stderr, "  X Y Z\n");
				fprintf(stderr, "e.g.\n");
				fprintf(stderr, "  0 0 1\n");
				fprintf(stderr, "  0 1 1\n");
				fprintf(stderr, "  1 0 1\n");
				fprintf(stderr, "  1 1 1\n");
				fprintf(stderr, "The above would cause all 4 tiles at zoom 1 to be rendered\n");
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

	if (config_file_name_passed) {
		int map_section_num = -1;
		process_config_file(config_file_name, 0, G_LOG_LEVEL_DEBUG);

		for (int i = 0; i < XMLCONFIGS_MAX; ++i) {
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

	if (all) {
		if (min_lat_passed && min_lon_passed && max_lat_passed && max_lon_passed) {
			if (min_x_passed || min_y_passed || max_x_passed || max_y_passed) {
				g_logger(G_LOG_LEVEL_CRITICAL, "min-lat, min-lon, max-lat & max-lon cannot be used together with min-x, max-x, min-y, or max-y");
				return 1;
			}

			if (max_lat < min_lat) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min-lat (%f) is larger than max-lat (%f).", min_lat, max_lat);
				return 1;
			}

			if (max_lon < min_lon) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min-lon (%f) is larger than max-lon (%f).", min_lon, max_lon);
				return 1;
			}
		}

		if (min_x_passed || min_y_passed || max_x_passed || max_y_passed) {
			if (min_zoom != max_zoom) {
				g_logger(G_LOG_LEVEL_CRITICAL, "min-zoom must be equal to max-zoom when using min-x, max-x, min-y, or max-y options");
				return 1;
			}

			if (min_x_passed && max_x_passed && max_x < min_x) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min-x (%i) is larger than max-x (%i).", min_x, max_x);
				return 1;
			}

			if (min_y_passed && max_y_passed && max_y < min_y) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min-y (%i) is larger than max-y (%i).", min_y, max_y);
				return 1;
			}
		}

		if (!min_x_passed) {
			min_x = 0;
		}

		if (!min_y_passed) {
			min_y = 0;
		}

		int lz = (1 << min_zoom) - 1;

		if (min_zoom == max_zoom) {
			if (!max_x_passed) {
				max_x = lz;
			}

			if (!max_y_passed) {
				max_y = lz;
			}

			if (min_x > lz || min_y > lz || max_x > lz || max_y > lz) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Invalid range, x and y values must be <= %d (2^zoom-1)", lz);
				return 1;
			}
		}
	}

	if (stop_renderd != 0) {
		g_logger(G_LOG_LEVEL_INFO, "Sending STOP comand to renderd");
		int fd = make_connection(socketname);

		if (fd < 0) {
			g_logger(G_LOG_LEVEL_ERROR, "connect failed for: %s", socketname);
			return 1;
		}

		struct protocol cmd;
		bzero(&cmd, sizeof(cmd));
		cmd.ver = 1;
		cmd.cmd = cmdStop;
		if (send_cmd(&cmd, fd) < 1) {
			g_logger(G_LOG_LEVEL_ERROR, "send error: %s", strerror(errno));
		}
	
		struct protocol rsp;
		bzero(&rsp, sizeof(rsp));

		g_logger(G_LOG_LEVEL_DEBUG, "Waiting for response");

		int ret = recv_cmd(&rsp, fd, 1);

		if (ret < 1) {
			return 0;
		}

		g_logger(G_LOG_LEVEL_DEBUG, "Got response %i", rsp.cmd);
		return 0;
	}

	store = init_storage_backend(tile_dir);

	if (store == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to initialise storage backend %s", tile_dir);
		return 1;
	}

	g_logger(G_LOG_LEVEL_INFO, "Started render_list with the following options:");

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

	spawn_workers(num_threads, socketname, max_load);

	if (all) {
		g_logger(G_LOG_LEVEL_MESSAGE, "Rendering all tiles from zoom %d to zoom %d", min_zoom, max_zoom);

		for (z = min_zoom; z <= max_zoom; z++) {
			int current_max_x = max_x_passed ? max_x : (1 << z) - 1;
			int current_max_y = max_y_passed ? max_y : (1 << z) - 1;

			if (min_lat_passed && min_lon_passed && max_lat_passed && max_lon_passed) {
				int max_x_tmp = lon2tilex(max_lon, z);
				int max_y_tmp = lat2tiley(min_lat, z);
				int min_x_tmp = lon2tilex(min_lon, z);
				int min_y_tmp = lat2tiley(max_lat, z);
				current_max_x = max_x_tmp ? max_x_tmp - 1 : max_x_tmp;
				current_max_y = max_y_tmp;
				min_x = min_x_tmp;
				min_y = min_y_tmp;
			}

			g_logger(G_LOG_LEVEL_MESSAGE, "Rendering all tiles for zoom %i from (%i, %i) to (%i, %i)", z, min_x, min_y, current_max_x, current_max_y);

			for (x = min_x; x <= current_max_x; x += METATILE) {
				for (y = min_y; y <= current_max_y; y += METATILE) {
					if (!force) {
						s = store->tile_stat(store, mapname, "", x, y, z);
					}

					if (force || (s.size < 0) || (s.expired)) {
						enqueue(mapname, x, y, z);
						num_render++;
					}

					num_all++;
				}
			}
		}
	} else {
		while (!feof(stdin)) {
			int n = fscanf(stdin, "%d %d %d", &x, &y, &z);

			if (n != 3) {
				// Discard input line
				char tmp[1024];
				const char *r = fgets(tmp, sizeof(tmp), stdin);

				if (!r) {
					continue;
				}

				if (verbose) {
					g_logger(G_LOG_LEVEL_WARNING, "bad line %d: %s", num_all, tmp);
				}

				continue;
			}

			if (verbose) {
				g_logger(G_LOG_LEVEL_MESSAGE, "got: x(%d) y(%d) z(%d)", x, y, z);
			}

			if (z < min_zoom || z > max_zoom) {
				g_logger(G_LOG_LEVEL_MESSAGE, "Ignoring tile, zoom %d outside valid range (%d..%d)", z, min_zoom, max_zoom);
				continue;
			}

			num_all++;

			if (!force) {
				s = store->tile_stat(store, mapname, "", x, y, z);
			}

			if (force || (s.size < 0) || (s.expired)) {
				// missing or old, render it
				enqueue(mapname, x, y, z);
				num_render++;

				// Attempts to adjust the stats for the QMAX tiles which are likely in the queue
				if (!(num_render % 10)) {
					gettimeofday(&end, NULL);
					g_logger(G_LOG_LEVEL_MESSAGE, "Metatiles rendered:");
					display_rate(start, end, num_render);
					g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles rendered:");
					display_rate(start, end, num_render * METATILE * METATILE);
					g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles handled:");
					display_rate(start, end, num_all);
				}
			} else {
				if (verbose) {
					char name[PATH_MAX];
					g_logger(G_LOG_LEVEL_MESSAGE, "Tile %s is clean, ignoring", store->tile_storage_id(store, mapname, "", x, y, z, name));
				}
			}
		}
	}

	finish_workers();

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

	gettimeofday(&end, NULL);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total for all tiles rendered");
	g_logger(G_LOG_LEVEL_MESSAGE, "Metatiles rendered:");
	display_rate(start, end, num_render);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles rendered:");
	display_rate(start, end, num_render * METATILE * METATILE);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total tiles handled:");
	display_rate(start, end, num_all);
	print_statistics();

	return 0;
}
#endif
