/*
 * Copyright (c) 2007 - 2023 by mod_tile contributors (see AUTHORS file)
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

#define CATCH_CONFIG_MAIN

#include "catch.hpp"
#include "catch_test_common.hpp"

int foreground = 1;

struct CaptureListener : Catch::TestEventListenerBase {

	using TestEventListenerBase::TestEventListenerBase;

	void testCaseStarting(Catch::TestCaseInfo const &testCaseInfo) override
	{
		start_capture();
	}

	void testCaseEnded(Catch::TestCaseStats const &testCaseStats) override
	{
		bool print = false;

		if (testCaseStats.totals.assertions.failed > 0) {
			print = true;
		}

		end_capture(print);
	}
};

CATCH_REGISTER_LISTENER(CaptureListener)
