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
include(ProcessorCount)

processorcount(PROCESSOR_COUNT)

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
find_program(PS_EXECUTABLE NAMES ps REQUIRED)
find_program(SHA256SUM_EXECUTABLE NAMES gsha256sum sha256sum REQUIRED)
find_program(SLEEP_EXECUTABLE NAMES sleep REQUIRED)
find_program(TOUCH_EXECUTABLE NAMES gtouch touch REQUIRED)

# Sets the host to be used for CTest test services
if(DEFINED ENV{CTEST_SERVER_HOST})
  # To the value of environment variable "CTEST_SERVER_HOST"
  set(CTEST_SERVER_HOST "$ENV{CTEST_SERVER_HOST}")
else()
  # Or to 0.0.0.0 by default
  set(CTEST_SERVER_HOST "0.0.0.0")
endif()

# Sets the host to be used for CTest client
if(DEFINED ENV{CTEST_CLIENT_HOST})
  # To the value of environment variable "CTEST_CLIENT_HOST"
  set(CTEST_CLIENT_HOST "$ENV{CTEST_CLIENT_HOST}")
else()
  # Or to 127.0.0.1 by default
  set(CTEST_CLIENT_HOST "127.0.0.1")
endif()

#-----------------------------------------------------------------------------
#
#  Test configurations
#
#-----------------------------------------------------------------------------

set(DEFAULT_MAP_NAME "default")
set(HTTPD0_HOST "${CTEST_SERVER_HOST}")
set(HTTPD0_PORT_BASE "59000")
set(HTTPD1_HOST "${CTEST_SERVER_HOST}")
set(HTTPD1_PORT_BASE "59100")
set(HTTPD2_HOST "${CTEST_SERVER_HOST}")
set(HTTPD2_PORT_BASE "59200")
set(MEMCACHED_HOST "${CTEST_SERVER_HOST}")
set(MEMCACHED_PORT_BASE "60000")
set(RENDERD1_HOST "${CTEST_SERVER_HOST}")
set(RENDERD1_PORT_BASE "59500")

set(CURL_CMD "${CURL_EXECUTABLE} --fail --silent")

set(MAP_NAMES jpg png256 png32 webp)

set(TESTS_CONF_DIR "${PROJECT_BINARY_DIR}/tests/conf")
set(TESTS_LOGS_DIR "${PROJECT_BINARY_DIR}/tests/logs")
set(TESTS_RUN_DIR "${PROJECT_BINARY_DIR}/tests/run")
set(TESTS_TILES_DIR "${PROJECT_BINARY_DIR}/tests/tiles")

file(COPY tiles.sha256sum DESTINATION ${PROJECT_BINARY_DIR}/tests)

set(TILE_ZXY "9/297/191")

set(WWW_USER_NAME "nobody")

execute_process(COMMAND ${ID_EXECUTABLE} -gn ${WWW_USER_NAME}
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE WWW_GROUP_NAME
)

execute_process(COMMAND ${ID_EXECUTABLE} -un
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE USER_NAME
)

# Storage backend name (for test display and configuration only)
set(STORAGE_BACKENDS file)

if(MEMCACHED_EXECUTABLE AND LIBMEMCACHED_FOUND)
  # Add MemcacheD storage backend
  list(APPEND STORAGE_BACKENDS memcached_custom memcached_default)
endif()

list(LENGTH STORAGE_BACKENDS STORAGE_BACKENDS_LENGTH)
math(EXPR STORAGE_BACKENDS_LENGTH "${STORAGE_BACKENDS_LENGTH} - 1")

#-----------------------------------------------------------------------------
#
#  Test functions
#
#-----------------------------------------------------------------------------

function(add_bad_tile_download_test TEST_NAME TEST_URL REQUIRED_FIXTURE_NAME)
  add_test(NAME bad_download_tile_${TEST_NAME}
    COMMAND ${BASH} -c "
      echo 'Downloading ${TEST_URL}'
      while true; do
        HTTP_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${TEST_URL})
        echo \"HTTP Code: '\${HTTP_CODE}'\"
        if [ \"\${HTTP_CODE}\" == \"404\" ] || [ \"\${HTTP_CODE}\" == \"500\" ]; then
          exit 0;
        fi
        echo 'Sleeping 1s';
        ${SLEEP_EXECUTABLE} 1;
      done
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(bad_download_tile_${TEST_NAME} PROPERTIES
    FIXTURES_REQUIRED ${REQUIRED_FIXTURE_NAME}
    TIMEOUT 10 # Needs to be more than ModTileMissingRequestTimeout in httpd.conf
  )
endfunction()

function(add_good_tile_download_test TEST_NAME TEST_URL FILE_NAME FIXTURE_NAME REQUIRED_FIXTURE_NAME)
  add_test(NAME download_tile_${TEST_NAME}
    COMMAND ${BASH} -c "
      echo 'Downloading ${TEST_URL}'
      until $(${CURL_CMD} ${TEST_URL} --output ${FILE_NAME}); do
        echo 'Sleeping 1s';
        ${SLEEP_EXECUTABLE} 1;
      done
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(download_tile_${TEST_NAME} PROPERTIES
    FIXTURES_REQUIRED ${REQUIRED_FIXTURE_NAME}
    FIXTURES_SETUP ${FIXTURE_NAME}
    TIMEOUT 10
  )
  add_test(NAME check_tile_${TEST_NAME}
    COMMAND ${BASH} -c "
      ${SHA256SUM_EXECUTABLE} -c tiles.sha256sum | ${GREP_EXECUTABLE} ${FILE_NAME} | ${GREP_EXECUTABLE} -q OK
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(check_tile_${TEST_NAME} PROPERTIES
    DEPENDS_ON download_tile_${TEST_NAME}
    FIXTURES_REQUIRED ${FIXTURE_NAME}
    REQUIRED_FILES ${FILE_NAME}
  )
  add_test(NAME remove_tile_${TEST_NAME}
    COMMAND ${RM} -v ${FILE_NAME}
    WORKING_DIRECTORY tests
  )
  set_tests_properties(remove_tile_${TEST_NAME} PROPERTIES
    DEPENDS_ON download_tile_${TEST_NAME}
    FIXTURES_CLEANUP ${FIXTURE_NAME}
    REQUIRED_FILES ${FILE_NAME}
  )
endfunction()

#-----------------------------------------------------------------------------
#
#  Tests
#
#-----------------------------------------------------------------------------

add_test(NAME gen_tile_test
  COMMAND gen_tile_test
)

add_test(
  NAME render_expired_test
  COMMAND render_expired_test
)

add_test(
  NAME render_list_test
  COMMAND render_list_test
)

add_test(
  NAME render_old_test
  COMMAND render_old_test
)

add_test(
  NAME render_speedtest_test
  COMMAND render_speedtest_test
)

add_test(
  NAME renderd_test
  COMMAND renderd_test
)

add_test(
  NAME renderd_config_test
  COMMAND renderd_config_test
)

foreach(STORAGE_BACKEND_INDEX RANGE ${STORAGE_BACKENDS_LENGTH})
  # Get STORAGE_BACKEND from STORAGE_BACKENDS list
  list(GET STORAGE_BACKENDS ${STORAGE_BACKEND_INDEX} STORAGE_BACKEND)

  # Increment Ports
  math(EXPR HTTPD0_PORT "${HTTPD0_PORT_BASE} + ${STORAGE_BACKEND_INDEX}")
  math(EXPR HTTPD1_PORT "${HTTPD1_PORT_BASE} + ${STORAGE_BACKEND_INDEX}")
  math(EXPR HTTPD2_PORT "${HTTPD2_PORT_BASE} + ${STORAGE_BACKEND_INDEX}")
  math(EXPR RENDERD1_PORT "${RENDERD1_PORT_BASE} + ${STORAGE_BACKEND_INDEX}")
  if(STORAGE_BACKEND STREQUAL memcached_custom)
    math(EXPR MEMCACHED_PORT "${MEMCACHED_PORT_BASE} + ${STORAGE_BACKEND_INDEX}")
  elseif(STORAGE_BACKEND STREQUAL memcached_default)
    set(MEMCACHED_PORT "11211")
  endif()

  # Set STORAGE_BACKEND-level directory names
  set(TEST_CONF_DIR "${TESTS_CONF_DIR}/${STORAGE_BACKEND}")
  set(TEST_LOGS_DIR "${TESTS_LOGS_DIR}/${STORAGE_BACKEND}")
  set(TEST_RUN_DIR "${TESTS_RUN_DIR}/${STORAGE_BACKEND}")
  set(TEST_TILES_DIR "${TESTS_TILES_DIR}/${STORAGE_BACKEND}")

  # Set STORAGE_BACKEND-level URLs
  set(METRICS_OFF_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}/metrics")
  set(METRICS_ON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/metrics")
  set(MOD_TILE_OFF_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}/mod_tile")
  set(MOD_TILE_ON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/mod_tile")
  set(TILE_DEFAULT_TILEJSON_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}/tiles/${DEFAULT_MAP_NAME}/tile-layer.json")

  # Set STORAGE_BACKEND-level config file/log/pid/socket file names
  set(HTTPD_CONF "${TEST_CONF_DIR}/httpd.conf")
  set(HTTPD_LOG "${TEST_LOGS_DIR}/httpd.log")
  set(HTTPD_LOG_ACCESS "${TEST_LOGS_DIR}/httpd_access.log")
  set(HTTPD_LOG_ERROR "${TEST_LOGS_DIR}/httpd_error.log")
  set(HTTPD_PID "${TEST_RUN_DIR}/httpd.pid")
  set(MEMCACHED_LOG "${TEST_LOGS_DIR}/memcached.log")
  set(MEMCACHED_PID "${TEST_RUN_DIR}/memcached.pid")
  set(RENDERD0_LOG "${TEST_LOGS_DIR}/renderd0.log")
  set(RENDERD0_PID "${TEST_RUN_DIR}/renderd0.pid")
  set(RENDERD0_SOCKET "${TEST_RUN_DIR}/renderd0.sock")
  set(RENDERD1_LOG "${TEST_LOGS_DIR}/renderd1.log")
  set(RENDERD1_PID "${TEST_RUN_DIR}/renderd1.pid")
  set(RENDERD2_LOG "${TEST_LOGS_DIR}/renderd2.log")
  set(RENDERD2_PID "${TEST_RUN_DIR}/renderd2.pid")
  set(RENDERD2_SOCKET "${TEST_RUN_DIR}/renderd2.sock")
  set(RENDERD_CONF "${TEST_CONF_DIR}/renderd.conf")

  # Set TILE_DIR value
  if(STORAGE_BACKEND STREQUAL file)
    # Use TEST_TILES_DIR for file backend
    set(TILE_DIR "${TEST_TILES_DIR}")
  elseif(STORAGE_BACKEND STREQUAL memcached_custom)
    # MemcacheD backend "custom" host:port
    set(TILE_DIR "memcached://${MEMCACHED_HOST}:${MEMCACHED_PORT}")
  elseif(STORAGE_BACKEND STREQUAL memcached_default)
    # MemcacheD backend "default"
    set(TILE_DIR "memcached://")
  endif()

  # Generate renderd.conf file
  configure_file(renderd.conf.in ${RENDERD_CONF})

  # Generate httpd.conf filelogs
  configure_file(httpd.conf.in ${HTTPD_CONF})

  # Set list of service start commands
  set(SERVICES_START_CMDS
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --slave 0 --foreground > ${RENDERD0_LOG} 2>&1 &
      PID=\${!}\;
      ${SLEEP_EXECUTABLE} 1\;
      ${PS_EXECUTABLE} -p \${PID} || exit 1\;
      echo \${PID} > ${RENDERD0_PID}"
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --slave 1 --foreground > ${RENDERD1_LOG} 2>&1 &
      PID=\${!}\;
      ${SLEEP_EXECUTABLE} 1\;
      ${PS_EXECUTABLE} -p \${PID} || exit 1\;
      echo \${PID} > ${RENDERD1_PID}"
    "$<TARGET_FILE:renderd> --config ${RENDERD_CONF} --slave 2"
    "${HTTPD_EXECUTABLE} -e debug -E ${HTTPD_LOG} -f ${HTTPD_CONF} -k start"
  )

  # Conditionally append memcached start commands to SERVICES_START_CMDS based on STORAGE_BACKEND value
  if(STORAGE_BACKEND MATCHES "memcached_.+")
    list(APPEND SERVICES_START_CMDS
      "${MEMCACHED_EXECUTABLE} -l ${MEMCACHED_HOST} -p ${MEMCACHED_PORT} -u ${USER_NAME} -vvv > ${MEMCACHED_LOG} 2>&1 &
        PID=\${!}\;
        ${SLEEP_EXECUTABLE} 1\;
        ${PS_EXECUTABLE} -p \${PID} || exit 1\;
        echo \${PID} > ${MEMCACHED_PID}"
    )
  endif()

  add_test(NAME create_dirs_${STORAGE_BACKEND}
    COMMAND ${MKDIR_EXECUTABLE} -p -v
      ${TEST_LOGS_DIR}
      ${TEST_RUN_DIR}
      ${TEST_TILES_DIR}
    WORKING_DIRECTORY tests
  )
  set_tests_properties(create_dirs_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_SETUP services_started_${STORAGE_BACKEND}
  )

  list(LENGTH SERVICES_START_CMDS SERVICES_START_CMDS_LENGTH)
  math(EXPR SERVICES_START_CMDS_LENGTH "${SERVICES_START_CMDS_LENGTH} - 1")

  foreach(SERVICES_START_CMDS_INDEX RANGE ${SERVICES_START_CMDS_LENGTH})
    # Get SERVICES_START_CMD from SERVICES_START_CMDS list
    list(GET SERVICES_START_CMDS ${SERVICES_START_CMDS_INDEX} SERVICES_START_CMD)
    add_test(NAME start_services_${STORAGE_BACKEND}_${SERVICES_START_CMDS_INDEX}
      COMMAND ${BASH} -c "${SERVICES_START_CMD}"
      WORKING_DIRECTORY tests
    )
    set_tests_properties(start_services_${STORAGE_BACKEND}_${SERVICES_START_CMDS_INDEX} PROPERTIES
      DEPENDS create_dirs_${STORAGE_BACKEND}
      FIXTURES_SETUP services_started_${STORAGE_BACKEND}
    )
  endforeach()

  foreach(SOCKET_TYPE sock tcp)
    # Use socket file as --socket value for communicating with renderd process
    if(SOCKET_TYPE STREQUAL sock)
      set(SOCKET ${RENDERD0_SOCKET})
    endif()
    # Use TCP host:port as --socket value for communicating with renderd process
    if(SOCKET_TYPE STREQUAL tcp)
      set(SOCKET ${RENDERD1_HOST}:${RENDERD1_PORT})
    endif()

    add_test(NAME render_expired_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        printf '0/0/0\n%.0s' {0..100} | $<TARGET_FILE:render_expired> \
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
    set_tests_properties(render_expired_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 90
    )
    add_test(NAME render_expired_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '100/100/100' | $<TARGET_FILE:render_expired> \
          --config ${RENDERD_CONF} \
          --no-progress
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_expired_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(NAME render_expired_delete_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0/0/0' | $<TARGET_FILE:render_expired> \
          --delete-from 0 \
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
    set_tests_properties(render_expired_delete_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_expired_touch_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 90
    )
    add_test(NAME render_expired_touch_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0/0/0' | $<TARGET_FILE:render_expired> \
          --map ${DEFAULT_MAP_NAME} \
          --max-zoom 5 \
          --min-zoom 0 \
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
      TIMEOUT 90
    )
    add_test(NAME render_list_${SOCKET_TYPE}_${STORAGE_BACKEND}
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
      TIMEOUT 90
    )
    add_test(NAME render_list_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        $<TARGET_FILE:render_list> \
          --all \
          --config ${RENDERD_CONF} \
          --max-lat 85.0511 \
          --max-lon 180 \
          --max-zoom 5 \
          --min-lat -85.0511 \
          --min-lon -180 \
          --min-zoom 5
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_list_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(NAME render_list_stdin_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        printf '0 0 0\n%.0s' {0..10} | $<TARGET_FILE:render_list> \
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
      TIMEOUT 90
    )
    add_test(NAME render_list_stdin_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        echo '0 0 0' | $<TARGET_FILE:render_list> \
          --config ${RENDERD_CONF} \
          --verbose
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_list_stdin_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(NAME render_old_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        ${TOUCH_EXECUTABLE} -d '+1 month' ${TEST_TILES_DIR}/planet-import-complete
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
      TIMEOUT 90
    )
    add_test(NAME render_old_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        ${TOUCH_EXECUTABLE} -d '+1 month' ${TEST_TILES_DIR}/planet-import-complete
        $<TARGET_FILE:render_old> \
          --config ${RENDERD_CONF}
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_old_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(NAME render_old_config_timestamp_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        $<TARGET_FILE:render_old> \
          --config ${RENDERD_CONF} \
          --timestamp 01/01/2024
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_old_config_timestamp_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      DEPENDS render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      TIMEOUT 60
    )
    add_test(NAME render_speedtest_${SOCKET_TYPE}_${STORAGE_BACKEND}
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
      FIXTURES_REQUIRED "services_started_${STORAGE_BACKEND};tiles_downloaded_${STORAGE_BACKEND}"
      TIMEOUT 90
    )
    add_test(NAME render_speedtest_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        $<TARGET_FILE:render_speedtest> \
          --config ${RENDERD_CONF}
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(render_speedtest_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED "services_started_${STORAGE_BACKEND};tiles_downloaded_${STORAGE_BACKEND}"
      TIMEOUT 60
    )
    add_test(NAME add_tile_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        CONFIG_NAME=\"bad_tile_config_${SOCKET_TYPE}\"
        SEARCH_LINE=\$(${GREP_EXECUTABLE} -m1 \"Loading tile config \${CONFIG_NAME}\" ${HTTPD_LOG})
        SEARCH_STRS=(
          \" at /\${CONFIG_NAME}/ \"
          \" extension .jpg \"
          \" mime type image/jpeg$\"
          \" tile directory ${TILE_DIR} \"
          \" zooms 10 - 15 \"
        )
        echo \"Searching log line '\${SEARCH_LINE}'\"
        for SEARCH_STR in \"\${SEARCH_STRS[@]}\"; do
          echo \"\tFor '\${SEARCH_STR}'\"
          echo \"\${SEARCH_LINE}\" | ${GREP_EXECUTABLE} -q -e \"\${SEARCH_STR}\" || exit 1
        done
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(add_tile_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )
    add_test(NAME add_tile_mime_config_${SOCKET_TYPE}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        for SEARCH_CONFIG in js png; do
          CONFIG_NAME=\"bad_tile_mime_config_\${SEARCH_CONFIG}_${SOCKET_TYPE}\"
          MIME_TYPE=image/png
          if [ \"\${SEARCH_CONFIG}\" = \"js\" ]; then
            MIME_TYPE=text/javascript
          fi
          SEARCH_LINE=\$(${GREP_EXECUTABLE} -m1 \"Loading tile config \${CONFIG_NAME}\" ${HTTPD_LOG})
          SEARCH_STRS=(
            \" at /\${CONFIG_NAME}/ \"
            \" extension .\${SEARCH_CONFIG} \"
            \" mime type \${MIME_TYPE}$\"
            \" tile directory ${RENDERD_TILE_DIR} \"
            \" zooms 0 - 20 \"
          )
          echo \"Searching log line '\${SEARCH_LINE}'\"
          for SEARCH_STR in \"\${SEARCH_STRS[@]}\"; do
            echo \"\tFor '\${SEARCH_STR}'\"
            echo \"\${SEARCH_LINE}\" | ${GREP_EXECUTABLE} -q -e \"\${SEARCH_STR}\" || exit 1
          done
          SEARCH_LINE=\$(${GREP_EXECUTABLE} \"AddTileMimeConfig will be deprecated\" ${HTTPD_LOG} | \
            ${GREP_EXECUTABLE} -m1 \"\${CONFIG_NAME}\")
          echo \"Searching log line '\${SEARCH_LINE}'\"
          SEARCH_STR=\"AddTileConfig /\${CONFIG_NAME}/ \${CONFIG_NAME} mimetype=\${MIME_TYPE} extension=\${SEARCH_CONFIG}\"
          echo \"\tFor '\${SEARCH_STR}'\"
          echo \"\${SEARCH_LINE}\" | ${GREP_EXECUTABLE} -q -e \"\${SEARCH_STR}\" || exit 1
        done
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(add_tile_mime_config_${SOCKET_TYPE}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )
    foreach(BAD_ADD_CONFIG bad_tile_config bad_tile_mime_config_js bad_tile_mime_config_png)
      if(BAD_ADD_CONFIG STREQUAL bad_tile_config)
        set(EXTENSION jpg)
      elseif(BAD_ADD_CONFIG STREQUAL bad_tile_mime_config_js)
        set(EXTENSION js)
      elseif(BAD_ADD_CONFIG STREQUAL bad_tile_mime_config_png)
        set(EXTENSION png)
      endif()
      # Generate URL path for tiles
      set(TILE_URL_PATH "/${BAD_ADD_CONFIG}_${SOCKET_TYPE}/${TILE_ZXY}.${EXTENSION}")
      # Generate tile URLs
      set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
      set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
      add_bad_tile_download_test(
        ${BAD_ADD_CONFIG}_${SOCKET_TYPE}_${STORAGE_BACKEND}_0
        ${HTTPD0_URL}
        services_started_${STORAGE_BACKEND}
      )
      add_bad_tile_download_test(
        ${BAD_ADD_CONFIG}_${SOCKET_TYPE}_${STORAGE_BACKEND}_1
        ${HTTPD1_URL}
        services_started_${STORAGE_BACKEND}
      )
    endforeach()
  endforeach()

  foreach(MAP_NAME IN LISTS MAP_NAMES)
    # Set EXTENSION from MAP_NAME (only works for map names containing an actual extension)
    string(REGEX REPLACE "[0-9]+" "" EXTENSION ${MAP_NAME})
    # Set count for stats to 0
    set(STATS_COUNTER 0)
    # Generate file and URL paths for tiles
    set(TILE_FILE_NAME "tile.${MAP_NAME}.${STORAGE_BACKEND}")
    set(TILE_URL_PATH "/tiles/${MAP_NAME}/${TILE_ZXY}.${EXTENSION}")
    # Generate tile URLs
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
    set(HTTPD2_URL "http://${HTTPD2_HOST}:${HTTPD2_PORT}${TILE_URL_PATH}")
    # Generate tile dirty and status URLs
    set(DIRTY_OFF_URL "${HTTPD1_URL}/dirty")
    set(DIRTY_ON_URL "${HTTPD0_URL}/dirty")
    set(STATUS_OFF_URL "${HTTPD1_URL}/status")
    set(STATUS_ON_URL "${HTTPD0_URL}/status")
    # Increment count for stats (only counted by requests to HTTPD0_URL, HTTPD1_URL has stats disabled)
    math(EXPR STATS_COUNTER "${STATS_COUNTER} + 1")

    add_good_tile_download_test(
      ${MAP_NAME}_${STORAGE_BACKEND}_0
      ${HTTPD0_URL}
      ${TILE_FILE_NAME}.0
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )
    add_good_tile_download_test(
      ${MAP_NAME}_${STORAGE_BACKEND}_1
      ${HTTPD1_URL}
      ${TILE_FILE_NAME}.1
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )

    add_test(NAME dirty_and_status_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        TILE_DIRTY_ON_CMD=\"${CURL_CMD} ${DIRTY_ON_URL}\"
        TILE_STATUS_ON_CMD=\"${CURL_CMD} ${STATUS_ON_URL}\"
        TILE_LAST_RENDERED_AT_OLD=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.')
        echo \"Tile Last Rendered At (Old): \${TILE_LAST_RENDERED_AT_OLD}\"
        ${SLEEP_EXECUTABLE} 1
        TILE_DIRTY_ON_OUTPUT=$(\${TILE_DIRTY_ON_CMD})
        echo \"Dirty On Output: \${TILE_DIRTY_ON_OUTPUT}\"
        until [ \"\${TILE_DIRTY_ON_OUTPUT}\" == \"Tile submitted for rendering\" ]; do
          echo 'Sleeping 1s';
          ${SLEEP_EXECUTABLE} 1;
          TILE_DIRTY_ON_OUTPUT=$(\${TILE_DIRTY_ON_CMD})
          echo \"Dirty On Output: \${TILE_DIRTY_ON_OUTPUT}\"
        done
        TILE_LAST_RENDERED_AT_NEW=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.')
        echo \"Tile Last Rendered At (New): \${TILE_LAST_RENDERED_AT_NEW}\"
        until [ \"\${TILE_LAST_RENDERED_AT_OLD}\" != \"\${TILE_LAST_RENDERED_AT_NEW}\" ]; do
          echo 'Sleeping 1s';
          ${SLEEP_EXECUTABLE} 1;
          TILE_LAST_RENDERED_AT_NEW=$(\${TILE_STATUS_ON_CMD} | ${GREP_EXECUTABLE} -o 'Last rendered at [^\\.]*.');
          echo \"Tile Last Rendered At (New): \${TILE_LAST_RENDERED_AT_NEW}\";
        done
        TILE_DIRTY_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${DIRTY_OFF_URL})
        echo \"Dirty Off code: '\${TILE_DIRTY_OFF_CODE}'\"
        until [ \"\${TILE_DIRTY_OFF_CODE}\" == \"404\" ]; do
          echo 'Sleeping 1s';
          ${SLEEP_EXECUTABLE} 1;
          TILE_DIRTY_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${DIRTY_OFF_URL})
          echo \"Dirty Off code: '\${TILE_DIRTY_OFF_CODE}'\"
        done
        TILE_STATUS_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${STATUS_OFF_URL})
        echo \"Status Off code: '\${TILE_STATUS_OFF_CODE}'\"
        until [ \"\${TILE_STATUS_OFF_CODE}\" == \"404\" ]; do
          echo 'Sleeping 1s';
          ${SLEEP_EXECUTABLE} 1;
          TILE_STATUS_OFF_CODE=$(${CURL_CMD} --write-out '%{http_code}' ${STATUS_OFF_URL})
          echo \"Status Off code: '\${TILE_STATUS_OFF_CODE}'\"
        done
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(dirty_and_status_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED "services_started_${STORAGE_BACKEND};tiles_downloaded_${STORAGE_BACKEND}"
      TIMEOUT 20
    )
    add_test(NAME stats_urls_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        METRICS_ON_CMD=\"${CURL_CMD} ${METRICS_ON_URL}\"
        METRICS_ON_OUTPUT=$(\${METRICS_ON_CMD})
        echo \"Metrics On output: \${METRICS_ON_OUTPUT}\"
        METRICS_200=\"modtile_layer_responses_total{layer=\\\"/tiles/${MAP_NAME}/\\\",status=\\\"200\\\"} ${STATS_COUNTER}\"
        echo \"\${METRICS_200}\";
        if [[ \"\${METRICS_ON_OUTPUT}\" != *\"\${METRICS_200}\"* ]]; then
          exit 1;
        fi
        METRICS_OFF_OUTPUT=$(${CURL_CMD} ${METRICS_OFF_URL})
        echo \"Metrics Off output: '\${METRICS_OFF_OUTPUT}'\";
        if [ \"\${METRICS_OFF_OUTPUT}\" != \"Stats are not enabled for this server\" ]; then
          exit 1;
        fi

        MOD_TILE_ON_CMD=\"${CURL_CMD} ${MOD_TILE_ON_URL}\"
        MOD_TILE_ON_OUTPUT=$(\${MOD_TILE_ON_CMD})
        echo \"Mod_tile On output: \${MOD_TILE_ON_OUTPUT}\"
        MOD_TILE_200=\"NoRes200Layer/tiles/${MAP_NAME}/: ${STATS_COUNTER}\"
        echo \"\${MOD_TILE_200}\";
        if [[ \"\${MOD_TILE_ON_OUTPUT}\" != *\"\${MOD_TILE_200}\"* ]]; then
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
    set_tests_properties(stats_urls_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED "services_started_${STORAGE_BACKEND};tiles_downloaded_${STORAGE_BACKEND}"
    )
    add_test(NAME tile_expired_${MAP_NAME}_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        ${TOUCH_EXECUTABLE} -d '-1 month' ${TEST_TILES_DIR}/planet-import-complete
        if ! ${CURL_CMD} --output /dev/null ${HTTPD1_URL}; then
          echo \"Past import: ${HTTPD1_URL}\";
        fi
        if ! ${CURL_CMD} --output /dev/null ${HTTPD2_URL}; then
          echo \"Past import: ${HTTPD2_URL}\";
        fi
        ${TOUCH_EXECUTABLE} -d '+1 month' ${TEST_TILES_DIR}/planet-import-complete
        if ! ${CURL_CMD} --output /dev/null ${HTTPD1_URL}; then
          echo \"Future import: ${HTTPD1_URL}\";
        fi
        if ! ${CURL_CMD} --output /dev/null ${HTTPD2_URL}; then
          echo \"Future import: ${HTTPD2_URL}\";
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(tile_expired_${MAP_NAME}_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )
  endforeach()

  # Generate file and URL paths for tiles
  set(TILE_FILE_NAME "tile.add_tile_config.${STORAGE_BACKEND}")
  set(TILE_URL_PATH "/good_add_tile_config/${TILE_ZXY}.png")
  # Generate tile URLs
  set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
  set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
  add_good_tile_download_test(
    add_tile_config_${STORAGE_BACKEND}_0
    ${HTTPD0_URL}
    ${TILE_FILE_NAME}.0
    tiles_downloaded_${STORAGE_BACKEND}
    services_started_${STORAGE_BACKEND}
  )
  add_good_tile_download_test(
    add_tile_config_${STORAGE_BACKEND}_1
    ${HTTPD1_URL}
    ${TILE_FILE_NAME}.1
    tiles_downloaded_${STORAGE_BACKEND}
    services_started_${STORAGE_BACKEND}
  )

  if(NOT PROCESSOR_COUNT EQUAL 0)
    # Set CTEST_NUM_SLAVE_THREADS to 5 (renderd1 = 1, renderd2 = 4 [NUM_THREADS])
    set(CTEST_NUM_SLAVE_THREADS 5)
    add_test(NAME renderd_num_threads_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        if ! ${GREP_EXECUTABLE} -q \"renderd: num_threads = '${PROCESSOR_COUNT}'\" \"${RENDERD0_LOG}\"; then
          ${GREP_EXECUTABLE} \"renderd: num_threads = \" \"${RENDERD0_LOG}\"
          exit 1;
        fi
        if ! ${GREP_EXECUTABLE} -q \"renderd: num_slave_threads = '${CTEST_NUM_SLAVE_THREADS}'\" \"${RENDERD0_LOG}\"; then
          ${GREP_EXECUTABLE} \"renderd: num_slave_threads = \" \"${RENDERD0_LOG}\"
          exit 1;
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(renderd_num_threads_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      REQUIRED_FILES ${RENDERD0_LOG}
    )
  endif()
  add_test(NAME stop_services_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      for SERVICE_PID_FILE in ${TEST_RUN_DIR}/*.pid; do
        ${KILL_EXECUTABLE} $(${CAT_EXECUTABLE} \${SERVICE_PID_FILE});
        ${RM} \${SERVICE_PID_FILE};
        ${SLEEP_EXECUTABLE} 1;
      done
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(stop_services_${STORAGE_BACKEND} PROPERTIES
    FIXTURES_CLEANUP services_started_${STORAGE_BACKEND}
  )
  add_test(NAME clear_dirs_${STORAGE_BACKEND}
    COMMAND ${BASH} -c "
      ${RM} -f -r -v \
        ${TEST_LOGS_DIR}/* \
        ${TEST_RUN_DIR}/* \
        ${TEST_TILES_DIR}/*
    "
    WORKING_DIRECTORY tests
  )
  set_tests_properties(clear_dirs_${STORAGE_BACKEND} PROPERTIES
    DEPENDS stop_services_${STORAGE_BACKEND}
    FIXTURES_CLEANUP services_started_${STORAGE_BACKEND}
    REQUIRED_FILES "${TEST_LOGS_DIR};${TEST_RUN_DIR};${TEST_TILES_DIR}"
  )

  if(STORAGE_BACKEND STREQUAL file)
    # Generate file and URL paths for tiles
    set(TILE_FILE_NAME "tile.parameterization.${STORAGE_BACKEND}")
    set(TILE_URL_PATH "/tiles/parameterization/en,de,_/${TILE_ZXY}.png")
    # Generate tile URLs
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
    add_good_tile_download_test(
      parameterization_${STORAGE_BACKEND}_0
      ${HTTPD0_URL}
      ${TILE_FILE_NAME}.0
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )
    add_good_tile_download_test(
      parameterization_${STORAGE_BACKEND}_1
      ${HTTPD1_URL}
      ${TILE_FILE_NAME}.1
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )

    # Generate file and URL paths for tiles
    set(TILE_FILE_NAME "tile.htcp.${STORAGE_BACKEND}")
    set(TILE_URL_PATH "/tiles/htcp/${TILE_ZXY}.png")
    # Generate tile URLs
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
    add_good_tile_download_test(
      htcp_${STORAGE_BACKEND}_0
      ${HTTPD0_URL}
      ${TILE_FILE_NAME}.0
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )
    add_good_tile_download_test(
      htcp_${STORAGE_BACKEND}_1
      ${HTTPD1_URL}
      ${TILE_FILE_NAME}.1
      tiles_downloaded_${STORAGE_BACKEND}
      services_started_${STORAGE_BACKEND}
    )

    # Generate URL path for tiles
    set(TILE_URL_PATH "/tiles/cors_all/${TILE_ZXY}.png")
    # Generate tile URLs
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
    add_test(NAME cors_all_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        RESPONSE_CODE_CMD=\"${CURL_CMD} --write-out %{http_code} --output /dev/null\"
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} ${HTTPD0_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"200\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} ${HTTPD1_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"200\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: example.com\" ${HTTPD0_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"200\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: example.com\" ${HTTPD1_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"200\" ]; then
          exit 1;
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(cors_all_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )

    # Generate URL path for tiles
    set(TILE_URL_PATH "/tiles/cors_localhost/${TILE_ZXY}.png")
    # Generate tile URLs
    set(HTTPD0_URL "http://${HTTPD0_HOST}:${HTTPD0_PORT}${TILE_URL_PATH}")
    set(HTTPD1_URL "http://${HTTPD1_HOST}:${HTTPD1_PORT}${TILE_URL_PATH}")
    add_test(NAME cors_localhost_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        RESPONSE_CODE_CMD=\"${CURL_CMD} --write-out %{http_code}\"
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: notlocalhost\" ${HTTPD0_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"403\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: notlocalhost\" ${HTTPD1_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"403\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: localhost\" ${HTTPD0_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" = \"403\" ]; then
          exit 1;
        fi
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"Origin: localhost\"  ${HTTPD1_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" = \"403\" ]; then
          exit 1;
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(cors_localhost_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )

    # Generate URL path for tiles
    set(TILE_URL_PATH "/tiles/png256/18/16125/115188.png")
    # Generate tile URL
    set(HTTPD2_URL "http://${HTTPD2_HOST}:${HTTPD2_PORT}${TILE_URL_PATH}")
    add_test(NAME max_load_missing_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        RESPONSE_CODE_CMD=\"${CURL_CMD} --write-out %{http_code}\"
        RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} ${HTTPD2_URL})
        echo \"Response code: '\${RESPONSE_CODE}'\"
        if [ \"\${RESPONSE_CODE}\" != \"404\" ]; then
          exit 1;
        fi
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(max_load_missing_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
    )
    add_test(NAME throttling_xforward_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        RESPONSE_CODE_CMD=\"${CURL_CMD} --write-out %{http_code}\"
        for i in {0..20}; do
          RESPONSE_CODE=$(\${RESPONSE_CODE_CMD} --header \"X-Forwarded-For: ${CTEST_SERVER_HOST}, ${CTEST_CLIENT_HOST}\" \
            http://${HTTPD2_HOST}:${HTTPD2_PORT}/tiles/webp/\$i/0/0.webp)
          echo \"Response code: '\${RESPONSE_CODE}'\"
          if [ \"\${RESPONSE_CODE}\" = \"503\" ]; then
            echo 'Request being rejected';
            exit 0;
          fi
        done
        echo 'Request was never rejected';
        exit 1;
      "
      WORKING_DIRECTORY tests
    )
    set_tests_properties(throttling_xforward_${STORAGE_BACKEND} PROPERTIES
      FIXTURES_REQUIRED services_started_${STORAGE_BACKEND}
      RUN_SERIAL TRUE
    )
  endif()

  if(JQ_EXECUTABLE)
    add_test(NAME tilejson_url_${STORAGE_BACKEND}
      COMMAND ${BASH} -c "
        TILE_DEFAULT_TILEJSON_CMD=\"${CURL_CMD} ${TILE_DEFAULT_TILEJSON_URL}\"
        TILE_DEFAULT_TILEJSON_OUTPUT=$(\${TILE_DEFAULT_TILEJSON_CMD})
        TILE_DEFAULT_TILEJSON_ATTRIBUTION=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .attribution)
        if [ \"\${TILE_DEFAULT_TILEJSON_ATTRIBUTION}\" != \"Attribution for ${DEFAULT_MAP_NAME}\" ]; then
          exit 1;
        fi
        TILE_DEFAULT_TILEJSON_DESCRIPTION=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .description)
        if [ \"\${TILE_DEFAULT_TILEJSON_DESCRIPTION}\" != \"Description for ${DEFAULT_MAP_NAME}\" ]; then
          exit 1;
        fi
        TILE_DEFAULT_TILEJSON_NAME=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .name)
        if [ \"\${TILE_DEFAULT_TILEJSON_NAME}\" != \"${DEFAULT_MAP_NAME}\" ]; then
          exit 1;
        fi
        TILE_DEFAULT_TILEJSON_VERSION=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .tilejson)
        if [ \"\${TILE_DEFAULT_TILEJSON_VERSION}\" != \"2.0.0\" ]; then
          exit 1;
        fi
        TILE_DEFAULT_TILEJSON_TILES_0=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .tiles[0])
        if [ \"\${TILE_DEFAULT_TILEJSON_TILES_0}\" != \"http://localhost/tiles/${DEFAULT_MAP_NAME}/{z}/{x}/{y}.png\" ]; then
          exit 1;
        fi
        TILE_DEFAULT_TILEJSON_TILES_1=$(echo \"\${TILE_DEFAULT_TILEJSON_OUTPUT}\" | ${JQ_EXECUTABLE} -r .tiles[1])
        if [ \"\${TILE_DEFAULT_TILEJSON_TILES_1}\" != \"http://alias/tiles/${DEFAULT_MAP_NAME}/{z}/{x}/{y}.png\" ]; then
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

# Test mal-formed HTTPD configuration directives
set(DIRECTIVES
  "AddTileConfig"
  "AddTileConfig string"
  "AddTileConfig string string maxzoom=100"
  "LoadTileConfigFile"
  "LoadTileConfigFile /tmp/bad/file/name"
  "ModTileCacheDurationDirty string"
  "ModTileCacheDurationLowZoom 1 string"
  "ModTileCacheDurationLowZoom string 1"
  "ModTileCacheDurationMax string"
  "ModTileCacheDurationMediumZoom 1 string"
  "ModTileCacheDurationMediumZoom string 1"
  "ModTileCacheDurationMinimum string"
  "ModTileCacheExtendedDuration string"
  "ModTileCacheLastModifiedFactor string"
  "ModTileEnableTileThrottlingXForward -1"
  "ModTileEnableTileThrottlingXForward 3"
  "ModTileEnableTileThrottlingXForward string"
  "ModTileMaxLoadMissing string"
  "ModTileMaxLoadOld string"
  "ModTileMissingRequestTimeout string"
  "ModTileRenderdSocketAddr string string"
  "ModTileRequestTimeout string"
  "ModTileThrottlingRenders 1 string"
  "ModTileThrottlingRenders string 1"
  "ModTileThrottlingTiles 1 string"
  "ModTileThrottlingTiles string 1"
  "ModTileVeryOldThreshold string"
)
set(DIRECTIVE_ERRORS
  "AddTileConfig error, URL path not defined"
  "AddTileConfig error, name of renderd config not defined"
  "AddTileConfig error, the configured zoom level lies outside of the range supported by this server"
  "LoadTileConfigFile takes one argument, load an entire renderd config file"
  "LoadTileConfigFile error, unable to open config file"
  "ModTileCacheDurationDirty argument must be an integer"
  "ModTileCacheDurationLowZoom second argument must be an integer"
  "ModTileCacheDurationLowZoom first argument must be an integer"
  "ModTileCacheDurationMax argument must be an integer"
  "ModTileCacheDurationMediumZoom second argument must be an integer"
  "ModTileCacheDurationMediumZoom first argument must be an integer"
  "ModTileCacheDurationMinimum argument must be an integer"
  "ModTileCacheExtendedDuration argument must be an integer"
  "ModTileCacheLastModifiedFactor argument must be a float"
  "ModTileEnableTileThrottlingXForward needs integer argument between 0 and 2 (0 => off\;
    1 => use client\; 2 => use last entry in chain"
  "ModTileEnableTileThrottlingXForward needs integer argument between 0 and 2 (0 => off\;
    1 => use client\; 2 => use last entry in chain"
  "ModTileEnableTileThrottlingXForward argument must be an integer"
  "ModTileMaxLoadMissing argument must be an integer"
  "ModTileMaxLoadOld argument must be an integer"
  "ModTileMissingRequestTimeout argument must be an integer"
  "ModTileRenderdSocketAddr second argument must be an integer"
  "ModTileRequestTimeout argument must be an integer"
  "ModTileThrottlingRenders second argument must be a float"
  "ModTileThrottlingRenders first argument must be an integer"
  "ModTileThrottlingTiles second argument must be a float"
  "ModTileThrottlingTiles first argument must be an integer"
  "ModTileVeryOldThreshold argument must be an integer"
)
list(LENGTH DIRECTIVES DIRECTIVES_LENGTH)
math(EXPR DIRECTIVES_LENGTH "${DIRECTIVES_LENGTH} - 1")

foreach(DIRECTIVE_INDEX RANGE ${DIRECTIVES_LENGTH})
  set(HTTPD_CONF "${TESTS_CONF_DIR}/httpd_bad_${DIRECTIVE_INDEX}.conf")
  # Get DIRECTIVE from DIRECTIVES list
  list(GET DIRECTIVES ${DIRECTIVE_INDEX} DIRECTIVE)
  # Get DIRECTIVE_ERROR from DIRECTIVE_ERRORS list
  list(GET DIRECTIVE_ERRORS ${DIRECTIVE_INDEX} DIRECTIVE_ERROR)

  # Generate httpd.conf file
  configure_file(httpd.conf.in ${HTTPD_CONF})

  add_test(NAME bad_httpd_config_${DIRECTIVE_INDEX}
    COMMAND ${BASH} -c "
      HTTPD_OUTPUT=$(${HTTPD_EXECUTABLE} -e debug -f ${HTTPD_CONF} -t 2>&1)
      if [ \"\${?}\" -eq \"0\" ]; then
        echo \"Unexpected success.\"
        echo \"\${HTTPD_OUTPUT}\"
        exit 1;
      fi
      if echo \"\${HTTPD_OUTPUT}\" | ${GREP_EXECUTABLE} -q \"${DIRECTIVE_ERROR}\"; then
        exit 0;
      else
        echo \"\${HTTPD_OUTPUT}\"
        exit 1;
      fi
    "
    WORKING_DIRECTORY tests
  )
endforeach()


#-----------------------------------------------------------------------------
#
#  Test targets
#
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
#
#  catch_main_o
#
#-----------------------------------------------------------------------------

add_library(catch_main_o OBJECT
  ${PROJECT_SOURCE_DIR}/tests/catch_main.cpp
  ${PROJECT_SOURCE_DIR}/tests/catch_test_common.cpp
)
target_include_directories(catch_main_o PRIVATE ${GLIB_INCLUDE_DIRS})

#-----------------------------------------------------------------------------
#
#  catch_test_common_o
#
#-----------------------------------------------------------------------------

add_library(catch_test_common_o OBJECT
  ${PROJECT_SOURCE_DIR}/tests/catch_test_common.cpp
)
target_include_directories(catch_test_common_o PRIVATE ${GLIB_INCLUDE_DIRS})

#-----------------------------------------------------------------------------
#
#  gen_tile_test
#
#-----------------------------------------------------------------------------

# Added by ${PROJECT_SOURCE_DIR}/src/CMakeLists.txt
# (in order to reduce redundant source and library definitions)

#-----------------------------------------------------------------------------
#
#  Additional options for targets added hereafter
#
#-----------------------------------------------------------------------------

add_compile_definitions(
  PROJECT_BINARY_DIR="${PROJECT_BINARY_DIR}/src"
  RENDERD_CONF="${PROJECT_SOURCE_DIR}/etc/renderd/renderd.conf.examples"
)
include_directories(${PROJECT_SOURCE_DIR}/includes)
link_libraries(${GLIB_LIBRARIES})

#-----------------------------------------------------------------------------
#
#  render_expired_test
#
#-----------------------------------------------------------------------------

add_executable(render_expired_test
  $<TARGET_OBJECTS:catch_main_o>
  render_expired_test.cpp
)

#-----------------------------------------------------------------------------
#
#  render_list_test
#
#-----------------------------------------------------------------------------

add_executable(render_list_test
  $<TARGET_OBJECTS:catch_main_o>
  render_list_test.cpp
)

#-----------------------------------------------------------------------------
#
#  render_old_test
#
#-----------------------------------------------------------------------------

add_executable(render_old_test
  $<TARGET_OBJECTS:catch_main_o>
  render_old_test.cpp
)

#-----------------------------------------------------------------------------
#
#  render_speedtest_test
#
#-----------------------------------------------------------------------------

add_executable(render_speedtest_test
  $<TARGET_OBJECTS:catch_main_o>
  render_speedtest_test.cpp
)

#-----------------------------------------------------------------------------
#
#  renderd_test
#
#-----------------------------------------------------------------------------

add_executable(renderd_test
  $<TARGET_OBJECTS:catch_main_o>
  renderd_test.cpp
)

#-----------------------------------------------------------------------------
#
#  renderd_config_test
#
#-----------------------------------------------------------------------------

add_executable(renderd_config_test
  $<TARGET_OBJECTS:catch_main_o>
  renderd_config_test.cpp
)
