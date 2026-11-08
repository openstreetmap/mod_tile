/*
 * Copyright (c) 2007 - 2026 by mod_tile contributors (see AUTHORS file)
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

#include <cstdlib>
#include <cstring>
#include <glib.h>
#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/map.hpp>
#include <mapnik/params.hpp>
#include <mapnik/version.hpp>
#include <string>
#include <unordered_set>

#if MAPNIK_MAJOR_VERSION < 4
#include <boost/optional.hpp>
#endif

#include "g_logger.h"
#include "parameterize_style.hpp"

static void parameterize_map_language(mapnik::Map &m, char *parameter)
{
	unsigned int i;
	char *data = strdup(parameter);
	char *tok;
	char name_replace[256];

	name_replace[0] = 0;
	g_logger(G_LOG_LEVEL_DEBUG, "Internationalizing map to language parameter: %s", parameter);
	tok = strtok(data, ",");

	if (!tok) {
		free(data);
		return; // No parameterization given
	}

	strncat(name_replace, ", coalesce(", 255);

	while (tok) {
		if (strcmp(tok, "_") == 0) {
			strncat(name_replace, "name,", 255);
		} else {
			strncat(name_replace, "tags->'name:", 255);
			strncat(name_replace, tok, 255);
			strncat(name_replace, "',", 255);
		}

		tok = strtok(NULL, ",");
	}

	free(data);
	name_replace[strlen(name_replace) - 1] = 0;
	strncat(name_replace, ") as name", 255);

	for (i = 0; i < m.layer_count(); i++) {
		mapnik::layer &l = m.get_layer(i);
		mapnik::parameters params = l.datasource()->params();

		if (params.find("table") != params.end()) {
			auto table = params.get<std::string>("table");

			if (table && table->find(",name") != std::string::npos) {
				std::string str = *table;
				size_t pos = str.find(",name");
				str.replace(pos, 5, name_replace);
				params["table"] = str;
				l.set_datasource(mapnik::datasource_cache::instance().create(params));
			}
		}
	}
}

static void parameterize_map_disable_layer(mapnik::Map &m, char *parameter)
{
	if (!parameter || strlen(parameter) == 0) {
		return;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Disabling map layers with parameter: %s", parameter);

	std::unordered_set<std::string> layers_to_disable;
	char *data = strdup(parameter);
	char *tok = strtok(data, ",");

	while (tok) {
		layers_to_disable.insert(tok);
		tok = strtok(NULL, ",");
	}
	free(data);

	for (std::size_t i = 0; i < m.layer_count(); ++i) {
		mapnik::layer &l = m.get_layer(i);
		if (layers_to_disable.count(l.name()) > 0) {
			l.set_active(false);
			g_logger(G_LOG_LEVEL_DEBUG, "Disabled layer: %s", l.name().c_str());
		}
	}
}

parameterize_function_ptr init_parameterization_function(const char *function_name)
{

	if (!function_name || strcmp(function_name, "") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Parameterize_style not specified (or empty string specified)");
		return NULL;
	} else if (strcmp(function_name, "language") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Loading parameterization function for '%s'", function_name);
		return parameterize_map_language;
	} else if (strcmp(function_name, "disable_layer") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Loading parameterization function for '%s'", function_name);
		return parameterize_map_disable_layer;
	} else {
		g_logger(G_LOG_LEVEL_WARNING, "unknown parameterization function for '%s'", function_name);
		return NULL;
	}
}
