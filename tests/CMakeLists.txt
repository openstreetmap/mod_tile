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

include(Dart)

execute_process(COMMAND ${APXS_EXECUTABLE} -q progname
  OUTPUT_VARIABLE APXS_PROGNAME
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(UnixCommands REQUIRED)
find_program(CAT_EXECUTABLE NAMES cat REQUIRED)
find_program(CURL_EXECUTABLE NAMES curl REQUIRED)
find_program(HTTPD_EXECUTABLE NAMES ${APXS_PROGNAME} REQUIRED)
find_program(ID_EXECUTABLE NAMES id REQUIRED)
find_program(KILL_EXECUTABLE NAMES kill REQUIRED)
find_program(MKDIR_EXECUTABLE NAMES mkdir REQUIRED)
find_program(SHA256SUM_EXECUTABLE NAMES gsha256sum sha256sum REQUIRED)

if(LibMapnik_VERSION STRLESS "4")
  find_program(MAPNIK_CONFIG_EXECUTABLE NAMES mapnik-config REQUIRED)
  execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --fonts
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE MAPNIK_FONTS_DIR
  )
  execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --input-plugins
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE MAPNIK_PLUGINS_DIR
  )
elseif(LibMapnik_VERSION STRGREATER_EQUAL "4")
  pkg_get_variable(MAPNIK_FONTS_DIR libmapnik fonts_dir)
  pkg_get_variable(MAPNIK_PLUGINS_DIR libmapnik plugins_dir)
endif()

execute_process(COMMAND ${ID_EXECUTABLE} -gn nobody
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE NOGROUP_NAME
)

#-----------------------------------------------------------------------------
#
#  Test configurations
#
#-----------------------------------------------------------------------------

set(MAP_NAME "default")
set(TILE_SHA256SUM "dbf26531286e844a3a9735cdd193598dca78d22f77cafe5824bcaf17f88cbb08")
set(TILE_URL "http://localhost:8081/tiles/renderd-example/9/297/191.png")
set(TILE_CMD "${CURL_EXECUTABLE} --fail --silent ${TILE_URL}")

configure_file(
  renderd.conf.in
  conf/renderd.conf
)

configure_file(
  httpd.conf.in
  conf/httpd.conf
)

configure_file(
  ../utils/example-map/index.html
  www/index.html
  COPYONLY
)

configure_file(
  ../utils/example-map/mapnik.xml
  www/mapnik.xml
  COPYONLY
)

configure_file(
  ../utils/example-map/very_simplified_land_polygons.gpkg
  www/very_simplified_land_polygons.gpkg
  COPYONLY
)

#-----------------------------------------------------------------------------
#
#  Tests
#
#-----------------------------------------------------------------------------

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
  COMMAND ${BASH} -c "
    echo '${PROJECT_BINARY_DIR}/src/renderd --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf --foreground --slave 0 > ${PROJECT_BINARY_DIR}/tests/logs/renderd.log 2>&1 &' > ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'printf \${!} > ${PROJECT_BINARY_DIR}/tests/run/renderd.pid' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo '${PROJECT_BINARY_DIR}/src/renderd --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf --foreground --slave 1 > ${PROJECT_BINARY_DIR}/tests/logs/renderd1.log 2>&1 &' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'printf \${!} > ${PROJECT_BINARY_DIR}/tests/run/renderd1.pid' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'exit 0' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    ${BASH} ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
  "
)
add_test(
  NAME start_httpd
  COMMAND ${HTTPD_EXECUTABLE} -e debug -f ${PROJECT_BINARY_DIR}/tests/conf/httpd.conf -k start
)
add_test(
  NAME render_speedtest
  COMMAND render_speedtest --map ${MAP_NAME} --max-zoom 10 --socket ${PROJECT_BINARY_DIR}/tests/run/renderd.sock
)
add_test(
  NAME download_tile
  COMMAND ${BASH} -c "
    until $(${TILE_CMD} --output tile.png); do
      echo 'Sleeping 1s';
      sleep 1;
    done
  "
)
add_test(
  NAME check_tile
  COMMAND ${BASH} -c "
    echo '${TILE_SHA256SUM}  tile.png' | ${SHA256SUM_EXECUTABLE} -c
  "
)
add_test(
  NAME dirty_tile
  COMMAND ${BASH} -c "
    TILE_STATUS_CMD=\"${TILE_CMD}/status | cut -d. -f2\"
    TILE_STATUS_OUTPUT_OLD=$(\${TILE_STATUS_CMD})
    sleep 5;
    TILE_DIRTY_OUTPUT=$(${TILE_CMD}/dirty)
    if [ \"\${TILE_DIRTY_OUTPUT}\" != \"Tile submitted for rendering\" ]; then
      exit 1;
    fi
    until [ \"\${TILE_STATUS_OUTPUT_OLD}\" != \"$(\${TILE_STATUS_CMD})\" ]; do
      echo 'Sleeping 1s';
      sleep 1;
    done
  "
)
add_test(
  NAME remove_tile
  COMMAND ${RM} -v tile.png
)
add_test(
  NAME stop_renderd
  COMMAND ${BASH} -c "
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/renderd1.pid) && ${RM} run/renderd1.pid
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/renderd.pid) && ${RM} run/renderd.pid
  "
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
  REQUIRED_FILES run/renderd.pid
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

set_tests_properties(render_speedtest PROPERTIES
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 60
)
set_tests_properties(download_tile PROPERTIES
  FIXTURES_REQUIRED httpd_started
  FIXTURES_SETUP tile_downloaded
  TIMEOUT 10
)
set_tests_properties(check_tile PROPERTIES
  DEPENDS download_tile
  FIXTURES_REQUIRED "httpd_started;tile_downloaded"
  REQUIRED_FILES tile.png
)
set_tests_properties(dirty_tile PROPERTIES
  DEPENDS download_tile
  FIXTURES_REQUIRED "httpd_started;tile_downloaded"
  REQUIRED_FILES tile.png
  TIMEOUT 20
)
set_tests_properties(remove_tile PROPERTIES
  DEPENDS download_tile
  FIXTURES_CLEANUP tile_downloaded
  REQUIRED_FILES tile.png
)
