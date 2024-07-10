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

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_expired";
extern std::string err_log_lines, out_log_lines;

TEST_CASE("render_expired common", "common testing")
{
	SECTION("invalid long option", "should return 1") {
		std::vector<std::string> argv = {"--doesnotexist"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("invalid short options", "should return 1") {
		std::vector<std::string> argv = {"-oesnotexist"};

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

TEST_CASE("render_expired specific", "specific testing")
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
		std::string tile_dir = GENERATE("invalid", "/path/is/invalid");
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map, "--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to initialise storage backend " + tile_dir));
	}

	SECTION("--config with valid --map, --verbose and bad input lines", "should return 0") {
		std::string input = "z/x/y\nx y z\n";
		std::string map = "example-map";
		std::string tile_dir = P_tmpdir;
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map, "--tile-dir", tile_dir, "--verbose"};

		int status = run_command(test_binary, argv, input);
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read invalid line: z/x/y"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read invalid line: x y z"));
	}

	SECTION("--touch-from 0 with --max-zoom 19, --verbose and overlapping input lines", "should return 0") {
		std::string input = "16/56715/4908\n17/113420/9816\n18/226860/19632\n19/453726/39265\n";
		std::string tile_dir = P_tmpdir;
		std::vector<std::string> argv = {"--max-zoom", std::to_string(19), "--tile-dir", tile_dir, "--touch-from", std::to_string(0), "--verbose"};

		int status = run_command(test_binary, argv, input);
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Raising --min-zoom from '0' to '3'"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read valid line: 16/56715/4908"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read valid line: 17/113420/9816"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Already requested metatile containing '15/28355/2454'"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read valid line: 18/226860/19632"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Already requested metatile containing '19/453720/39264'"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read valid line: 19/453726/39265"));
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Already requested metatile containing '19/453726/39265'"));
	}

	SECTION("--tile-dir with invalid option", "should return 1") {
		std::string tile_dir = "invalid";
		std::vector<std::string> argv = {"--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_backend: No valid storage backend found for options: " + tile_dir));
	}

	SECTION("--tile-dir with invalid path", "should return 1") {
		std::string tile_dir = "/path/is/invalid";
		std::vector<std::string> argv = {"--tile-dir", tile_dir};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_backend: Failed to stat " + tile_dir + " with error: No such file or directory"));
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
}

TEST_CASE("render_expired min/max int generator", "min/max int generator testing")
{
	std::string option = GENERATE("--delete-from", "--max-load", "--max-zoom", "--min-zoom", "--num-threads", "--touch-from");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}
