# - Find LibMemcached
# Find the LibMemcached includes and libraries.
# This module defines:
#  LibMemcached_FOUND
#  LibMemcached_INCLUDE_DIRS
#  LibMemcached_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(LibMemcached QUIET libmemcached)

find_path(LibMemcached_INCLUDE_DIR
  NAMES memcached.h
  PATHS ${LibMemcached_INCLUDE_DIRS}
  PATH_SUFFIXES libmemcached
)

if((NOT LibMemcached_INCLUDE_DIRS) AND (LibMemcached_INCLUDE_DIR))
  set(LibMemcached_INCLUDE_DIRS ${LibMemcached_INCLUDE_DIR})
elseif(LibMemcached_INCLUDE_DIRS AND LibMemcached_INCLUDE_DIR)
  list(APPEND LibMemcached_INCLUDE_DIRS ${LibMemcached_INCLUDE_DIR})
endif()

find_library(LibMemcached_LIBRARY
  NAMES ${LibMemcached_LIBRARIES} memcached
)

if((NOT LibMemcached_LIBRARIES) AND (LibMemcached_LIBRARY))
  set(LibMemcached_LIBRARIES ${LibMemcached_LIBRARY})
elseif(LibMemcached_LIBRARIES AND LibMemcached_LIBRARY)
  list(APPEND LibMemcached_LIBRARIES ${LibMemcached_LIBRARY})
endif()

message(VERBOSE "LibMemcached_INCLUDE_DIRS=${LibMemcached_INCLUDE_DIRS}")
message(VERBOSE "LibMemcached_INCLUDE_DIR=${LibMemcached_INCLUDE_DIR}")
message(VERBOSE "LibMemcached_LIBRARIES=${LibMemcached_LIBRARIES}")
message(VERBOSE "LibMemcached_LIBRARY=${LibMemcached_LIBRARY}")

if((NOT LibMemcached_FOUND) AND (LibMemcached_INCLUDE_DIRS) AND (LibMemcached_LIBRARIES))
  set(LibMemcached_FOUND True)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMemcached
  FOUND_VAR LibMemcached_FOUND
  REQUIRED_VARS LibMemcached_FOUND LibMemcached_INCLUDE_DIRS LibMemcached_LIBRARIES
  VERSION_VAR LibMemcached_VERSION
)

mark_as_advanced(LibMemcached_INCLUDE_DIR LibMemcached_LIBRARY)
