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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
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

int run_command(const std::string &file, std::vector<std::string> argv = {}, const std::string &input = "");

std::string create_tile_dir(const std::string &dir_name = "mod_tile_test", const char *tmp_dir = getenv("TMPDIR"));
int delete_tile_dir(const std::string &tile_dir);

extern "C" {
	void mocked_exit(int status);
	void mocked_g_logger(int log_level, const char *format, ...);

	int mocked_asprintf(char **strp, const char *fmt, ...);
	int mocked_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	int mocked_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
	int mocked_getloadavg(double loadavg[], int nelem);
	void *mocked_malloc(size_t size);
	int mocked_mkdir(const char *path, mode_t mode);
	int mocked_open(const char *pathname, int flags, ...);
	ssize_t mocked_recv(int fd, void *buf, size_t n, int flags);
	int mocked_socket(int domain, int type, int protocol) noexcept;
	char *mocked_strndup(const char *s, size_t n);
	char *mocked_strtok(char *s, const char *delim);
	ssize_t mocked_write(int fd, const void *buf, size_t count);
}

#endif
