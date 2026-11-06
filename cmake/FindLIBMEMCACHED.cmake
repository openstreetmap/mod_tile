# - Find LIBMEMCACHED
# Find the LIBMEMCACHED includes and libraries.
# This module defines:
#  LIBMEMCACHED_FOUND        - True if LIBMEMCACHED is found
#  LIBMEMCACHED_INCLUDE_DIRS - Include directories for LIBMEMCACHED
#  LIBMEMCACHED_LIBRARIES    - Libraries to link against for LIBMEMCACHED
#  LIBMEMCACHED_LIBRARY_DIRS - Library search directories for LIBMEMCACHED
#  LIBMEMCACHED_VERSION      - Version of LIBMEMCACHED found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBMEMCACHED QUIET libmemcached)
endif()

# Search for main header (libmemcached/memcached.h or memcached.h)
find_path(LIBMEMCACHED_INCLUDE_DIR
  NAMES libmemcached/memcached.h memcached.h
  HINTS ${PC_LIBMEMCACHED_INCLUDE_DIRS}
  PATH_SUFFIXES libmemcached
)

# Search for library binary
find_library(LIBMEMCACHED_LIBRARY
  NAMES memcached libmemcached
  HINTS ${PC_LIBMEMCACHED_LIBRARY_DIRS}
)

# Assemble include and library lists
set(LIBMEMCACHED_INCLUDE_DIRS ${LIBMEMCACHED_INCLUDE_DIR})
if(PC_LIBMEMCACHED_INCLUDE_DIRS)
  list(APPEND LIBMEMCACHED_INCLUDE_DIRS ${PC_LIBMEMCACHED_INCLUDE_DIRS})
endif()
if(LIBMEMCACHED_INCLUDE_DIRS)
  list(REMOVE_DUPLICATES LIBMEMCACHED_INCLUDE_DIRS)
endif()

set(LIBMEMCACHED_LIBRARIES ${LIBMEMCACHED_LIBRARY})
if(PC_LIBMEMCACHED_LIBRARIES AND NOT LIBMEMCACHED_LIBRARY)
  set(LIBMEMCACHED_LIBRARIES ${PC_LIBMEMCACHED_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(LIBMEMCACHED_LIBRARY_DIRS ${PC_LIBMEMCACHED_LIBRARY_DIRS})
if(LIBMEMCACHED_LIBRARY)
  get_filename_component(LIBMEMCACHED_LIB_DIR "${LIBMEMCACHED_LIBRARY}" DIRECTORY)
  list(APPEND LIBMEMCACHED_LIBRARY_DIRS "${LIBMEMCACHED_LIB_DIR}")
endif()
if(LIBMEMCACHED_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES LIBMEMCACHED_LIBRARY_DIRS)
endif()

# Version detection
if(LIBMEMCACHED_INCLUDE_DIR)
  set(_memcached_version_h "")
  if(EXISTS "${LIBMEMCACHED_INCLUDE_DIR}/libmemcached/configure.h")
    set(_memcached_version_h "${LIBMEMCACHED_INCLUDE_DIR}/libmemcached/configure.h")
  elseif(EXISTS "${LIBMEMCACHED_INCLUDE_DIR}/libmemcached-1.0/configure.h")
    set(_memcached_version_h "${LIBMEMCACHED_INCLUDE_DIR}/libmemcached-1.0/configure.h")
  elseif(EXISTS "${LIBMEMCACHED_INCLUDE_DIR}/configure.h")
    set(_memcached_version_h "${LIBMEMCACHED_INCLUDE_DIR}/configure.h")
  endif()

  if(_memcached_version_h)
    file(STRINGS "${_memcached_version_h}" _memcached_version_str REGEX "#define[ \t]+LIBMEMCACHED_VERSION_STRING[ \t]+\".*\"")
    if(_memcached_version_str)
      string(REGEX REPLACE ".*#define[ \t]+LIBMEMCACHED_VERSION_STRING[ \t]+\"([^\"]+)\".*" "\\1" LIBMEMCACHED_VERSION "${_memcached_version_str}")
    endif()
  endif()
endif()

if(NOT LIBMEMCACHED_VERSION AND PC_LIBMEMCACHED_VERSION)
  set(LIBMEMCACHED_VERSION ${PC_LIBMEMCACHED_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBMEMCACHED
  REQUIRED_VARS LIBMEMCACHED_LIBRARY LIBMEMCACHED_INCLUDE_DIR
  VERSION_VAR LIBMEMCACHED_VERSION
)

mark_as_advanced(LIBMEMCACHED_INCLUDE_DIR LIBMEMCACHED_LIBRARY)
