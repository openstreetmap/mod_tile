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

#include "config.h"
#include "g_logger.h"
#include "render_config.h"
#include "renderd.h"

#ifdef HAVE_INIPARSER_INIPARSER_H
#include <iniparser/iniparser.h>
#else
#include <iniparser.h>
#endif

int num_slave_threads;

renderd_config config;
renderd_config config_slaves[MAX_SLAVES];
xmlconfigitem maps[XMLCONFIGS_MAX];

static void copy_string_with_snprintf(const char *src, char **dest, size_t maxlen)
{
	int len, size;

	size = sizeof(char) * strnlen(src, maxlen) + sizeof(char);
	*dest = malloc(size);

	if (*dest == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "malloc error");
		exit(7);
	}

	len = snprintf(*dest, size, "%s", src);

	if (len < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "snprintf encoding error");
		exit(7);
	} else if (len >= maxlen) {
		g_logger(G_LOG_LEVEL_CRITICAL, "snprintf buffer too small");
		exit(7);
	}
}

static char *name_with_section(const char *section, const char *name)
{
	int len, maxlen = INILINE_MAX - 1;
	char *key;

	key = malloc(sizeof(char) * maxlen);

	if (key == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "malloc error");
		exit(7);
	}

	len = snprintf(key, maxlen, "%s:%s", section, name);

	if (len < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "snprintf encoding error");
		exit(7);
	} else if (len >= maxlen) {
		g_logger(G_LOG_LEVEL_CRITICAL, "snprintf buffer too small");
		exit(7);
	}

	return key;
}

static void process_config_bool(const dictionary *ini, const char *section, const char *name, int *dest, int notfound)
{
	char *key = name_with_section(section, name);
	int src = iniparser_getboolean(ini, key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src ? "true" : "false");

	*dest = src;

	free(key);
}

static void process_config_double(const dictionary *ini, const char *section, const char *name, double *dest, double notfound)
{
	char *key = name_with_section(section, name);
	double src = iniparser_getdouble(ini, key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%lf'", key, src);

	*dest = src;

	free(key);
}

static void process_config_int(const dictionary *ini, const char *section, const char *name, int *dest, int notfound)
{
	char *key = name_with_section(section, name);
	int src = iniparser_getint(ini, key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%i'", key, src);

	*dest = src;

	free(key);
}

static void process_config_string(const dictionary *ini, const char *section, const char *name, char **dest, char *notfound, int maxlen)
{
	int len;
	char *key = name_with_section(section, name);
	const char *src = iniparser_getstring(ini, key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src);

	copy_string_with_snprintf(src, dest, maxlen);

	free(key);
}

int min_max_int_opt(const char *opt_arg, const char *opt_type_name, int minimum, int maximum)
{
	int opt;
	float opt_float;

	if (sscanf(opt_arg, "%i", &opt) != 1) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be an integer (%s was provided)", opt_type_name, opt_arg);
		exit(1);
	}

	if (minimum != -1 && opt < minimum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be >= %i (%s was provided)", opt_type_name, minimum, opt_arg);
		exit(1);
	}

	if (maximum != -1 && opt > maximum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be <= %i (%s was provided)", opt_type_name, maximum, opt_arg);
		exit(1);
	}

	if (sscanf(opt_arg, "%f", &opt_float) == 1) {
		if ((float)opt != opt_float) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be an integer (%s was provided)", opt_type_name, opt_arg);
			exit(1);
		}
	}

	return opt;
}

void process_config_file(const char *config_file_name, int active_slave, int log_level)
{
	int i, map_section_num = -1;
	bzero(&config, sizeof(renderd_config));
	bzero(config_slaves, sizeof(renderd_config) * MAX_SLAVES);
	bzero(maps, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

	g_logger(log_level, "Parsing renderd config file '%s':", config_file_name);
	dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file: %s", config_file_name);
		exit(1);
	}

	num_slave_threads = 0;

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);

		if (strncmp(section, "renderd", 7) == 0) {
			/* this is a renderd config section */
			int renderd_section_num = 0;

			if (sscanf(section, "renderd%i", &renderd_section_num) != 1) {
				renderd_section_num = 0;
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section %i: %s", renderd_section_num, section);

			if (renderd_section_num >= MAX_SLAVES) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i renderd config sections", MAX_SLAVES);
				exit(7);
			}

			process_config_int(ini, section, "ipport", &config_slaves[renderd_section_num].ipport, 0);
			process_config_int(ini, section, "num_threads", &config_slaves[renderd_section_num].num_threads, NUM_THREADS);
			process_config_string(ini, section, "iphostname", &config_slaves[renderd_section_num].iphostname, "", INILINE_MAX - 1);
			process_config_string(ini, section, "pid_file", &config_slaves[renderd_section_num].pid_filename, RENDERD_PIDFILE, PATH_MAX - 1);
			process_config_string(ini, section, "socketname", &config_slaves[renderd_section_num].socketname, RENDERD_SOCKET, PATH_MAX - 1);
			process_config_string(ini, section, "stats_file", &config_slaves[renderd_section_num].stats_filename, "", PATH_MAX - 1);
			process_config_string(ini, section, "tile_dir", &config_slaves[renderd_section_num].tile_dir, RENDERD_TILE_DIR, PATH_MAX - 1);

			if (config_slaves[renderd_section_num].num_threads == -1) {
				config_slaves[renderd_section_num].num_threads = sysconf(_SC_NPROCESSORS_ONLN);
			}

			if (renderd_section_num == active_slave) {
				config = config_slaves[renderd_section_num];
			} else {
				num_slave_threads += config_slaves[renderd_section_num].num_threads;
			}
		}
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing mapnik config section");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);

		if (strcmp(section, "mapnik") == 0) {
			/* this is a mapnik config section */
			if (config.num_threads == 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "No valid (active) renderd config section available");
				exit(7);
			}

			process_config_bool(ini, section, "font_dir_recurse", &config.mapnik_font_dir_recurse, MAPNIK_FONTS_DIR_RECURSE);
			process_config_string(ini, section, "font_dir", &config.mapnik_font_dir, MAPNIK_FONTS_DIR, PATH_MAX - 1);
			process_config_string(ini, section, "plugins_dir", &config.mapnik_plugins_dir, MAPNIK_PLUGINS_DIR, PATH_MAX - 1);
		}
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);

		if (strncmp(section, "renderd", 7) && strcmp(section, "mapnik")) {
			/* this is a map config section */
			if (config.num_threads == 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "No valid (active) renderd config section available");
				exit(7);
			}

			map_section_num++;

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section %i: %s", map_section_num, section);

			if (map_section_num >= XMLCONFIGS_MAX) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i map config sections", XMLCONFIGS_MAX);
				exit(7);
			}

			copy_string_with_snprintf(section, &maps[map_section_num].xmlname, XMLCONFIG_MAX - 1);

			process_config_int(ini, section, "aspectx", &maps[map_section_num].aspect_x, 1);
			process_config_int(ini, section, "aspecty", &maps[map_section_num].aspect_y, 1);
			process_config_int(ini, section, "tilesize", &maps[map_section_num].tile_px_size, 256);
			process_config_string(ini, section, "attribution", &maps[map_section_num].attribution, "", PATH_MAX - 1);
			process_config_string(ini, section, "cors", &maps[map_section_num].cors, "", PATH_MAX - 1);
			process_config_string(ini, section, "description", &maps[map_section_num].description, "", PATH_MAX - 1);
			process_config_string(ini, section, "host", &maps[map_section_num].host, "", PATH_MAX - 1);
			process_config_string(ini, section, "htcphost", &maps[map_section_num].htcpip, "", PATH_MAX - 1);
			process_config_string(ini, section, "parameterize_style", &maps[map_section_num].parameterization, "", PATH_MAX - 1);
			process_config_string(ini, section, "server_alias", &maps[map_section_num].server_alias, "", PATH_MAX - 1);
			process_config_string(ini, section, "tiledir", &maps[map_section_num].tile_dir, config.tile_dir, PATH_MAX - 1);
			process_config_string(ini, section, "uri", &maps[map_section_num].xmluri, "", PATH_MAX - 1);
			process_config_string(ini, section, "xml", &maps[map_section_num].xmlfile, "", PATH_MAX - 1);

			process_config_double(ini, section, "scale", &maps[map_section_num].scale_factor, 1.0);

			if (maps[map_section_num].scale_factor < 0.1) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too small, must be greater than or equal to %lf.", maps[map_section_num].scale_factor, 0.1);
				exit(7);
			} else if (maps[map_section_num].scale_factor > 8.0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too large, must be less than or equal to %lf.", maps[map_section_num].scale_factor, 8.0);
				exit(7);
			}

			process_config_int(ini, section, "maxzoom", &maps[map_section_num].max_zoom, 18);

			if (maps[map_section_num].max_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too small, must be greater than or equal to %i.", maps[map_section_num].max_zoom, 0);
				exit(7);
			} else if (maps[map_section_num].max_zoom > MAX_ZOOM) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too large, must be less than or equal to %i.", maps[map_section_num].max_zoom, MAX_ZOOM);
				exit(7);
			}

			process_config_int(ini, section, "minzoom", &maps[map_section_num].min_zoom, 0);

			if (maps[map_section_num].min_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is too small, must be greater than or equal to %i.", maps[map_section_num].min_zoom, 0);
				exit(7);
			} else if (maps[map_section_num].min_zoom > maps[map_section_num].max_zoom) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is larger than max zoom (%i).", maps[map_section_num].min_zoom, maps[map_section_num].max_zoom);
				exit(7);
			}

			char ini_fileExtension[] = "png";
			char ini_mimeType[] = "image/png";
			char ini_outputFormat[] = "png256";
			char *ini_type;

			process_config_string(ini, section, "type", &ini_type, "png image/png png256", INILINE_MAX - 1);

			sscanf(ini_type, "%[^ ] %[^ ] %[^;#]", ini_fileExtension, ini_mimeType, ini_outputFormat);
			copy_string_with_snprintf(ini_outputFormat, &maps[map_section_num].output_format, INILINE_MAX - 1);
			free(ini_type);

			/* Pass this information into the rendering threads,
			 * as it is needed to configure mapniks number of connections
			 */
			maps[map_section_num].num_threads = config.num_threads;
		}
	}

	iniparser_freedict(ini);

	if (config.ipport > 0) {
		g_logger(log_level, "\trenderd: ip socket = '%s':%i", config.iphostname, config.ipport);
	} else {
		g_logger(log_level, "\trenderd: unix socketname = '%s'", config.socketname);
	}

	g_logger(log_level, "\trenderd: num_threads = '%i'", config.num_threads);

	if (active_slave == 0) {
		g_logger(log_level, "\trenderd: num_slave_threads = '%i'", num_slave_threads);
	}

	g_logger(log_level, "\trenderd: pid_file = '%s'", config.pid_filename);

	if (strnlen(config.stats_filename, PATH_MAX - 1)) {
		g_logger(log_level, "\trenderd: stats_file = '%s'", config.stats_filename);
	}

	g_logger(log_level, "\trenderd: tile_dir = '%s'", config.tile_dir);
	g_logger(log_level, "\tmapnik:  font_dir = '%s'", config.mapnik_font_dir);
	g_logger(log_level, "\tmapnik:  font_dir_recurse = '%s'", config.mapnik_font_dir_recurse ? "true" : "false");
	g_logger(log_level, "\tmapnik:  plugins_dir = '%s'", config.mapnik_plugins_dir);

	for (i = 0; i < XMLCONFIGS_MAX; i++) {
		if (maps[i].xmlname != NULL) {
			g_logger(log_level, "\tmap %i:   name(%s) file(%s) uri(%s) output_format(%s) htcp(%s) host(%s)", i, maps[i].xmlname, maps[i].xmlfile, maps[i].xmluri, maps[i].output_format, maps[i].htcpip, maps[i].host);
		}
	}

	for (i = 0; i < MAX_SLAVES; i++) {
		if (config_slaves[i].num_threads == 0) {
			continue;
		}

		if (i == active_slave) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): Active", i);
		}

		if (config_slaves[i].ipport > 0) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): ip socket = '%s:%i'", i, config_slaves[i].iphostname, config_slaves[i].ipport);
		} else {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): unix socketname = '%s'", i, config_slaves[i].socketname);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): num_threads = '%i'", i, config_slaves[i].num_threads);
		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): pid_file = '%s'", i, config_slaves[i].pid_filename);

		if (strnlen(config_slaves[i].stats_filename, PATH_MAX - 1)) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): stats_file = '%s'", i, config_slaves[i].stats_filename);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): tile_dir = '%s'", i, config_slaves[i].tile_dir);
	}
}
