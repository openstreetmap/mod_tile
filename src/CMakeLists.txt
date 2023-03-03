#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

include_directories(${PROJECT_SOURCE_DIR}/includes)

include_directories(SYSTEM ${APR_INCLUDE_DIRS} ${Cairo_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS} ${GLib_INCLUDE_DIRS} ${HTTPD_INCLUDE_DIRS})
include_directories(SYSTEM ${ICU_INCLUDE_DIRS} ${IniParser_INCLUDE_DIRS} ${LibMapnik_INCLUDE_DIRS} ${LibMemcached_INCLUDE_DIRS} ${LibRados_INCLUDE_DIRS})

link_directories(${CMAKE_LIBRARY_PATH})

set(COMMON_SRCS
  g_logger.c
  sys_utils.c
)
set(COMMON_LIBRARIES
  ${GLib_LIBRARIES}
  Threads::Threads
)

set(STORE_SRCS
  store_file_utils.c
  store_file.c
  store_memcached.c
  store_null.c
  store_rados.c
  store_ro_composite.c
  store_ro_http_proxy.c
  store.c
)
set(STORE_LIBRARIES
  ${Cairo_LIBRARIES}
  ${CURL_LIBRARIES}
  ${LibMemcached_LIBRARIES}
  ${LibRados_LIBRARIES}
)

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
)
set(mod_tile_LIBS
  ${APR_LIBRARIES}
  ${COMMON_LIBRARIES}
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
  ${COMMON_SRCS}
  ${STORE_SRCS}
  protocol_helper.c
  render_expired.c
  render_submit_queue.c
)
set(render_expired_LIBS
  ${COMMON_LIBRARIES}
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
  ${COMMON_SRCS}
  ${STORE_SRCS}
  protocol_helper.c
  render_list.c
  render_submit_queue.c
)
set(render_list_LIBS
  ${COMMON_LIBRARIES}
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
  ${COMMON_SRCS}
  protocol_helper.c
  render_old.c
  render_submit_queue.c
  store_file_utils.c
)
set(render_old_LIBS
  ${COMMON_LIBRARIES}
)
add_executable(render_old ${render_old_SRCS})
target_link_libraries(render_old ${render_old_LIBS})

#-----------------------------------------------------------------------------
#
#  render_speedtest
#
#-----------------------------------------------------------------------------

set(render_speedtest_SRCS
  ${COMMON_SRCS}
  protocol_helper.c
  render_submit_queue.c
  speedtest.cpp
)
set(render_speedtest_LIBS
  ${COMMON_LIBRARIES}
)
add_executable(render_speedtest ${render_speedtest_SRCS})
target_link_libraries(render_speedtest ${render_speedtest_LIBS})

#-----------------------------------------------------------------------------
#
#  renderd
#
#-----------------------------------------------------------------------------

set(renderd_SRCS
  ${COMMON_SRCS}
  ${STORE_SRCS}
  cache_expire.c
  daemon_compat.c
  daemon.c
  gen_tile.cpp
  metatile.cpp
  parameterize_style.cpp
  protocol_helper.c
  request_queue.c
)
set(renderd_LIBS
  ${COMMON_LIBRARIES}
  ${ICU_LIBRARIES}
  ${IniParser_LIBRARIES}
  ${LibMapnik_LIBRARIES}
  ${STORE_LIBRARIES}
)
add_executable(renderd ${renderd_SRCS})
target_link_libraries(renderd ${renderd_LIBS})

#-----------------------------------------------------------------------------
#
#  Test targets
#
#-----------------------------------------------------------------------------

if (ENABLE_TESTS)
#-----------------------------------------------------------------------------
#
#  gen_tile_test
#
#-----------------------------------------------------------------------------

set(gen_tile_test_SRCS
  ${renderd_SRCS}
	gen_tile_test.cpp
)
set(gen_tile_test_LIBS
  ${renderd_LIBS}
)
add_executable(gen_tile_test ${gen_tile_test_SRCS})
target_link_libraries(gen_tile_test ${gen_tile_test_LIBS})
target_compile_options(gen_tile_test PRIVATE -DMAIN_ALREADY_DEFINED)
endif()
