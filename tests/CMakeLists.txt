#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
#
#  Find external dependencies
#
#-----------------------------------------------------------------------------

find_package(UnixCommands REQUIRED)
find_program(CAT_EXECUTABLE cat REQUIRED)
find_program(ID_EXECUTABLE id REQUIRED)
find_program(MAPNIK_CONFIG_EXECUTABLE mapnik-config REQUIRED)
find_program(MKDIR_EXECUTABLE mkdir REQUIRED)
find_program(PKILL_EXECUTABLE pkill REQUIRED)
find_program(SHA256SUM_EXECUTABLE NAMES sha256sum sha256 REQUIRED)

execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --fonts
  OUTPUT_STRIP_TRAILING_WHITESPACE
OUTPUT_VARIABLE MAPNIK_FONTS_DIR)

execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --input-plugins
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE MAPNIK_INPUT_DIR)

execute_process(COMMAND ${ID_EXECUTABLE} -gn nobody
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE NOGROUP_NAME)

execute_process(COMMAND ${APXS_EXECUTABLE} -q progname
  OUTPUT_VARIABLE HTTPD_EXECUTABLE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

#-----------------------------------------------------------------------------
#
#  Test configurations
#
#-----------------------------------------------------------------------------

configure_file(
    renderd.conf.in
    conf/renderd.conf
)

configure_file(
  httpd.conf.in
  conf/httpd.conf
)

#-----------------------------------------------------------------------------
#
#  Tests
#
#-----------------------------------------------------------------------------

set(TILE_SHA256SUM "dbf26531286e844a3a9735cdd193598dca78d22f77cafe5824bcaf17f88cbb08")
set(TILE_URL "http://localhost:8081/tiles/renderd-example/9/297/191.png")

add_test(
  NAME gen_tile_test
  COMMAND gen_tile_test
  WORKING_DIRECTORY src
)
add_test(
  NAME create_dirs
  COMMAND ${MKDIR_EXECUTABLE} -p -v logs run tiles
)
add_test(
  NAME start_renderd
  COMMAND renderd --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf
)
add_test(
  NAME start_httpd
  COMMAND ${HTTPD_EXECUTABLE} -e debug -f ${PROJECT_BINARY_DIR}/tests/conf/httpd.conf -k start
)
add_test(
  NAME download_tile
  COMMAND ${BASH} -c "
    until curl --fail --output tile.png --silent ${TILE_URL}; do
      echo 'Sleeping 1s';
      sleep 1;
    done
  "
)
add_test(
  NAME check_tile
  COMMAND ${BASH} -c "
    if [[ '${SHA256SUM_EXECUTABLE}' == *sha256sum ]]; then
      echo '${TILE_SHA256SUM}  tile.png' | ${SHA256SUM_EXECUTABLE} -c;
    elif [[ '${SHA256SUM_EXECUTABLE}' == *sha256 ]]; then
      ${SHA256SUM_EXECUTABLE} -c '${TILE_SHA256SUM}' tile.png;
    fi
  "
)
add_test(
  NAME remove_tile
  COMMAND ${RM} -v tile.png
)
add_test(
  NAME stop_renderd
  COMMAND ${PKILL_EXECUTABLE} renderd
)
add_test(
  NAME stop_httpd
  COMMAND ${BASH} -c "
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/httpd.pid) && ${RM} run/httpd.pid
  "
)
add_test(
  NAME clear_dirs
  COMMAND ${BASH} -c "
    ${RM} -f -r -v logs/* run/* tiles/*
  "
)

set_tests_properties(create_dirs PROPERTIES
  FIXTURES_SETUP httpd_started
)
set_tests_properties(start_renderd PROPERTIES
  DEPENDS create_dirs
  FIXTURES_SETUP httpd_started
)
set_tests_properties(start_httpd PROPERTIES
  DEPENDS create_dirs
  FIXTURES_SETUP httpd_started
)
set_tests_properties(stop_renderd PROPERTIES
  FIXTURES_CLEANUP httpd_started
)
set_tests_properties(stop_httpd PROPERTIES
  FIXTURES_CLEANUP httpd_started
  REQUIRED_FILES run/httpd.pid
)
set_tests_properties(clear_dirs PROPERTIES
  DEPENDS "stop_renderd;stop_httpd"
  FIXTURES_CLEANUP httpd_started
  REQUIRED_FILES "logs;run;tiles"
)

set_tests_properties(download_tile PROPERTIES
  FIXTURES_REQUIRED httpd_started
  FIXTURES_SETUP tile_downloaded
  TIMEOUT 10
)
set_tests_properties(check_tile PROPERTIES
  DEPENDS download_tile
  FIXTURES_REQUIRED tile_downloaded
  REQUIRED_FILES "tile.png;tiles/example-map/9/0/0/16/43/136.meta"
)
set_tests_properties(remove_tile PROPERTIES
  FIXTURES_CLEANUP tile_downloaded
  REQUIRED_FILES "tile.png;tiles/example-map/9/0/0/16/43/136.meta"
)
