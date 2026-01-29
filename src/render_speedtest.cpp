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
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "render_config.h"
#include "render_submit_queue.h"
#include "renderd_config.h"

#define DEG_TO_RAD (M_PI / 180)
#define RAD_TO_DEG (180 / M_PI)

#ifndef METATILE
#warning("Speed test not implemented for non-metatile mode. Feel free to submit fix")
int main(int argc, char **argv)
{
	fprintf(stderr, "Speed test not implemented for non-metatile mode. Feel free to submit fix!\n");
	return -1;
}

#else

#if 1
static double boundx0 = -0.5;
static double boundy0 = 51.25;
static double boundx1 = 0.5;
static double boundy1 = 51.75;
#endif
#if 0
//    bbox = (-6.0, 50.0,3.0,58.0)
static double boundx0 = -6.0;
static double boundy0 = 50.0;
static double boundx1 = 3.0;
static double boundy1 = 58.0;
#endif
#if 0
// UK: 49.7,-7.6, 58.8, 3.2
static double boundx0 = -7.6;
static double boundy0 = 49.7;
static double boundx1 = 3.2;
static double boundy1 = 58.8;
#endif

static double minmax(double a, double b, double c)
{
	a = MAX(a, b);
	a = MIN(a, c);
	return a;
}

class GoogleProjection
{
	double *Ac, *Bc, *Cc, *zc;

public:
	GoogleProjection(int levels = 18)
	{
		Ac = new double[levels];
		Bc = new double[levels];
		Cc = new double[levels];
		zc = new double[levels];
		int d, c = 256;

		for (d = 0; d < levels; d++) {
			int e = c / 2;
			Bc[d] = c / 360.0;
			Cc[d] = c / (2 * M_PI);
			zc[d] = e;
			Ac[d] = c;
			c *= 2;
		}
	}

	void fromLLtoPixel(double &x, double &y, int zoom)
	{
		double d = zc[zoom];
		double f = minmax(sin(DEG_TO_RAD * y), -0.9999, 0.9999);
		x = round(d + x * Bc[zoom]);
		y = round(d + 0.5 * log((1 + f) / (1 - f)) * -Cc[zoom]);
	}

	void fromPixelToLL(double &x, double &y, int zoom)
	{
		double e = zc[zoom];
		double g = (y - e) / -Cc[zoom];
		x = (x - e) / Bc[zoom];
		y = RAD_TO_DEG * (2 * atan(exp(g)) - 0.5 * M_PI);
	}
};

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
	int max_zoom_default = MAX_ZOOM;
	int min_zoom_default = 0;
	int num_threads_default = 1;

	const char *config_file_name = config_file_name_default;
	const char *mapname = mapname_default;
	const char *socketname = socketname_default;
	int max_zoom = max_zoom_default;
	int min_zoom = min_zoom_default;
	int num_threads = num_threads_default;

	int config_file_name_passed = 0;
	int mapname_passed = 0;
	int socketname_passed = 0;
	int max_zoom_passed = 0;
	int min_zoom_passed = 0;
	int num_threads_passed = 0;

	int z;
	struct timeval start, end;
	struct timeval start_all, end_all;
	int num_all = 0;

	foreground = 1;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"config",      required_argument, 0, 'c'},
			{"map",         required_argument, 0, 'm'},
			{"max-zoom",    required_argument, 0, 'Z'},
			{"min-zoom",    required_argument, 0, 'z'},
			{"num-threads", required_argument, 0, 'n'},
			{"socket",      required_argument, 0, 's'},

			{"help",        no_argument,       0, 'h'},
			{"version",     no_argument,       0, 'V'},
			{0, 0, 0, 0}
		};

		int c = getopt_long(argc, argv, "c:m:Z:z:n:s:hV", long_options, &option_index);

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

			case 'h': /* -h, --help */
				fprintf(stderr, "Usage: render_speedtest [OPTION] ...\n");
				fprintf(stderr, "  -c, --config=CONFIG               specify the renderd config file (default is off)\n");
				fprintf(stderr, "  -m, --map=MAP                     render tiles in this map (default is '%s')\n", mapname_default);
				fprintf(stderr, "  -n, --num-threads=N               the number of parallel request threads (default is '%d')\n", num_threads_default);
				fprintf(stderr, "  -s, --socket=SOCKET|HOSTNAME:PORT unix domain socket name or hostname and port for contacting renderd (default is '%s')\n", socketname_default);
				fprintf(stderr, "  -Z, --max-zoom=ZOOM               only render tiles less than or equal to this zoom level (default is '%d')\n", max_zoom_default);
				fprintf(stderr, "  -z, --min-zoom=ZOOM               only render tiles greater than or equal to this zoom level (default is '%d')\n", min_zoom_default);
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
			socketname = strndup(config->socketname, PATH_MAX);
			socketname_passed = 1;
		}
	}

	g_logger(G_LOG_LEVEL_INFO, "Started render_speedtest with the following options:");

	if (config_file_name_passed) {
		g_logger(G_LOG_LEVEL_INFO, "\t--config      = '%s' (user-specified)", config_file_name);
	}

	g_logger(G_LOG_LEVEL_INFO, "\t--map         = '%s' (%s)", mapname, mapname_passed ? "user-specified" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--max-zoom    = '%i' (%s)", max_zoom, max_zoom_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--min-zoom    = '%i' (%s)", min_zoom, min_zoom_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--num-threads = '%i' (%s)", num_threads, num_threads_passed ? "user-specified/from config" : "default");
	g_logger(G_LOG_LEVEL_INFO, "\t--socket      = '%s' (%s)", socketname, socketname_passed ? "user-specified/from config" : "default");

	static GoogleProjection gprj(max_zoom + 1);

	spawn_workers(num_threads, socketname, 1000);

	// Render something to counter act the startup costs
	// of obtaining the Postgis table extents

	g_logger(G_LOG_LEVEL_MESSAGE, "Initial startup costs");
	gettimeofday(&start, NULL);
	enqueue(mapname, 0, 0, 0);
	gettimeofday(&end, NULL);
	display_rate(start, end, 1);

	gettimeofday(&start_all, NULL);

	for (z = min_zoom; z <= max_zoom; z++) {
		double px0 = boundx0;
		double py0 = boundy1;
		double px1 = boundx1;
		double py1 = boundy0;

		gprj.fromLLtoPixel(px0, py0, z);
		gprj.fromLLtoPixel(px1, py1, z);

		int x, xmin, xmax;
		xmin = (int)(px0 / 256.0);
		xmax = (int)(px1 / 256.0);

		int y, ymin, ymax;
		ymin = (int)(py0 / 256.0);
		ymax = (int)(py1 / 256.0);

		int num = (xmax - xmin + 1) * (ymax - ymin + 1);

		g_logger(G_LOG_LEVEL_MESSAGE, "Zoom(%d) Now rendering %d tiles", z, num);
		num_all += num;
		gettimeofday(&start, NULL);

		for (x = xmin; x <= xmax; x++) {
			for (y = ymin; y <= ymax; y++) {
				enqueue(mapname, x, y, z);
			}
		}

		wait_for_empty_queue();
		gettimeofday(&end, NULL);
		display_rate(start, end, num);
	}

	finish_workers();

	gettimeofday(&end_all, NULL);
	g_logger(G_LOG_LEVEL_MESSAGE, "Total for all tiles rendered");
	display_rate(start_all, end_all, num_all);

	free_map_sections(maps);
	free_renderd_sections(config_slaves);

	return 0;
}

#endif
