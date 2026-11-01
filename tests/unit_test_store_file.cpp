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

#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <utime.h>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "metatile.h"
#include "store_file.h"
#include "store_file_utils.h"

static void create_mock_metatile(const char *path, bool compressed, int count, size_t tile_data_len, const char *tile_data)
{
	mkdirp(path);

	size_t header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	struct meta_layout *m = (struct meta_layout *)calloc(1, header_len);

	if (compressed) {
		memcpy(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED));
	} else {
		memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
	}

	m->count = count;
	m->index[0].offset = header_len;
	m->index[0].size = tile_data_len;

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0) {
		REQUIRE(write(fd, m, header_len) == (ssize_t)(header_len));
		if (tile_data_len > 0 && tile_data != NULL) {
			REQUIRE(write(fd, tile_data, tile_data_len) == (ssize_t)(tile_data_len));
		}
		close(fd);
	}

	free(m);
}

TEST_CASE("store_file.c", "[store_file]")
{
	std::string tmp_dir = create_tile_dir("store_file_test");

	SECTION("init_storage_file function")
	{
		err_log_lines.clear();

		SECTION("init_storage_file handles malloc failure", "should return NULL")
		{
			fail_next_malloc = true;

			struct storage_backend *store = init_storage_file(tmp_dir.c_str());

			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("init_storage_file: Failed to allocate memory"));
		}

		SECTION("init_storage_file creates valid backend", "should set storage_ctx and function pointers")
		{
			struct storage_backend *store = init_storage_file(tmp_dir.c_str());

			REQUIRE(store != NULL);
			REQUIRE(store->storage_ctx != NULL);
			REQUIRE(std::string((char *)store->storage_ctx) == tmp_dir);
			REQUIRE(store->tile_read != NULL);
			REQUIRE(store->tile_stat != NULL);
			REQUIRE(store->metatile_write != NULL);
			REQUIRE(store->metatile_delete != NULL);
			REQUIRE(store->metatile_expire != NULL);
			REQUIRE(store->tile_storage_id != NULL);
			REQUIRE(store->close_storage != NULL);

			store->close_storage(store);
			free(store);
		}
	}

	SECTION("file_tile_storage_id function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		char uri_buf[PATH_MAX] = {0};

		char *res = store->tile_storage_id(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, uri_buf);

		REQUIRE(res == uri_buf);
		REQUIRE_THAT(std::string(uri_buf), Catch::Matchers::StartsWith("file://"));
		REQUIRE_THAT(std::string(uri_buf), Catch::Matchers::ContainsSubstring(tmp_dir));

		store->close_storage(store);
		free(store);
	}

	SECTION("file_metatile_write function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		const char test_payload[] = "metatile_payload_data";
		int payload_len = sizeof(test_payload);

		SECTION("write handles malloc failure", "should return -1")
		{
			fail_next_malloc = true;

			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, test_payload, payload_len);

			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for temp path"));
		}

		SECTION("write handles open failure", "should return -1")
		{
			fail_next_open = true;

			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, test_payload, payload_len);

			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Error creating file"));
		}

		SECTION("write handles write failure", "should return -1")
		{
			fail_next_write = true;

			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, test_payload, payload_len);

			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Error writing file"));
		}

		SECTION("write handles rename failure", "should return -1")
		{
			char meta_path[PATH_MAX];
			xyzo_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			mkdirp(meta_path);
			mkdir(meta_path, 0755);

			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, test_payload, payload_len);

			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Error renaming temp file"));

			rmdir(meta_path);
		}

		SECTION("write successfully creates and renames metatile", "should return written size")
		{
			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, test_payload, payload_len);

			REQUIRE(res == payload_len);

			char meta_path[PATH_MAX];
			xyzo_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			struct stat st;
			REQUIRE(stat(meta_path, &st) == 0);
			REQUIRE(st.st_size == payload_len);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("file_tile_read function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		char log_msg[PATH_MAX] = {0};
		char read_buf[256] = {0};
		int compressed = -1;

		char meta_path[PATH_MAX];
		xyzo_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, "", 0, 0, 0);

		SECTION("read handles header malloc failure", "should return -1")
		{
			fail_next_malloc = true;

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to allocate memory for metatile header"));
		}

		SECTION("read handles non-existent file open error", "should return -1")
		{
			unlink(meta_path);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Could not open metatile"));
		}

		SECTION("read handles header too small", "should return -3")
		{
			mkdirp(meta_path);
			int fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			REQUIRE(write(fd, "short", 5) == (ssize_t)(5));
			close(fd);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -3);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("too small to contain header"));
		}

		SECTION("read handles magic mismatch", "should return -4")
		{
			size_t header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			char *bad_header = (char *)calloc(1, header_len);
			memcpy(bad_header, "BADMAG", 6);

			mkdirp(meta_path);
			int fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			REQUIRE(write(fd, bad_header, header_len) == (ssize_t)(header_len));
			close(fd);
			free(bad_header);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -4);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("header magic mismatch"));
		}

		SECTION("read handles bad count mismatch", "should return -5")
		{
			create_mock_metatile(meta_path, false, 10, 0, NULL);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -5);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("header bad count"));
		}

		SECTION("read handles tile_size larger than buffer", "should return -6")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 500, "data");

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, 10, &compressed, log_msg);

			REQUIRE(res == -6);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Truncating tile"));
		}

		SECTION("read handles seek error on corrupted offset", "should return -7")
		{
			size_t header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			struct meta_layout *m = (struct meta_layout *)calloc(1, header_len);
			memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
			m->count = METATILE * METATILE;
			m->index[0].offset = -1;
			m->index[0].size = 10;

			mkdirp(meta_path);
			int fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			REQUIRE(write(fd, m, header_len) == (ssize_t)(header_len));
			close(fd);
			free(m);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -7);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("seek error"));
		}

		SECTION("read handles premature EOF when reading tile data", "should return partial size")
		{
			size_t header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			struct meta_layout *m = (struct meta_layout *)calloc(1, header_len);
			memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
			m->count = METATILE * METATILE;
			m->index[0].offset = header_len;
			m->index[0].size = 100;

			mkdirp(meta_path);
			int fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			REQUIRE(write(fd, m, header_len) == (ssize_t)(header_len));

			const char short_data[] = "shortdata";
			REQUIRE(write(fd, short_data, 9) == (ssize_t)(9));
			close(fd);
			free(m);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == 9);
		}

		SECTION("read reads uncompressed tile successfully", "should return read length")
		{
			const char tile_content[] = "png_tile_binary_stream";
			create_mock_metatile(meta_path, false, METATILE * METATILE, strlen(tile_content), tile_content);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == static_cast<int>(strlen(tile_content)));
			REQUIRE(compressed == 0);
			REQUIRE(std::string(read_buf, res) == tile_content);
		}

		SECTION("read reads compressed tile successfully", "should set compressed = 1")
		{
			const char tile_content[] = "compressed_stream";
			create_mock_metatile(meta_path, true, METATILE * METATILE, strlen(tile_content), tile_content);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == static_cast<int>(strlen(tile_content)));
			REQUIRE(compressed == 1);
		}

		SECTION("expire handles gmtime_r failure", "should fallback to default epoch time")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");
			fail_next_gmtime_r = true;

			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);
		}

		SECTION("expire on tile with mtime prior to 2015", "should execute tm_year <= 115 branch")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			struct utimbuf old_time;
			old_time.actime = 1262304000;
			old_time.modtime = 1262304000;
			utime(meta_path, &old_time);

			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);
		}

		SECTION("expire handles utime error", "should return 0 despite perror logging")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");
			fail_next_utime = true;

			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);
		}

		SECTION("read handles read error during header loop", "should return -2")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			reset_read_call_count();
			fail_read_on_call_count = 1;

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -2);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to read complete header"));
		}

		SECTION("read handles read error during payload loop", "should return -8")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			reset_read_call_count();
			fail_read_on_call_count = 2;

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);

			REQUIRE(res == -8);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to read data from file"));
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("file_tile_stat function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		char meta_path[PATH_MAX];
		xyzo_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, "", 0, 0, 0);

		char planet_file[PATH_MAX];
		snprintf(planet_file, sizeof(planet_file), "%s/%s%s", tmp_dir.c_str(), XMLCONFIG_DEFAULT, PLANET_TIMESTAMP);

		SECTION("stat non-existent file", "should return size -1")
		{
			unlink(meta_path);

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			REQUIRE(info.size == -1);
			REQUIRE(info.mtime == 0);
			REQUIRE(info.expired == 1);
		}

		SECTION("stat file expired relative to planet file", "should mark expired = 1")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			int fd = open(planet_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			close(fd);

			struct utimbuf times;
			times.actime = time(NULL) + 1000;
			times.modtime = time(NULL) + 1000;
			utime(planet_file, &times);

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			REQUIRE(info.size > 0);
			REQUIRE(info.expired == 1);

			unlink(planet_file);
		}

		SECTION("stat file newer than planet file", "should mark expired = 0")
		{
			int fd = open(planet_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			close(fd);

			struct utimbuf planet_times;
			planet_times.actime = time(NULL) - 1000;
			planet_times.modtime = time(NULL) - 1000;
			utime(planet_file, &planet_times);

			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);

			REQUIRE(info.size > 0);
			REQUIRE(info.expired == 0);

			unlink(planet_file);
		}

		SECTION("stat fallback to global planet timestamp and missing planet fallback", "should execute planet time branches")
		{
			char global_planet[PATH_MAX];
			snprintf(global_planet, sizeof(global_planet), "%s/%s", tmp_dir.c_str(), PLANET_TIMESTAMP);

			int fd = open(global_planet, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			close(fd);

			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size > 0);

			unlink(global_planet);

			info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size > 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("file_metatile_delete function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		char meta_path[PATH_MAX];
		xyz_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, 0, 0, 0);

		create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

		int res = store->metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0);

		REQUIRE(res == 0);

		struct stat st;
		REQUIRE(stat(meta_path, &st) != 0);

		store->close_storage(store);
		free(store);
	}

	SECTION("file_metatile_expire function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());
		char meta_path[PATH_MAX];
		xyz_to_meta(meta_path, sizeof(meta_path), (char *)store->storage_ctx, XMLCONFIG_DEFAULT, 0, 0, 0);

		SECTION("expire on missing file", "should return 0")
		{
			unlink(meta_path);

			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);
		}

		SECTION("expire on existing file sets timestamp back 20 years", "should adjust modtime")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			struct stat st_before;
			REQUIRE(stat(meta_path, &st_before) == 0);

			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);

			struct stat st_after;
			REQUIRE(stat(meta_path, &st_after) == 0);

			REQUIRE(st_after.st_mtime < st_before.st_mtime);
		}

		SECTION("expire on already expired tile", "should execute tm_year <= 115 branch")
		{
			create_mock_metatile(meta_path, false, METATILE * METATILE, 10, "data");

			store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

			REQUIRE(res == 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("file_close_storage function")
	{
		struct storage_backend *store = init_storage_file(tmp_dir.c_str());

		int res = store->close_storage(store);

		REQUIRE(res == 0);
		REQUIRE(store->storage_ctx == NULL);

		free(store);
	}

	delete_tile_dir(tmp_dir);
}
