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

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_speedtest";
extern std::string err_log_lines, out_log_lines;

TEST_CASE("render_speedtest common", "common testing")
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

TEST_CASE("render_speedtest specific", "specific testing")
{
	std::string renderd_conf = (std::string)RENDERD_CONF;

	SECTION("--config with invalid --map", "should return 1") {
		std::string map = "invalid";
		std::vector<std::string> argv = {"--config", renderd_conf, "--map", map};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Map section '" + map + "' does not exist in config file '" + renderd_conf + "'."));
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

TEST_CASE("render_speedtest min/max int generator", "min/max int generator testing")
{
	std::string option = GENERATE("--max-zoom", "--min-zoom", "--num-threads");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::vector<std::string> argv = {option, "1", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}
