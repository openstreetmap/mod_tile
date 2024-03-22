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

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_expired";
int found;
std::string err_log_lines;

TEST_CASE("render_expired common", "common testing")
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
		std::string option = "-oesnotexist";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--help", "should return 0") {
		std::string option = "--help";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
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

TEST_CASE("render_expired specific", "specific testing")
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
}

TEST_CASE("render_expired min/max int generator", "min/max int generator testing")
{
	std::string option = GENERATE("--delete-from", "--max-load", "--max-zoom", "--min-zoom", "--num-threads", "--touch-from");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}
