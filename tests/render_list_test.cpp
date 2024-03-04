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
#include <stdlib.h>
#include <string>

#include "catch/catch.hpp"
#include "config.h"
#include "render_config.h"

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#ifndef PROJECT_BINARY_DIR
#define PROJECT_BINARY_DIR "."
#endif

#ifndef RENDERD_CONF
#define RENDERD_CONF "./etc/renderd/renderd.conf"
#endif

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "render_list";

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
		std::string option = "--config /path/is/invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}
}

TEST_CASE("render_list specific", "specific testing")
{
	SECTION("--num-threads subceeds minimum of 1", "should return 1") {
		std::string option = "--num-threads 0";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--min-zoom/--max-zoom exceeds maximum of MAX_ZOOM", "should return 1") {
		std::string option = GENERATE("--max-zoom", "--min-zoom");
		std::string command = test_binary + " " + option + " " + std::to_string(MAX_ZOOM + 1);

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--min-zoom exceeds --max-zoom", "should return 1") {
		std::string option = "--max-zoom " + std::to_string(MAX_ZOOM - 2) + " --min-zoom " + std::to_string(MAX_ZOOM - 1);
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--all --min-zoom not equal to --max-zoom with X/Y options", "should return 1") {
		std::string option = "--all --max-x 1 --max-zoom 10 --max-y 1 --min-zoom 9";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--all --max-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::string option = "--all --max-x 2 --max-y 2 --max-zoom 1 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--all --min-x/y options exceed maximum (2^zoom-1)", "should return 1") {
		std::string option = "--all --max-zoom 1 --min-x 2 --min-y 2 --min-zoom 1";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--config without maps", "should return 1") {
		std::string option = "--config " + (std::string)RENDERD_CONF;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--config with invalid --map", "should return 1") {
		std::string renderd_conf_examples = (std::string)RENDERD_CONF + ".examples";
		std::string option = "--config " + renderd_conf_examples + " --map invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--config with valid --map and --tile-dir with invalid path", "should return 1") {
		std::string renderd_conf_examples = (std::string)RENDERD_CONF + ".examples";
		std::string option = "--config " + renderd_conf_examples + " --map example-map --tile-dir /path/is/invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--config with valid --map, --verbose and bad input lines", "should return 0") {
		std::string renderd_conf_examples = (std::string)RENDERD_CONF + ".examples";
		std::string option = "--config " + renderd_conf_examples + " --map example-map --verbose";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "w");
		fputs("z/x/y\n", pipe);
		fputs("x y z\n", pipe);
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION("--config with valid --map, --verbose and invalid zoom input lines", "should return 0") {
		std::string renderd_conf_examples = (std::string)RENDERD_CONF + ".examples";
		std::string option = "--config " + renderd_conf_examples + " --map example-map --verbose";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "w");
		fputs("0 0 -100\n", pipe);
		fputs("0 0 100\n", pipe);
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION("--tile-dir with invalid option", "should return 1") {
		std::string option = "--tile-dir invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("--tile-dir with invalid path", "should return 1") {
		std::string option = "--tile-dir /path/is/invalid";
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}
}

TEST_CASE("render_list generator", "generator testing")
{
	std::string option = GENERATE("--max-load", "--max-x", "--max-y", "--max-zoom", "--min-x", "--min-y", "--min-zoom", "--num-threads");

	SECTION("option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION("option is negative", "should return 1") {
		std::string command = test_binary + " " + option + " -1";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}

	SECTION("option is float", "should return 1") {
		std::string command = test_binary + " " + option + " 1.23456789";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);
	}
}
