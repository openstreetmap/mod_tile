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

#include <fstream>
#include <string>
#include <sys/un.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"
#include "render_config.h"
#include "renderd.h"

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#ifndef PROJECT_BINARY_DIR
#define PROJECT_BINARY_DIR "."
#endif

// Only render_list uses all functions in renderd_config.c
std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_list";
extern std::string err_log_lines;

TEST_CASE("renderd_config min/max int", "min/max int generator testing")
{
	std::string option = GENERATE("--max-load", "--max-x", "--max-y", "--max-zoom", "--min-x", "--min-y", "--min-zoom", "--num-threads");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative", "should return 1") {
		std::vector<std::string> argv = {option, "-1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be >="));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("(-1 was provided)"));
	}

	SECTION(option + " option is float", "should return 1") {
		std::vector<std::string> argv = {option, "1.23456789"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be an integer (1.23456789 was provided)"));
	}

	SECTION(option + " option is not an integer", "should return 1") {
		std::vector<std::string> argv = {option, "invalid"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be an integer (invalid was provided)"));
	}
}

TEST_CASE("renderd_config min/max double lat generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lat", "--min-lat");
	double min = -85.051100;
	double max = 85.051100;

	SECTION(option + " option is too large", "should return 1") {
		std::vector<std::string> argv = {option, std::to_string(max + .1)};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be <= 85.051100 (85.151100 was provided)"));
	}

	SECTION(option + " option is too small", "should return 1") {
		std::vector<std::string> argv = {option, std::to_string(min - .1)};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be >= -85.051100 (-85.151100 was provided)"));
	}

	SECTION(option + " option is not a double", "should return 1") {
		std::vector<std::string> argv = {option, "invalid"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be a double (invalid was provided)"));
	}

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, std::to_string(max), "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::vector<std::string> argv = {option, std::to_string(min), "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is double with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1.23456789", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("renderd_config min/max double lon generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lon", "--min-lon");
	double min = -180.000000;
	double max = 180.000000;

	SECTION(option + " option is too large", "should return 1") {
		std::vector<std::string> argv = {option, std::to_string(max + .1)};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be <= 180.000000 (180.100000 was provided)"));
	}

	SECTION(option + " option is too small", "should return 1") {
		std::vector<std::string> argv = {option, std::to_string(min - .1)};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be >= -180.000000 (-180.100000 was provided)"));
	}

	SECTION(option + " option is not a double", "should return 1") {
		std::vector<std::string> argv = {option, "invalid"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be a double (invalid was provided)"));
	}

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, std::to_string(max), "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::vector<std::string> argv = {option, std::to_string(min), "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is double with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1.23456789", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("renderd_config config parser", "specific testing")
{
	SECTION("renderd.conf with too many map sections", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";

		for (int i = 0; i <= XMLCONFIGS_MAX; i++) {
			renderd_conf_file << "[map" + std::to_string(i) + "]\n";
		}

		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Can't handle more than " + std::to_string(XMLCONFIGS_MAX) + " map config sections"));
	}

	SECTION("renderd.conf without map sections", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("No map config sections were found in file: " + renderd_conf));
	}

	SECTION("renderd.conf without mapnik section", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[map]\n[renderd]\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("No mapnik config section was found in file: " + renderd_conf));
	}

	SECTION("renderd.conf with invalid renderd sections", "should return 7") {
		std::string renderd_conf_renderd_section_name = "renderdinvalid";

		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n[" + renderd_conf_renderd_section_name + "]\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Invalid renderd section name: " + renderd_conf_renderd_section_name));
	}

	SECTION("renderd.conf with too many renderd sections", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n";

		for (int i = 0; i <= MAX_SLAVES; i++) {
			renderd_conf_file << "[renderd" + std::to_string(i) + "]\n";
		}

		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Can't handle more than " + std::to_string(MAX_SLAVES) + " renderd config sections"));
	}

	SECTION("renderd.conf without renderd sections", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[map]\n[mapnik]\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("No renderd config sections were found in file: " + renderd_conf));
	}

	SECTION("renderd.conf map section scale too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nscale=0.0\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified scale factor (0.000000) is too small, must be greater than or equal to 0.100000."));
	}

	SECTION("renderd.conf map section scale too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nscale=8.1\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified scale factor (8.100000) is too large, must be less than or equal to 8.000000."));
	}

	SECTION("renderd.conf map section maxzoom too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nmaxzoom=-1\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified max zoom (-1) is too small, must be greater than or equal to 0."));
	}

	SECTION("renderd.conf map section maxzoom too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nmaxzoom=" << MAX_ZOOM + 1 << "\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified max zoom (" + std::to_string(MAX_ZOOM + 1) + ") is too large, must be less than or equal to " + std::to_string(MAX_ZOOM) + "."));
	}

	SECTION("renderd.conf map section minzoom too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nminzoom=-1\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min zoom (-1) is too small, must be greater than or equal to 0."));
	}

	SECTION("renderd.conf map section minzoom too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nminzoom=" << MAX_ZOOM + 1 << "\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min zoom (" + std::to_string(MAX_ZOOM + 1) + ") is larger than max zoom (" + std::to_string(MAX_ZOOM) + ")."));
	}

	SECTION("renderd.conf map section type has too few parts", "should return 7") {
		std::string renderd_conf_map_type = "a";

		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\ntype=" + renderd_conf_map_type + "\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified type (" + renderd_conf_map_type + ") has too few parts, there must be at least 2, e.g., 'png image/png'."));
	}

	SECTION("renderd.conf map section type has too many parts", "should return 7") {
		std::string renderd_conf_map_type = "a b c d";

		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\ntype=" + renderd_conf_map_type + "\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified type (" + renderd_conf_map_type + ") has too many parts, there must be no more than 3, e.g., 'png image/png png256'."));
	}

	SECTION("renderd.conf renderd section socketname is too long", "should return 7") {
		int renderd_socketname_maxlen = sizeof(((struct sockaddr_un *)0)->sun_path);
		std::string renderd_socketname = "/" + std::string(renderd_socketname_maxlen, 'A');

		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n";
		renderd_conf_file << "[renderd]\nsocketname=" << renderd_socketname << "\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified socketname (" + renderd_socketname + ") exceeds maximum allowed length of " + std::to_string(renderd_socketname_maxlen) + "."));
	}

	SECTION("renderd.conf duplicate renderd section names", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n";
		renderd_conf_file << "[renderd0]\n[renderd]\n";
		renderd_conf_file.close();

		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Duplicate renderd config section names for section 0: renderd0 & renderd"));
	}
}
