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

#include <limits.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "store.h"
#include "store_null.h"

TEST_CASE("store_null.c", "[store_null]")
{
	SECTION("init_storage_null function")
	{
		err_log_lines.clear();

		SECTION("init_storage_null handles malloc failure", "should return NULL")
		{
			fail_next_malloc = true;

			struct storage_backend *store = init_storage_null();

			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("init_storage_null: Failed to allocate memory for storage backend"));
		}

		SECTION("init_storage_null creates valid storage interface", "should return initialized backend struct")
		{
			struct storage_backend *store = init_storage_null();

			REQUIRE(store != NULL);
			REQUIRE(store->storage_ctx == NULL);
			REQUIRE(store->tile_read != NULL);
			REQUIRE(store->tile_stat != NULL);
			REQUIRE(store->metatile_write != NULL);
			REQUIRE(store->metatile_delete != NULL);
			REQUIRE(store->metatile_expire != NULL);
			REQUIRE(store->tile_storage_id != NULL);
			REQUIRE(store->close_storage != NULL);

			SECTION("tile_read method", "should set err_msg and return -1")
			{
				char err_msg[PATH_MAX] = {0};
				char buf[256] = {0};
				int compressed = 0;

				int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, buf, sizeof(buf), &compressed, err_msg);

				REQUIRE(res == -1);
				REQUIRE(std::string(err_msg) == "Cannot read from NULL storage.");
			}

			SECTION("tile_stat method", "should return expired stat with size -1")
			{
				struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);

				REQUIRE(info.size == -1);
				REQUIRE(info.atime == 0);
				REQUIRE(info.mtime == 0);
				REQUIRE(info.ctime == 0);
				REQUIRE(info.expired == 1);
			}

			SECTION("metatile_write method", "should return buffer size as written bytes")
			{
				const char data[] = "test_tile_data";
				int res = store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, data, sizeof(data));

				REQUIRE(res == static_cast<int>(sizeof(data)));
			}

			SECTION("metatile_delete method", "should return 0")
			{
				int res = store->metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0);

				REQUIRE(res == 0);
			}

			SECTION("metatile_expire method", "should return 0")
			{
				int res = store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0);

				REQUIRE(res == 0);
			}

			SECTION("tile_storage_id method", "should populate string with null:// URI scheme")
			{
				char uri_buf[PATH_MAX] = {0};

				char *res = store->tile_storage_id(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, uri_buf);

				REQUIRE(res == uri_buf);
				REQUIRE(std::string(uri_buf) == "null://");
			}

			SECTION("close_storage method", "should return 0")
			{
				int res = store->close_storage(store);

				REQUIRE(res == 0);
			}

			free(store);
		}
	}
}
