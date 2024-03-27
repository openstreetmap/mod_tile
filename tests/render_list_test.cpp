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

#include <stdio.h>
#include <string>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"
#include "config.h"
#include "render_config.h"

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#ifndef PROJECT_BINARY_DIR
#define PROJECT_BINARY_DIR "."
#endif

#ifndef RENDERD_CONF
#define RENDERD_CONF "./etc/renderd/renderd.conf.examples"
#endif

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_list";
extern std::string err_log_lines, out_log_lines;

TEST_CASE("render_list common", "common testing")
{
	SECTION("invalid long option", "should return 1") {
		std::vector<std::string> argv = {"--doesnotexist"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("invalid short options", "should return 1") {
		std::vector<std::string> argv = {"-doesnotexist"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--help", "should return 0") {
		std::vector<std::string> argv = {"--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION("--version", "should show version number") {
		std::vector<std::string> argv = {"--version"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE(out_log_lines == VERSION);
	}

	SECTION("--config invalid file", "should return 1") {
		std::string renderd_conf = "/path/is/invalid";
		std::vector<std::string> argv = {"--config", renderd_conf};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Config file '" + renderd_conf + "' does not exist, please specify a valid file"));
	}
}

TEST_CASE("render_list specific", "specific testing")
{
	std::string renderd_conf = (std::string)RENDERD_CONF;

	SECTION("--config with invalid --map", "should return 1") {
		std::string map = "invalid";
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Map section '" + map + "' does not exist in config file '" + renderd_conf + "'."));
	}

	SECTION("--config with valid --map and --tile-dir with invalid path", "should return 1") {
		std::string map = "example-map";
		std::string tile_dir = "/path/is/invalid";
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map, "--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_backend: Failed to stat " + tile_dir + " with error: No such file or directory"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to initialise storage backend " + tile_dir));
	}

	SECTION("--config with valid --map, --verbose and bad input lines", "should return 0") {
		std::string map = "example-map";
		std::string tile_dir = P_tmpdir;
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map, "--tile-dir", tile_dir, "--verbose"};
		std::string input = "z/x/y\nx y z\n";

		int status = run_command(test_binary, argv, input);
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("bad line 0: z/x/y"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("bad line 0: x y z"));
	}

	SECTION("--config with valid --map, --verbose and invalid zoom input lines", "should return 0") {
		std::string map = "example-map";
		std::string tile_dir = P_tmpdir;
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map, "--tile-dir", tile_dir, "--verbose"};
		std::string input = "0 0 -100\n0 0 100\n";

		int status = run_command(test_binary, argv, input);
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Ignoring tile, zoom -100 outside valid range (0..20)"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Ignoring tile, zoom 100 outside valid range (0..20)"));
	}

	SECTION("--tile-dir with invalid option", "should return 1") {
		std::string tile_dir = "invalid";
		std::vector<std::string> argv = {"--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_backend: No valid storage backend found for options: " + tile_dir));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to initialise storage backend " + tile_dir));
	}

	SECTION("--tile-dir with invalid path", "should return 1") {
		std::string tile_dir = "/path/is/invalid";
		std::vector<std::string> argv = {"--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_backend: Failed to stat " + tile_dir + " with error: No such file or directory"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to initialise storage backend " + tile_dir));
	}

	SECTION("--num-threads subceeds minimum of 1", "should return 1") {
		std::vector<std::string> argv = {"--num-threads", "0"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Invalid number of threads, must be >= 1 (0 was provided)"));
	}

	SECTION("--min-zoom/--max-zoom exceeds maximum of MAX_ZOOM", "should return 1") {
		std::vector<std::string> argv = {GENERATE("--max-zoom", "--min-zoom"), std::to_string(MAX_ZOOM + 1)};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("zoom, must be <= 20 (21 was provided)"));
	}

	SECTION("--min-zoom exceeds --max-zoom", "should return 1") {
		std::vector<std::string> argv = {"--max-zoom", "1", "--min-zoom", "2"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min zoom (2) is larger than max zoom (1)."));
	}

	SECTION("--all --min-zoom not equal to --max-zoom with X/Y options", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-x", "1", "--max-zoom", "10", "--max-y", "1", "--min-zoom", "9"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("min-zoom must be equal to max-zoom when using min-x, max-x, min-y, or max-y options"));
	}

	SECTION("--all --max-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-x", "2", "--max-y", "2", "--max-zoom", "1", "--min-zoom", "1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Invalid range, x and y values must be <= 1 (2^zoom-1)"));
	}

	SECTION("--all --min-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-zoom", "1", "--min-x", "2", "--min-y", "2", "--min-zoom", "1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Invalid range, x and y values must be <= 1 (2^zoom-1)"));
	}

	SECTION("--all --min-x exceeds --max-x", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-x", "1", "--max-zoom", "1", "--min-x", "2", "--min-zoom", "1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min-x (2) is larger than max-x (1)."));
	}

	SECTION("--all --min-y exceeds --max-y", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-y", "1", "--max-zoom", "1", "--min-y", "2", "--min-zoom", "1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min-y (2) is larger than max-y (1)."));
	}

	SECTION("--all --max-lat --max-lon --min-lat --min-lon with --min-x/y and/or --max-x/y", "should return 1") {
		std::vector<std::string> suboptions = GENERATE(std::vector<std::string>({"--min-x", "0"}), std::vector<std::string>({"--min-y", "0"}), std::vector<std::string>({"--max-x", "0"}), std::vector<std::string>({"--max-y", "0"}), std::vector<std::string>({"--min-x", "0", "--min-y", "0", "--max-x", "0", "--max-y", "0"}));
		std::vector<std::string> argv = {"--all", "--max-lat", "0", "--max-lon", "0", "--min-lat", "0", "--min-lon", "0"};
		argv.insert(argv.end(), suboptions.begin(), suboptions.end());

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("min-lat, min-lon, max-lat & max-lon cannot be used together with min-x, max-x, min-y, or max-y"));
	}

	SECTION("--all --min-lat exceeds --max-lat", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-lat", "0", "--max-lon", "0", "--min-lat", "1", "--min-lon", "0"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min-lat (1.000000) is larger than max-lat (0.000000)."));
	}

	SECTION("--all --min-lon exceeds --max-lon", "should return 1") {
		std::vector<std::string> argv = {"--all", "--max-lat", "0", "--max-lon", "0", "--min-lat", "0", "--min-lon", "1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Specified min-lon (1.000000) is larger than max-lon (0.000000)."));
	}
}

TEST_CASE("render_list min/max int generator", "min/max int generator testing")
{
	std::string option = GENERATE("--max-load", "--max-x", "--max-y", "--max-zoom", "--min-x", "--min-y", "--min-zoom", "--num-threads");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("render_list min/max lat generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lat", "--min-lat");
	double min = -85.051100;
	double max = 85.051100;

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
}

TEST_CASE("render_list min/max lon generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lon", "--min-lon");
	double min = -180.000000;
	double max = 180.000000;

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
}
