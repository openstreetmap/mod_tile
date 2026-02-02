#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "sys_utils.h"

extern bool fail_next_getloadavg;
extern std::string err_log_lines;

TEST_CASE("sys_utils.c", "[sys_utils]")
{
	SECTION("get_load_avg function") {
		err_log_lines.clear();

		SECTION("get_load_avg", "should return positive") {
			double loadavg = get_load_avg();

			REQUIRE(loadavg > 0);
		}

		SECTION("get_load_avg with unobtainable load average", "should return 1000") {
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
