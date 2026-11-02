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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "metatile.h"
#include "protocol.h"
#include "render_config.h"
#include "store.h"
#include "store_memcached.h"

#ifdef HAVE_LIBMEMCACHED
static char *memcached_xyz_to_storagekey(const char *xmlconfig, int x, int y, int z, char *key);
static char *memcached_xyzo_to_storagekey(const char *xmlconfig, const char *options, int x, int y, int z, char *key);
static int memcached_tile_read(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *buf, size_t count, int *compressed, char *logmsg);
static struct stat_info memcached_tile_stat(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z);
static int memcached_metatile_write(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, const char *buf, int sz);
static int memcached_metatile_delete(struct storage_backend *store, const char *xmlconfig, int x, int y, int z);
static int memcached_metatile_expire(struct storage_backend *store, const char *xmlconfig, int x, int y, int z);
static char *memcached_tile_storage_id(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z, char *string);
static int memcached_close_storage(struct storage_backend *store);
#endif

extern std::string cli_option_memcached;

// Helper to resolve memcached baseurl
static std::string get_memcached_baseurl()
{
	if (!cli_option_memcached.empty()) {
		return cli_option_memcached;
	}

	const char *baseurl = std::getenv("UNIT_TEST_MEMCACHED");
	if (baseurl && strlen(baseurl) > 0) {
		return std::string(baseurl);
	}

	return "127.0.0.1:11211";
}

TEST_CASE("store_memcached.c", "[store_memcached]")
{
	std::string unreachable_conn = "memcached://127.0.0.1:59999";
	std::string valid_conn = "memcached://" + get_memcached_baseurl();

	SECTION("init_storage_memcached")
	{
		err_log_lines.clear();
		reset_malloc_test_count();

#ifndef HAVE_LIBMEMCACHED
		SECTION("returns NULL when HAVE_LIBMEMCACHED is disabled")
		{
			struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Support for memcached storage has not been compiled into this program"));
		}
#else
		SECTION("handles allocation failure for store backend struct")
		{
			fail_next_malloc = true;
			struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Failed to allocate memory for storage backend"));
			reset_malloc_test_count();
		}

		SECTION("initializes valid memcached storage backend with default connection string")
		{
			reset_malloc_test_count();
			struct storage_backend *store = init_storage_memcached("memcached://");
			REQUIRE(store != NULL);
			REQUIRE(store->storage_ctx != NULL);

			REQUIRE(store->tile_read == &memcached_tile_read);
			REQUIRE(store->tile_stat == &memcached_tile_stat);
			REQUIRE(store->metatile_write == &memcached_metatile_write);
			REQUIRE(store->metatile_delete == &memcached_metatile_delete);
			REQUIRE(store->metatile_expire == &memcached_metatile_expire);
			REQUIRE(store->tile_storage_id == &memcached_tile_storage_id);
			REQUIRE(store->close_storage == &memcached_close_storage);

			memcached_close_storage(store);
			free(store);
		}

		SECTION("initializes valid memcached storage backend with custom server string")
		{
			reset_malloc_test_count();
			struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
			REQUIRE(store != NULL);
			REQUIRE(store->storage_ctx != NULL);

			memcached_close_storage(store);
			free(store);
		}
#endif
	}

#ifdef HAVE_LIBMEMCACHED
	SECTION("memcached_xyzo_to_storagekey")
	{
		SECTION("formats key without options")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = memcached_xyzo_to_storagekey(XMLCONFIG_DEFAULT, "", 10, 20, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == std::string(XMLCONFIG_DEFAULT) + "/8/16/5.meta");
		}

		SECTION("formats key with options")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = memcached_xyzo_to_storagekey(XMLCONFIG_DEFAULT, "options_str", 10, 20, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == std::string(XMLCONFIG_DEFAULT) + "/8/16/5.options_str.meta");
		}
	}

	SECTION("memcached_xyz_to_storagekey")
	{
		SECTION("delegates to memcached_xyzo_to_storagekey with empty options")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = memcached_xyz_to_storagekey(XMLCONFIG_DEFAULT, 8, 16, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == std::string(XMLCONFIG_DEFAULT) + "/8/16/5.meta");
		}
	}

	SECTION("memcached_tile_read")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		char read_buf[1024 * 64] = {0};
		char log_msg[1024] = {0};
		int compressed = 0;

		SECTION("fails when header memory allocation fails")
		{
			fail_next_malloc = true;
			int res = memcached_tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to allocate memory for metatile header"));
			reset_malloc_test_count();
		}

		SECTION("fails when memcached lookup returns non-success")
		{
			int res = memcached_tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_tile_stat")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns empty stat info on memcached fetch error")
		{
			struct stat_info info = memcached_tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == -1);
			REQUIRE(info.expired == 0);
			REQUIRE(info.mtime == 0);
			REQUIRE(info.atime == 0);
			REQUIRE(info.ctime == 0);
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_tile_storage_id")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("formats expected memcached storage URI")
		{
			char uri_buf[PATH_MAX] = {0};
			char *id = memcached_tile_storage_id(store, XMLCONFIG_DEFAULT, "", 8, 16, 5, uri_buf);
			REQUIRE(std::string(id) == "memcached:///" + std::string(XMLCONFIG_DEFAULT) + "/8/16/5.meta");
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_write")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("handles payload allocation failure")
		{
			fail_next_malloc = true;
			int res = memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "mock_data", 9);
			REQUIRE(res == -2);
			reset_malloc_test_count();
		}

		SECTION("returns error -1 on unconnectable memcached server")
		{
			int res = memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "mock_data", 9);
			REQUIRE(res == -1);
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_delete")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns error -1 when key is not found or connection fails")
		{
			int res = memcached_metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_expire")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns error -1 on fetch failure")
		{
			int res = memcached_metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);
		}

		memcached_close_storage(store);
		free(store);
	}

	SECTION("memcached_close_storage")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(unreachable_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("cleans up storage context cleanly")
		{
			REQUIRE(memcached_close_storage(store) == 0);
			REQUIRE(store->storage_ctx == NULL);
			free(store);
		}
	}

	SECTION("executes live memcached operations when target is provided")
	{
		if (cli_option_memcached.empty() && !std::getenv("UNIT_TEST_MEMCACHED")) {
			SKIP("No explicit --memcached or UNIT_TEST_MEMCACHED provided. Skipping live daemon integration tests.");
		}

		SECTION("memcached_metatile_write live server write")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);

			const char dummy_data[] = "MOCK_LIVE_METATILE_PAYLOAD";
			int res = memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, dummy_data, sizeof(dummy_data));
			REQUIRE(res == (int)sizeof(dummy_data));

			memcached_close_storage(store);
			free(store);
		}

		SECTION("memcached_tile_stat live metadata lookup")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);

			const char dummy_data[] = "MOCK_LIVE_METATILE_PAYLOAD";
			memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, dummy_data, sizeof(dummy_data));

			struct stat_info st = memcached_tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(st.size > 0);

			memcached_close_storage(store);
			free(store);
		}

		SECTION("memcached_tile_read live metatile payload fetch")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);

			const char dummy_tile_payload[] = "PNG_MOCK_TILE_DATA";
			int payload_sz = sizeof(dummy_tile_payload);

			size_t header_len = sizeof(struct meta_layout) + (METATILE * METATILE * sizeof(struct entry));
			size_t total_metatile_sz = header_len + payload_sz;

			char *valid_metatile_buf = (char *)calloc(1, total_metatile_sz);
			REQUIRE(valid_metatile_buf != NULL);

			struct meta_layout *meta = (struct meta_layout *)valid_metatile_buf;
			memcpy(meta->magic, META_MAGIC, sizeof(meta->magic));
			meta->count = METATILE * METATILE;

			for (int i = 0; i < meta->count; i++) {
				meta->index[i].offset = header_len;
				meta->index[i].size = payload_sz;
			}

			memcpy(valid_metatile_buf + header_len, dummy_tile_payload, payload_sz);

			int write_res = memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, valid_metatile_buf, (int)total_metatile_sz);
			REQUIRE(write_res == (int)total_metatile_sz);

			char read_buf[1024 * 64] = {0};
			char log_msg[1024] = {0};
			int compressed = 0;

			int read_bytes = memcached_tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(read_bytes > 0);

			SECTION("handles compressed metatile magic header")
			{
				memcpy(meta->magic, META_MAGIC_COMPRESSED, sizeof(meta->magic));
				memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, valid_metatile_buf, (int)total_metatile_sz);

				int comp_read = memcached_tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
				REQUIRE(comp_read > 0);
				REQUIRE(compressed == 1);
			}

			SECTION("returns -6 when target buffer is too small for tile payload")
			{
				char small_buf[2] = {0};
				int trunc_read = memcached_tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, small_buf, sizeof(small_buf), &compressed, log_msg);
				REQUIRE(trunc_read == -6);
				REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Truncating tile"));
			}

			free(valid_metatile_buf);
			memcached_close_storage(store);
			free(store);
		}

		SECTION("memcached_metatile_expire live item expiration")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);

			const char dummy_data[] = "MOCK_EXPIRE_TILE_PAYLOAD";
			int write_res = memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, dummy_data, sizeof(dummy_data));
			REQUIRE(write_res == (int)sizeof(dummy_data));

			int expire_res = memcached_metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(expire_res == 0);

			memcached_close_storage(store);
			free(store);
		}

		SECTION("memcached_metatile_delete live record removal")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);

			const char dummy_data[] = "MOCK_LIVE_METATILE_PAYLOAD";
			memcached_metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, dummy_data, sizeof(dummy_data));

			int del_res = memcached_metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(del_res == 0);

			memcached_close_storage(store);
			free(store);
		}
	}
#endif
}
