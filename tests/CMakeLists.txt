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
find_program(TOUCH_EXECUTABLE NAMES gtouch touch REQUIRED)

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

set(TILE_PNG_URL "http://localhost:8081/tiles/renderd-example/9/297/191.png")
set(TILE_PNG32_URL "http://localhost:8081/tiles/renderd-example-png32/9/297/191.png")
set(TILE_WEBP_URL "http://localhost:8081/tiles/renderd-example-webp/9/297/191.webp")

set(TILE_PNG_CMD "${CURL_EXECUTABLE} --fail --silent ${TILE_PNG_URL}")
set(TILE_PNG_SHA256SUM "dbf26531286e844a3a9735cdd193598dca78d22f77cafe5824bcaf17f88cbb08")
set(TILE_PNG32_CMD "${CURL_EXECUTABLE} --fail --silent ${TILE_PNG32_URL}")
set(TILE_PNG32_SHA256SUM "1006d92152f1e18896e0016fb43201b14bbcf7655955b74495ad3610541d325b")
set(TILE_WEBP_CMD "${CURL_EXECUTABLE} --fail --silent ${TILE_WEBP_URL}")
set(TILE_WEBP_SHA256SUM_6 "96fc0455b2269a7bcd4a5b3c9844529c3c77e3bb15f56e72f78a5af3bc15b6b5") # libwebp6
set(TILE_WEBP_SHA256SUM_7 "a82ef9ba5dc333de88af7b645084c30ab2b01c664e17162cbf6659c287cc4df4") # libwebp7

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
  NAME render_expired
  COMMAND ${BASH} -c "
    echo '0/0/0' | ${PROJECT_BINARY_DIR}/src/render_expired \
      --map ${MAP_NAME} \
      --max-zoom 5 \
      --min-zoom 0 \
      --num-threads 1 \
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
)
add_test(
  NAME render_list
  COMMAND ${BASH} -c "
    ${PROJECT_BINARY_DIR}/src/render_list \
      --all \
      --force \
      --map ${MAP_NAME} \
      --max-zoom 5 \
      --min-zoom 0 \
      --num-threads 1 \
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
)
add_test(
  NAME render_old
  COMMAND ${BASH} -c "
    ${TOUCH_EXECUTABLE} -d '+1 month' ${PROJECT_BINARY_DIR}/tests/tiles/planet-import-complete
    ${PROJECT_BINARY_DIR}/src/render_old \
      --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf \
      --map ${MAP_NAME} \
      --max-zoom 5 \
      --min-zoom 0 \
      --num-threads 1 \
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
)
add_test(
  NAME download_tiles
  COMMAND ${BASH} -c "
    until $(${TILE_PNG_CMD} --output tile.png); do
      echo 'Sleeping 1s';
      sleep 1;
    done
    until $(${TILE_PNG32_CMD} --output tile.png32); do
      echo 'Sleeping 1s';
      sleep 1;
    done
    until $(${TILE_WEBP_CMD} --output tile.webp); do
      echo 'Sleeping 1s';
      sleep 1;
    done
  "
)
add_test(
  NAME check_tiles
  COMMAND ${BASH} -c "
    echo '${TILE_PNG_SHA256SUM}  tile.png' | ${SHA256SUM_EXECUTABLE} -c
    echo '${TILE_PNG32_SHA256SUM}  tile.png32' | ${SHA256SUM_EXECUTABLE} -c
    (echo '${TILE_WEBP_SHA256SUM_6}  tile.webp' | ${SHA256SUM_EXECUTABLE} -c) || (echo '${TILE_WEBP_SHA256SUM_7}  tile.webp' | ${SHA256SUM_EXECUTABLE} -c)
  "
)
add_test(
  NAME dirty_tile
  COMMAND ${BASH} -c "
    TILE_PNG_STATUS_CMD=\"${TILE_PNG_CMD}/status | cut -d. -f2\"
    TILE_PNG_STATUS_OUTPUT_OLD=$(\${TILE_PNG_STATUS_CMD})
    sleep 5;
    TILE_PNG_DIRTY_OUTPUT=$(${TILE_PNG_CMD}/dirty)
    if [ \"\${TILE_PNG_DIRTY_OUTPUT}\" != \"Tile submitted for rendering\" ]; then
      exit 1;
    fi
    until [ \"\${TILE_PNG_STATUS_OUTPUT_OLD}\" != \"$(\${TILE_PNG_STATUS_CMD})\" ]; do
      echo 'Sleeping 1s';
      sleep 1;
    done
  "
)
add_test(
  NAME remove_tiles
  COMMAND ${RM} -v tile.png tile.png32 tile.webp
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
set_tests_properties(render_expired PROPERTIES
  DEPENDS render_speedtest
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 20
)
set_tests_properties(render_list PROPERTIES
  DEPENDS render_speedtest
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 20
)
set_tests_properties(render_old PROPERTIES
  DEPENDS render_speedtest
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 20
)
set_tests_properties(download_tiles PROPERTIES
  FIXTURES_REQUIRED httpd_started
  FIXTURES_SETUP tiles_downloaded
  TIMEOUT 20
)
set_tests_properties(check_tiles PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
  REQUIRED_FILES "tile.png;tile.png32;tile.webp"
)
set_tests_properties(dirty_tile PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
  REQUIRED_FILES "tile.png;tile.png32;tile.webp"
  TIMEOUT 20
)
set_tests_properties(remove_tiles PROPERTIES
  DEPENDS download_tiles
  FIXTURES_CLEANUP tiles_downloaded
  REQUIRED_FILES "tile.png;tile.png32;tile.webp"
)
