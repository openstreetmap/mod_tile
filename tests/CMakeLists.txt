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
find_program(MEMCACHED_EXECUTABLE NAMES memcached)
find_program(MKDIR_EXECUTABLE NAMES mkdir REQUIRED)
find_program(SHA256SUM_EXECUTABLE NAMES gsha256sum sha256sum REQUIRED)
find_program(TOUCH_EXECUTABLE NAMES gtouch touch REQUIRED)

#-----------------------------------------------------------------------------
#
#  Test configurations
#
#-----------------------------------------------------------------------------

set(DEFAULT_MAP_NAME "default")
set(HTTPD0_HOST "localhost")
set(HTTPD0_PORT_BASE "59000")
set(HTTPD1_HOST "localhost")
set(HTTPD1_PORT_BASE "59100")
set(MEMCACHED_HOST "localhost")
set(MEMCACHED_PORT "51211")
set(RENDERD1_HOST "localhost")
set(RENDERD1_PORT_BASE "59500")
set(WWW_USER_NAME "nobody")

set(MAP_NAMES "jpg" "png256" "png32" "webp")

set(TILE_ZXY "9/297/191")

set(TILE_JPG_SHA256SUM "e09c3406c02f03583dadf0c8404c2d3efdc06a40d399e381ed2f47f49fde42d7")
set(TILE_PNG256_SHA256SUM "dbf26531286e844a3a9735cdd193598dca78d22f77cafe5824bcaf17f88cbb08")
set(TILE_PNG32_SHA256SUM "1006d92152f1e18896e0016fb43201b14bbcf7655955b74495ad3610541d325b")
set(TILE_WEBP_SHA256SUM_01 "ef3862a57831b21ec69c15be196e1e2b4fea66246c361142631b9fa22b85decc") # libwebp.so.4
set(TILE_WEBP_SHA256SUM_02 "96fc0455b2269a7bcd4a5b3c9844529c3c77e3bb15f56e72f78a5af3bc15b6b5") # libwebp.so.6
set(TILE_WEBP_SHA256SUM_03 "a82ef9ba5dc333de88af7b645084c30ab2b01c664e17162cbf6659c287cc4df4") # libwebp.so.7
set(TILE_WEBP_SHA256SUM_04 "904593e291cce2561138bd83b704588c02c16630b8c133d78d535b8986e901af") # libwebp.so.7

set(CURL_CMD "${CURL_EXECUTABLE} --fail --silent")

execute_process(COMMAND ${ID_EXECUTABLE} -gn ${WWW_USER_NAME}
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE WWW_GROUP_NAME
)

execute_process(COMMAND ${ID_EXECUTABLE} -un
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE USER_NAME
)

# Storage backend name (for display only)
set(STORAGE_BACKENDS file)
# Value of TILE_DIR in renderd.conf/httpd.conf
set(TILE_DIRS "${PROJECT_BINARY_DIR}/tests/tiles")

if(MEMCACHED_EXECUTABLE AND LIBMEMCACHED_FOUND)
  # Add MemcacheD storage backend/TILE_DIR
  list(APPEND TILE_DIRS "memcached://") # "memcached://${MEMCACHED_HOST}:${MEMCACHED_PORT}")
  list(APPEND STORAGE_BACKENDS memcached_default) # memcached_custom)
endif()

list(LENGTH TILE_DIRS TILE_DIRS_LENGTH)
math(EXPR TILE_DIRS_LENGTH "${TILE_DIRS_LENGTH} - 1")

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

foreach(TILE_DIR_INDEX RANGE ${TILE_DIRS_LENGTH})
  list(GET TILE_DIRS ${TILE_DIR_INDEX} TILE_DIR)
  list(GET STORAGE_BACKENDS ${TILE_DIR_INDEX} STORAGE_BACKEND)

  # Increment Ports
  math(EXPR HTTPD0_PORT "${HTTPD0_PORT_BASE} + ${TILE_DIR_INDEX}")
  math(EXPR HTTPD1_PORT "${HTTPD1_PORT_BASE} + ${TILE_DIR_INDEX}")
  math(EXPR RENDERD1_PORT "${RENDERD1_PORT_BASE} + ${TILE_DIR_INDEX}")

  set(METRICS_OFF_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}/metrics")
  set(METRICS_ON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/metrics")
  set(MOD_TILE_OFF_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}/mod_tile")
  set(MOD_TILE_ON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/mod_tile")
  set(TILE_DEFAULT_TILEJSON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/tiles/${DEFAULT_MAP_NAME}/tile-layer.json")

  set(HTTPD_CONF "${PROJECT_BINARY_DIR}/tests/conf/httpd_${STORAGE_BACKEND}.conf")
  set(HTTPD_LOG_ACCESS "${PROJECT_BINARY_DIR}/tests/logs/httpd_access_${STORAGE_BACKEND}.log")
  set(HTTPD_LOG_ERROR "${PROJECT_BINARY_DIR}/tests/logs/httpd_error_${STORAGE_BACKEND}.log")
  set(HTTPD_PID "${PROJECT_BINARY_DIR}/tests/run/httpd_${STORAGE_BACKEND}.pid")
  set(RENDERD0_LOG "${PROJECT_BINARY_DIR}/tests/logs/renderd0_${STORAGE_BACKEND}.log")
  set(RENDERD0_PID "${PROJECT_BINARY_DIR}/tests/run/renderd0_${STORAGE_BACKEND}.pid")
  set(RENDERD0_SOCKET "${PROJECT_BINARY_DIR}/tests/run/renderd0_${STORAGE_BACKEND}.sock")
  set(RENDERD1_LOG "${PROJECT_BINARY_DIR}/tests/logs/renderd1_${STORAGE_BACKEND}.log")
  set(RENDERD1_PID "${PROJECT_BINARY_DIR}/tests/run/renderd1_${STORAGE_BACKEND}.pid")
  set(RENDERD2_LOG "${PROJECT_BINARY_DIR}/tests/logs/renderd2_${STORAGE_BACKEND}.log")
  set(RENDERD2_PID "${PROJECT_BINARY_DIR}/tests/run/renderd2_${STORAGE_BACKEND}.pid")
  set(RENDERD2_SOCKET "${PROJECT_BINARY_DIR}/tests/run/renderd2_${STORAGE_BACKEND}.sock")
  set(RENDERD_CONF "${PROJECT_BINARY_DIR}/tests/conf/renderd_${STORAGE_BACKEND}.conf")
  if(STORAGE_BACKEND STREQUAL "memcached_default")
    set(MEMCACHED_HOST "localhost")
    set(MEMCACHED_PORT "11211")
  endif()

  configure_file(
    renderd.conf.in
    ${RENDERD_CONF}
  )

  configure_file(
    httpd.conf.in
    ${HTTPD_CONF}
  )

  set(SERVICES_START_CMDS
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --foreground --slave 0 > ${RENDERD0_LOG} 2>&1 &"
    "printf \${!} > ${RENDERD0_PID}"
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --foreground --slave 1 > ${RENDERD1_LOG} 2>&1 &"
    "printf \${!} > ${RENDERD1_PID}"
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --slave 2"
    "${HTTPD_EXECUTABLE} -e debug -f ${HTTPD_CONF} -k start"
  )

  if(STORAGE_BACKEND STREQUAL "memcached_custom" OR STORAGE_BACKEND STREQUAL "memcached_default")
    list(APPEND SERVICES_START_CMDS
      "${MEMCACHED_EXECUTABLE} -l ${MEMCACHED_HOST} -p ${MEMCACHED_PORT} -u ${USER_NAME} -vvv \
        > logs/memcached.log 2>&1 &"
      "printf \${!} > run/memcached.pid"
    )
  endif()

  string(REPLACE ";" " " MAP_NAMES_STR "${MAP_NAMES}")
  string(REPLACE ";" "\n" SERVICES_START_CMDS_STR "${SERVICES_START_CMDS}")

  add_test(
    NAME create_dirs_${STORAGE_BACKEND}
    COMMAND ${MKDIR_EXECUTABLE} -p -v logs run tiles
    WORKING_DIRECTORY tests
  )
  set_tests_properties(create_dirs_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_SETUP services_started_${STORAGE_BACKEND}
  )
  add_test(
    NAME start_services_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "${SERVICES_START_CMDS_STR}"
    WORKING_DIRECTORY tests
  )
  set_tests_properties(start_services_${STORAGE_BACKEND} PROPERTIES
    DEPENDS create_dirs
    FIXTURES_SETUP services_started_${STORAGE_BACKEND}
  )

  foreach(SOCKET_TYPE sock tcp)
    if(SOCKET_TYPE STREQUAL "sock")
      set(SOCKET ${RENDERD0_SOCKET})
    endif()
    if(SOCKET_TYPE STREQUAL "tcp")
      set(SOCKET ${RENDERD1_HOST}:${RENDERD1_PORT})
    endif()

    add_test(
      NAME render_expired_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0/0/0' | $<TARGET_FILE:render_expired> \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --no-progress \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_expired_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_expired_touch_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0/0/0' | $<TARGET_FILE:render_expired> \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --no-progress \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --touch-from 0 \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_expired_touch_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_expired_delete_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0/0/0' | $<TARGET_FILE:render_expired> \
          --delete-from 0 \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --no-progress \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_expired_delete_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_list_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        $<TARGET_FILE:render_list> \
          --all \
          --force \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_list_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_list_stdin_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0 0 0' | $<TARGET_FILE:render_list> \
          --force \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_list_stdin_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_old_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        ${TOUCH_EXECUTABLE} -d '+1 month' ${PROJECT_BINARY_DIR}/tests/tiles/planet-import-complete
        $<TARGET_FILE:render_old> \
          --config ${RENDERD_CONF} \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
          --num-threads 1 \
          --socket ${SOCKET} \
          --tile-dir ${TILE_DIR} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_old_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(
      NAME render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        $<TARGET_FILE:render_speedtest> \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 10 \
          --min-zoom 0 \
          --num-threads 1 \
          --socket ${SOCKET}
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
  endforeach()

  foreach(MAP_NAME IN LISTS MAP_NAMES)
    string(REGEX REPLACE "[0-9]+" "" EXTENSION ${MAP_NAME})
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/tiles/${MAP_NAME}/${TILE_ZXY}.${EXTENSION}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}/tiles/${MAP_NAME}/${TILE_ZXY}.${EXTENSION}")
    add_test(
      NAME download_tile_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        until $(${CURL_CMD} ${HTTPD0_URL} --output tile.${MAP_NAME}.0); do
          echo 'Sleeping 1s (${MAP_NAME}.0)';
          sleep 1;
        done
        until $(${CURL_CMD} ${HTTPD1_URL} --output tile.${MAP_NAME}.1); do
          echo 'Sleeping 1s (${MAP_NAME}.1)';
          sleep 1;
        done
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(download_tile_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      FIXTURES_SETUP tiles_downloaded_${STORAGE_BACKEND}
      TIMEOUT 10
    )
    add_test(
      NAME status_and_dirty_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        TILE_DIRTY_ON_CMD=\"${CURL_CMD} ${HTTPD0_URL}/dirty\"
        TILE_STATUS_ON_CMD=\"${CURL_CMD} ${HTTPD0_URL}/status\"
        TILE_LAST_RENDERED_AT_OLD=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.')
        echo \"Tile Last Rendered At (Old): \${TILE_LAST_RENDERED_AT_OLD}\"
        sleep 1
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
        TILE_DIRTY_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${HTTPD1_URL}/dirty)
        echo \"Dirty Off code: '\${TILE_DIRTY_OFF_CODE}'\"
        if [ \"\${TILE_DIRTY_OFF_CODE}\" != \"404\" ]; then
          exit 1;
        fi
        TILE_STATUS_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${HTTPD1_URL}/status)
        echo \"Status Off code: '\${TILE_STATUS_OFF_CODE}'\"
        if [ \"\${TILE_STATUS_OFF_CODE}\" != \"404\" ]; then
          exit 1;
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(status_and_dirty_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS_ON download_tile_${MAP_NAME}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 20
    )
    add_test(
      NAME remove_tile_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${RM} -v tile.${MAP_NAME}.0 tile.${MAP_NAME}.1
      WORKING_DIRECTORY tests
    )
    set_tests_properties(remove_tile_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS_ON download_tile_${MAP_NAME}_${STORAGE_BACKEND}
      FIXTURES_CLEANUP tiles_downloaded_${STORAGE_BACKEND}
      REQUIRED_FILES "tile.${MAP_NAME}.0;tile.${MAP_NAME}.1"
    )
  endforeach()

  add_test(
    NAME check_tiles_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      (echo '${TILE_JPG_SHA256SUM}  tile.jpg.0' | ${SHA256SUM_EXECUTABLE} -c) && \
      (echo '${TILE_JPG_SHA256SUM}  tile.jpg.1' | ${SHA256SUM_EXECUTABLE} -c) && \
      (echo '${TILE_PNG256_SHA256SUM}  tile.png256.0' | ${SHA256SUM_EXECUTABLE} -c) && \
      (echo '${TILE_PNG256_SHA256SUM}  tile.png256.1' | ${SHA256SUM_EXECUTABLE} -c) && \
      (echo '${TILE_PNG32_SHA256SUM}  tile.png32.0' | ${SHA256SUM_EXECUTABLE} -c) && \
      (echo '${TILE_PNG32_SHA256SUM}  tile.png32.1' | ${SHA256SUM_EXECUTABLE} -c) && \
      ( \
        (echo '${TILE_WEBP_SHA256SUM_01}  tile.webp.0' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_02}  tile.webp.0' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_03}  tile.webp.0' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_04}  tile.webp.0' | ${SHA256SUM_EXECUTABLE} -c) \
      ) && \
      ( \
        (echo '${TILE_WEBP_SHA256SUM_01}  tile.webp.1' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_02}  tile.webp.1' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_03}  tile.webp.1' | ${SHA256SUM_EXECUTABLE} -c) || \
        (echo '${TILE_WEBP_SHA256SUM_04}  tile.webp.1' | ${SHA256SUM_EXECUTABLE} -c) \
      )
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(check_tiles_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_REQUIRED tiles_downloaded_${STORAGE_BACKEND}
  )
  add_test(
    NAME stats_urls_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      METRICS_ON_CMD=\"${CURL_CMD} ${METRICS_ON_URL}\"
      METRICS_ON_OUTPUT=$(\${METRICS_ON_CMD})
      echo \"Metrics On output: \${METRICS_ON_OUTPUT}\"
      MOD_TILE_ON_CMD=\"${CURL_CMD} ${MOD_TILE_ON_URL}\"
      MOD_TILE_ON_OUTPUT=$(\${MOD_TILE_ON_CMD})
      echo \"Mod_tile On output: \${MOD_TILE_ON_OUTPUT}\"
      for LAYER in ${MAP_NAMES_STR}; do
        METRICS_LAYER_200=\"modtile_layer_responses_total{layer=\\\"/tiles/\${LAYER}/\\\",status=\\\"200\\\"} 1\"
        echo \"\${METRICS_LAYER_200}\";
        if [[ \"\${METRICS_ON_OUTPUT}\" != *\"\${METRICS_LAYER_200}\"* ]]; then
          exit 1;
        fi
        MOD_TILE_LAYER_200=\"NoRes200Layer/tiles/\${LAYER}/: 1\"
        echo \"\${MOD_TILE_LAYER_200}\";
        if [[ \"\${MOD_TILE_ON_OUTPUT}\" != *\"\${MOD_TILE_LAYER_200}\"* ]]; then
          exit 1;
        fi
      done
      METRICS_OFF_OUTPUT=$(${CURL_CMD} ${METRICS_OFF_URL})
      echo \"Metrics Off output: '\${METRICS_OFF_OUTPUT}'\";
      if [ \"\${METRICS_OFF_OUTPUT}\" != \"Stats are not enabled for this server\" ]; then
        exit 1;
      fi
      MOD_TILE_OFF_OUTPUT=$(${CURL_CMD} ${MOD_TILE_OFF_URL})
      echo \"Mod_tile Off output: '\${MOD_TILE_OFF_OUTPUT}'\";
      if [ \"\${MOD_TILE_OFF_OUTPUT}\" != \"Stats are not enabled for this server\" ]; then
        exit 1;
      fi
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(stats_urls_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_REQUIRED "services_started_${STORAGE_BACKEND};tiles_downloaded_${STORAGE_BACKEND}"
  )
  add_test(
    NAME stop_services_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      for SERVICE_PID_FILE in run/*.pid; do
        ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} \${SERVICE_PID_FILE});
        ${RM} \${SERVICE_PID_FILE};
        sleep 1;
      done
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(stop_services_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_CLEANUP services_started_${STORAGE_BACKEND}
  )
  add_test(
    NAME clear_dirs_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      ${RM} -f -r -v logs/* run/* tiles/*
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(clear_dirs_${STORAGE_BACKEND} PROPERTIES
    DEPENDS stop_services_${STORAGE_BACKEND}
    FIXTURES_CLEANUP services_started_${STORAGE_BACKEND}
    REQUIRED_FILES "logs;run;tiles"
  )

  if(STORAGE_BACKEND STREQUAL "file")
    add_test(
      NAME download_tile_parameterization_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        until ${CURL_CMD} \"http://${HTTPD0_HOST}:${HTTPD0_PORT}/tiles/${DEFAULT_MAP_NAME}/en,de,_/${TILE_ZXY}.png\"; do
          echo 'Sleeping 1s (${DEFAULT_MAP_NAME}/en,de,_/${TILE_ZXY}.png.0)';
          sleep 1;
        done
        until ${CURL_CMD} \"http://${HTTPD1_HOST}:${HTTPD1_PORT}/tiles/${DEFAULT_MAP_NAME}/en,de,_/${TILE_ZXY}.png\"; do
        echo 'Sleeping 1s (${DEFAULT_MAP_NAME}/en,de,_/${TILE_ZXY}.png.1)';
          sleep 1;
        done
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(download_tile_parameterization_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 10
    )
  endif()

  if(JQ_EXECUTABLE)
    add_test(
      NAME tilejson_url_${STORAGE_BACKEND}
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
    set_tests_properties(tilejson_url_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )
  endif()
endforeach()
