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

#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/map.hpp>
#include <mapnik/params.hpp>
#include <mapnik/version.hpp>

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

parameterize_function_ptr init_parameterization_function(const char *function_name)
{
	if (strcmp(function_name, "") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Parameterize_style not specified (or empty string specified)");
		return NULL;
	} else if (strcmp(function_name, "language") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Loading parameterization function for '%s'", function_name);
		return parameterize_map_language;
	} else {
		g_logger(G_LOG_LEVEL_WARNING, "unknown parameterization function for '%s'", function_name);
	}

	return NULL;
}
