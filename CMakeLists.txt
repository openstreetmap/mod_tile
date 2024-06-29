#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

#-----------------------------------------------------------------------------
#
#  Project version
#
#-----------------------------------------------------------------------------

project(mod_tile
  DESCRIPTION "Renders map tiles with Mapnik and serves them using Apache HTTP Server"
  HOMEPAGE_URL "https://github.com/openstreetmap/mod_tile"
  VERSION 0.8.0
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11 CACHE STRING "Sets the C++ standard.")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_INSTALL_MODULESDIR CACHE PATH "Apache HTTP Server module installation directory")
set(ENABLE_MAN ON CACHE BOOL "Build man pages")
set(ENABLE_TESTS OFF CACHE BOOL "Build test suite")
set(USE_CAIRO ON CACHE BOOL "Add cairo support if available (for `store_ro_composite.c` backend)")
set(USE_CURL ON CACHE BOOL "Add curl support if available (for `store_ro_http_proxy.c` backend)")
set(USE_MEMCACHED ON CACHE BOOL "Add memcached support if available (for `store_memcached.c` backend)")
set(USE_RADOS ON CACHE BOOL "Add rados support if available (for `store_rados.c` backend)")
set(MALLOC_LIB "libc" CACHE STRING "Memory Management Library for `renderd`")
set_property(CACHE MALLOC_LIB PROPERTY STRINGS "libc" "jemalloc" "mimalloc" "tcmalloc")

#-----------------------------------------------------------------------------
#
#  Find external dependencies
#
#-----------------------------------------------------------------------------

include(GNUInstallDirs)

# Packages
find_package(ICU REQUIRED uc)
find_package(Threads REQUIRED)

find_package(APR REQUIRED)
find_package(GLIB 2.50 REQUIRED)
find_package(HTTPD 2.4 REQUIRED)
find_package(INIPARSER REQUIRED)
find_package(LIBMAPNIK 3 REQUIRED)

if(LIBMAPNIK_VERSION VERSION_GREATER_EQUAL 4)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(USE_CURL)
  find_package(CURL)
endif()
if(USE_CAIRO)
  find_package(CAIRO)
endif()
if(USE_MEMCACHED)
  find_package(LIBMEMCACHED)
endif()
if(USE_RADOS)
  find_package(LIBRADOS)
endif()

# Programs
find_program(APXS_EXECUTABLE apxs REQUIRED)

# Functions
include(CheckFunctionExists)
check_function_exists(daemon HAVE_DAEMON)
check_function_exists(getloadavg HAVE_GETLOADAVG)
check_function_exists(pow HAVE_POW)

# Include files
include(CheckIncludeFile)
check_include_file(paths.h HAVE_PATHS_H)
check_include_file(sys/cdefs.h HAVE_SYS_CDEFS_H)
check_include_file(sys/loadavg.h HAVE_SYS_LOADAVG_H)

# Libraries
include(CheckLibraryExists)
if(NOT HAVE_POW)
  check_library_exists(m pow "" HAVE_LIB_M)
  find_library(MATH_LIBRARY m REQUIRED)
endif()

if(NOT MALLOC_LIB STREQUAL "libc")
  message(STATUS "Using '${MALLOC_LIB}' for memory managment")
  if(MALLOC_LIB STREQUAL "jemalloc")
    # jemalloc (http://jemalloc.net)
    find_library(MALLOC_LIBRARY jemalloc REQUIRED)
  elseif(MALLOC_LIB STREQUAL "mimalloc")
    # mimalloc (https://github.com/microsoft/mimalloc)
    find_library(MALLOC_LIBRARY mimalloc REQUIRED)
  elseif(MALLOC_LIB STREQUAL "tcmalloc")
    # tcmalloc (https://github.com/google/tcmalloc)
    find_library(MALLOC_LIBRARY tcmalloc REQUIRED)
  endif()
endif()

#-----------------------------------------------------------------------------
#
#  Set variables
#
#-----------------------------------------------------------------------------

execute_process(COMMAND ${APXS_EXECUTABLE} -q libexecdir
  OUTPUT_VARIABLE HTTPD_LIBEXECDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(COMMAND ${APXS_EXECUTABLE} -q sysconfdir
  OUTPUT_VARIABLE HTTPD_SYSCONFDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(LIBMAPNIK_VERSION VERSION_LESS 4)
  find_program(MAPNIK_CONFIG_EXECUTABLE NAMES mapnik-config REQUIRED)
  execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --fonts
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE MAPNIK_FONTS_DIR
  )
  execute_process(COMMAND ${MAPNIK_CONFIG_EXECUTABLE} --input-plugins
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE MAPNIK_PLUGINS_DIR
  )
elseif(LIBMAPNIK_VERSION VERSION_GREATER_EQUAL 4)
  pkg_get_variable(MAPNIK_FONTS_DIR libmapnik fonts_dir)
  pkg_get_variable(MAPNIK_PLUGINS_DIR libmapnik plugins_dir)
endif()

if(NOT CMAKE_INSTALL_MODULESDIR)
  set(CMAKE_INSTALL_MODULESDIR ${HTTPD_LIBEXECDIR})
endif()

if(CAIRO_FOUND)
  set(HAVE_CAIRO 1)
endif()

if(CURL_FOUND)
  set(HAVE_LIBCURL 1)
endif()

if(LIBMEMCACHED_FOUND)
  set(HAVE_LIBMEMCACHED 1)
endif()

if(LIBRADOS_FOUND)
  set(HAVE_LIBRADOS 1)
endif()

if(CMAKE_HAVE_PTHREAD_H)
  set(HAVE_PTHREAD 1)
endif()

set(MAPNIK_FONTS_DIR "${MAPNIK_FONTS_DIR}")
set(MAPNIK_FONTS_DIR_RECURSE 0)
set(MAPNIK_PLUGINS_DIR "${MAPNIK_PLUGINS_DIR}")
set(RENDERD_CONFIG "${CMAKE_INSTALL_SYSCONFDIR}/renderd.conf")
set(RENDERD_RUN_DIR "${CMAKE_INSTALL_RUNSTATEDIR}/renderd")
set(RENDERD_TILE_DIR "${CMAKE_INSTALL_LOCALSTATEDIR}/cache/renderd/tiles")

set(RENDERD_PIDFILE "${RENDERD_RUN_DIR}/renderd.pid")
set(RENDERD_SOCKET "${RENDERD_RUN_DIR}/renderd.sock")

set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}")
set(TILE_LOAD_FILENAME "tile.load")
set(VERSION "${PROJECT_VERSION}")

#-----------------------------------------------------------------------------
#
#  Configure
#
#-----------------------------------------------------------------------------

# include/config.h.in
configure_file(
  ${PROJECT_SOURCE_DIR}/includes/config.h.in
  ${PROJECT_SOURCE_DIR}/includes/config.h
)

# etc/apache2/tile.load.in
configure_file(
  ${PROJECT_SOURCE_DIR}/etc/apache2/tile.load.in
  ${PROJECT_BINARY_DIR}/tile.load
)

# etc/renderd/renderd.conf.in
configure_file(
  ${PROJECT_SOURCE_DIR}/etc/renderd/renderd.conf.in
  ${PROJECT_BINARY_DIR}/renderd.conf
)

#-----------------------------------------------------------------------------
#
#  Build
#
#-----------------------------------------------------------------------------

add_subdirectory(src)

#-----------------------------------------------------------------------------
#
#  Install
#
#-----------------------------------------------------------------------------

# Determine install destination for 'etc/apache2/tile.load.in'
if(EXISTS "/etc/os-release")
  execute_process(COMMAND sh -c ". /etc/os-release && echo $ID"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE DISTRO_ID
  )
  message(STATUS "Found ID='${DISTRO_ID}' in '/etc/os-release'")
  if(DISTRO_ID MATCHES "arch")
    set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}/extra")
    set(TILE_LOAD_FILENAME "httpd-tile.conf")
  elseif(DISTRO_ID MATCHES "centos|fedora|rhel")
    set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}.modules.d")
    set(TILE_LOAD_FILENAME "11-tile.conf")
  elseif(DISTRO_ID MATCHES "debian|ubuntu")
    set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}/mods-available")
  elseif(DISTRO_ID MATCHES "freebsd")
    set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}/modules.d")
    set(TILE_LOAD_FILENAME "080_tile.conf")
  elseif(DISTRO_ID MATCHES "opensuse-leap|opensuse-tumbleweed")
    set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}/conf.d")
    set(TILE_LOAD_FILENAME "mod_tile.conf")
  endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(TILE_LOAD_DIRECTORY "${HTTPD_SYSCONFDIR}/extra")
  set(TILE_LOAD_FILENAME "httpd-tile.conf")
endif()
message(STATUS "File 'etc/apache2/tile.load.in' will be installed to '${TILE_LOAD_DIRECTORY}/${TILE_LOAD_FILENAME}'")

# Directories
install(
  DIRECTORY
  DESTINATION ${RENDERD_TILE_DIR}
)

install(
  DIRECTORY
  DESTINATION ${RENDERD_RUN_DIR}
)

# Configuration files
install(
  FILES
    ${PROJECT_BINARY_DIR}/tile.load
  DESTINATION ${TILE_LOAD_DIRECTORY}
  RENAME ${TILE_LOAD_FILENAME}
)

install(
  FILES
    ${PROJECT_BINARY_DIR}/renderd.conf
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
)

# Targets
install(
  TARGETS
    mod_tile
    render_expired
    render_list
    render_old
    render_speedtest
    renderd
  LIBRARY DESTINATION ${CMAKE_INSTALL_MODULESDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Man files
if(ENABLE_MAN)
  install(
    FILES
      docs/man/render_expired.1
      docs/man/render_list.1
      docs/man/render_old.1
      docs/man/render_speedtest.1
      docs/man/renderd.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
  )
  install(
    FILES
      docs/man/renderd.conf.5
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man5
  )
endif()

#-----------------------------------------------------------------------------
#
#  Package
#
#-----------------------------------------------------------------------------

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGE_CONTACT "OpenStreetMap")
set(CPACK_PACKAGE_VENDOR "OpenStreetMap")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")

include(CPack)

#-----------------------------------------------------------------------------
#
#  Test
#
#-----------------------------------------------------------------------------

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
