#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "metatile.h"
#include "store.h"

extern bool fail_next_malloc;
extern bool fail_next_mkdir;
extern bool fail_next_open;
extern bool fail_next_write;
extern std::string err_log_lines;

TEST_CASE("metatile.cpp", "[metatile]")
{
	int x = 1024;
	int y = 1024;
	int z = 10;
	std::string host("host");
	std::string metatile_path;
	std::string tile_data_string = "DEADBEAF";
	std::string tile_dir;
	std::string uri("/uri/");
	std::string xmlconfig = XMLCONFIG_DEFAULT;
	struct storage_backend *store;

	metaTile tiles(xmlconfig.c_str(), "", x, y, z);

	SECTION("metaTile::save function") {
		tile_dir = create_tile_dir("mod_tile.unit_test_metatile");
		store = init_storage_backend(tile_dir.c_str());

		metatile_path = std::string((char *)store->storage_ctx) + "/" + xmlconfig + "/" + std::to_string(z) + "/0/0/68/0/0.meta";

		err_log_lines.clear();

		for (int xx = 0; xx < METATILE; xx++) {
			for (int yy = 0; yy < METATILE; yy++) {
				std::string tile_set_data(tile_data_string + " " + std::to_string(xx) + " " + std::to_string(yy));
				tiles.set(xx, yy, tile_set_data);
			}
		}

		SECTION("metaTile::save handles malloc failure", "should return") {
			fail_next_malloc = true;

			tiles.save(store);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to write metatile. Out of memory"));
		}

		SECTION("metaTile::save handles mkdir failure", "should return") {
			fail_next_mkdir = true;

			tiles.save(store);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error creating directory " + metatile_path));
		}

		SECTION("metaTile::save handles open failure", "should return") {
			fail_next_open = true;

			tiles.save(store);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error creating file " + metatile_path));
		}

		SECTION("metaTile::save handles write failure", "should return") {
			fail_next_write = true;

			tiles.save(store);

			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Error writing file " + metatile_path));
		}

		store->metatile_delete(store, xmlconfig.c_str(), 1024, 1024, 10);
		store->close_storage(store);
		delete_tile_dir(tile_dir);
		tiles.clear();
	}


	SECTION("metaTile::set function") {
		err_log_lines.clear();

		SECTION("metaTile::set", "then metaTile::get, then metaTile::clear, then metaTile::get") {
			for (int xx = 0; xx < METATILE; xx++) {
				for (int yy = 0; yy < METATILE; yy++) {
					std::string tile_set_data(tile_data_string + " " + std::to_string(xx) + " " + std::to_string(yy));
					tiles.set(xx, yy, tile_set_data);

					std::string tile_get_data = tiles.get(xx, yy);
					CHECK(tile_get_data == tile_set_data);
				}
			}

			tiles.clear();

			for (int xx = 0; xx < METATILE; xx++) {
				for (int yy = 0; yy < METATILE; yy++) {
					std::string tile_get_data = tiles.get(xx, yy);
					CHECK(tile_get_data == "");
				}
			}
		}
	}


	SECTION("metaTile::expire_tiles function") {
		err_log_lines.clear();

		SECTION("metaTile::expire_tile", "should return") {
			int sock = 0;

			tiles.expire_tiles(sock, "host", "/uri/");
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Purging metatile via HTCP cache expiry"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send HTCP purge for http://" + host + uri + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png"));
		}

		SECTION("metaTile::expire_tiles handles negative sock", "should return") {
			int sock = -1;

			tiles.expire_tiles(sock, "host", "/uri/");
			REQUIRE(err_log_lines == "");
		}
	}


	SECTION("metaTile::xyz_to_meta_offset function") {
		err_log_lines.clear();

		SECTION("metaTile::xyz_to_meta_offset", "should return") {
			int i = 0;

			for (int xx = 0; xx < METATILE; xx++) {
				for (int yy = 0; yy < METATILE; yy++) {
					CHECK(tiles.xyz_to_meta_offset(xx, yy, z) == i++);
				}
			}
		}
	}
}
