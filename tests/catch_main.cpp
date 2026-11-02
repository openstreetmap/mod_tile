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

#include <string>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

extern "C" {
	int foreground = 1;
}

std::string cli_option_memcached = "";
std::string cli_option_ro_http_proxy = "";

int main(int argc, char *argv[])
{
	Catch::Session session;

	// Bind custom options into Catch2 CLI parser
	using namespace Catch::Clara;
	auto cli = session.cli() |
		   Opt(cli_option_memcached, "hostname:port")
		       ["--memcached"]("Set memcached target for unit tests") |
		   Opt(cli_option_ro_http_proxy, "hostname:port[/path]")
		       ["--ro_http_proxy"]("Set HTTP proxy target for unit tests");

	session.cli(cli);

	// Apply command line options first to validate inputs
	int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0) {
		return returnCode;
	}

	return session.run();
}
