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

#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/version.hpp>

#if MAPNIK_MAJOR_VERSION < 4
#include <boost/optional/optional_io.hpp>
#endif

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "parameterize_style.hpp"

static void parameterize_map_language(mapnik::Map &m, char *parameter);
static void parameterize_map_toggle_layer(mapnik::Map &m, char *parameter);

TEST_CASE("parameterize_style.cpp", "[parameterize_style]")
{
	mapnik::datasource_cache::instance().register_datasources(MAPNIK_PLUGINS_DIR);
	mapnik::Map map(256, 256);
	mapnik::load_map(map, MAPNIK_XML);

	mapnik::layer layer("Unnamed");

	err_log_lines.clear();

	SECTION("parameterize_map_language function")
	{
		layer = map.get_layer(0);
		map.remove_all();
		mapnik::parameters parameters = layer.datasource()->params();
		parameters["table"] = ",name";
		layer.set_datasource(mapnik::datasource_cache::instance().create(parameters));
		map.add_layer(layer);

		SECTION("parameterize_map_language with NULL parameter", "should return")
		{
			parameterize_map_language(map, NULL);

			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Internationalizing map to language parameter:"));
		}

		SECTION("parameterize_map_language with empty parameter", "should return")
		{
			std::string parameter = "";
			parameterize_map_language(map, (char *)parameter.c_str());

			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Internationalizing map to language parameter:"));
		}

		SECTION("parameterize_map_language modifies 'table' parameter", "should return")
		{
			std::string parameter = "en,de,_";
			layer = map.get_layer(0);
			REQUIRE(layer.datasource()->params().get<std::string>("table") == std::string(",name"));

			parameterize_map_language(map, (char *)parameter.c_str());

			layer = map.get_layer(0);
			REQUIRE(layer.datasource()->params().get<std::string>("table") != std::string(",name"));

			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Internationalizing map to language parameter: " + parameter));
		}
	}

	SECTION("parameterize_map_toggle_layer function")
	{
		SECTION("parameterize_map_toggle_layer with NULL parameter", "should return")
		{
			parameterize_map_toggle_layer(map, NULL);

			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Toggling map layer visibility with parameter:"));
		}

		SECTION("parameterize_map_toggle_layer with empty parameter", "should return")
		{
			std::string parameter = "";
			parameterize_map_toggle_layer(map, (char *)parameter.c_str());

			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Toggling map layer visibility with parameter:"));
		}

		SECTION("parameterize_map_toggle_layer with and without + or -", "should enable or disable layer and return")
		{
			std::string parameter = "world";
			std::string parameter_minus = "-" + parameter;
			std::string parameter_plus = "+" + parameter;

			layer = map.get_layer(0);
			REQUIRE(layer.active());

			parameterize_map_toggle_layer(map, (char *)parameter.c_str());

			layer = map.get_layer(0);
			REQUIRE(!layer.active());

			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Toggling map layer visibility with parameter: " + parameter));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Disabled layer: " + parameter));

			err_log_lines.clear();

			parameterize_map_toggle_layer(map, (char *)parameter_plus.c_str());

			layer = map.get_layer(0);
			REQUIRE(layer.active());

			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Toggling map layer visibility with parameter: " + parameter_plus));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Enabled layer: " + parameter));

			err_log_lines.clear();

			parameterize_map_toggle_layer(map, (char *)parameter_minus.c_str());

			layer = map.get_layer(0);
			REQUIRE(!layer.active());

			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Toggling map layer visibility with parameter: " + parameter_minus));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Disabled layer: " + parameter));
		}
	}

	SECTION("init_parameterization_function function")
	{
		err_log_lines.clear();

		SECTION("init_parameterization_function with NULL function_name", "should return NULL")
		{
			parameterize_function_ptr response = init_parameterization_function(nullptr);

			REQUIRE(response == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Parameterize_style not specified (or empty string specified)"));
		}

		SECTION("init_parameterization_function with empty function_name", "should return NULL")
		{
			parameterize_function_ptr response = init_parameterization_function("");

			REQUIRE(response == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Parameterize_style not specified (or empty string specified)"));
		}

		SECTION("init_parameterization_function with non-'language/toggle_layer' function_name", "should return NULL")
		{
			parameterize_function_ptr response = init_parameterization_function("doesnotexist");

			REQUIRE(response == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("unknown parameterization function for 'doesnotexist'"));
		}

		SECTION("init_parameterization_function with 'language' function_name", "should return parameterize_map_language")
		{
			parameterize_function_ptr response = init_parameterization_function("language");

			REQUIRE(response == parameterize_map_language);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Loading parameterization function for 'language'"));
		}

		SECTION("init_parameterization_function with 'toggle_layer' function_name", "should return parameterize_map_toggle_layer")
		{
			parameterize_function_ptr response = init_parameterization_function("toggle_layer");

			REQUIRE(response == parameterize_map_toggle_layer);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Loading parameterization function for 'toggle_layer'"));
		}
	}
}
