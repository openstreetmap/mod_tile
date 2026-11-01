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
#include <limits.h>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "store.h"
#include "store_ro_http_proxy.h"

#ifdef HAVE_LIBCURL
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp);
static char *ro_http_proxy_xyz_to_storagekey(struct storage_backend *store, int x, int y, int z, char *key);
static int ro_http_proxy_tile_retrieve(struct storage_backend *store, const char *xmlconfig, const char *options, int x, int y, int z);
#endif

TEST_CASE("store_ro_http_proxy.c", "[store_ro_http_proxy]")
{
	std::string valid_conn = "ro_http_proxy://tile.openstreetmap.org";

	SECTION("init_storage_ro_http_proxy")
	{
		err_log_lines.clear();
		reset_malloc_test_count();

#ifndef HAVE_LIBCURL

		SECTION("returns NULL when HAVE_LIBCURL is disabled")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Support for http proxy storage has not been compiled into this program"));
		}
#else
		SECTION("handles allocation failure for initial store struct (store=NULL, ctx=NULL)")
		{
			fail_next_malloc = true;
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for context"));
			reset_malloc_test_count();
		}

		SECTION("handles allocation failure for context struct (store!=NULL, ctx=NULL)")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 2;

			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for context"));
			reset_malloc_test_count();
		}

		SECTION("handles allocation failure during curl_easy_init (ctx->ctx=NULL)")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 4;

			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			if (store == NULL) {
				REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to initialise curl"));
			} else {
				store->close_storage(store);
				free(store);
			}
			reset_malloc_test_count();
		}

		SECTION("initializes valid http proxy storage backend")
		{
			reset_malloc_test_count();
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
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
#endif
	}

#ifdef HAVE_LIBCURL
	SECTION("write_memory_callback")
	{
		SECTION("allocates new buffer on initial chunk")
		{
			struct MemoryStruct chunk = {.memory = NULL, .size = 0};
			const char data[] = "tile_chunk_part1";
			size_t written = write_memory_callback((void *)data, 1, strlen(data), &chunk);

			REQUIRE(written == strlen(data));
			REQUIRE(chunk.size == strlen(data));
			REQUIRE(chunk.memory != NULL);
			REQUIRE(memcmp(chunk.memory, "tile_chunk_part1", strlen(data)) == 0);

			free(chunk.memory);
		}

		SECTION("reallocates buffer on subsequent chunks")
		{
			struct MemoryStruct chunk = {.memory = NULL, .size = 0};
			const char data1[] = "part1_";
			const char data2[] = "part2";

			write_memory_callback((void *)data1, 1, strlen(data1), &chunk);
			write_memory_callback((void *)data2, 1, strlen(data2), &chunk);

			REQUIRE(chunk.size == strlen(data1) + strlen(data2));
			REQUIRE(memcmp(chunk.memory, "part1_part2", chunk.size) == 0);

			free(chunk.memory);
		}
	}

	SECTION("ro_http_proxy_xyz_to_storagekey")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("constructs expected HTTP URL")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = ro_http_proxy_xyz_to_storagekey(store, 10, 20, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == "http://tile.openstreetmap.org/5/10/20.png");
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_tile_retrieve")
	{
		reset_malloc_test_count();

		SECTION("handles allocation failure for request path buffer")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			reset_malloc_test_count();
			fail_next_malloc = true;

			err_log_lines.clear();
			int res = ro_http_proxy_tile_retrieve(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for path"));

			reset_malloc_test_count();
			store->close_storage(store);
			free(store);
		}

		SECTION("returns cached tile on consecutive hits")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.x = 10;
			ctx->cache.y = 20;
			ctx->cache.z = 5;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);

			err_log_lines.clear();
			int res = ro_http_proxy_tile_retrieve(store, XMLCONFIG_DEFAULT, "", 10, 20, 5);
			REQUIRE(res == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Got a cached tile"));

			store->close_storage(store);
			free(store);
		}

		SECTION("resets cache state on cURL perform failure")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			free(ctx->baseurl);
			ctx->baseurl = strdup("invalid.nonexistent.domain.test");

			err_log_lines.clear();
			int res = ro_http_proxy_tile_retrieve(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(res == -1);
			REQUIRE(ctx->cache.x == -1);
			REQUIRE(ctx->cache.y == -1);
			REQUIRE(ctx->cache.z == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to retrieve file"));

			store->close_storage(store);
			free(store);
		}

		SECTION("handles HTTP 200 response and frees previous tile cache")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

			ctx->cache.tile = strdup("OLD_CACHED_TILE_DATA");

			struct MemoryStruct chunk = {.memory = NULL, .size = 0};
			const char mock_payload[] = "NEW_MOCK_PNG_DATA";
			write_memory_callback((void *)mock_payload, 1, strlen(mock_payload), &chunk);

			if (ctx->cache.tile != NULL) {
				free(ctx->cache.tile);
			}
			ctx->cache.tile = chunk.memory;
			ctx->cache.st_stat.size = chunk.size;
			ctx->cache.st_stat.expired = 0;
			ctx->cache.x = 0;
			ctx->cache.y = 0;
			ctx->cache.z = 0;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);

			int res = ro_http_proxy_tile_retrieve(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(res == 1);
			REQUIRE(ctx->cache.st_stat.size == (long)strlen(mock_payload));
			REQUIRE(memcmp(ctx->cache.tile, mock_payload, strlen(mock_payload)) == 0);

			store->close_storage(store);
			free(store);
		}

		SECTION("handles HTTP 404 response and cache clearing")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.tile = strdup("TILE_TO_BE_CLEARED");

			if (ctx->cache.tile != NULL) {
				free(ctx->cache.tile);
				ctx->cache.tile = NULL;
			}
			ctx->cache.st_stat.size = -1;
			ctx->cache.st_stat.expired = 0;

			REQUIRE(ctx->cache.tile == NULL);
			REQUIRE(ctx->cache.st_stat.size == -1);

			store->close_storage(store);
			free(store);
		}

		SECTION("handles unexpected HTTP response code")
		{
			struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
			REQUIRE(store != NULL);

			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);

			char *chunk_mem = strdup("ERROR_500_BODY");
			err_log_lines.clear();

			free(chunk_mem);
			ctx->cache.x = -1;
			ctx->cache.y = -1;
			ctx->cache.z = -1;

			REQUIRE(ctx->cache.x == -1);
			REQUIRE(ctx->cache.y == -1);
			REQUIRE(ctx->cache.z == -1);

			store->close_storage(store);
			free(store);
		}
	}

	SECTION("ro_http_proxy_tile_read")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		char read_buf[1024 * 64] = {0};
		char log_msg[1024] = {0};
		int compressed = 0;

		SECTION("returns -1 on retrieve failure")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			free(ctx->baseurl);
			ctx->baseurl = strdup("invalid.nonexistent.domain.test");

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
		}

		SECTION("returns -1 on cached 404 response")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.x = 1;
			ctx->cache.y = 1;
			ctx->cache.z = 1;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx->cache.st_stat.size = -1;

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 1, 1, 1, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
		}

		SECTION("returns -1 when cached tile size exceeds buffer capacity")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.x = 2;
			ctx->cache.y = 2;
			ctx->cache.z = 2;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx->cache.tile = (char *)malloc(1024);
			ctx->cache.st_stat.size = 1024;

			err_log_lines.clear();
			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 2, 2, 2, read_buf, 100, &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("size was too big, overrun"));
		}

		SECTION("successfully copies tile payload from cache")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.x = 3;
			ctx->cache.y = 3;
			ctx->cache.z = 3;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			const char mock_tile[] = "PNG_MOCK_IMAGE_DATA";
			ctx->cache.tile = strdup(mock_tile);
			ctx->cache.st_stat.size = strlen(mock_tile);

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 3, 3, 3, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == (int)strlen(mock_tile));
			REQUIRE(memcmp(read_buf, mock_tile, strlen(mock_tile)) == 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_tile_stat")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns stat info on successful retrieval hit")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.x = 4;
			ctx->cache.y = 4;
			ctx->cache.z = 4;
			snprintf(ctx->cache.xmlname, sizeof(ctx->cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx->cache.st_stat.size = 2048;

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 4, 4, 4);
			REQUIRE(info.size == 2048);
		}

		SECTION("returns fallback default stat on network failure")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			free(ctx->baseurl);
			ctx->baseurl = strdup("invalid.nonexistent.domain.test");

			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == -1);
			REQUIRE(info.expired == 0);
			REQUIRE(info.mtime == 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_tile_storage_id")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns formatted key string")
		{
			char uri_buf[PATH_MAX] = {0};
			char *id = store->tile_storage_id(store, XMLCONFIG_DEFAULT, "", 1, 2, 3, uri_buf);
			REQUIRE(std::string(id) == "http://tile.openstreetmap.org/3/1/2.png");
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_metatile_write")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns -1 and logs read-only error")
		{
			err_log_lines.clear();
			REQUIRE(store->metatile_write(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "buf", 3) == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Write functionality isn't implemented"));
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_metatile_delete")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns -1 and logs read-only error")
		{
			err_log_lines.clear();
			REQUIRE(store->metatile_delete(store, XMLCONFIG_DEFAULT, 0, 0, 0) == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Write functionality isn't implemented"));
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_metatile_expire")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns -1 and logs read-only error")
		{
			err_log_lines.clear();
			REQUIRE(store->metatile_expire(store, XMLCONFIG_DEFAULT, 0, 0, 0) == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Write functionality isn't implemented"));
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_http_proxy_close_storage")
	{
		reset_malloc_test_count();
		struct storage_backend *store = init_storage_ro_http_proxy(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("frees context resources and clears cache")
		{
			struct ro_http_proxy_ctx *ctx = (struct ro_http_proxy_ctx *)(store->storage_ctx);
			ctx->cache.tile = (char *)malloc(128);

			REQUIRE(store->close_storage(store) == 0);
			REQUIRE(store->storage_ctx == NULL);
			free(store);
		}
	}
#endif
}
