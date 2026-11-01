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

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "metatile.h"
#include "render_config.h"
#include "store_file_utils.h"
#include "store_ro_composite.h"

#ifdef HAVE_CAIRO
static cairo_status_t read_png_stream_from_byte_array(void *in_closure, unsigned char *data, unsigned int length);
static cairo_status_t write_png_stream_to_byte_array(void *in_closure, const unsigned char *data, unsigned int length);

static void create_mock_metatile_for_composite(const char *tile_dir, const char *xmlconfig, int x, int y, int z, const char *data, size_t data_len)
{
	char meta_path[PATH_MAX];
	xyzo_to_meta(meta_path, sizeof(meta_path), tile_dir, xmlconfig, "", x, y, z);
	mkdirp(meta_path);

	size_t header_len = sizeof(struct meta_layout) + METATILE * METATILE * sizeof(struct entry);
	struct meta_layout *m = (struct meta_layout *)calloc(1, header_len);

	memcpy(m->magic, META_MAGIC, strlen(META_MAGIC));
	m->count = METATILE * METATILE;

	int offset_index = (x % METATILE) * METATILE + (y % METATILE);
	m->index[offset_index].offset = header_len;
	m->index[offset_index].size = data_len;

	int fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0) {
		REQUIRE(write(fd, m, header_len) == (ssize_t)(header_len));
		if (data_len > 0 && data != NULL) {
			REQUIRE(write(fd, data, data_len) == (ssize_t)(data_len));
		}
		close(fd);
	}

	free(m);
}

static std::vector<unsigned char> create_mock_png_bytes(int size = 256)
{
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	std::vector<unsigned char> buffer;

	auto write_func = [](void *closure, const unsigned char *data, unsigned int length) -> cairo_status_t {
		auto *buf = static_cast<std::vector<unsigned char> *>(closure);
		buf->insert(buf->end(), data, data + length);
		return CAIRO_STATUS_SUCCESS;
	};

	cairo_surface_write_to_png_stream(surface, write_func, &buffer);
	cairo_surface_destroy(surface);
	return buffer;
}
#endif

TEST_CASE("store_ro_composite.c", "[store_ro_composite]")
{
	std::string primary_dir = create_tile_dir("composite_primary");
	std::string secondary_dir = create_tile_dir("composite_secondary");

	std::string valid_conn = "composite:{" + std::string(XMLCONFIG_DEFAULT) + "," + primary_dir + "}{" + std::string(XMLCONFIG_DEFAULT) + "," + secondary_dir + "}";

	SECTION("init_storage_ro_composite")
	{
		err_log_lines.clear();

#ifdef HAVE_CAIRO
		SECTION("handles allocation failure for initial store struct")
		{
			fail_next_malloc = true;
			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for context"));
		}

		SECTION("handles allocation failure for context struct (store allocated)")
		{
			fail_next_calloc = true;
			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for context"));
		}

		SECTION("handles allocation failure for primary connection string buffer")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 2;

			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to allocate memory for primary connection string"));
		}

		SECTION("handles allocation failure for primary storage backend")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 3;

			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to initialise primary storage backend"));
		}

		SECTION("handles allocation failure for secondary storage backend")
		{
			reset_malloc_test_count();
			fail_malloc_on_call_count = 4;

			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to initialise secondary storage backend"));
		}

		SECTION("handles malformed connection string missing '}{' separator")
		{
			std::string bad_conn = "composite:{default," + primary_dir + "}";
			struct storage_backend *store = init_storage_ro_composite(bad_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("missing '}{' separator"));
		}

		SECTION("handles malformed primary string missing ',' separator")
		{
			std::string bad_conn = "composite:{default_no_comma}{default," + secondary_dir + "}";
			struct storage_backend *store = init_storage_ro_composite(bad_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("malformed primary connection string, missing ',' separator"));
		}

		SECTION("handles malformed secondary string missing ',' separator")
		{
			std::string bad_conn = "composite:{" + std::string(XMLCONFIG_DEFAULT) + "," + primary_dir + "}{secondary_no_comma}";
			struct storage_backend *store = init_storage_ro_composite(bad_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("malformed secondary connection string, missing ',' separator"));
		}

		SECTION("initializes valid compositing storage backend")
		{
			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
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
#else
		SECTION("returns NULL when HAVE_CAIRO is disabled")
		{
			struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Support for compositing storage has not been compiled into this program"));
		}
#endif
	}

#ifdef HAVE_CAIRO
	SECTION("ro_composite_tile_read")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
		REQUIRE(store != NULL);

		char read_buf[1024 * 64] = {0};
		char log_msg[1024] = {0};
		int compressed = 0;

		SECTION("fails when primary backend fails to read tile")
		{
			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to read tile data of primary backend"));
		}

		SECTION("fails when primary backend returns invalid non-PNG tile data")
		{
			const char invalid_png[] = "NOT_A_PNG_IMAGE_DATA";
			auto png_bytes = create_mock_png_bytes();

			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, invalid_png, sizeof(invalid_png));
			create_mock_metatile_for_composite(secondary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to decode png data from primary backend"));
		}

		SECTION("fails when secondary backend fails to read tile")
		{
			auto png_bytes = create_mock_png_bytes();
			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to read tile data of secondary backend"));
		}

		SECTION("fails when secondary backend returns invalid non-PNG tile data")
		{
			auto png_bytes = create_mock_png_bytes();
			const char invalid_png[] = "NOT_A_PNG_IMAGE_DATA";

			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());
			create_mock_metatile_for_composite(secondary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, invalid_png, sizeof(invalid_png));

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to decode png data from secondary backend"));
		}

		SECTION("fails when output buffer is too small for output PNG")
		{
			auto png_bytes = create_mock_png_bytes();

			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());
			create_mock_metatile_for_composite(secondary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, 10, &compressed, log_msg);
			REQUIRE(res == -1);
		}

		SECTION("fails when primary decode buffer stream is truncated")
		{
			const unsigned char truncated_png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)truncated_png, sizeof(truncated_png));

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to decode png data from primary backend"));
		}

		SECTION("fails when output surface creation fails")
		{
			auto png_bytes = create_mock_png_bytes();
			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());
			create_mock_metatile_for_composite(secondary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());

			struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)(store->storage_ctx);
			int orig_size = ctx->render_size;
			ctx->render_size = 32767000;

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res == -1);
			REQUIRE_THAT(std::string(log_msg), Catch::Matchers::ContainsSubstring("Failed to create output png"));

			ctx->render_size = orig_size;
		}

		SECTION("successfully composites primary and secondary PNG tiles")
		{
			auto png_bytes = create_mock_png_bytes();

			create_mock_metatile_for_composite(primary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());
			create_mock_metatile_for_composite(secondary_dir.c_str(), XMLCONFIG_DEFAULT, 0, 0, 0, (const char *)png_bytes.data(), png_bytes.size());

			int res = store->tile_read(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, read_buf, sizeof(read_buf), &compressed, log_msg);
			REQUIRE(res > 0);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_composite_tile_stat")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("reflects primary backend stat result")
		{
			struct stat_info info = store->tile_stat(store, XMLCONFIG_DEFAULT, "", 0, 0, 0);
			REQUIRE(info.size == -1);
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_composite_tile_storage_id")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("returns constant storage label")
		{
			char uri_buf[PATH_MAX] = {0};
			char *id = store->tile_storage_id(store, XMLCONFIG_DEFAULT, "", 0, 0, 0, uri_buf);
			REQUIRE(std::string(id) == "Composite tile");
		}

		store->close_storage(store);
		free(store);
	}

	SECTION("ro_composite_metatile_write")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
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

	SECTION("ro_composite_metatile_delete")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
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

	SECTION("ro_composite_metatile_expire")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
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

	SECTION("ro_composite_close_storage")
	{
		struct storage_backend *store = init_storage_ro_composite(valid_conn.c_str());
		REQUIRE(store != NULL);

		SECTION("frees context resources and clears cache if present")
		{
			struct ro_composite_ctx *ctx = (struct ro_composite_ctx *)(store->storage_ctx);
			ctx->cache.tile = (char *)malloc(128);

			REQUIRE(store->close_storage(store) == 0);
			REQUIRE(store->storage_ctx == NULL);
			free(store);
		}
	}

	SECTION("write_png_stream_to_byte_array")
	{
		SECTION("writes data successfully")
		{
			char buffer[16] = {0};
			png_stream_to_byte_array_closure_t closure = {
			    .data = buffer,
			    .max_size = sizeof(buffer),
			    .pos = 0};

			const unsigned char test_data[] = "hello";
			cairo_status_t status = write_png_stream_to_byte_array(&closure, test_data, 5);
			REQUIRE(status == CAIRO_STATUS_SUCCESS);
			REQUIRE(closure.pos == 5);
			REQUIRE(memcmp(buffer, "hello", 5) == 0);
		}

		SECTION("handles buffer write overflow")
		{
			char buffer[16] = {0};
			png_stream_to_byte_array_closure_t closure = {
			    .data = buffer,
			    .max_size = sizeof(buffer),
			    .pos = 0};

			const unsigned char overflow_data[20] = {0};
			cairo_status_t status = write_png_stream_to_byte_array(&closure, overflow_data, sizeof(overflow_data));
			REQUIRE(status == CAIRO_STATUS_WRITE_ERROR);
			REQUIRE(closure.pos == 0);
		}
	}

	SECTION("read_png_stream_from_byte_array")
	{
		SECTION("reads data successfully")
		{
			char buffer[] = "stream_data_test";
			png_stream_to_byte_array_closure_t closure = {
			    .data = buffer,
			    .max_size = sizeof(buffer) - 1,
			    .pos = 0};

			unsigned char out_buf[8] = {0};
			cairo_status_t status = read_png_stream_from_byte_array(&closure, out_buf, 6);
			REQUIRE(status == CAIRO_STATUS_SUCCESS);
			REQUIRE(closure.pos == 6);
			REQUIRE(memcmp(out_buf, "stream", 6) == 0);
		}

		SECTION("handles buffer read overflow")
		{
			char buffer[] = "stream_data_test";
			png_stream_to_byte_array_closure_t closure = {
			    .data = buffer,
			    .max_size = sizeof(buffer) - 1,
			    .pos = 0};

			unsigned char overflow_out[20] = {0};
			cairo_status_t status = read_png_stream_from_byte_array(&closure, overflow_out, 20);
			REQUIRE(status == CAIRO_STATUS_READ_ERROR);
			REQUIRE(closure.pos == 0);
		}
	}
#endif

	delete_tile_dir(primary_dir);
	delete_tile_dir(secondary_dir);
}
