#include <iostream>

// https://github.com/philsquared/Catch/wiki/Supplying-your-own-main()
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "gen_tile.h"
#include <syslog.h>
#include <sstream>

std::string get_current_stderr() {
    FILE * input = fopen("stderr.out", "r+");
    std::string log_lines;
    unsigned sz = 1024;
    char buffer[sz];
    while (fgets(buffer, 512, input))
    {
        log_lines += buffer;
    }
    // truncate the file now so future reads
    // only get the new stuff
    FILE * input2 = fopen("stderr.out", "w");
    fclose(input2);
    fclose(input);
    return log_lines;
}

TEST_CASE( "renderd", "tile generation" ) {

      SECTION("render_init 1", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "a: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      // we run this test twice to ensure that our stderr reading is working correctlyu
      SECTION("render_init 2", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "b: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      SECTION("renderd startup --help", "should start and show help message") {
          int ret = system("./renderd -h");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 0 );
      }

      SECTION("renderd startup unrecognized option", "should return 1") {
          int ret = system("./renderd --doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }

      SECTION("renderd startup invalid option", "should return 1") {
          int ret = system("./renderd -doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }
}

int main (int argc, char* const argv[])
{
  //std::ios_base::sync_with_stdio(false);
  // start by supressing stderr
  // this avoids noisy test output that is intentionally
  // testing for things that produce stderr and also
  // allows us to catch and read it in these tests to validate
  // the stderr contains the right messages
  // http://stackoverflow.com/questions/13533655/how-to-listen-to-stderr-in-c-c-for-sending-to-callback
  FILE * stream = freopen("stderr.out", "w", stderr);
  //setvbuf(stream, 0, _IOLBF, 0); // No Buffering
  openlog("renderd", LOG_PID | LOG_PERROR, LOG_DAEMON);
  int result = Catch::Main( argc, argv );
  return result;
}
