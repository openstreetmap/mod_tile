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

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "store_rados.h"

TEST_CASE("store_rados.c", "[store_rados]")
{
	std::string valid_conn = "rados://tiles/etc/ceph/ceph.conf";

	SECTION("init_storage_rados")
	{
		err_log_lines.clear();
		reset_malloc_test_count();

#ifdef HAVE_LIBRADOS
		SECTION("handles allocation failure for context or store struct (store=NULL, ctx=NULL)")
		{
			fail_next_malloc = true;
			struct storage_backend *store = init_storage_rados(valid_conn.c_str());
			REQUIRE(store == NULL);
			reset_malloc_test_count();
		}

		SECTION("handles allocation failure for context struct (store!=NULL, ctx=NULL)")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 2;
			struct storage_backend *store = init_storage_rados(valid_conn.c_str());
			REQUIRE(store == NULL);
			reset_malloc_test_count();
		}

		SECTION("fails gracefully when configuration file cannot be read")
		{
			err_log_lines.clear();
			struct storage_backend *store = init_storage_rados("rados://tiles/nonexistent/ceph.conf");
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to read rados config file"));
		}
#else
		SECTION("returns NULL when HAVE_LIBRADOS is disabled")
		{
			struct storage_backend *store = init_storage_rados(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Support for rados storage has not been compiled into this program"));
		}
#endif
	}

#ifdef HAVE_LIBRADOS
	SECTION("rados_xyzo_to_storagekey")
	{
		SECTION("formats storage key without options")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = rados_xyzo_to_storagekey(XMLCONFIG_DEFAULT, "", 10, 20, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == std::string(XMLCONFIG_DEFAULT) + "/5/8/16.meta");
		}

		SECTION("formats storage key with options string")
		{
			char key_buf[PATH_MAX] = {0};
			char *res = rados_xyzo_to_storagekey(XMLCONFIG_DEFAULT, "options_str", 10, 20, 5, key_buf);
			REQUIRE(res == key_buf);
			REQUIRE(std::string(key_buf) == std::string(XMLCONFIG_DEFAULT) + "/5/8/16.options_str.meta");
		}
	}

	SECTION("read_meta_data")
	{
		SECTION("returns cached data on consecutive coordinate hits")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 8;
			ctx.metadata_cache.y = 16;
			ctx.metadata_cache.z = 5;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);

			char mock_buf[] = "MOCK_METADATA_HEADER";
			ctx.metadata_cache.data = mock_buf;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			err_log_lines.clear();
			char *res = read_meta_data(&store, XMLCONFIG_DEFAULT, "", 8, 16, 5);
			REQUIRE(res == mock_buf);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Returning cached data for"));
		}
	}

	SECTION("rados_tile_read")
	{
		char read_buf[1024 * 64] = {0};
		char log_msg[1024] = {0};
		int compressed = 0;

		SECTION("fails when header memory allocation fails")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = (char *)"dummy_cache_payload";

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			reset_malloc_test_count();
			fail_next_malloc = true;

			int res = rados_tile_read(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to allocate memory for metatile header"));
			reset_malloc_test_count();
		}

		SECTION("fails when metadata read returns NULL")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = NULL;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			int res = rados_tile_read(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -3);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to read metadata of tile"));
		}

		SECTION("detects magic mismatch on corrupted header")
		{
			size_t header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			char *mock_meta = (char *)calloc(1, header_len);

			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = mock_meta;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			int res = rados_tile_read(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -4);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Meta file header magic mismatch"));

			free(mock_meta);
		}

		SECTION("validates compressed header magic and bad metatile tile count")
		{
			size_t header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			char *mock_meta = (char *)calloc(1, header_len);

			struct meta_layout *m = (struct meta_layout *)(mock_meta + sizeof(struct stat_info));
			memcpy(m->magic, META_MAGIC_COMPRESSED, strlen(META_MAGIC_COMPRESSED));
			m->count = 10;

			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = mock_meta;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			int res = rados_tile_read(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -5);
			REQUIRE(compressed == 1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Meta file header bad count"));

			free(mock_meta);
		}

		SECTION("detects buffer overrun on oversized tile")
		{
			size_t header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			char *mock_meta = (char *)calloc(1, header_len);

			struct meta_layout *m = (struct meta_layout *)(mock_meta + sizeof(struct stat_info));
			memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
			m->count = METATILE * METATILE;
			m->index[0].offset = 0;
			m->index[0].size = 2048;

			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = mock_meta;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			int res = rados_tile_read(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, 100, &compressed, log_msg);
			REQUIRE(res == -6);
			REQUIRE(compressed == 0);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Truncating tile"));

			free(mock_meta);
		}
	}

	SECTION("rados_tile_stat")
	{
		SECTION("returns empty stat structure when metadata read fails")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = NULL;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			struct stat_info info = rados_tile_stat(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == -1);
			REQUIRE(info.expired == 0);
			REQUIRE(info.mtime == 0);
			REQUIRE(info.atime == 0);
			REQUIRE(info.ctime == 0);
		}

		SECTION("parses stat payload and tile size from metadata cache")
		{
			size_t header_len = sizeof(struct stat_info) + sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
			char *mock_meta = (char *)calloc(1, header_len);

			struct stat_info *st = (struct stat_info *)mock_meta;
			st->size = 4096;
			st->mtime = 12345678;

			struct meta_layout *m = (struct meta_layout *)(mock_meta + sizeof(struct stat_info));
			m->index[0].size = 512;

			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.metadata_cache.x = 0;
			ctx.metadata_cache.y = 0;
			ctx.metadata_cache.z = 0;
			snprintf(ctx.metadata_cache.xmlname, sizeof(ctx.metadata_cache.xmlname), "%s", XMLCONFIG_DEFAULT);
			ctx.metadata_cache.data = mock_meta;

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			struct stat_info info = rados_tile_stat(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == 512);
			REQUIRE(info.mtime == 12345678);

			free(mock_meta);
		}
	}

	SECTION("rados_tile_storage_id")
	{
		SECTION("formats rados URI with pool name and path")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.pool = strdup("tiles");

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			char uri_buf[PATH_MAX] = {0};
			char *id = rados_tile_storage_id(&store, XMLCONFIG_DEFAULT, "", 8, 16, 5, uri_buf);
			REQUIRE(std::string(id) == "rados://tiles/" + std::string(XMLCONFIG_DEFAULT) + "/5/8/16.meta");

			free(ctx.pool);
		}
	}

	SECTION("rados_metatile_write")
	{
		SECTION("handles allocation failure for payload write buffer")
		{
			struct storage_backend store;
			memset(&store, 0, sizeof(store));

			reset_malloc_test_count();
			fail_next_malloc = true;

			err_log_lines.clear();
			int res = rados_metatile_write(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "payload", 7);
			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for write buffer"));
			reset_malloc_test_count();
		}

		SECTION("executes write buffer prep and logging on write error branch")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.pool = strdup("tiles");

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			err_log_lines.clear();
			reset_malloc_test_count();

			int res = rados_metatile_write(&store, XMLCONFIG_DEFAULT, "", 0, 0, 0, "payload", 7);
			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Trying to create and write a tile to rados://tiles/"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("cannot write rados://tiles/"));

			free(ctx.pool);
		}
	}

	SECTION("rados_metatile_expire")
	{
		SECTION("handles uninitialized context gracefully")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.pool = strdup("tiles");

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			err_log_lines.clear();
			int res = rados_metatile_expire(&store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Trying to expire a tile at rados://tiles/"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("cannot expire rados://tiles/"));

			free(ctx.pool);
		}
	}

	SECTION("rados_metatile_delete")
	{
		SECTION("handles uninitialized context gracefully")
		{
			struct rados_ctx ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.pool = strdup("tiles");

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = &ctx;

			err_log_lines.clear();
			int res = rados_metatile_delete(&store, XMLCONFIG_DEFAULT, 0, 0, 0);
			REQUIRE(res == -1);

			free(ctx.pool);
		}
	}

	SECTION("rados_close_storage")
	{
		SECTION("frees allocated memory structures cleanly")
		{
			struct rados_ctx *ctx = (struct rados_ctx *)calloc(1, sizeof(struct rados_ctx));
			ctx->pool = strdup("tiles");
			ctx->metadata_cache.data = (char *)malloc(1024);

			struct storage_backend store;
			memset(&store, 0, sizeof(store));
			store.storage_ctx = ctx;

			err_log_lines.clear();
			int res = rados_close_storage(&store);
			REQUIRE(res == 0);
			REQUIRE(store.storage_ctx == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Closed rados backend"));
		}
	}
#endif
}
