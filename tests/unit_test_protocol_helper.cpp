#include <strings.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "protocol.h"
#include "protocol_helper.h"

extern int fail_next_next_recv_reponse_size;
extern int fail_next_next_recv_reponse_version;
extern int fail_next_recv_reponse_size;
extern int fail_next_recv_reponse_version;
extern std::string err_log_lines;

TEST_CASE("protocol_helper.c", "[protocol_helper]")
{
	int x = 1024;
	int y = 1024;
	int z = 10;

	int block = 1, fd, ret;
	int pipefd[2];
	pipe(pipefd);
	struct protocol *cmd = (struct protocol *)malloc(sizeof(struct protocol));
	struct protocol rsp;
	bzero(&rsp, sizeof(rsp));

	err_log_lines.clear();

	cmd->x = x;
	cmd->y = y;
	cmd->z = z;

	SECTION("recv_cmd function") {
		fd = pipefd[0];

		SECTION("recv_cmd with invalid fd", "should return -1") {
			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to read cmd on fd " + std::to_string(fd)));
		}

		SECTION("recv_cmd with invalid version", "should return -1") {
			fail_next_recv_reponse_size = sizeof(struct protocol);
			fail_next_recv_reponse_version = GENERATE(0, 4);
			std::string expected_message = "Failed to receive render cmd with unknown protocol version " + std::to_string(fail_next_recv_reponse_version);

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains(expected_message));
		}

		SECTION("recv_cmd with incomplete response", "should return 0") {
			fail_next_recv_reponse_size = 1;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Read incomplete cmd on fd " + std::to_string(fd)));
		}

		SECTION("recv_cmd with invalid version and correct size response", "should return -1") {
			fail_next_recv_reponse_size = GENERATE(sizeof(struct protocol_v1), sizeof(struct protocol_v2), sizeof(struct protocol));
			fail_next_recv_reponse_version = GENERATE(0, 4);

			int expected_size = -1;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to receive render cmd with unknown protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with correct size response (v1)", "should return") {
			fail_next_recv_reponse_size = sizeof(struct protocol_v1);
			fail_next_recv_reponse_version = 1;

			int expected_size = fail_next_recv_reponse_size;
			int expected_version = fail_next_recv_reponse_version;

			ret = recv_cmd(&rsp, fd, block);

			REQUIRE(ret == expected_size);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Got incoming request with protocol version " + std::to_string(expected_version)));
		}

		SECTION("recv_cmd with correct size response (v2)", "should return") {
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

		SECTION("recv_cmd with correct size response (v3)", "should return") {
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

		SECTION("recv_cmd with incorrect size response (v2-v3)", "should return") {
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

		SECTION("recv_cmd with incomplete second response (v2-v3)", "should return -1") {
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

	SECTION("send_cmd function") {
		fd = pipefd[1];

		SECTION("send_cmd with invalid version", "should return -1") {
			cmd->ver = GENERATE(0, 4);

			ret = send_cmd(cmd, fd);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send render cmd with unknown protocol version " + std::to_string(cmd->ver)));
		}

		SECTION("send_cmd with invalid fd", "should return -1") {
			cmd->ver = GENERATE(1, 2, 3);

			ret = send_cmd(cmd, fd);

			REQUIRE(ret == -1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::Contains("Failed to send render cmd on fd " + std::to_string(fd)));
		}
	}

	free(cmd);
}
