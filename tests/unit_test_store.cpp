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

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "store.h"

TEST_CASE("store.c", "[store]")
{
	SECTION("init_storage_backend")
	{
		err_log_lines.clear();
		reset_malloc_test_count();

		SECTION("returns NULL when options string is empty")
		{
			struct storage_backend *store = init_storage_backend("");
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Options string was empty"));
		}

		SECTION("handles absolute path storage options")
		{
			SECTION("fails when path does not exist (stat failure)")
			{
				err_log_lines.clear();
				struct storage_backend *store = init_storage_backend("/tmp/nonexistent_mod_tile_dir_12345");
				REQUIRE(store == NULL);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to stat /tmp/nonexistent_mod_tile_dir_12345"));
			}

			SECTION("fails when path points to a file rather than a directory")
			{
				std::string tmpl = std::string(P_tmpdir) + "/mod_tile_regular_file_XXXXXX";
				std::vector<char> tmp_file(tmpl.begin(), tmpl.end());
				tmp_file.push_back('\0');
				int fd = mkstemp(tmp_file.data());
				REQUIRE(fd >= 0);
				close(fd);

				err_log_lines.clear();
				struct storage_backend *store = init_storage_backend(tmp_file.data());
				REQUIRE(store == NULL);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("is not a directory"));

				unlink(tmp_file.data());
			}

			SECTION("initializes file storage backend when path is a valid directory")
			{
				std::string tile_dir = create_tile_dir("store_file_dir", NULL);

				err_log_lines.clear();
				struct storage_backend *store = init_storage_backend(tile_dir.c_str());
				REQUIRE(store != NULL);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising file storage backend at:"));

				store->close_storage(store);
				free(store);

				std::string tile_dir_slash = tile_dir + "/";
				err_log_lines.clear();
				store = init_storage_backend(tile_dir_slash.c_str());
				REQUIRE(store != NULL);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising file storage backend at:"));

				store->close_storage(store);
				free(store);

				delete_tile_dir(tile_dir);
			}
		}

		SECTION("handles rados:// prefix options")
		{
			err_log_lines.clear();
			std::string rados_opts = "rados://config_file=/etc/ceph/ceph.conf;pool=tiles";
			struct storage_backend *store = init_storage_backend(rados_opts.c_str());

#ifndef HAVE_LIBRADOS
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Support for rados storage has not been compiled into this program"));
#else
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("failed to read rados config file"));
#endif
		}

		SECTION("handles memcached:// prefix options")
		{
			err_log_lines.clear();
			std::string memc_opts = "memcached://127.0.0.1:11211";
			struct storage_backend *store = init_storage_backend(memc_opts.c_str());

#ifndef HAVE_LIBMEMCACHED
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Support for memcached storage has not been compiled into this program"));
#else
			REQUIRE(store != NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising memcached storage backend at:"));

			store->close_storage(store);
			free(store);
#endif
		}

		SECTION("handles ro_http_proxy:// prefix options")
		{
			err_log_lines.clear();
			std::string http_opts = "ro_http_proxy://tile.openstreetmap.org";
			struct storage_backend *store = init_storage_backend(http_opts.c_str());

#ifndef HAVE_LIBCURL
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Support for http proxy storage has not been compiled into this program"));
#else
			REQUIRE(store != NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising ro_http_proxy storage backend at:"));

			store->close_storage(store);
			free(store);
#endif
		}

		SECTION("handles composite:{ prefix options")
		{
			std::string primary_dir = create_tile_dir("store_comp_primary", NULL);
			std::string secondary_dir = create_tile_dir("store_comp_secondary", NULL);
			std::string comp_opts = "composite:{" + std::string(XMLCONFIG_DEFAULT) + "," + primary_dir + "}{" + std::string(XMLCONFIG_DEFAULT) + "," + secondary_dir + "}";

			err_log_lines.clear();
			struct storage_backend *store = init_storage_backend(comp_opts.c_str());

#ifndef HAVE_CAIRO
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Support for compositing storage has not been compiled into this program"));
#else
			REQUIRE(store != NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising ro_composite storage backend at:"));

			store->close_storage(store);
			free(store);
#endif
			delete_tile_dir(primary_dir);
			delete_tile_dir(secondary_dir);
		}

		SECTION("handles null:// prefix options")
		{
			err_log_lines.clear();
			struct storage_backend *store = init_storage_backend("null://");
			REQUIRE(store != NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("initialising null storage backend at:"));

			store->close_storage(store);
			free(store);
		}

		SECTION("returns NULL when no backend matches unrecognized options prefix")
		{
			err_log_lines.clear();
			struct storage_backend *store = init_storage_backend("unsupported_backend_schema://localhost");
			REQUIRE(store == NULL);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("No valid storage backend found for options:"));
		}
	}
}
