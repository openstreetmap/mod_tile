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

#include <algorithm>
#include <cctype>
#include <cstring>
#include <glib.h>
#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/map.hpp>
#include <mapnik/params.hpp>
#include <mapnik/version.hpp>
#include <sstream>
#include <string>
#include <unordered_map>

#if MAPNIK_MAJOR_VERSION < 4
#include <boost/optional.hpp>
#endif

#include "g_logger.h"
#include "parameterize_style.hpp"

static bool is_valid_osm_lang_tag(const std::string &tok)
{
	// Covers "_" fallback, 2/3-letter ISO 639 codes, and IETF BCP 47 subtags
	if (tok.empty() || tok.size() > 32) {
		return false;
	}
	return std::all_of(tok.begin(), tok.end(), [](unsigned char c) {
		return std::isalnum(c) || c == '_' || c == '-';
	});
}

static void parameterize_map_language(mapnik::Map &m, char *parameter)
{
	if (!parameter || !*parameter) {
		return;
	}

	std::string param_str(parameter);
	if (param_str.size() > 256) {
		g_logger(G_LOG_LEVEL_WARNING, "Language parameter exceeds maximum allowed length");
		return;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Internationalizing map to language parameter: %s", param_str.c_str());

	std::string name_replace = ", coalesce(";
	std::stringstream ss(param_str);
	std::string tok;
	bool has_valid_tokens = false;

	while (std::getline(ss, tok, ',')) {
		if (!is_valid_osm_lang_tag(tok)) {
			g_logger(G_LOG_LEVEL_WARNING, "Ignoring invalid language token in parameter");
			continue;
		}

		if (has_valid_tokens) {
			name_replace += ",";
		}

		if (tok == "_") {
			name_replace += "name";
		} else {
			name_replace += "tags->'name:" + tok + "'";
		}
		has_valid_tokens = true;
	}

	if (!has_valid_tokens) {
		return;
	}

	name_replace += ") as name";

	for (std::size_t i = 0; i < m.layer_count(); ++i) {
		mapnik::layer &layer = m.get_layer(i);
		auto ds = layer.datasource();
		if (!ds) {
			continue;
		}

		mapnik::parameters params = ds->params();
		if (params.find("table") != params.end()) {
			auto table = params.get<std::string>("table");

			if (table && table->find(",name") != std::string::npos) {
				std::string str = *table;
				size_t pos = str.find(",name");
				str.replace(pos, 5, name_replace);
				params["table"] = str;
				layer.set_datasource(mapnik::datasource_cache::instance().create(params));
			}
		}
	}
}

static void parameterize_map_toggle_layer(mapnik::Map &m, char *parameter)
{
	if (!parameter || !*parameter) {
		return;
	}

	std::string param_str(parameter);
	if (param_str.size() > 1024) {
		g_logger(G_LOG_LEVEL_WARNING, "Toggle layer parameter exceeds maximum allowed length");
		return;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Toggling map layer visibility with parameter: %s", param_str.c_str());

	std::unordered_map<std::string, bool> layer_actions;
	std::stringstream ss(param_str);
	std::string tok;

	while (std::getline(ss, tok, ',')) {
		if (tok.empty()) {
			continue;
		}
		if (tok[0] == '+') {
			if (tok.size() > 1) {
				layer_actions[tok.substr(1)] = true;
			}
		} else if (tok[0] == '-') {
			if (tok.size() > 1) {
				layer_actions[tok.substr(1)] = false;
			}
		} else {
			// Default to disabling if no prefix is supplied
			layer_actions[tok] = false;
		}
	}

	for (std::size_t i = 0; i < m.layer_count(); ++i) {
		mapnik::layer &layer = m.get_layer(i);
		auto it = layer_actions.find(layer.name());

		if (it != layer_actions.end()) {
			if (layer.active() != it->second) {
				layer.set_active(it->second);
				g_logger(G_LOG_LEVEL_DEBUG, "%s layer: %s", it->second ? "Enabled" : "Disabled", layer.name().c_str());
			}
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
	} else if (strcmp(function_name, "toggle_layer") == 0) {
		g_logger(G_LOG_LEVEL_DEBUG, "Loading parameterization function for '%s'", function_name);
		return parameterize_map_toggle_layer;
	} else {
		g_logger(G_LOG_LEVEL_WARNING, "unknown parameterization function for '%s'", function_name);
		return NULL;
	}
}
