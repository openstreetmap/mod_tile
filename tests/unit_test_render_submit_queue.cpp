#include <chrono>
#include <csetjmp>
#include <cstddef>
#include <string>

#include "catch/catch_amalgamated.hpp"
#include "catch_test_common.hpp"

#include "protocol.h"
#include "render_submit_queue.h"

extern bool fail_next_getloadavg;
extern bool fail_next_socket;
extern int exit_status;
extern int fail_next_next_recv_reponse_size;
extern int fail_next_next_recv_reponse_version;
extern int fail_next_recv_reponse_cmd;
extern int fail_next_recv_reponse_size;
extern int fail_next_recv_reponse_version;
extern jmp_buf exit_jump;
extern std::string err_log_lines;

TEST_CASE("render_submit_queue.c", "[render_submit_queue]")
{
	SECTION("check_load function") {
		err_log_lines.clear();

		maxLoad = 999;
		auto start = std::chrono::high_resolution_clock::now();

		SECTION("check_load with max load of 999", "should return") {
			check_load();
		}

		SECTION("check_load with max load of 999 and unobtainable load average (which returns 1000)", "should return after sleeping 5 seconds") {
			fail_next_getloadavg = true;
			check_load();
			REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count() >= 5);
		}
	}

	SECTION("process function") {
		int fd, ret;
		int pipefd[2];
		REQUIRE(pipe(pipefd) == 0);
		struct protocol *cmd = (struct protocol *)calloc(1, sizeof(struct protocol));

		cmd->cmd = cmdRender;
		cmd->ver = 3;
		cmd->x = 1024;
		cmd->y = 1024;
		cmd->z = 10;

		auto start = std::chrono::high_resolution_clock::now();

		fd = pipefd[0];

		err_log_lines.clear();

		SECTION("process with incomplete response", "should return 0") {
			ret = process(cmd, fd);

			REQUIRE(ret == 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Waiting for response"));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Got response"));
		}

		SECTION("process with cmdNotDone response", "should return positive after sleeping 1 second") {
			fail_next_next_recv_reponse_size = 64;
			fail_next_next_recv_reponse_version = 3;
			fail_next_recv_reponse_size = sizeof(struct protocol) - 64;
			fail_next_recv_reponse_version = 3;
			fail_next_recv_reponse_cmd = cmdNotDone;

			ret = process(cmd, fd);

			REQUIRE(ret > 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Got response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Rendering not done with command"));
			REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count() >= 1);
		}

		SECTION("process with cmdDone response", "should return positive") {
			fail_next_next_recv_reponse_size = 64;
			fail_next_next_recv_reponse_version = 3;
			fail_next_recv_reponse_size = sizeof(struct protocol) - 64;
			fail_next_recv_reponse_version = 3;
			fail_next_recv_reponse_cmd = cmdDone;

			ret = process(cmd, fd);

			REQUIRE(ret > 0);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Sending request"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Waiting for response"));
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Got response"));
			REQUIRE_THAT(err_log_lines, !Catch::Matchers::ContainsSubstring("Rendering not done with command"));
			REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count() < 1);
		}
	}

	SECTION("fetch function") {
		struct protocol *fetch_response;

		err_log_lines.clear();
		exit_status = 0;

		SECTION("qLen=0, work_complete=1", "should return NULL") {
			qLen = 0;
			work_complete = 1;

			fetch_response = fetch();

			REQUIRE(fetch_response == nullptr);
		}

		SECTION("qHead=NULL, qLen=1, work_complete=0", "should exit 1") {
			qHead = nullptr;
			qLen = 1;
			work_complete = 0;

			if (setjmp(exit_jump) == 0) {
				fetch();
				FAIL("fetch should have called exit() but didn't");
			} else {
				SUCCEED("Captured expected exit() call due to null qHead failure");
			}

			REQUIRE(exit_status == 1);
			REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Queue failure, null qHead with " + std::to_string(qLen) + " items in list"));
		}

		// SECTION("qLen=1, work_complete=0", "should exit 1") {
		// 	// qHead = nullptr;
		// 	struct qItem *e;
		// 	qHead = e;
		// 	qLen = 1;
		// 	work_complete = 0;


		// 	fetch_response = fetch();

		// 	// if (setjmp(exit_jump) == 0) {
		// 	// 	fetch();
		// 	// 	FAIL("fetch should have called exit() but didn't");
		// 	// } else {
		// 	// 	SUCCEED("Captured expected exit() call due to null qHead failure");
		// 	// }

		// 	REQUIRE(exit_status == 1);
		// 	REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("Queue failure, null qHead with " + std::to_string(qLen) + " items in list"));
		// }
	}

	SECTION("make_connection function") {
		int ret;

		err_log_lines.clear();
		exit_status = 0;

		SECTION("Unix domain socket") {
			std::string socket_path = std::string(P_tmpdir) + "/renderd.sock";

			SECTION("make_connection with unix domain socket", "should return positive") {
				ret = make_connection(socket_path.c_str());

				// REQUIRE(ret > 0);
			}

			SECTION("make_connection handles socket failure by exiting", "should exit 2") {
				fail_next_socket = true;

				if (setjmp(exit_jump) == 0) {
					make_connection(socket_path.c_str());
					FAIL("make_connection should have called exit() but didn't");
				} else {
					SUCCEED("Captured expected exit() call due to socket failure");
				}

				REQUIRE(exit_status == 2);
				REQUIRE_THAT(err_log_lines, Catch::Matchers::ContainsSubstring("failed to create unix socket"));
			}
		}

		// SECTION("Network socket") {
		// 	std::string socket_path = "127.0.0.1:1234";

		// 	SECTION("make_connection with network socket", "should return positive") {
		// 		ret = make_connection(socket_path.c_str());

		// 		// REQUIRE(ret > 0);
		// 	}
		// }
	}
}
