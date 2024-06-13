/*
 * Copyright (c) 2007 - 2024 by mod_tile contributors (see AUTHORS file)
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

#define _GNU_SOURCE

#include <sys/un.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "render_config.h"
#include "renderd.h"

#ifdef HAVE_INIPARSER_INIPARSER_H
#include <iniparser/iniparser.h>
#else
#include <iniparser.h>
#endif

static void copy_string(const char *src, const char **dest, size_t maxlen)
{
	size_t size = sizeof(char) * strnlen(src, maxlen);

	*dest = strndup(src, size);

	if (*dest == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "copy_string: strndup error");
		exit(7);
	}
}

static char *name_with_section(const char *section, const char *name)
{
	int len;
	char *key;

	len = asprintf(&key, "%s:%s", section, name);

	if (len == -1) {
		g_logger(G_LOG_LEVEL_CRITICAL, "name_with_section: asprintf error");
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

static void process_config_string(const dictionary *ini, const char *section, const char *name, const char **dest, const char *notfound, size_t maxlen)
{
	char *key = name_with_section(section, name);
	const char *src = iniparser_getstring(ini, key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src);

	copy_string(src, dest, maxlen);

	free(key);
}

void free_map_section(xmlconfigitem map_section)
{
	free((void *)map_section.attribution);
	free((void *)map_section.cors);
	free((void *)map_section.description);
	free((void *)map_section.file_extension);
	free((void *)map_section.host);
	free((void *)map_section.htcpip);
	free((void *)map_section.mime_type);
	free((void *)map_section.output_format);
	free((void *)map_section.parameterization);
	free((void *)map_section.server_alias);
	free((void *)map_section.tile_dir);
	free((void *)map_section.xmlfile);
	free((void *)map_section.xmlname);
	free((void *)map_section.xmluri);
	bzero(&map_section, sizeof(xmlconfigitem));
}

void free_map_sections(xmlconfigitem *map_sections)
{
	for (int i = 0; i < XMLCONFIGS_MAX; i++) {
		if (map_sections[i].xmlname != NULL) {
			free_map_section(map_sections[i]);
		}
	}
}

void free_renderd_section(renderd_config renderd_section)
{
	free((void *)renderd_section.iphostname);
	free((void *)renderd_section.mapnik_font_dir);
	free((void *)renderd_section.mapnik_plugins_dir);
	free((void *)renderd_section.name);
	free((void *)renderd_section.pid_filename);
	free((void *)renderd_section.socketname);
	free((void *)renderd_section.stats_filename);
	free((void *)renderd_section.tile_dir);
	bzero(&renderd_section, sizeof(renderd_config));
}

void free_renderd_sections(renderd_config *renderd_sections)
{
	for (int i = 0; i < MAX_SLAVES; i++) {
		if (renderd_sections[i].num_threads != 0) {
			free_renderd_section(renderd_sections[i]);
		}
	}
}

double min_max_double_opt(const char *opt_arg, const char *opt_type_name, double minimum, double maximum)
{
	char *endptr;
	double opt = strtod(opt_arg, &endptr);

	if (endptr == opt_arg) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be a double (%s was provided)", opt_type_name, opt_arg);
		exit(1);
	} else if (minimum != -1 && opt < minimum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be >= %f (%s was provided)", opt_type_name, minimum, opt_arg);
		exit(1);
	} else if (maximum != -1 && opt > maximum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be <= %f (%s was provided)", opt_type_name, maximum, opt_arg);
		exit(1);
	}

	return opt;
}

int min_max_int_opt(const char *opt_arg, const char *opt_type_name, int minimum, int maximum)
{
	char *endptr, *endptr_float;
	int opt = strtol(opt_arg, &endptr, 10);
	float opt_float = strtof(opt_arg, &endptr_float);

	if (endptr == opt_arg || endptr_float == opt_arg || (float)opt != opt_float) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be an integer (%s was provided)", opt_type_name, opt_arg);
		exit(1);
	} else if (minimum != -1 && opt < minimum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be >= %i (%s was provided)", opt_type_name, minimum, opt_arg);
		exit(1);
	} else if (maximum != -1 && opt > maximum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be <= %i (%s was provided)", opt_type_name, maximum, opt_arg);
		exit(1);
	}

	return opt;
}

void process_map_sections(const char *config_file_name, xmlconfigitem *maps_dest, const char *default_tile_dir, int num_threads)
{
	int map_section_num = -1;

	dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file: %s", config_file_name);
		exit(1);
	}

	bzero(maps_dest, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);

		if (strncmp(section, "renderd", 7) && strcmp(section, "mapnik")) { // this is a map config section
			char *ini_type_copy, *ini_type_part, *ini_type_context;
			const char *ini_type;
			int ini_type_part_maxlen = 64, ini_type_part_num = 0;

			map_section_num++;
			g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section %i: %s", map_section_num, section);

			if (map_section_num >= XMLCONFIGS_MAX) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i map config sections", XMLCONFIGS_MAX);
				exit(7);
			}

			copy_string(section, &maps_dest[map_section_num].xmlname, XMLCONFIG_MAX);

			process_config_int(ini, section, "aspectx", &maps_dest[map_section_num].aspect_x, 1);
			process_config_int(ini, section, "aspecty", &maps_dest[map_section_num].aspect_y, 1);
			process_config_int(ini, section, "tilesize", &maps_dest[map_section_num].tile_px_size, 256);
			process_config_string(ini, section, "attribution", &maps_dest[map_section_num].attribution, "", PATH_MAX);
			process_config_string(ini, section, "cors", &maps_dest[map_section_num].cors, "", PATH_MAX);
			process_config_string(ini, section, "description", &maps_dest[map_section_num].description, "", PATH_MAX);
			process_config_string(ini, section, "host", &maps_dest[map_section_num].host, "", PATH_MAX);
			process_config_string(ini, section, "htcphost", &maps_dest[map_section_num].htcpip, "", PATH_MAX);
			process_config_string(ini, section, "parameterize_style", &maps_dest[map_section_num].parameterization, "", PATH_MAX);
			process_config_string(ini, section, "server_alias", &maps_dest[map_section_num].server_alias, "", PATH_MAX);
			process_config_string(ini, section, "tiledir", &maps_dest[map_section_num].tile_dir, default_tile_dir, PATH_MAX);
			process_config_string(ini, section, "uri", &maps_dest[map_section_num].xmluri, "", PATH_MAX);
			process_config_string(ini, section, "xml", &maps_dest[map_section_num].xmlfile, "", PATH_MAX);

			process_config_double(ini, section, "scale", &maps_dest[map_section_num].scale_factor, 1.0);

			if (maps_dest[map_section_num].scale_factor < 0.1) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too small, must be greater than or equal to %lf.", maps_dest[map_section_num].scale_factor, 0.1);
				exit(7);
			} else if (maps_dest[map_section_num].scale_factor > 8.0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too large, must be less than or equal to %lf.", maps_dest[map_section_num].scale_factor, 8.0);
				exit(7);
			}

			process_config_int(ini, section, "maxzoom", &maps_dest[map_section_num].max_zoom, MAX_ZOOM);

			if (maps_dest[map_section_num].max_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too small, must be greater than or equal to %i.", maps_dest[map_section_num].max_zoom, 0);
				exit(7);
			} else if (maps_dest[map_section_num].max_zoom > MAX_ZOOM) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too large, must be less than or equal to %i.", maps_dest[map_section_num].max_zoom, MAX_ZOOM);
				exit(7);
			}

			process_config_int(ini, section, "minzoom", &maps_dest[map_section_num].min_zoom, 0);

			if (maps_dest[map_section_num].min_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is too small, must be greater than or equal to %i.", maps_dest[map_section_num].min_zoom, 0);
				exit(7);
			} else if (maps_dest[map_section_num].min_zoom > maps_dest[map_section_num].max_zoom) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is larger than max zoom (%i).", maps_dest[map_section_num].min_zoom, maps_dest[map_section_num].max_zoom);
				exit(7);
			}

			process_config_string(ini, section, "type", &ini_type, "png image/png png256", INILINE_MAX);
			ini_type_copy = strndup(ini_type, INILINE_MAX);

			for (ini_type_part = strtok_r(ini_type_copy, " ", &ini_type_context);
					ini_type_part;
					ini_type_part = strtok_r(NULL, " ", &ini_type_context)) {
				switch (ini_type_part_num) {
					case 0:
						copy_string(ini_type_part, &maps_dest[map_section_num].file_extension, ini_type_part_maxlen);
						break;

					case 1:
						copy_string(ini_type_part, &maps_dest[map_section_num].mime_type, ini_type_part_maxlen);
						break;

					case 2:
						copy_string(ini_type_part, &maps_dest[map_section_num].output_format, ini_type_part_maxlen);
						break;

					default:
						g_logger(G_LOG_LEVEL_CRITICAL, "Specified type (%s) has too many parts, there must be no more than 3, e.g., 'png image/png png256'.", ini_type);
						exit(7);
				}

				ini_type_part_num++;
			}

			if (ini_type_part_num < 2) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified type (%s) has too few parts, there must be at least 2, e.g., 'png image/png'.", ini_type);
				exit(7);
			}

			if (ini_type_part_num < 3) {
				copy_string("png256", &maps_dest[map_section_num].output_format, ini_type_part_maxlen);
			}

			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:file_extension: '%s'", section, "type", maps_dest[map_section_num].file_extension);
			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:mime_type: '%s'", section, "type", maps_dest[map_section_num].mime_type);
			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:output_format: '%s'", section, "type", maps_dest[map_section_num].output_format);

			/* Pass this information into the rendering threads,
			 * as it is needed to configure mapniks number of connections
			 */
			maps_dest[map_section_num].num_threads = num_threads;

			free(ini_type_copy);
			free(ini_type_part);
			free((void *)ini_type);
		}
	}

	iniparser_freedict(ini);

	if (map_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No map config sections were found in file: %s", config_file_name);
		exit(1);
	}
}

void process_mapnik_section(const char *config_file_name, renderd_config *config_dest)
{
	int mapnik_section_num = -1;

	dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file: %s", config_file_name);
		exit(1);
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing mapnik config section");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);

		if (strcmp(section, "mapnik") == 0) { // this is a mapnik config section
			mapnik_section_num = section_num;
			process_config_bool(ini, section, "font_dir_recurse", &config_dest->mapnik_font_dir_recurse, MAPNIK_FONTS_DIR_RECURSE);
			process_config_string(ini, section, "font_dir", &config_dest->mapnik_font_dir, MAPNIK_FONTS_DIR, PATH_MAX);
			process_config_string(ini, section, "plugins_dir", &config_dest->mapnik_plugins_dir, MAPNIK_PLUGINS_DIR, PATH_MAX);
			break;
		}
	}

	iniparser_freedict(ini);

	if (mapnik_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No mapnik config section was found in file: %s", config_file_name);
		exit(1);
	}
}

void process_renderd_sections(const char *config_file_name, renderd_config *configs_dest)
{
	int renderd_section_num = -1;
	int renderd_socketname_maxlen = sizeof(((struct sockaddr_un *)0)->sun_path);

	dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file: %s", config_file_name);
		exit(1);
	}

	bzero(configs_dest, sizeof(renderd_config) * MAX_SLAVES);

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(ini); section_num++) {
		const char *section = iniparser_getsecname(ini, section_num);
		int renderd_strlen = 7;

		if (strncmp(section, "renderd", renderd_strlen) == 0) { // this is a renderd config section
			if (strcmp(section, "renderd") == 0 || strcmp(section, "renderd0") == 0) {
				renderd_section_num = 0;
			} else {
				char *endptr;
				renderd_section_num = strtol(&section[renderd_strlen], &endptr, 10);

				if (endptr == &section[renderd_strlen]) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Invalid renderd section name: %s", section);
					exit(7);
				}
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section %i: %s", renderd_section_num, section);

			if (renderd_section_num >= MAX_SLAVES) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i renderd config sections", MAX_SLAVES);
				exit(7);
			}

			if (configs_dest[renderd_section_num].name != NULL) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Duplicate renderd config section names for section %i: %s & %s", renderd_section_num, configs_dest[renderd_section_num].name, section);
				exit(7);
			}

			copy_string(section, &configs_dest[renderd_section_num].name, renderd_strlen + 2);

			process_config_int(ini, section, "ipport", &configs_dest[renderd_section_num].ipport, 0);
			process_config_int(ini, section, "num_threads", &configs_dest[renderd_section_num].num_threads, NUM_THREADS);
			process_config_string(ini, section, "iphostname", &configs_dest[renderd_section_num].iphostname, "", INILINE_MAX);
			process_config_string(ini, section, "pid_file", &configs_dest[renderd_section_num].pid_filename, RENDERD_PIDFILE, PATH_MAX);
			process_config_string(ini, section, "socketname", &configs_dest[renderd_section_num].socketname, RENDERD_SOCKET, PATH_MAX);
			process_config_string(ini, section, "stats_file", &configs_dest[renderd_section_num].stats_filename, "", PATH_MAX);
			process_config_string(ini, section, "tile_dir", &configs_dest[renderd_section_num].tile_dir, RENDERD_TILE_DIR, PATH_MAX);

			if (configs_dest[renderd_section_num].num_threads == -1) {
				configs_dest[renderd_section_num].num_threads = sysconf(_SC_NPROCESSORS_ONLN);
			}

			if (strnlen(configs_dest[renderd_section_num].socketname, PATH_MAX) >= renderd_socketname_maxlen) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified socketname (%s) exceeds maximum allowed length of %i.", configs_dest[renderd_section_num].socketname, renderd_socketname_maxlen);
				exit(7);
			}
		}
	}

	iniparser_freedict(ini);

	if (renderd_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No renderd config sections were found in file: %s", config_file_name);
		exit(1);
	}
}

void process_config_file(const char *config_file_name, int active_renderd_section_num, int log_level)
{
	extern int num_slave_threads;

	extern renderd_config config;
	extern renderd_config config_slaves[MAX_SLAVES];
	extern xmlconfigitem maps[XMLCONFIGS_MAX];

	num_slave_threads = 0;

	g_logger(log_level, "Parsing renderd config file '%s':", config_file_name);

	process_renderd_sections(config_file_name, config_slaves);
	process_mapnik_section(config_file_name, &config_slaves[active_renderd_section_num]);
	process_map_sections(config_file_name, maps, config_slaves[active_renderd_section_num].tile_dir, config_slaves[active_renderd_section_num].num_threads);
	config = config_slaves[active_renderd_section_num];

	for (int i = 0; i < MAX_SLAVES; i++) {
		if (config_slaves[i].num_threads == 0) {
			continue;
		}

		if (i == active_renderd_section_num) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): Active", i);
		} else {
			num_slave_threads += config_slaves[i].num_threads;
		}

		if (config_slaves[i].ipport > 0) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): ip socket = '%s:%i'", i, config_slaves[i].iphostname, config_slaves[i].ipport);
		} else {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): unix socketname = '%s'", i, config_slaves[i].socketname);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): num_threads = '%i'", i, config_slaves[i].num_threads);
		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): pid_file = '%s'", i, config_slaves[i].pid_filename);

		if (strnlen(config_slaves[i].stats_filename, PATH_MAX)) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): stats_file = '%s'", i, config_slaves[i].stats_filename);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): tile_dir = '%s'", i, config_slaves[i].tile_dir);
	}

	if (config.ipport > 0) {
		g_logger(log_level, "\trenderd: ip socket = '%s':%i", config.iphostname, config.ipport);
	} else {
		g_logger(log_level, "\trenderd: unix socketname = '%s'", config.socketname);
	}

	g_logger(log_level, "\trenderd: num_threads = '%i'", config.num_threads);

	if (active_renderd_section_num == 0 && num_slave_threads > 0) {
		g_logger(log_level, "\trenderd: num_slave_threads = '%i'", num_slave_threads);
	}

	g_logger(log_level, "\trenderd: pid_file = '%s'", config.pid_filename);

	if (strnlen(config.stats_filename, PATH_MAX)) {
		g_logger(log_level, "\trenderd: stats_file = '%s'", config.stats_filename);
	}

	g_logger(log_level, "\trenderd: tile_dir = '%s'", config.tile_dir);
	g_logger(log_level, "\tmapnik:  font_dir = '%s'", config.mapnik_font_dir);
	g_logger(log_level, "\tmapnik:  font_dir_recurse = '%s'", config.mapnik_font_dir_recurse ? "true" : "false");
	g_logger(log_level, "\tmapnik:  plugins_dir = '%s'", config.mapnik_plugins_dir);

	for (int i = 0; i < XMLCONFIGS_MAX; i++) {
		if (maps[i].xmlname != NULL) {
			g_logger(log_level, "\tmap %i:   name(%s) file(%s) uri(%s) output_format(%s) htcp(%s) host(%s)", i, maps[i].xmlname, maps[i].xmlfile, maps[i].xmluri, maps[i].output_format, maps[i].htcpip, maps[i].host);
		}
	}
}
