#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

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

link_directories(${CMAKE_LIBRARY_PATH})

set(COMMON_SRCS
  g_logger.c
  sys_utils.c
)
set(COMMON_LIBRARIES
  ${GLIB_LIBRARIES}
  Threads::Threads
)

set(RENDER_SRCS
  ${COMMON_SRCS}
  protocol_helper.c
  renderd_config.c
)
set(RENDER_LIBRARIES
  ${COMMON_LIBRARIES}
  ${INIPARSER_LIBRARIES}
  ${MATH_LIBRARY}
)

set(STORE_SRCS
  store.c
  store_file.c
  store_file_utils.c
  store_memcached.c
  store_null.c
  store_rados.c
  store_ro_composite.c
  store_ro_http_proxy.c
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
#  Installed targets
#
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
#
#  mod_tile.so
#
#-----------------------------------------------------------------------------

set(mod_tile_SRCS
  ${COMMON_SRCS}
  ${STORE_SRCS}
  mod_tile.c
  renderd_config.c
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
  ${RENDER_SRCS}
  ${STORE_SRCS}
  render_expired.c
  render_submit_queue.c
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
  ${RENDER_SRCS}
  ${STORE_SRCS}
  render_list.c
  render_submit_queue.c
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
  ${RENDER_SRCS}
  render_old.c
  render_submit_queue.c
  store_file_utils.c
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
  ${RENDER_SRCS}
  render_speedtest.cpp
  render_submit_queue.c
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
  ${RENDER_SRCS}
  ${STORE_SRCS}
  cache_expire.c
  daemon_compat.c
  gen_tile.cpp
  metatile.cpp
  parameterize_style.cpp
  renderd.c
  request_queue.c
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
)
add_executable(gen_tile_test ${gen_tile_test_SRCS})
target_compile_definitions(gen_tile_test PRIVATE MAIN_ALREADY_DEFINED)
target_include_directories(gen_tile_test PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(gen_tile_test ${gen_tile_test_LIBS})
set_target_properties(gen_tile_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/tests)

endif()
