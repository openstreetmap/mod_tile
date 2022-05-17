# - Find Cairo
# Find the Cairo includes and libraries.
# This module defines:
#  Cairo_FOUND
#  Cairo_INCLUDE_DIRS
#  Cairo_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(Cairo QUIET cairo)

find_path(Cairo_INCLUDE_DIR
  NAMES cairo.h
  PATHS ${Cairo_INCLUDE_DIRS}
  PATH_SUFFIXES cairo
)

if((NOT Cairo_INCLUDE_DIRS) AND (Cairo_INCLUDE_DIR))
  set(Cairo_INCLUDE_DIRS ${Cairo_INCLUDE_DIR})
endif()

find_library(Cairo_LIBRARY
  NAMES ${Cairo_LIBRARIES} cairo
)

if((NOT Cairo_LIBRARIES) AND (Cairo_LIBRARY))
  set(Cairo_LIBRARIES ${Cairo_LIBRARY})
endif()

message(VERBOSE "Cairo_INCLUDE_DIRS=${Cairo_INCLUDE_DIRS}")
message(VERBOSE "Cairo_INCLUDE_DIR=${Cairo_INCLUDE_DIR}")
message(VERBOSE "Cairo_LIBRARIES=${Cairo_LIBRARIES}")
message(VERBOSE "Cairo_LIBRARY=${Cairo_LIBRARY}")

if((NOT Cairo_FOUND) AND (Cairo_INCLUDE_DIRS) AND (Cairo_LIBRARIES))
  set(Cairo_FOUND True)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cairo
  FOUND_VAR Cairo_FOUND
  REQUIRED_VARS Cairo_FOUND Cairo_INCLUDE_DIRS Cairo_LIBRARIES
  VERSION_VAR Cairo_VERSION
)

mark_as_advanced(Cairo_INCLUDE_DIR Cairo_LIBRARY)
