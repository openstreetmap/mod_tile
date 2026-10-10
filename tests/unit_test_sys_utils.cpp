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
#include "sys_utils.h"

TEST_CASE("sys_utils.c", "[sys_utils]")
{
	SECTION("get_load_avg function")
	{
		err_log_lines.clear();

		SECTION("get_load_avg", "should return positive")
		{
			double loadavg = get_load_avg();

			REQUIRE(loadavg > 0);
		}

		SECTION("get_load_avg with unobtainable load average", "should return 1000")
		{
			fail_next_getloadavg = true;

			double loadavg = get_load_avg();

#ifdef HAVE_GETLOADAVG
			REQUIRE(loadavg == 1000);
#else
			REQUIRE(loadavg > 0);
#endif
		}
	}
}
