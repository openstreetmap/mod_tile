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

#include <arpa/inet.h>
#include <atomic>
#include <csetjmp>
#include <netdb.h>
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

extern std::string err_log_lines, out_log_lines;

extern "C" {
	extern int exit_status;
	extern int mock_errno;

	extern std::atomic<bool> exit_pthread; // set by the test thread, read by worker threads

	extern bool fail_next_asprintf;
	extern bool fail_next_calloc;
	extern bool fail_next_connect;
	extern bool fail_next_getaddrinfo;
	extern bool fail_next_getaddrinfo_empty_res;
	extern bool fail_next_getloadavg;
	extern bool fail_next_getnameinfo;
	extern bool fail_next_getprotobyname;
	extern bool fail_next_gmtime_r;
	extern bool fail_next_malloc;
	extern bool fail_next_mkdir;
	extern bool fail_next_open;
	extern bool fail_next_pthread_cond_init;
	extern bool fail_next_pthread_cond_wait;
	extern bool fail_next_pthread_create;
	extern bool fail_next_pthread_mutex_init;
	extern bool fail_next_read;
	extern bool fail_next_recv;
	extern bool fail_next_rename;
	extern bool fail_next_socket;
	extern bool fail_next_strdup;
	extern bool fail_next_strndup;
	extern bool fail_next_strtok;
	extern bool fail_next_utime;
	extern bool fail_next_write;

	extern bool force_real_sleep;

	extern std::atomic<bool> pass_next_connect; // may be consumed by worker threads
	extern bool pass_next_sleep;

	extern int fail_next_next_recv_reponse_size;
	extern int fail_next_next_recv_reponse_version;
	extern int fail_next_recv_reponse_cmd;
	extern int fail_next_recv_reponse_size;
	extern int fail_next_recv_reponse_version;

	extern int fail_asprintf_on_call_count;
	extern int fail_malloc_on_call_count;
	extern int fail_read_on_call_count;

	extern jmp_buf exit_jump;

	void reset_asprintf_test_count(void);
	void reset_malloc_test_count(void);
	void reset_read_call_count(void);

	[[noreturn]] void mocked_exit(int status);
	void mocked_g_logger(int log_level, const char *format, ...);

	int mocked_asprintf(char **strp, const char *fmt, ...);
	void *mocked_calloc(size_t num, size_t size);
	int mocked_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	int mocked_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
	int mocked_getloadavg(double loadavg[], int nelem);
	int mocked_getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags);
	struct protoent *mocked_getprotobyname(const char *name);
	struct tm *mocked_gmtime_r(const time_t *timer, struct tm *tp);
	void *mocked_malloc(size_t size);
	int mocked_mkdir(const char *path, mode_t mode);
	int mocked_open(const char *pathname, int flags, ...);
	int mocked_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *cond_attr);
	int mocked_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
	int mocked_pthread_create(pthread_t *__restrict newthread, const pthread_attr_t *__restrict attr, void *(*start_routine)(void *), void *__restrict arg);
	int mocked_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);
	ssize_t mocked_read(int fd, void *buf, size_t nbytes);
	ssize_t mocked_recv(int fd, void *buf, size_t n, int flags);
	int mocked_rename(const char *__old, const char *__new);
	unsigned int mocked_sleep(unsigned int seconds);
	int mocked_socket(int domain, int type, int protocol) noexcept;
	char *mocked_strdup(const char *s);
	char *mocked_strndup(const char *s, size_t n);
	char *mocked_strtok(char *s, const char *delim);
	int mocked_utime(const char *file, const struct utimbuf *file_times);
	ssize_t mocked_write(int fd, const void *buf, size_t count);
}

#endif
