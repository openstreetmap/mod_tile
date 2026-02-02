#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/map.hpp>
#include <mapnik/params.hpp>

#if MAPNIK_MAJOR_VERSION < 4
#include <boost/optional/optional_io.hpp>
#endif

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "parameterize_style.hpp"

extern std::string err_log_lines;

TEST_CASE("parameterize_style.cpp", "[parameterize_style]")
{
	SECTION("parameterize_map_language function") {
		mapnik::datasource_cache::instance().register_datasources(MAPNIK_PLUGINS_DIR);
		mapnik::Map map(256, 256);
		mapnik::load_map(map, MAPNIK_XML);
		mapnik::layer layer = map.get_layer(0);
		map.remove_all();
		mapnik::parameters parameters = layer.datasource()->params();
		parameters["table"] = ",name";
		layer.set_datasource(mapnik::datasource_cache::instance().create(parameters));
		map.add_layer(layer);

		err_log_lines.clear();

		SECTION("parameterize_map_language with empty parameter", "should return") {
			const char * parameter = "";
			parameterize_map_language(map, (char *)parameter);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Internationalizing map to language parameter: "));
		}

		SECTION("parameterize_map_language modifies 'table' parameter", "should return") {
			const char * parameter = "en,de,_";
			layer = map.get_layer(0);
			REQUIRE(layer.datasource()->params().get<std::string>("table") == std::string(",name"));

			parameterize_map_language(map, (char *)parameter);

			layer = map.get_layer(0);
			REQUIRE(layer.datasource()->params().get<std::string>("table") != std::string(",name"));

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Internationalizing map to language parameter: en,de,_"));
		}
	}

	SECTION("init_parameterization_function function") {
		err_log_lines.clear();

		SECTION("init_parameterization_function with empty function_name", "should return NULL") {
			parameterize_function_ptr response = init_parameterization_function("");

			REQUIRE(response == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Parameterize_style not specified (or empty string specified)"));
		}

		SECTION("init_parameterization_function with non-'language' function_name", "should return NULL") {
			parameterize_function_ptr response = init_parameterization_function("doesnotexist");

			REQUIRE(response == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("unknown parameterization function for 'doesnotexist'"));
		}

		SECTION("init_parameterization_function with 'language' function_name", "should return parameterize_map_language") {
			parameterize_function_ptr response = init_parameterization_function("language");

			REQUIRE(response == parameterize_map_language);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Loading parameterization function for 'language'"));
		}
	}
}
