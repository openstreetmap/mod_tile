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

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#ifndef PROJECT_BINARY_DIR
#define PROJECT_BINARY_DIR "."
#endif

std::string test_binary = (std::string)PROJECT_BINARY_DIR + "/" + "renderd";
extern std::string err_log_lines, out_log_lines;

TEST_CASE("renderd common", "common testing")
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
		std::vector<std::string> argv = {"--foreground", "--config", renderd_conf};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Config file '" + renderd_conf + "' does not exist, please specify a valid file"));
	}
}

TEST_CASE("renderd specific", "specific testing")
{
	std::string option = "--slave";

	SECTION(option + " is positive with --help", "should return 0") {
		std::vector<std::string> argv = {"--foreground", option, "1", "--help"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " subceeds minimum of 0", "should return 1") {
		std::vector<std::string> argv = {"--foreground", option, "-1"};

		int status = run_command(test_binary, argv);
		REQUIRE(WEXITSTATUS(status) == 1);
		REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("must be >= 0 (-1 was provided)"));
	}
}
