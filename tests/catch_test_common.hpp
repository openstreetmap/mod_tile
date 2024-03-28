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

#include <string>
#include <vector>

#ifndef CATCH_TEST_COMMON_HPP
#define CATCH_TEST_COMMON_HPP

typedef struct _captured_stdio {
	int temp_fd;
	int pipes[2];
} captured_stdio;

std::string read_stderr(int buffer_size = 16384);
std::string read_stdout(int buffer_size = 16384);

void capture_stderr();
void capture_stdout();

std::string get_captured_stderr(bool print = false);
std::string get_captured_stdout(bool print = false);

void start_capture(bool debug = false);
std::tuple<std::string, std::string> end_capture(bool print = false);

int run_command(std::string file, std::vector<std::string> argv = {}, std::string input = "");

#endif
