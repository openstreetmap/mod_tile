#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

# Generated config.h lives in the build tree and must be found first
include_directories(BEFORE ${PROJECT_BINARY_DIR}/includes)
include_directories(${PROJECT_SOURCE_DIR}/includes)

include_directories(SYSTEM
  ${APR_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
  ${GLIB_INCLUDE_DIRS}
  ${HTTPD_INCLUDE_DIRS}
)
include_directories(SYSTEM
  ${ICU_INCLUDE_DIRS}
  ${INIPARSER_INCLUDE_DIRS}
  ${LIBMAPNIK_INCLUDE_DIRS}
  ${LIBMEMCACHED_INCLUDE_DIRS}
  ${LIBRADOS_INCLUDE_DIRS}
)

set(COMMON_LIBRARIES
  ${GLIB_LIBRARIES}
  Threads::Threads
)
set(RENDER_LIBRARIES
  ${COMMON_LIBRARIES}
  ${INIPARSER_LIBRARIES}
  ${MATH_LIBRARY}
)
set(STORE_LIBRARIES
  ${CAIRO_LIBRARIES}
  ${CURL_LIBRARIES}
  ${LIBMEMCACHED_LIBRARIES}
  ${LIBRADOS_LIBRARIES}
)

if(NOT MALLOC_LIB STREQUAL "libc")
  message(STATUS "Prepending '${MALLOC_LIBRARY}' to RENDER_LIBRARIES")
  list(PREPEND RENDER_LIBRARIES ${MALLOC_LIBRARY})
endif()

#-----------------------------------------------------------------------------
#
#  Internal object libraries
#
#  Sources shared between several targets are compiled once here rather than
#  once per target. They are built position-independent so that they can also
#  be linked into the `mod_tile.so` Apache module.
#
#-----------------------------------------------------------------------------

# Logging & system helpers (used by everything)
add_library(common_objs OBJECT
  g_logger.c
  sys_utils.c
)

# renderd.conf parsing (used by everything)
add_library(config_objs OBJECT
  renderd_config.c
)

# renderd socket protocol (used by renderd and the render_* clients)
add_library(protocol_objs OBJECT
  protocol_helper.c
)

# Client-side render request queue (used by the render_* clients)
add_library(submit_queue_objs OBJECT
  render_submit_queue.c
)

# Metatile path helpers (used by render_old without the full storage layer)
add_library(store_file_utils_objs OBJECT
  store_file_utils.c
)

# Pluggable storage backends
add_library(store_objs OBJECT
  store.c
  store_file.c
  store_memcached.c
  store_null.c
  store_rados.c
  store_ro_composite.c
  store_ro_http_proxy.c
)

# Mapnik rendering core (everything in renderd apart from `main()`)
add_library(renderd_core_objs OBJECT
  cache_expire.c
  daemon_compat.c
  gen_tile.cpp
  metatile.cpp
  parameterize_style.cpp
  request_queue.c
)

set_target_properties(
  common_objs
  config_objs
  protocol_objs
  renderd_core_objs
  store_file_utils_objs
  store_objs
  submit_queue_objs
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

set(COMMON_OBJS $<TARGET_OBJECTS:common_objs>)
set(CONFIG_OBJS $<TARGET_OBJECTS:config_objs>)
set(PROTOCOL_OBJS $<TARGET_OBJECTS:protocol_objs>)
set(SUBMIT_QUEUE_OBJS $<TARGET_OBJECTS:submit_queue_objs>)
set(RENDER_OBJS ${COMMON_OBJS} ${CONFIG_OBJS} ${PROTOCOL_OBJS})
set(STORE_OBJS $<TARGET_OBJECTS:store_objs> $<TARGET_OBJECTS:store_file_utils_objs>)
set(RENDERD_CORE_OBJS $<TARGET_OBJECTS:renderd_core_objs>)

#-----------------------------------------------------------------------------
#
#  Installed targets
#
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
#
#  mod_tile.so
#
#-----------------------------------------------------------------------------

set(mod_tile_SRCS
  ${COMMON_OBJS}
  ${CONFIG_OBJS}
  ${STORE_OBJS}
  mod_tile.c
)
set(mod_tile_LIBS
  ${APR_LIBRARIES}
  ${COMMON_LIBRARIES}
  ${INIPARSER_LIBRARIES}
  ${STORE_LIBRARIES}
)
add_library(mod_tile SHARED ${mod_tile_SRCS})
target_link_libraries(mod_tile ${mod_tile_LIBS})
set_target_properties(mod_tile PROPERTIES PREFIX "" SUFFIX ".so")

#-----------------------------------------------------------------------------
#
#  render_expired
#
#-----------------------------------------------------------------------------

set(render_expired_SRCS
  ${RENDER_OBJS}
  ${STORE_OBJS}
  ${SUBMIT_QUEUE_OBJS}
  render_expired.c
)
set(render_expired_LIBS
  ${RENDER_LIBRARIES}
  ${STORE_LIBRARIES}
)
add_executable(render_expired ${render_expired_SRCS})
target_link_libraries(render_expired ${render_expired_LIBS})

#-----------------------------------------------------------------------------
#
#  render_list
#
#-----------------------------------------------------------------------------

set(render_list_SRCS
  ${RENDER_OBJS}
  ${STORE_OBJS}
  ${SUBMIT_QUEUE_OBJS}
  render_list.c
)
set(render_list_LIBS
  ${RENDER_LIBRARIES}
  ${STORE_LIBRARIES}
)
add_executable(render_list ${render_list_SRCS})
target_link_libraries(render_list ${render_list_LIBS})

#-----------------------------------------------------------------------------
#
#  render_old
#
#-----------------------------------------------------------------------------

set(render_old_SRCS
  ${RENDER_OBJS}
  ${SUBMIT_QUEUE_OBJS}
  $<TARGET_OBJECTS:store_file_utils_objs>
  render_old.c
)
set(render_old_LIBS
  ${RENDER_LIBRARIES}
)
add_executable(render_old ${render_old_SRCS})
target_link_libraries(render_old ${render_old_LIBS})

#-----------------------------------------------------------------------------
#
#  render_speedtest
#
#-----------------------------------------------------------------------------

set(render_speedtest_SRCS
  ${RENDER_OBJS}
  ${SUBMIT_QUEUE_OBJS}
  render_speedtest.cpp
)
set(render_speedtest_LIBS
  ${RENDER_LIBRARIES}
)
add_executable(render_speedtest ${render_speedtest_SRCS})
target_link_libraries(render_speedtest ${render_speedtest_LIBS})

#-----------------------------------------------------------------------------
#
#  renderd
#
#-----------------------------------------------------------------------------

set(renderd_SRCS
  ${RENDER_OBJS}
  ${STORE_OBJS}
  ${RENDERD_CORE_OBJS}
  renderd.c
)
set(renderd_LIBS
  ${ICU_LIBRARIES}
  ${LIBMAPNIK_LIBRARIES}
  ${RENDER_LIBRARIES}
  ${STORE_LIBRARIES}
)
add_executable(renderd ${renderd_SRCS})
target_link_libraries(renderd ${renderd_LIBS})

if(ENABLE_TESTS)
#-----------------------------------------------------------------------------
#
#  Test targets
#
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
#
#  gen_tile_test
#
#-----------------------------------------------------------------------------

set(gen_tile_test_SRCS
  $<TARGET_OBJECTS:catch_test_common_o>
  ${renderd_SRCS}
  ${PROJECT_SOURCE_DIR}/tests/gen_tile_test.cpp
)
set(gen_tile_test_LIBS
  ${renderd_LIBS}
  ${STORE_LIBRARIES}
)
add_executable(gen_tile_test ${gen_tile_test_SRCS})
set_target_properties(gen_tile_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/tests)
target_compile_definitions(gen_tile_test PRIVATE MAIN_ALREADY_DEFINED)
target_include_directories(gen_tile_test PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(gen_tile_test ${gen_tile_test_LIBS})


#-----------------------------------------------------------------------------
#
#  unit_tests
#
#-----------------------------------------------------------------------------

set(unit_tests_SRCS
  $<TARGET_OBJECTS:catch_main_o>
  ${PROJECT_SOURCE_DIR}/tests/unit_tests.cpp
)
set(unit_tests_LIBS
  ${COMMON_LIBRARIES}
  ${INIPARSER_LIBRARIES}
  ${LIBMAPNIK_LIBRARIES}
)
add_executable(unit_tests ${unit_tests_SRCS})
set_target_properties(unit_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/tests)
target_compile_definitions(unit_tests PRIVATE
  MAPNIK_XML="${PROJECT_SOURCE_DIR}/utils/example-map/mapnik.xml"
  RENDERD_CONF="${PROJECT_SOURCE_DIR}/etc/renderd/renderd.conf.examples"
)
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(unit_tests ${unit_tests_LIBS})

endif()
