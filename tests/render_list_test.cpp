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
#include "catch/catch_test_common.hpp"
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
int found;
std::string err_log_lines;

TEST_CASE("render_list common", "common testing")
{
	SECTION("invalid long option", "should return 1") {
		std::string option = "--doesnotexist";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("invalid short options", "should return 1") {
		std::string option = "-doesnotexist";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--version", "should show version number") {
		std::string option = "--version";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		std::string output;
		char buffer[sizeof(VERSION)];
		fgets(buffer, sizeof(buffer), pipe);
		output += buffer;
		REQUIRE(output == VERSION);
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION("--config invalid file", "should return 1") {
		std::string renderd_conf = "/path/is/invalid";
		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Config file '" + renderd_conf + "' does not exist, please specify a valid file");
		REQUIRE(found > -1);
	}
}

TEST_CASE("render_list specific", "specific testing")
{
	SECTION("--config with invalid --map", "should return 1") {
		std::string renderd_conf = (std::string)RENDERD_CONF;
		std::string option = "--config " + renderd_conf + " --map invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Map section 'invalid' does not exist in config file '" + renderd_conf + "'.");
		REQUIRE(found > -1);
	}

	SECTION("--config with valid --map and --tile-dir with invalid path", "should return 1") {
		std::string renderd_conf = (std::string)RENDERD_CONF;
		std::string option = "--config " + renderd_conf + " --map example-map --tile-dir /path/is/invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("init_storage_backend: Failed to stat /path/is/invalid with error: No such file or directory");
		REQUIRE(found > -1);
		found = err_log_lines.find("Failed to initialise storage backend /path/is/invalid");
		REQUIRE(found > -1);
	}

	SECTION("--config with valid --map, --verbose and bad input lines", "should return 0") {
		std::string renderd_conf = (std::string)RENDERD_CONF;
		std::string option = "--config " + renderd_conf + " --map example-map --tile-dir " + P_tmpdir + " --verbose";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "w");
		fputs("z/x/y\n", pipe);
		fputs("x y z\n", pipe);
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);

		err_log_lines = read_stderr();
		found = err_log_lines.find("bad line 0: z/x/y");
		REQUIRE(found > -1);
		found = err_log_lines.find("bad line 0: x y z");
		REQUIRE(found > -1);
	}

	SECTION("--config with valid --map, --verbose and invalid zoom input lines", "should return 0") {
		std::string renderd_conf = (std::string)RENDERD_CONF;
		std::string option = "--config " + renderd_conf + " --map example-map --tile-dir " + P_tmpdir + " --verbose";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "w");
		fputs("0 0 -100\n", pipe);
		fputs("0 0 100\n", pipe);
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Ignoring tile, zoom -100 outside valid range (0..20)");
		REQUIRE(found > -1);
		found = err_log_lines.find("Ignoring tile, zoom 100 outside valid range (0..20)");
		REQUIRE(found > -1);
	}

	SECTION("--tile-dir with invalid option", "should return 1") {
		std::string option = "--tile-dir invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("init_storage_backend: No valid storage backend found for options: invalid");
		REQUIRE(found > -1);
		found = err_log_lines.find("Failed to initialise storage backend invalid");
		REQUIRE(found > -1);
	}

	SECTION("--tile-dir with invalid path", "should return 1") {
		std::string option = "--tile-dir /path/is/invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("init_storage_backend: Failed to stat /path/is/invalid with error: No such file or directory");
		REQUIRE(found > -1);
		found = err_log_lines.find("Failed to initialise storage backend /path/is/invalid");
		REQUIRE(found > -1);
	}

	SECTION("--num-threads subceeds minimum of 1", "should return 1") {
		std::string option = "--num-threads 0";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Invalid number of threads, must be >= 1 (0 was provided)");
		REQUIRE(found > -1);
	}

	SECTION("--min-zoom/--max-zoom exceeds maximum of MAX_ZOOM", "should return 1") {
		std::string option = GENERATE("--max-zoom", "--min-zoom");
		std::string command = test_binary + " " + option + " " + std::to_string(MAX_ZOOM + 1);

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("zoom, must be <= 20 (21 was provided)");
		REQUIRE(found > -1);
	}

	SECTION("--min-zoom exceeds --max-zoom", "should return 1") {
		std::string option = "--max-zoom 1 --min-zoom 2";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min zoom (2) is larger than max zoom (1).");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-zoom not equal to --max-zoom with X/Y options", "should return 1") {
		std::string option = "--all --max-x 1 --max-zoom 10 --max-y 1 --min-zoom 9";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("min-zoom must be equal to max-zoom when using min-x, max-x, min-y, or max-y options");
		REQUIRE(found > -1);
	}

	SECTION("--all --max-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::string option = "--all --max-x 2 --max-y 2 --max-zoom 1 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Invalid range, x and y values must be <= 1 (2^zoom-1)");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::string option = "--all --max-zoom 1 --min-x 2 --min-y 2 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Invalid range, x and y values must be <= 1 (2^zoom-1)");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-x exceeds --max-x", "should return 1") {
		std::string option = "--all --max-x 1 --max-zoom 1 --min-x 2 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min-x (2) is larger than max-x (1).");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-y exceeds --max-y", "should return 1") {
		std::string option = "--all --max-y 1 --max-zoom 1 --min-y 2 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min-y (2) is larger than max-y (1).");
		REQUIRE(found > -1);
	}

	SECTION("--all --max-lat --max-lon --min-lat --min-lon with --min-x/y and/or --max-x/y", "should return 1") {
		std::string suboption = GENERATE("--min-x 0", "--min-y 0", "--max-x 0", "--max-y 0", "--min-x 0 --min-y 0 --max-x 0 --max-y 0");
		std::string option = "--all --max-lat 0 --max-lon 0 --min-lat 0 --min-lon 0 " + suboption;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("min-lat, min-lon, max-lat & max-lon cannot be used together with min-x, max-x, min-y, or max-y");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-lat exceeds --max-lat", "should return 1") {
		std::string option = "--all --max-lat 0 --max-lon 0 --min-lat 1 --min-lon 0";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min-lat (1.000000) is larger than max-lat (0.000000).");
		REQUIRE(found > -1);
	}

	SECTION("--all --min-lon exceeds --max-lon", "should return 1") {
		std::string option = "--all --max-lat 0 --max-lon 0 --min-lat 0 --min-lon 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min-lon (1.000000) is larger than max-lon (0.000000).");
		REQUIRE(found > -1);
	}
}

TEST_CASE("render_list min/max int generator", "min/max int generator testing")
{
	std::string option = GENERATE("--max-load", "--max-x", "--max-y", "--max-zoom", "--min-x", "--min-y", "--min-zoom", "--num-threads");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("render_list min/max lat generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lat", "--min-lat");
	double min = -85.051100;
	double max = 85.051100;

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " " + std::to_string(max) + " --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::string command = test_binary + " " + option + " " + std::to_string(min) + " --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("render_list min/max lon generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lon", "--min-lon");
	double min = -180.000000;
	double max = 180.000000;

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " " + std::to_string(max) + " --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::string command = test_binary + " " + option + " " + std::to_string(min) + " --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}
