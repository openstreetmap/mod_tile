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

#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <iostream>
#include <netdb.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utime.h>
#include <vector>

#include "catch_test_common.hpp"
#include "protocol.h"
#include "pstreams/pstream.hpp"

extern int foreground;

std::string err_log_lines, out_log_lines;

captured_stdio captured_stderr;
captured_stdio captured_stdout;

std::string read_stderr(int buffer_size)
{
	std::string buffer(buffer_size, '\0');
	ssize_t len = read(captured_stderr.pipes[0], &buffer[0], buffer_size);
	buffer.resize(len > 0 ? strnlen(buffer.c_str(), len) : 0);
	return buffer;
}

std::string read_stdout(int buffer_size)
{
	std::string buffer(buffer_size, '\0');
	ssize_t len = read(captured_stdout.pipes[0], &buffer[0], buffer_size);
	buffer.resize(len > 0 ? strnlen(buffer.c_str(), len) : 0);
	return buffer;
}

void capture_stderr()
{
	fflush(stderr);

	int temp_stderr = dup(fileno(stderr));
	int pipes[2];

	if (pipe(pipes) != 0) {
		throw std::runtime_error("capture: pipe() failed");
	}

	dup2(pipes[1], fileno(stderr));

	captured_stderr.temp_fd = temp_stderr;
	captured_stderr.pipes[0] = pipes[0];
	captured_stderr.pipes[1] = pipes[1];
}

void capture_stdout()
{
	fflush(stdout);

	int temp_stdout = dup(fileno(stdout));
	int pipes[2];

	if (pipe(pipes) != 0) {
		throw std::runtime_error("capture: pipe() failed");
	}

	dup2(pipes[1], fileno(stdout));

	captured_stdout.temp_fd = temp_stdout;
	captured_stdout.pipes[0] = pipes[0];
	captured_stdout.pipes[1] = pipes[1];
}

std::string get_captured_stderr(bool print)
{
	if (write(captured_stderr.pipes[1], "", 1) != 1) {
		throw std::runtime_error("capture: write() failed");
	}

	fflush(stderr);
	dup2(captured_stderr.temp_fd, fileno(stderr));

	std::string log_lines = read_stderr();

	if (print) {
		std::cout << "err_log_lines: " << log_lines << "\n";
	}

	return log_lines;
}

std::string get_captured_stdout(bool print)
{
	if (write(captured_stdout.pipes[1], "", 1) != 1) {
		throw std::runtime_error("capture: write() failed");
	}

	fflush(stdout);
	dup2(captured_stdout.temp_fd, fileno(stdout));

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

	if (lstat(path, &st) != 0) {
		return -1;
	}

	if (!S_ISDIR(st.st_mode)) {
		return unlink(path);
	}

	DIR *d = opendir(path);
	if (!d) {
		return -1;
	}

	struct dirent *entry;
	int result = 0;

	while ((entry = readdir(d)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char sub_path[PATH_MAX];
		snprintf(sub_path, sizeof(sub_path), "%s/%s", path, entry->d_name);

		result = remove_recursive(sub_path);
		if (result != 0) {
			break;
		}
	}

	closedir(d);

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

	std::atomic<bool> exit_pthread{false};

	bool fail_next_asprintf = false;
	bool fail_next_calloc = false;
	bool fail_next_connect = false;
	bool fail_next_getaddrinfo = false;
	bool fail_next_getaddrinfo_empty_res = false;
	bool fail_next_getloadavg = false;
	bool fail_next_getnameinfo = false;
	bool fail_next_getprotobyname = false;
	bool fail_next_gmtime_r = false;
	bool fail_next_malloc = false;
	bool fail_next_mkdir = false;
	bool fail_next_open = false;
	bool fail_next_pthread_cond_init = false;
	bool fail_next_pthread_cond_wait = false;
	bool fail_next_pthread_create = false;
	bool fail_next_pthread_mutex_init = false;
	bool fail_next_read = false;
	bool fail_next_recv = false;
	bool fail_next_rename = false;
	bool fail_next_socket = false;
	bool fail_next_strdup = false;
	bool fail_next_strndup = false;
	bool fail_next_strtok = false;
	bool fail_next_utime = false;
	bool fail_next_write = false;

	bool force_real_sleep = false;

	std::atomic<bool> pass_next_connect{false};
	bool pass_next_sleep = false;

	int fail_next_next_recv_reponse_size = -1;
	int fail_next_next_recv_reponse_version = -1;
	int fail_next_recv_reponse_cmd = -1;
	int fail_next_recv_reponse_size = -1;
	int fail_next_recv_reponse_version = -1;

	int fail_asprintf_on_call_count = 0;
	int fail_malloc_on_call_count = 0;
	int fail_read_on_call_count = 0;

	jmp_buf exit_jump;

	void reset_asprintf_test_count(void)
	{
		fail_asprintf_on_call_count = 0;
	}

	void reset_malloc_test_count(void)
	{
		fail_malloc_on_call_count = 0;
	}

	void reset_read_call_count(void)
	{
		fail_read_on_call_count = 0;
	}

	[[noreturn]] void mocked_exit(int status)
	{
		exit_status = status;
		longjmp(exit_jump, 1);
	}

	void mocked_g_logger(int log_level, const char *format, ...)
	{
		(void)log_level;
		char *log_message;
		va_list args;

		va_start(args, format);
		int len = vasprintf(&log_message, format, args);
		va_end(args);

		if (len < 0) {
			throw std::runtime_error("mocked_g_logger: vasprintf() failed");
		}

		err_log_lines.append(log_message);
		err_log_lines.append("\n");

		free(log_message);
	}

	int mocked_asprintf(char **strp, const char *fmt, ...)
	{
		if (fail_next_asprintf) {
			fail_next_asprintf = false;
			errno = (mock_errno != 0) ? mock_errno : ENOMEM;
			*strp = nullptr;
			return -1;
		}

		if (fail_asprintf_on_call_count > 0) {
			if (--fail_asprintf_on_call_count == 0) {
				errno = (mock_errno != 0) ? mock_errno : ENOMEM;
				*strp = nullptr;
				return -1;
			}
		}

		va_list args;
		va_start(args, fmt);
		int result = vasprintf(strp, fmt, args);
		va_end(args);
		return result;
	}

	void *mocked_calloc(size_t num, size_t size)
	{
		if (fail_next_calloc) {
			fail_next_calloc = false;
			errno = (mock_errno != 0) ? mock_errno : ENOMEM;
			return nullptr;
		}

		return calloc(num, size);
	}

	int mocked_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
	{
		if (fail_next_connect) {
			fail_next_connect = false;
			errno = (mock_errno != 0) ? mock_errno : EACCES;
			return -1;
		}

		// Test and clear in one step: several worker threads may connect at once
		if (pass_next_connect.exchange(false)) {
			return 0;
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

	int mocked_getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags)
	{
		if (fail_next_getnameinfo) {
			fail_next_getnameinfo = false;
			return (mock_errno != 0) ? mock_errno : EAI_AGAIN;
		}

		return getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags);
	}

	struct protoent *mocked_getprotobyname(const char *name)
	{
		if (fail_next_getprotobyname) {
			fail_next_getprotobyname = false;
			return nullptr;
		}

		return getprotobyname(name);
	}

	struct tm *mocked_gmtime_r(const time_t *timer, struct tm *tp)
	{
		if (fail_next_gmtime_r) {
			fail_next_gmtime_r = false;
			return NULL;
		}

		return gmtime_r(timer, tp);
	}

	void *mocked_malloc(size_t size)
	{
		if (fail_next_malloc) {
			fail_next_malloc = false;
			errno = (mock_errno != 0) ? mock_errno : ENOMEM;
			return nullptr;
		}

		if (fail_malloc_on_call_count > 0) {
			if (--fail_malloc_on_call_count == 0) {
				errno = (mock_errno != 0) ? mock_errno : ENOMEM;
				return nullptr;
			}
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

	int mocked_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *cond_attr)
	{
		if (fail_next_pthread_cond_init) {
			fail_next_pthread_cond_init = false;
			return (mock_errno != 0) ? mock_errno : ENOMEM;
		}

		return pthread_cond_init(cond, cond_attr);
	}

	int mocked_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
	{
		if (fail_next_pthread_cond_wait) {
			fail_next_pthread_cond_wait = false;
			return (mock_errno != 0) ? mock_errno : EINVAL;
		}

		return pthread_cond_wait(cond, mutex);
	}

	int mocked_pthread_create(pthread_t *newthread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
	{
		if (fail_next_pthread_create) {
			fail_next_pthread_create = false;
			return (mock_errno != 0) ? mock_errno : EAGAIN;
		}

		return pthread_create(newthread, attr, start_routine, arg);
	}

	int mocked_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
	{
		if (fail_next_pthread_mutex_init) {
			fail_next_pthread_mutex_init = false;
			return (mock_errno != 0) ? mock_errno : ENOMEM;
		}

		return pthread_mutex_init(mutex, mutexattr);
	}

	ssize_t mocked_read(int fd, void *buf, size_t nbytes)
	{
		if (fail_next_read) {
			fail_next_read = false;
			errno = (mock_errno != 0) ? mock_errno : EIO;
			return -1;
		}

		if (fail_read_on_call_count > 0) {
			if (--fail_read_on_call_count == 0) {
				errno = (mock_errno != 0) ? mock_errno : EIO;
				return -1;
			}
		}

		return read(fd, buf, nbytes);
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

	int mocked_rename(const char *__old, const char *__new)
	{
		if (fail_next_rename) {
			fail_next_rename = false;
			errno = (mock_errno != 0) ? mock_errno : EACCES;
			return -1;
		}

		return rename(__old, __new);
	}

	unsigned int mocked_sleep(unsigned int seconds)
	{
		if (force_real_sleep) {
			return sleep(seconds);
		}

		if (exit_pthread) {
			pthread_exit(NULL);
		}

		if (pass_next_sleep) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			return 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		return 0;
	}

	int mocked_socket(int domain, int type, int protocol) noexcept
	{
		if (fail_next_socket) {
			fail_next_socket = false;
			return -1;
		}

		return socket(domain, type, protocol);
	}

	char *mocked_strdup(const char *s)
	{
		if (fail_next_strdup) {
			fail_next_strdup = false;
			errno = (mock_errno != 0) ? mock_errno : ENOMEM;
			return nullptr;
		}

		return strdup(s);
	}

	char *mocked_strndup(const char *s, size_t n)
	{
		if (fail_next_strndup) {
			fail_next_strndup = false;
			errno = (mock_errno != 0) ? mock_errno : ENOMEM;
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

	int mocked_utime(const char *file, const struct utimbuf *file_times)
	{
		if (fail_next_utime) {
			fail_next_utime = false;
			errno = (mock_errno != 0) ? mock_errno : EPERM;
			return -1;
		}

		return utime(file, file_times);
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
