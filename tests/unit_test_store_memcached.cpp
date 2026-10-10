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

TEST_CASE("store_memcached.c", "[store_memcached]")
{
	std::string valid_conn = "memcached://127.0.0.1:59999";

	SECTION("init_storage_memcached")
	{
		err_log_lines.clear();
		reset_malloc_test_count();

#ifndef HAVE_LIBMEMCACHED
		SECTION("returns NULL when HAVE_LIBMEMCACHED is disabled")
		{
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Support for memcached storage has not been compiled into this program"));
		}
#else
		SECTION("handles allocation failure for store backend struct")
		{
			fail_next_malloc = true;
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
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

		SECTION("initializes valid memcached storage backend with custom server string")
		{
			reset_malloc_test_count();
			struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
			REQUIRE(store != NULL);
			REQUIRE(store->storage_ctx != NULL);

			store->close_storage(store);
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
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		char read_buf[1024 * 64] = {0};
		char log_msg[1024] = {0};
		int compressed = 0;

		SECTION("fails when header memory allocation fails (line 80)")
		{
			fail_next_malloc = true;
			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to allocate memory for metatile header"));
			reset_malloc_test_count();
		}

		SECTION("fails when memcached lookup returns non-success")
		{
			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_tile_stat")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns empty stat info on memcached fetch error")
		{
			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == -1);
			REQUIRE(info.expired == 0);
			REQUIRE(info.mtime == 0);
			REQUIRE(info.atime == 0);
			REQUIRE(info.ctime == 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_tile_storage_id")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("formats expected memcached storage URI")
		{
			char uri_buf[PATH_MAX] = {0};
			char *id = store->tile_storage_id(store, XMLCONFIG_DEFAULT, "", 8, 16, 5, uri_buf);
			REQUIRE(std::string(id) == "memcached:///" + std::string(XMLCONFIG_DEFAULT) + "/8/16/5.meta");
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_write")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("handles payload allocation failure (line 173)")
		{
			fail_next_malloc = true;
			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "mock_data", 9);
			REQUIRE(res == -2);
			reset_malloc_test_count();
		}

		SECTION("returns error -1 on unconnectable memcached server")
		{
			int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "mock_data", 9);
			REQUIRE(res == -1);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_delete")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns error -1 when key is not found or connection fails")
		{
			int res = store->metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_metatile_expire")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns error -1 on fetch failure")
		{
			int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("memcached_close_storage")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_memcached(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("cleans up storage context cleanly")
		{
			REQUIRE(store->close_storage(store) == 0);
			REQUIRE(store->storage_ctx == NULL);
			free(store);
		}
	}
#endif
}
