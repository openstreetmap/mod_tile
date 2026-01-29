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

#ifndef RENDERD_CONFIG_H
#define RENDERD_CONFIG_H

#include "render_config.h"
#include "renderd.h"

#ifdef HAVE_INIPARSER_INIPARSER_H
#include <iniparser/iniparser.h>
#else
#include <iniparser.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int num_slave_threads;
renderd_config *config;
renderd_config config_slaves[MAX_SLAVES];
xmlconfigitem maps[XMLCONFIGS_MAX];

double min_max_double_opt(const char *opt_arg, const char *opt_type_name, double minimum, double maximum);
int min_max_int_opt(const char *opt_arg, const char *opt_type_name, int minimum, int maximum);
void free_map_section(xmlconfigitem *map_section);
void free_map_sections(xmlconfigitem *map_sections);
void free_renderd_section(renderd_config *renderd_section);
void free_renderd_sections(renderd_config *renderd_sections);
void process_config_file(const char *config_file_name, int active_renderd_section_num, int log_level);
void process_map_sections(dictionary *ini, const char *config_file_name, xmlconfigitem *maps_dest, const char *default_tile_dir, int num_threads);
void process_mapnik_section(dictionary *ini, const char *config_file_name, renderd_config *config_dest);
void process_renderd_sections(dictionary *ini, const char *config_file_name, renderd_config *configs_dest);

#ifdef __cplusplus
}

#endif
#endif
