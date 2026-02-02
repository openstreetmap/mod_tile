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

#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <iostream>
#include <netdb.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>

#include "catch_test_common.hpp"
#include "pstreams/pstream.hpp"

#include "protocol.h"

extern int foreground;

std::string err_log_lines, out_log_lines;

captured_stdio captured_stderr;
captured_stdio captured_stdout;

std::string read_stderr(int buffer_size)
{
	char buffer[buffer_size];
	read(captured_stderr.pipes[0], buffer, buffer_size);
	return buffer;
}

std::string read_stdout(int buffer_size)
{
	char buffer[buffer_size];
	read(captured_stdout.pipes[0], buffer, buffer_size);
	return buffer;
}

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

std::string get_captured_stderr(bool print)
{
	// Terminate captured output with a zero
	write(captured_stderr.pipes[1], "", 1);

	// Restore stderr
	fflush(stderr);
	dup2(captured_stderr.temp_fd, fileno(stderr));

	// Save & return the captured stderr
	std::string log_lines = read_stderr();

	if (print) {
		std::cout << "err_log_lines: " << log_lines << "\n";
	}

	return log_lines;
}

std::string get_captured_stdout(bool print)
{
	// Terminate captured output with a zero
	write(captured_stdout.pipes[1], "", 1);

	// Restore stdout
	fflush(stdout);
	dup2(captured_stdout.temp_fd, fileno(stdout));

	// Save & return the captured stdout
	std::string log_lines = read_stdout();

	if (print) {
		std::cout << "out_log_lines: " << log_lines << "\n";
	}

	return log_lines;
}

void start_capture(bool debug)
{
	foreground = debug ? 1 : 0;

	if (debug) {
		setenv("G_MESSAGES_DEBUG", "all", 1);
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 79
		// https://gitlab.gnome.org/GNOME/glib/-/merge_requests/3710
		const gchar *domains[] = {"all", NULL};
		g_log_writer_default_set_debug_domains(domains);
#endif
	}

	capture_stderr();
	capture_stdout();
}

std::tuple<std::string, std::string> end_capture(bool print)
{
	setenv("G_MESSAGES_DEBUG", "", 1);
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 79
	// https://gitlab.gnome.org/GNOME/glib/-/merge_requests/3710
	g_log_writer_default_set_debug_domains(NULL);
#endif
	foreground = 0;

	return std::tuple<std::string, std::string>(get_captured_stderr(print), get_captured_stdout(print));
}

int run_command(const std::string &file, std::vector<std::string> argv, const std::string &input)
{
	auto mode = redi::pstreams::pstdout | redi::pstreams::pstderr | redi::pstreams::pstdin;

	argv.insert(argv.begin(), file);

	redi::pstream proc(file, argv, mode);

	std::string line;
	err_log_lines = "";
	out_log_lines = "";

	if (!input.empty()) {
		proc << input << redi::peof;
	}

	while (std::getline(proc.err(), line)) {
		err_log_lines += line;
	}

	proc.clear();

	while (std::getline(proc.out(), line)) {
		out_log_lines += line;
	}

	return proc.close();
}

std::string create_tile_dir(const std::string &dir_name, const char *tmp_dir)
{
	if (tmp_dir == NULL) {
		tmp_dir = P_tmpdir;
	}

	std::string tile_dir(tmp_dir);
	tile_dir.append("/").append(dir_name);

	mkdir(tile_dir.c_str(), 0777);

	return tile_dir;
}

int remove_recursive(const char *path)
{
	struct stat st;

	// 1. Get path metadata
	if (lstat(path, &st) != 0) {
		return -1;
	}

	// 2. If it's not a directory, just delete it (file, symlink, etc.)
	if (!S_ISDIR(st.st_mode)) {
		return unlink(path);
	}

	// 3. It's a directory: open it to iterate over contents
	DIR *d = opendir(path);

	if (!d) {
		return -1;
	}

	struct dirent *entry;

	int result = 0;

	while ((entry = readdir(d)) != NULL) {
		// Skip the special "." and ".." entries
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		// Build the full sub-path
		char sub_path[PATH_MAX];
		snprintf(sub_path, sizeof(sub_path), "%s/%s", path, entry->d_name);

		// Recursive call
		result = remove_recursive(sub_path);

		if (result != 0) {
			break;        // Stop early on error
		}
	}

	closedir(d);

	// 4. Once the directory is empty, remove the directory itself
	if (result == 0) {
		result = rmdir(path);
	}

	return result;
}

int delete_tile_dir(const std::string &tile_dir)
{
	return remove_recursive(tile_dir.c_str());
}

extern "C" {
	int exit_status = 0;
	int mock_errno = 0;

	bool fail_next_asprintf = false;
	bool fail_next_connect = false;
	bool fail_next_getaddrinfo = false;
	bool fail_next_getaddrinfo_empty_res = false;
	bool fail_next_getloadavg = false;
	bool fail_next_malloc = false;
	bool fail_next_mkdir = false;
	bool fail_next_open = false;
	bool fail_next_recv = false;
	bool fail_next_socket = false;
	bool fail_next_strndup = false;
	bool fail_next_strtok = false;
	bool fail_next_write = false;

	bool fail_next_next_asprintf = false;

	int fail_next_next_recv_reponse_size = -1;
	int fail_next_next_recv_reponse_version = -1;
	int fail_next_recv_reponse_cmd = -1;
	int fail_next_recv_reponse_size = -1;
	int fail_next_recv_reponse_version = -1;

	jmp_buf exit_jump;

	void mocked_exit(int status)
	{
		exit_status = status;
		longjmp(exit_jump, 1);
	}

	void mocked_g_logger(int log_level, const char *format, ...)
	{
		char *log_message;
		va_list args;

		va_start(args, format);

		vasprintf(&log_message, format, args);

		va_end(args);

		err_log_lines.append(log_message);
		err_log_lines.append("\n");

		free(log_message);
	}

	int mocked_asprintf(char **strp, const char *fmt, ...)
	{
		if (fail_next_asprintf) {
			fail_next_asprintf = false;
			*strp = nullptr;
			return -1;
		}

		if (fail_next_next_asprintf) {
			fail_next_asprintf = true;
			fail_next_next_asprintf = false;
		}

		va_list args;
		va_start(args, fmt);
		int result = vasprintf(strp, fmt, args);
		va_end(args);
		return result;
	}

	int mocked_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
	{
		if (fail_next_connect) {
			fail_next_connect = false;
			return -1;
		}

		return connect(sockfd, addr, addrlen);
	}

	int mocked_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
	{
		if (fail_next_getaddrinfo) {
			fail_next_getaddrinfo = false;
			return -1;
		}

		if (fail_next_getaddrinfo_empty_res) {
			fail_next_getaddrinfo_empty_res = false;
			*res = nullptr;
			return 0;
		}

		return getaddrinfo(node, service, hints, res);
	}

	int mocked_getloadavg(double loadavg[], int nelem)
	{
		if (fail_next_getloadavg) {
			fail_next_getloadavg = false;
			return -1;
		}

		return getloadavg(loadavg, nelem);
	}

	void *mocked_malloc(size_t size)
	{
		if (fail_next_malloc) {
			fail_next_malloc = false;
			return nullptr;
		}

		return malloc(size);
	}

	int mocked_mkdir(const char *path, mode_t mode)
	{
		if (fail_next_mkdir) {
			fail_next_mkdir = false;
			return -1;
		}

		return mkdir(path, mode);
	}

	int mocked_open(const char *pathname, int flags, ...)
	{
		if (fail_next_open) {
			fail_next_open = false;
			errno = (mock_errno != 0) ? mock_errno : EACCES;
			return -1;
		}

		va_list args;
		va_start(args, flags);
		int result = open(pathname, flags, args);
		va_end(args);
		return result;
	}

	ssize_t mocked_recv(int fd, void *buf, size_t n, int flags)
	{
		if (fail_next_recv) {
			fail_next_recv = false;
			return -1;
		}

		if (fail_next_recv_reponse_size != -1) {
			int reponse_size = fail_next_recv_reponse_size;
			fail_next_recv_reponse_size = (fail_next_next_recv_reponse_size != -1) ? fail_next_next_recv_reponse_size : -1;
			fail_next_next_recv_reponse_size = -1;

			if (fail_next_recv_reponse_version != -1) {
				struct protocol *cmd = (struct protocol *)malloc(sizeof(struct protocol));

				if (fail_next_recv_reponse_cmd != -1) {
					cmd->cmd = (protoCmd)fail_next_recv_reponse_cmd;
					fail_next_recv_reponse_cmd = -1;
				} else {
					cmd->cmd = cmdNotDone;
				}
				
				cmd->ver = fail_next_recv_reponse_version;
				fail_next_recv_reponse_version = (fail_next_next_recv_reponse_version != -1) ? fail_next_next_recv_reponse_version : -1;
				fail_next_next_recv_reponse_version = -1;

				memcpy(buf, cmd, reponse_size);

				free(cmd);
			}

			return reponse_size;
		}

		return recv(fd, buf, n, flags);
	}

	int mocked_socket(int domain, int type, int protocol) noexcept
	{
		if (fail_next_socket) {
			fail_next_socket = false;
			return -1;
		}

		return socket(domain, type, protocol);
	}

	char *mocked_strndup(const char *s, size_t n)
	{
		if (fail_next_strndup) {
			fail_next_strndup = false;
			return nullptr;
		}

		return strndup(s, n);
	}

	char *mocked_strtok(char *s, const char *delim)
	{
		if (fail_next_strtok) {
			fail_next_strtok = false;
			return nullptr;
		}

		return strtok(s, delim);
	}

	ssize_t mocked_write(int fd, const void *buf, size_t count)
	{
		if (fail_next_write) {
			fail_next_write = false;
			errno = (mock_errno != 0) ? mock_errno : ENOSPC;
			return -1;
		}

		return write(fd, buf, count);
	}
}
