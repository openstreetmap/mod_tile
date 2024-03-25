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
#include <stdio.h>
#include <string>
#include <sys/un.h>

#include "catch/catch.hpp"
#include "catch/catch_test_common.hpp"
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
int found;
std::string err_log_lines;

TEST_CASE("renderd_config min/max int", "min/max int generator testing")
{
	std::string option = GENERATE("--max-load", "--max-x", "--max-y", "--max-zoom", "--min-x", "--min-y", "--min-zoom", "--num-threads");

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative", "should return 1") {
		std::string command = test_binary + " " + option + " -1";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be >=");
		REQUIRE(found > -1);
		found = err_log_lines.find("(-1 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is float", "should return 1") {
		std::string command = test_binary + " " + option + " 1.23456789";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be an integer (1.23456789 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is not an integer", "should return 1") {
		std::string command = test_binary + " " + option + " invalid";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be an integer (invalid was provided)");
		REQUIRE(found > -1);
	}
}

TEST_CASE("renderd_config min/max double lat generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lat", "--min-lat");
	double min = -85.051100;
	double max = 85.051100;

	SECTION(option + " option is too large", "should return 1") {
		std::string command = test_binary + " " + option + " 85.05111";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be <= 85.051100 (85.05111 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is too small", "should return 1") {
		std::string command = test_binary + " " + option + " -85.05111";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be >= -85.051100 (-85.05111 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is not a double", "should return 1") {
		std::string command = test_binary + " " + option + " invalid";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be a double (invalid was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 85.0511 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::string command = test_binary + " " + option + " -85.0511 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is double with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1.23456789 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("renderd_config min/max double lon generator", "min/max double generator testing")
{
	std::string option = GENERATE("--max-lon", "--min-lon");
	double min = -180.000000;
	double max = 180.000000;

	SECTION(option + " option is too large", "should return 1") {
		std::string command = test_binary + " " + option + " 180.1";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be <= 180.000000 (180.1 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is too small", "should return 1") {
		std::string command = test_binary + " " + option + " -180.1";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be >= -180.000000 (-180.1 was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is not a double", "should return 1") {
		std::string command = test_binary + " " + option + " invalid";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("must be a double (invalid was provided)");
		REQUIRE(found > -1);
	}

	SECTION(option + " option is positive with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 180 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is negative with --help", "should return 0") {
		std::string command = test_binary + " " + option + " -180 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		REQUIRE(WEXITSTATUS(status) == 0);
	}

	SECTION(option + " option is double with --help", "should return 0") {
		std::string command = test_binary + " " + option + " 1.23456789 --help";

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
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

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Can't handle more than " + std::to_string(XMLCONFIGS_MAX) + " map config sections");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf without map sections", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("No map config sections were found in file: " + renderd_conf);
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf without mapnik section", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[map]\n[renderd]\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("No mapnik config section was found in file: " + renderd_conf);
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf with invalid renderd sections", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n[renderdinvalid]\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Invalid renderd section name: renderdinvalid");
		REQUIRE(found > -1);
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

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Can't handle more than " + std::to_string(MAX_SLAVES) + " renderd config sections");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf without renderd sections", "should return 1") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[map]\n[mapnik]\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 1);

		err_log_lines = read_stderr();
		found = err_log_lines.find("No renderd config sections were found in file: " + renderd_conf);
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section scale too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nscale=0.0\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified scale factor (0.000000) is too small, must be greater than or equal to 0.100000.");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section scale too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nscale=8.1\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified scale factor (8.100000) is too large, must be less than or equal to 8.000000.");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section maxzoom too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nmaxzoom=-1\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified max zoom (-1) is too small, must be greater than or equal to 0.");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section maxzoom too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nmaxzoom=" << MAX_ZOOM + 1 << "\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified max zoom (" + std::to_string(MAX_ZOOM + 1) + ") is too large, must be less than or equal to " + std::to_string(MAX_ZOOM) + ".");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section minzoom too small", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nminzoom=-1\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min zoom (-1) is too small, must be greater than or equal to 0.");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section minzoom too large", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\nminzoom=" << MAX_ZOOM + 1 << "\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified min zoom (" + std::to_string(MAX_ZOOM + 1) + ") is larger than max zoom (" + std::to_string(MAX_ZOOM) + ").");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section type has too few parts", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\ntype=a\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified type (a) has too few parts, there must be at least 2, e.g., 'png image/png'.");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf map section type has too many parts", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[renderd]\n";
		renderd_conf_file << "[map]\ntype=a b c d\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified type (a b c d) has too many parts, there must be no more than 3, e.g., 'png image/png png256'.");
		REQUIRE(found > -1);
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

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Specified socketname (" + renderd_socketname + ") exceeds maximum allowed length of " + std::to_string(renderd_socketname_maxlen) + ".");
		REQUIRE(found > -1);
	}

	SECTION("renderd.conf duplicate renderd section names", "should return 7") {
		std::string renderd_conf = std::tmpnam(nullptr);
		std::ofstream renderd_conf_file;
		renderd_conf_file.open(renderd_conf);
		renderd_conf_file << "[mapnik]\n[map]\n";
		renderd_conf_file << "[renderd0]\n[renderd]\n";
		renderd_conf_file.close();

		std::string option = "--config " + renderd_conf;
		std::string command = test_binary + " " + option;

		// flawfinder: ignore
		FILE *pipe = popen(command.c_str(), "r");
		int status = pclose(pipe);
		std::remove(renderd_conf.c_str());
		REQUIRE(WEXITSTATUS(status) == 7);

		err_log_lines = read_stderr();
		found = err_log_lines.find("Duplicate renderd config section names for section 0: renderd0 & renderd");
		REQUIRE(found > -1);
	}
}
