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

#include <strings.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "protocol_helper.h"

TEST_CASE("protocol_helper.c", "[protocol_helper]")
{
	int x = 1024;
	int y = 1024;
	int z = 10;

	int block = 1, fd = -1, ret;
	int pipefd[2];
	REQUIRE(pipe(pipefd) == 0);
	struct protocol *cmd = (struct protocol *)calloc(1, sizeof(struct protocol));
	struct protocol rsp;
	bzero(&rsp, sizeof(rsp));

	err_log_lines.clear();

	cmd->x = x;
	cmd->y = y;
	cmd->z = z;

	SECTION("recv_cmd function")
	{
		fd = pipefd[0];

		SECTION("recv_cmd with invalid fd", "should return -1")
		{
			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to read cmd on fd " + std::to_string(fd)));
		}

		SECTION("recv_cmd with invalid version", "should return -1")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol);
			fail_next_recv_reponse_version = GENERATE(0, 4);
			std::string expected_message = "Failed to receive render cmd with unknown protocol version " + std::to_string(fail_next_recv_reponse_version);

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains(expected_message));
		}

		SECTION("recv_cmd with incomplete response", "should return 0")
		{
			fail_next_recv_reponse_size = 1;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read incomplete cmd on fd " + std::to_string(fd)));
		}

		SECTION("recv_cmd with invalid version and correct size response", "should return -1")
		{
			fail_next_recv_reponse_size = GENERATE(sizeof(struct protocol_v1), sizeof(struct protocol_v2), sizeof(struct protocol));
			fail_next_recv_reponse_version = GENERATE(0, 4);

			int expected_size = -1;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to receive render cmd with unknown protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with correct size response (v1)", "should return")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = 1;

			int expected_size = fail_next_recv_reponse_size;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with correct size response (v2)", "should return")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = 2;
			fail_next_next_recv_reponse_size = sizeof(struct protocol_v2) - fail_next_recv_reponse_size;
			fail_next_next_recv_reponse_version = fail_next_recv_reponse_version;

			int expected_size = fail_next_recv_reponse_size + fail_next_next_recv_reponse_size;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with correct size response (v3)", "should return")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = 3;
			fail_next_next_recv_reponse_size = sizeof(struct protocol) - fail_next_recv_reponse_size;
			fail_next_next_recv_reponse_version = fail_next_recv_reponse_version;

			int expected_size = fail_next_recv_reponse_size + fail_next_next_recv_reponse_size;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with incorrect size response (v2-v3)", "should return")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = GENERATE(2, 3);
			fail_next_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_next_recv_reponse_version = fail_next_recv_reponse_version;

			int expected_size = fail_next_recv_reponse_size + fail_next_next_recv_reponse_size;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Socket read wrong number of bytes: " + std::to_string(expected_size)));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with incomplete second response (v2-v3)", "should return -1")
		{
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = GENERATE(2, 3);
			fail_next_next_recv_reponse_size = 0;
			fail_next_next_recv_reponse_version = fail_next_recv_reponse_version;

			int expected_size = -1;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Socket prematurely closed: " + std::to_string(fd)));
		}
	}

	SECTION("send_cmd function")
	{
		fd = pipefd[1];

		SECTION("send_cmd with negative/invalid file descriptor", "should return -1")
		{
			cmd->ver = 1;
			int invalid_fd = -1;

			ret = send_cmd(cmd, invalid_fd);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Attempted to send cmd on invalid fd " + std::to_string(invalid_fd)));
		}

		SECTION("send_cmd with unknown protocol version", "should return -1")
		{
			cmd->ver = GENERATE(0, 4);

			ret = send_cmd(cmd, fd);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send render cmd with unknown protocol version " + std::to_string(cmd->ver)));
		}

		SECTION("send_cmd with pipe descriptor (non-socket ENOTTY check)", "should fail socket validation")
		{
			cmd->ver = GENERATE(1, 2, 3);

			ret = send_cmd(cmd, fd);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("fd " + std::to_string(fd) + " is not a valid socket"));
		}

		SECTION("send_cmd successfully sends across protocol versions 1, 2, and 3")
		{
			int sv[2];
			REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
			int socket_fd = sv[0];

			int cmd_ver;
			size_t expected_size;

			std::tie(cmd_ver, expected_size) =
			    GENERATE(table<int, size_t>({std::make_tuple(1, sizeof(struct protocol_v1)),
							 std::make_tuple(2, sizeof(struct protocol_v2)),
							 std::make_tuple(3, sizeof(struct protocol))}));

			cmd->ver = cmd_ver;
			ret = send_cmd(cmd, socket_fd);

			REQUIRE(ret == expected_size);

			close(sv[0]);
			close(sv[1]);
		}
	}

	close(pipefd[0]);
	close(pipefd[1]);
	free(cmd);
}
