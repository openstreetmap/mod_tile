/*
 * Copyright (c) 2007 - 2026 by mod_tile contributors (see AUTHORS file)
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

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <utime.h>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "render_config.h"
#include "store_file_utils.h"

extern std::string err_log_lines;

// Helper function to touch a test file with an explicit mtime
static void create_test_file(const std::string &path, time_t mtime)
{
	FILE *f = fopen(path.c_str(), "w");
	REQUIRE(f != nullptr);
	fputs("test", f);
	fclose(f);

	struct utimbuf new_times;
	new_times.actime = mtime;
	new_times.modtime = mtime;
	REQUIRE(utime(path.c_str(), &new_times) == 0);
}

TEST_CASE("store_file_utils.c", "[store_file_utils]")
{
	SECTION("get_planet_time function")
	{
		err_log_lines.clear();
		std::string base_dir = create_tile_dir("get_planet_time_test");

		// Jan 2, 2024 00:00:00 UTC
		time_t expected_time = 1704153600;

		SECTION("returns style-specific timestamp when present")
		{
			std::string section_dir = base_dir + "/style_test";
			REQUIRE(mkdir(section_dir.c_str(), 0755) == 0);

			std::string style_dir = section_dir + "/default";
			REQUIRE(mkdir(style_dir.c_str(), 0755) == 0);

			std::string style_file = style_dir + "/" + PLANET_TIMESTAMP;
			create_test_file(style_file, expected_time);

			time_t result = get_planet_time(section_dir.c_str(), "default");
			REQUIRE(result == expected_time);

			unlink(style_file.c_str());
			rmdir(style_dir.c_str());
			rmdir(section_dir.c_str());
		}

		SECTION("falls back to global timestamp when style timestamp is missing")
		{
			std::string section_dir = base_dir + "/global_test";
			REQUIRE(mkdir(section_dir.c_str(), 0755) == 0);

			std::string global_file = section_dir + "/" + PLANET_TIMESTAMP;

			create_test_file(global_file, expected_time);

			SECTION("with non-existent style name")
			{
				time_t result = get_planet_time(section_dir.c_str(), "nonexistent_style");
				REQUIRE(result == expected_time);
			}

			SECTION("with NULL style name")
			{
				time_t result = get_planet_time(section_dir.c_str(), nullptr);
				REQUIRE(result == expected_time);
			}

			unlink(global_file.c_str());
			rmdir(section_dir.c_str());
		}

		SECTION("falls back to 3 days prior when no timestamp file exists")
		{
			std::string section_dir = base_dir + "/missing_test";
			REQUIRE(mkdir(section_dir.c_str(), 0755) == 0);

			time_t now = time(nullptr);
			time_t expected_approx = now - (3 * 24 * 60 * 60);

			time_t result = get_planet_time(section_dir.c_str(), "nonexistent_style");

			REQUIRE(result >= expected_approx - 2);
			REQUIRE(result <= expected_approx + 2);

			rmdir(section_dir.c_str());
		}

		rmdir(base_dir.c_str());
	}

	SECTION("mkdirp function")
	{
		err_log_lines.clear();
		std::string base_dir = create_tile_dir("mkdirp_test");

		SECTION("mkdirp with path containing no slash", "should return 0")
		{
			int res = mkdirp("noslashpath");
			REQUIRE(res == 0);
		}

		SECTION("mkdirp with empty or root path", "should return 0")
		{
			int res = mkdirp("/");
			REQUIRE(res == 0);
		}

		SECTION("mkdirp when target parent directory already exists", "should return 0")
		{
			std::string existing_dir = base_dir + "/sub1";
			mkdir(existing_dir.c_str(), 0755);

			std::string target_file = existing_dir + "/file.txt";
			int res = mkdirp(target_file.c_str());

			REQUIRE(res == 0);
		}

		SECTION("mkdirp when existing parent path component is a regular file instead of a directory", "should return 1")
		{
			std::string file_path = base_dir + "/regular_file";
			int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
			REQUIRE(fd >= 0);
			close(fd);

			std::string blocked_path = file_path + "/sub/file.png";
			int res = mkdirp(blocked_path.c_str());

			REQUIRE(res == 1);
		}

		SECTION("mkdirp recursively creates multi-level directory tree", "should return 0")
		{
			std::string deep_path = base_dir + "/level1/level2/level3/tile.meta";
			int res = mkdirp(deep_path.c_str());

			REQUIRE(res == 0);

			struct stat st;
			std::string expected_dir = base_dir + "/level1/level2/level3";
			REQUIRE(stat(expected_dir.c_str(), &st) == 0);
			REQUIRE(S_ISDIR(st.st_mode));
		}

		delete_tile_dir(base_dir);
	}

	SECTION("path_to_xyz function")
	{
		err_log_lines.clear();
		char xmlconfig[PATH_MAX] = {0};
		int x = -1, y = -1, z = -1;

#ifdef DIRECTORY_HASH
		SECTION("path_to_xyz handles prefix mismatch between tilepath and path", "should return 1")
		{
			const char *tilepath = "/var/lib/mod_tile";
			const char *path = "/var/lib/different_path/default/0/0/0/0/0/0.meta";

			int res = path_to_xyz(tilepath, path, xmlconfig, &x, &y, &z);

			REQUIRE(res == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Tile path does not match settings"));
		}

		SECTION("path_to_xyz handles malformed tile path structure", "should return 1")
		{
			const char *tilepath = "/var/lib/mod_tile";
			const char *path = "/var/lib/mod_tile/default/invalid_format";

			int res = path_to_xyz(tilepath, path, xmlconfig, &x, &y, &z);

			REQUIRE(res == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Failed to parse tile path"));
		}

		SECTION("path_to_xyz handles invalid hash component values (> 255 or < 0)", "should return 2")
		{
			const char *tilepath = "/var/lib/mod_tile";

			const char *path = "/var/lib/mod_tile/default/5/300/0/0/0/0.meta";

			int res = path_to_xyz(tilepath, path, xmlconfig, &x, &y, &z);

			REQUIRE(res == 2);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Failed to parse tile path (invalid 300)"));
		}

		SECTION("path_to_xyz detects out-of-bounds zoom or tile coordinates", "should return 1")
		{
			const char *tilepath = "/var/lib/mod_tile";

			const char *path = "/var/lib/mod_tile/default/30/0/0/0/0/0.meta";

			int res = path_to_xyz(tilepath, path, xmlconfig, &x, &y, &z);

			REQUIRE(res == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("got bad co-ords"));
		}

		SECTION("path_to_xyz successfully parses valid hashed metatile path", "should set x, y, z and return 0")
		{
			const char *tilepath = "/var/lib/mod_tile";

			char expected_meta[PATH_MAX];
			xyzo_to_meta(expected_meta, sizeof(expected_meta), tilepath, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			int res = path_to_xyz(tilepath, expected_meta, xmlconfig, &x, &y, &z);

			REQUIRE(res == 0);
			REQUIRE(std::string(xmlconfig) == XMLCONFIG_DEFAULT);
			REQUIRE(z == 0);
			REQUIRE(x == 0);
			REQUIRE(y == 0);
		}
#else
		SECTION("path_to_xyz handles non-hashed malformed path", "should return 1")
		{
			const char *path = "bad_non_hashed_path";

			int res = path_to_xyz("/var/lib/mod_tile", path, xmlconfig, &x, &y, &z);

			REQUIRE(res == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Failed to parse tile path"));
		}
#endif
	}

	SECTION("xyzo_to_meta function")
	{
		char path_buf[PATH_MAX] = {0};

		SECTION("xyzo_to_meta without options suffix", "should format base .meta path")
		{
			int offset = xyzo_to_meta(path_buf, sizeof(path_buf), "/var/lib/mod_tile", XMLCONFIG_DEFAULT, "", 0, 0, 0);

			REQUIRE(offset == 0);
			REQUIRE_THAT(std::string(path_buf), Catch::Matchers::StartsWith("/var/lib/mod_tile/default/0/"));
			REQUIRE_THAT(std::string(path_buf), Catch::Matchers::EndsWith(".meta"));
			REQUIRE(!Catch::Matchers::ContainsSubstring(".myoption.meta").match(path_buf));
		}

		SECTION("xyzo_to_meta with options suffix", "should include options before .meta extension")
		{
			int offset = xyzo_to_meta(path_buf, sizeof(path_buf), "/var/lib/mod_tile", XMLCONFIG_DEFAULT, "dirty", 0, 0, 0);

			REQUIRE(offset == 0);
			REQUIRE_THAT(std::string(path_buf), Catch::Matchers::EndsWith(".dirty.meta"));
		}

		SECTION("xyzo_to_meta calculates correct internal sub-tile offset for metatiles", "should compute 0..63 offset")
		{
			int offset = xyzo_to_meta(path_buf, sizeof(path_buf), "/var/lib/mod_tile", XMLCONFIG_DEFAULT, "", 1, 2, 10);

			REQUIRE(offset == 10);
		}
	}

	SECTION("xyz_to_meta function")
	{
		char path_buf[PATH_MAX] = {0};

		SECTION("xyz_to_meta delegates to xyzo_to_meta with empty options", "should return offset and path")
		{
			int offset = xyz_to_meta(path_buf, sizeof(path_buf), "/var/lib/mod_tile", XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(offset == 0);
			REQUIRE_THAT(std::string(path_buf), Catch::Matchers::StartsWith("/var/lib/mod_tile/default/"));
			REQUIRE_THAT(std::string(path_buf), Catch::Matchers::EndsWith(".meta"));
		}
	}
}
