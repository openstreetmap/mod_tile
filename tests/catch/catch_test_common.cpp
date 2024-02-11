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

#include <glib.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <tuple>
#include <unistd.h>

extern int foreground;

typedef struct _captured_stdio {
	int temp_fd;
	int pipes[2];
} captured_stdio;

captured_stdio captured_stderr;
captured_stdio captured_stdout;

void capture_stderr()
{
	// Flush stderr first if you've previously printed something
	fflush(stderr);

	// Save stderr so it can be restored later
	int temp_stderr;
	temp_stderr = dup(fileno(stderr));

	// Redirect stderr to a new pipe
	int pipes[2];
	pipe(pipes);
	dup2(pipes[1], fileno(stderr));

	captured_stderr.temp_fd = temp_stderr;
	captured_stderr.pipes[0] = pipes[0];
	captured_stderr.pipes[1] = pipes[1];
}

std::string get_captured_stderr(bool print = false)
{
	// Terminate captured output with a zero
	write(captured_stderr.pipes[1], "", 1);

	// Restore stderr
	fflush(stderr);
	dup2(captured_stderr.temp_fd, fileno(stderr));

	// Save & return the captured output
	std::string log_lines;
	const int buffer_size = 4096;
	char buffer[buffer_size];
	read(captured_stderr.pipes[0], buffer, buffer_size);
	log_lines += buffer;

	if (print) {
		std::cout << "err_log_lines: " << log_lines << "\n";
	}

	return log_lines;
}

void capture_stdout()
{
	// Flush stdout first if you've previously printed something
	fflush(stdout);

	// Save stdout so it can be restored later
	int temp_stdout;
	temp_stdout = dup(fileno(stdout));

	// Redirect stdout to a new pipe
	int pipes[2];
	pipe(pipes);
	dup2(pipes[1], fileno(stdout));

	captured_stdout.temp_fd = temp_stdout;
	captured_stdout.pipes[0] = pipes[0];
	captured_stdout.pipes[1] = pipes[1];
}

std::string get_captured_stdout(bool print = false)
{
	// Terminate captured output with a zero
	write(captured_stdout.pipes[1], "", 1);

	// Restore stdout
	fflush(stdout);
	dup2(captured_stdout.temp_fd, fileno(stdout));

	// Save & return the captured output
	std::string log_lines;
	const int buffer_size = 4096;
	char buffer[buffer_size];
	read(captured_stdout.pipes[0], buffer, buffer_size);
	log_lines += buffer;

	if (print) {
		std::cout << "out_log_lines: " << log_lines << "\n";
	}

	return log_lines;
}

void start_capture(bool debug = false)
{
	foreground = debug ? 1 : 0;

	if (debug) {
		setenv("G_MESSAGES_DEBUG", "all", 1);
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 79
		// https://gitlab.gnome.org/GNOME/glib/-/merge_requests/3710
		std::cout << "Resetting G_MESSAGES_DEBUG env var in runtime no longer has an effect.\n";
		const gchar *domains[] = {"all", NULL};
		g_log_writer_default_set_debug_domains(domains);
#endif
	}

	capture_stderr();
	capture_stdout();
}

std::tuple<std::string, std::string> end_capture(bool print = false)
{
	setenv("G_MESSAGES_DEBUG", "", 1);
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 79
	g_log_writer_default_set_debug_domains(NULL);
#endif
	foreground = 0;

	return std::tuple<std::string, std::string>(get_captured_stderr(print), get_captured_stdout(print));
}
