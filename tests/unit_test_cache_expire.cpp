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

#include "cache_expire.h"

static void cache_expire_url(int sock, char *url);

TEST_CASE("cache_expire.c", "[cache_expire]")
{
	int x = 0;
	int y = 0;
	int z = 0;
	std::string host("host");
	std::string uri("/uri/");

	SECTION("cache_expire_url function")
	{
		std::string url("http://" + host + uri + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png");

		err_log_lines.clear();

		SECTION("cache_expire_url", "should return")
		{
			int sock = 0;

			cache_expire_url(sock, (char *)url.c_str());
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send HTCP purge for " + url));
		}

		SECTION("cache_expire_url handles malloc failure", "should return")
		{
			int sock = 0;

			fail_next_malloc = true;

			cache_expire_url(sock, (char *)url.c_str());
			REQUIRE(err_log_lines == "");
		}

		SECTION("cache_expire_url handles negative sock", "should return")
		{
			int sock = -1;

			cache_expire_url(sock, (char *)url.c_str());
			REQUIRE(err_log_lines == "");
		}
	}

	SECTION("cache_expire function")
	{
		err_log_lines.clear();

		SECTION("cache_expire", "should return")
		{
			int sock = 0;

			cache_expire(sock, (char *)host.c_str(), (char *)uri.c_str(), x, y, z);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send HTCP purge for http://" + host + uri + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png"));
		}

		SECTION("cache_expire handles malloc failure", "should return")
		{
			int sock = 0;

			cache_expire(sock, (char *)host.c_str(), (char *)uri.c_str(), x, y, z);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send HTCP purge for http://" + host + uri + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png"));
		}

		SECTION("cache_expire handles negative sock", "should return")
		{
			int sock = -1;

			cache_expire(sock, (char *)host.c_str(), (char *)uri.c_str(), x, y, z);
			REQUIRE(err_log_lines == "");
		}
	}

	SECTION("init_cache_expire function")
	{
		err_log_lines.clear();

		SECTION("init_cache_expire", "should return")
		{
			std::string htcphost("localhost");
			init_cache_expire((char *)htcphost.c_str());
			REQUIRE(err_log_lines == "");
		}

		SECTION("init_cache_expire with nonexistent host", "should return")
		{
			std::string htcphost("nonexistenthost");
			init_cache_expire((char *)htcphost.c_str());
			REQUIRE_THAT(err_log_lines,
				     Catch::Matchers::Contains("Failed to lookup HTCP cache host: Address family for hostname not supported") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: Name could not be resolved at this time") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: Name does not resolve") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: Name or service not known") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: Temporary failure in name resolution") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: nodename nor servname provided, or not known"));
		}

		SECTION("init_cache_expire with failed socket", "should return")
		{
			std::string htcphost("localhost");

			fail_next_socket = true;

			init_cache_expire((char *)htcphost.c_str());
			REQUIRE(err_log_lines == "");
		}

		SECTION("init_cache_expire with failed getaddrinfo", "should return")
		{
			std::string htcphost("localhost");

			fail_next_getaddrinfo = true;

			init_cache_expire((char *)htcphost.c_str());
			REQUIRE_THAT(err_log_lines,
				     Catch::Matchers::Contains("Failed to lookup HTCP cache host: Bad value for ai_flags") ||
					 Catch::Matchers::Contains("Failed to lookup HTCP cache host: Unknown error"));
		}

		SECTION("init_cache_expire with empty getaddrinfo response", "should return")
		{
			std::string htcphost("localhost");

			fail_next_getaddrinfo_empty_res = true;

			init_cache_expire((char *)htcphost.c_str());
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to create HTCP cache socket"));
		}

		SECTION("init_cache_expire with failed connect", "should return")
		{
			std::string htcphost("localhost");

			fail_next_connect = true;

			init_cache_expire((char *)htcphost.c_str());
			REQUIRE(err_log_lines == "");
		}
	}
}
