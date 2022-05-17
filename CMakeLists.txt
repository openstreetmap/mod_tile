#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

#-----------------------------------------------------------------------------
#
#  Project version
#
#-----------------------------------------------------------------------------

project(mod_tile VERSION 0.6.1)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

#-----------------------------------------------------------------------------
#
#  Find external dependencies
#
#-----------------------------------------------------------------------------

include(GNUInstallDirs)

# Packages
find_package(CURL)
find_package(ICU REQUIRED uc)
find_package(Threads REQUIRED)

find_package(APR REQUIRED)
find_package(Cairo REQUIRED)
find_package(GLib REQUIRED)
find_package(HTTPD REQUIRED)
find_package(IniParser REQUIRED)
find_package(LibMemcached)
find_package(LibRados)
find_package(Mapnik REQUIRED)

# Programs
find_program(APXS_EXECUTABLE apxs REQUIRED)

# Functions
include(CheckFunctionExists)
# check_function_exists(bzero HAVE_BZERO)
check_function_exists(daemon HAVE_DAEMON)
# check_function_exists(gethostbyname HAVE_GETHOSTBYNAME)
check_function_exists(getloadavg HAVE_GETLOADAVG)
# check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)
# check_function_exists(inet_ntoa HAVE_INET_NTOA)
# check_function_exists(memset HAVE_MEMSET)
# check_function_exists(mkdir HAVE_MKDIR)
# check_function_exists(pow HAVE_POW)
# check_function_exists(select HAVE_SELECT)
# check_function_exists(socket HAVE_SOCKET)
# check_function_exists(strchr HAVE_STRCHR)
# check_function_exists(strdup HAVE_STRDUP)
# check_function_exists(strerror HAVE_STRERROR)
# check_function_exists(strrchr HAVE_STRRCHR)
# check_function_exists(strstr HAVE_STRSTR)
# check_function_exists(strtol HAVE_STRTOL)
# check_function_exists(strtoul HAVE_STRTOUL)
# check_function_exists(utime HAVE_UTIME)

# Include files
include(CheckIncludeFile)
# check_include_file(arpa/inet.h HAVE_ARPA_INET_H)
# check_include_file(fcntl.h HAVE_FCNTL_H)
# check_include_file(limits.h HAVE_LIMITS_H)
# check_include_file(netdb.h HAVE_NETDB_H)
# check_include_file(netinet/in.h HAVE_NETINET_IN_H)
check_include_file(paths.h HAVE_PATHS_H)
# check_include_file(stdint.h HAVE_STDINT_H)
# check_include_file(stdlib.h HAVE_STDLIB_H)
# check_include_file(string.h HAVE_STRING_H)
check_include_file(sys/cdefs.h HAVE_SYS_CDEFS_H)
check_include_file(sys/loadavg.h HAVE_SYS_LOADAVG_H)
# check_include_file(sys/socket.h HAVE_SYS_SOCKET_H)
# check_include_file(sys/time.h HAVE_SYS_TIME_H)
# check_include_file(syslog.h HAVE_SYSLOG_H)
# check_include_file(unistd.h HAVE_UNISTD_H)
# check_include_file(utime.h HAVE_UTIME_H)

#-----------------------------------------------------------------------------
#
#  Set variables
#
#-----------------------------------------------------------------------------

execute_process(COMMAND ${APXS_EXECUTABLE} -q exp_libexecdir
  OUTPUT_VARIABLE HTTPD_MODULES_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(Cairo_FOUND)
  set(HAVE_CAIRO 1)
endif()

if(CURL_FOUND)
  set(HAVE_LIBCURL 1)
endif()

if(LibMemcached_FOUND)
  set(HAVE_LIBMEMCACHED 1)
endif()

if(LibRados_FOUND)
  set(HAVE_LIBRADOS 1)
endif()

if(CMAKE_HAVE_PTHREAD_H)
  set(HAVE_PTHREAD 1)
endif()

set(VERSION ${PROJECT_VERSION})

#-----------------------------------------------------------------------------
#
#  config.h
#
#-----------------------------------------------------------------------------

configure_file(
    ${PROJECT_SOURCE_DIR}/includes/config.h.in
    ${PROJECT_SOURCE_DIR}/includes/config.h
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

install(
  TARGETS
    mod_tile
    render_expired
    render_list
    render_old
    renderd
    speedtest
  LIBRARY DESTINATION ${HTTPD_MODULES_DIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  FILES
    docs/man/render_expired.1
    docs/man/render_list.1
    docs/man/render_old.1
    docs/man/render_speedtest.1
    docs/man/renderd.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

#-----------------------------------------------------------------------------
#
#  Test
#
#-----------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
