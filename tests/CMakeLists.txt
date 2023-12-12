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

include(CTest)

execute_process(COMMAND ${APXS_EXECUTABLE} -q progname
  OUTPUT_VARIABLE HTTPD_PROGNAME
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(UnixCommands REQUIRED)
find_program(CAT_EXECUTABLE NAMES cat REQUIRED)
find_program(CURL_EXECUTABLE NAMES curl REQUIRED)
find_program(GREP_EXECUTABLE NAMES grep REQUIRED)
find_program(HTTPD_EXECUTABLE NAMES ${HTTPD_PROGNAME} REQUIRED)
find_program(ID_EXECUTABLE NAMES id REQUIRED)
find_program(JQ_EXECUTABLE NAMES jq)
find_program(KILL_EXECUTABLE NAMES kill REQUIRED)
find_program(MKDIR_EXECUTABLE NAMES mkdir REQUIRED)
find_program(SHA256SUM_EXECUTABLE NAMES gsha256sum sha256sum REQUIRED)
find_program(TOUCH_EXECUTABLE NAMES gtouch touch REQUIRED)

#-----------------------------------------------------------------------------
#
#  Test configurations
#
#-----------------------------------------------------------------------------

set(MAP_NAME "default")
set(HTTPD0_PORT "59980")
set(HTTPD1_PORT "59981")
set(RENDERD1_HOST "0.0.0.0")
set(RENDERD1_PORT "59991")
set(WWW_USER_NAME "nobody")

set(TILE_DEFAULT_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example/9/297/191.png")
set(TILE_JPG_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example-jpg/9/297/191.jpg")
set(TILE_PNG256_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example-png256/9/297/191.png")
set(TILE_PNG32_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example-png32/9/297/191.png")
set(TILE_WEBP_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example-webp/9/297/191.webp")

set(TILE_DIRTY_OFF_URL "http://localhost:${HTTPD1_PORT}/tiles/renderd-example/9/297/191.png/dirty")
set(TILE_DIRTY_ON_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example/9/297/191.png/dirty")
set(TILE_STATUS_OFF_URL "http://localhost:${HTTPD1_PORT}/tiles/renderd-example/9/297/191.png/status")
set(TILE_STATUS_ON_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example/9/297/191.png/status")

set(METRICS_URL "http://localhost:${HTTPD0_PORT}/metrics")
set(MOD_TILE_URL "http://localhost:${HTTPD0_PORT}/mod_tile")
set(TILE_DEFAULT_TILEJSON_URL "http://localhost:${HTTPD0_PORT}/tiles/renderd-example/tile-layer.json")

set(TILE_DEFAULT_SHA256SUM "dbf26531286e844a3a9735cdd193598dca78d22f77cafe5824bcaf17f88cbb08")
set(TILE_JPG_SHA256SUM "e09c3406c02f03583dadf0c8404c2d3efdc06a40d399e381ed2f47f49fde42d7")
set(TILE_PNG256_SHA256SUM "${TILE_DEFAULT_SHA256SUM}")
set(TILE_PNG32_SHA256SUM "1006d92152f1e18896e0016fb43201b14bbcf7655955b74495ad3610541d325b")
set(TILE_WEBP_SHA256SUM_4 "ef3862a57831b21ec69c15be196e1e2b4fea66246c361142631b9fa22b85decc") # libwebp.so.4
set(TILE_WEBP_SHA256SUM_6 "96fc0455b2269a7bcd4a5b3c9844529c3c77e3bb15f56e72f78a5af3bc15b6b5") # libwebp.so.6
set(TILE_WEBP_SHA256SUM_7 "a82ef9ba5dc333de88af7b645084c30ab2b01c664e17162cbf6659c287cc4df4") # libwebp.so.7

set(CURL_CMD "${CURL_EXECUTABLE} --fail --silent")

execute_process(COMMAND ${ID_EXECUTABLE} -gn ${WWW_USER_NAME}
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE WWW_GROUP_NAME
)

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

add_test(
  NAME gen_tile_test
  COMMAND gen_tile_test
  WORKING_DIRECTORY src
)
add_test(
  NAME create_dirs
  COMMAND ${MKDIR_EXECUTABLE} -p -v logs run tiles
  WORKING_DIRECTORY tests
)
add_test(
  NAME start_renderd
  COMMAND ${BASH} -c "
    echo '${PROJECT_BINARY_DIR}/src/renderd --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf --foreground \
      --slave 0 > ${PROJECT_BINARY_DIR}/tests/logs/renderd0.log 2>&1 &' > ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'printf \${!} > ${PROJECT_BINARY_DIR}/tests/run/renderd0.pid' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo '${PROJECT_BINARY_DIR}/src/renderd --config ${PROJECT_BINARY_DIR}/tests/conf/renderd.conf --foreground \
      --slave 1 > ${PROJECT_BINARY_DIR}/tests/logs/renderd1.log 2>&1 &' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'printf \${!} > ${PROJECT_BINARY_DIR}/tests/run/renderd1.pid' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    echo 'exit 0' >> ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
    ${BASH} ${PROJECT_BINARY_DIR}/tests/renderd_start.sh
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME start_httpd
  COMMAND ${HTTPD_EXECUTABLE} -e debug -f ${PROJECT_BINARY_DIR}/tests/conf/httpd.conf -k start
  WORKING_DIRECTORY tests
)
add_test(
  NAME render_speedtest
  COMMAND render_speedtest --map ${MAP_NAME} --max-zoom 10 --socket ${PROJECT_BINARY_DIR}/tests/run/renderd0.sock
  WORKING_DIRECTORY tests
)
add_test(
  NAME render_expired
  COMMAND ${BASH} -c "
    echo '0/0/0' | ${PROJECT_BINARY_DIR}/src/render_expired \
      --map ${MAP_NAME} \
      --max-zoom 5 \
      --min-zoom 0 \
      --num-threads 1 \
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd0.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
  WORKING_DIRECTORY tests
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
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd0.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
  WORKING_DIRECTORY tests
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
      --socket ${PROJECT_BINARY_DIR}/tests/run/renderd0.sock \
      --tile-dir ${PROJECT_BINARY_DIR}/tests/tiles
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME download_tiles
  COMMAND ${BASH} -c "
    until $(${CURL_CMD} ${TILE_DEFAULT_URL} --output tile.png); do
      echo 'Sleeping 1s (DEFAULT)';
      sleep 1;
    done
    until $(${CURL_CMD} ${TILE_JPG_URL} --output tile.jpg); do
      echo 'Sleeping 1s (JPG)';
      sleep 1;
    done
    until $(${CURL_CMD} ${TILE_PNG256_URL} --output tile.png256); do
      echo 'Sleeping 1s (PNG256)';
      sleep 1;
    done
    until $(${CURL_CMD} ${TILE_PNG32_URL} --output tile.png32); do
      echo 'Sleeping 1s (PNG32)';
      sleep 1;
    done
    until $(${CURL_CMD} ${TILE_WEBP_URL} --output tile.webp); do
      echo 'Sleeping 1s (WEBP)';
      sleep 1;
    done
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME check_tiles
  COMMAND ${BASH} -c "
    (echo '${TILE_DEFAULT_SHA256SUM}  tile.png' | ${SHA256SUM_EXECUTABLE} -c) && \
    (echo '${TILE_JPG_SHA256SUM}  tile.jpg' | ${SHA256SUM_EXECUTABLE} -c) && \
    (echo '${TILE_PNG256_SHA256SUM}  tile.png256' | ${SHA256SUM_EXECUTABLE} -c) && \
    (echo '${TILE_PNG32_SHA256SUM}  tile.png32' | ${SHA256SUM_EXECUTABLE} -c) && \
    ( \
      (echo '${TILE_WEBP_SHA256SUM_7}  tile.webp' | ${SHA256SUM_EXECUTABLE} -c) || \
      (echo '${TILE_WEBP_SHA256SUM_6}  tile.webp' | ${SHA256SUM_EXECUTABLE} -c) || \
      (echo '${TILE_WEBP_SHA256SUM_4}  tile.webp' | ${SHA256SUM_EXECUTABLE} -c) \
    )
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME status_and_dirty_urls
  COMMAND ${BASH} -c "
    TILE_DIRTY_ON_CMD=\"${CURL_CMD} ${TILE_DIRTY_ON_URL}\"
    TILE_STATUS_ON_CMD=\"${CURL_CMD} ${TILE_STATUS_ON_URL}\"
    TILE_LAST_RENDERED_AT_OLD=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.')
    echo \"Tile Last Rendered At (Old): \${TILE_LAST_RENDERED_AT_OLD}\"
    sleep 5;
    TILE_DIRTY_ON_OUTPUT=$(\${TILE_DIRTY_ON_CMD})
    echo \"Dirty: \${TILE_DIRTY_ON_OUTPUT}\"
    if [ \"\${TILE_DIRTY_ON_OUTPUT}\" != \"Tile submitted for rendering\" ]; then
      exit 1;
    fi
    TILE_LAST_RENDERED_AT_NEW=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.')
    echo \"Tile Last Rendered At (New): \${TILE_LAST_RENDERED_AT_NEW}\"
    until [ \"\${TILE_LAST_RENDERED_AT_OLD}\" != \"\${TILE_LAST_RENDERED_AT_NEW}\" ]; do
      echo 'Sleeping 1s';
      sleep 1;
      TILE_LAST_RENDERED_AT_NEW=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.');
      echo \"Tile Last Rendered At (New): \${TILE_LAST_RENDERED_AT_NEW}\";
    done
    TILE_DIRTY_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${TILE_DIRTY_OFF_URL})
    echo \"Dirty Off code: '\${TILE_DIRTY_OFF_CODE}'\"
    if [ \"\${TILE_DIRTY_OFF_CODE}\" != \"404\" ]; then
      exit 1;
    fi
    TILE_STATUS_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${TILE_STATUS_OFF_URL})
    echo \"Status Off code: '\${TILE_STATUS_OFF_CODE}'\"
    if [ \"\${TILE_STATUS_OFF_CODE}\" != \"404\" ]; then
      exit 1;
    fi
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME metrics_url
  COMMAND ${BASH} -c "
    METRICS_CMD=\"${CURL_CMD} ${METRICS_URL}\"
    METRICS_OUTPUT=$(\${METRICS_CMD})
    echo \"\${METRICS_OUTPUT}\"
    for LAYER in '' '-jpg' '-png256' '-png32' '-webp'; do
      LAYER_200=\"modtile_layer_responses_total{layer=\\\"/tiles/renderd-example\${LAYER}/\\\",status=\\\"200\\\"} 1\"
      LAYER_404=\"modtile_layer_responses_total{layer=\\\"/tiles/renderd-example\${LAYER}/\\\",status=\\\"404\\\"} 0\"
      echo \"\${LAYER_200}\"
      echo \"\${LAYER_404}\"
      if [[ \"\${METRICS_OUTPUT}\" != *\"\${LAYER_200}\"* ]]; then
        exit 1;
      fi
      if [[ \"\${METRICS_OUTPUT}\" != *\"\${LAYER_404}\"* ]]; then
        exit 1;
      fi
    done
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME mod_tile_url
  COMMAND ${BASH} -c "
    MOD_TILE_CMD=\"${CURL_CMD} ${MOD_TILE_URL}\"
    MOD_TILE_OUTPUT=$(\${MOD_TILE_CMD})
    echo \"\${MOD_TILE_OUTPUT}\"
    for LAYER in '' '-jpg' '-png256' '-png32' '-webp'; do
      LAYER_200=\"NoRes200Layer/tiles/renderd-example\${LAYER}/: 1\"
      LAYER_404=\"NoRes404Layer/tiles/renderd-example\${LAYER}/: 0\"
      echo \"\${LAYER_200}\"
      echo \"\${LAYER_404}\"
      if [[ \"\${MOD_TILE_OUTPUT}\" != *\"\${LAYER_200}\"* ]]; then
        exit 1;
      fi
      if [[ \"\${MOD_TILE_OUTPUT}\" != *\"\${LAYER_404}\"* ]]; then
        exit 1;
      fi
    done
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME remove_tiles
  COMMAND ${RM} -v tile.png tile.jpg tile.png256 tile.png32 tile.webp
  WORKING_DIRECTORY tests
)
add_test(
  NAME stop_renderd
  COMMAND ${BASH} -c "
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/renderd1.pid) && ${RM} run/renderd1.pid
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/renderd0.pid) && ${RM} run/renderd0.pid
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME stop_httpd
  COMMAND ${BASH} -c "
    ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} run/httpd.pid) && ${RM} run/httpd.pid
  "
  WORKING_DIRECTORY tests
)
add_test(
  NAME clear_dirs
  COMMAND ${BASH} -c "
    ${RM} -f -r -v logs/* run/* tiles/*
  "
  WORKING_DIRECTORY tests
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
  REQUIRED_FILES "run/renderd0.pid;run/renderd1.pid"
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
  TIMEOUT 60
)
set_tests_properties(render_list PROPERTIES
  DEPENDS render_speedtest
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 60
)
set_tests_properties(render_old PROPERTIES
  DEPENDS render_speedtest
  FIXTURES_REQUIRED httpd_started
  TIMEOUT 60
)
set_tests_properties(download_tiles PROPERTIES
  FIXTURES_REQUIRED httpd_started
  FIXTURES_SETUP tiles_downloaded
  TIMEOUT 60
)
set_tests_properties(check_tiles PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
  REQUIRED_FILES "tile.png;tile.jpg;tile.png256;tile.png32;tile.webp"
)
set_tests_properties(status_and_dirty_urls PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
  REQUIRED_FILES "tile.png;tile.jpg;tile.png256;tile.png32;tile.webp"
  TIMEOUT 60
)
set_tests_properties(metrics_url PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
)
set_tests_properties(mod_tile_url PROPERTIES
  DEPENDS download_tiles
  FIXTURES_REQUIRED "httpd_started;tiles_downloaded"
)
set_tests_properties(remove_tiles PROPERTIES
  DEPENDS download_tiles
  FIXTURES_CLEANUP tiles_downloaded
  REQUIRED_FILES "tile.png;tile.jpg;tile.png256;tile.png32;tile.webp"
)

if(JQ_EXECUTABLE)
  add_test(
    NAME tilejson_url
    COMMAND ${BASH} -c "
      TILE_DEFAULT_TILEJSON_CMD=\"${CURL_CMD} ${TILE_DEFAULT_TILEJSON_URL}\"
      TILE_DEFAULT_TILEJSON_OUTPUT=$(\${TILE_DEFAULT_TILEJSON_CMD})
      TILE_DEFAULT_TILEJSON_VERSION=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .tilejson)
      if [ \"\${TILE_DEFAULT_TILEJSON_VERSION}\" != \"2.0.0\" ]; then
        exit 1;
      fi
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(tilejson_url PROPERTIES
    FIXTURES_REQUIRED httpd_started
  )
endif()