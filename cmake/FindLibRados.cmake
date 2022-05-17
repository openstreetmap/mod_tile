# - Find LibRados
# Find the LibRados includes and libraries.
# This module defines:
#  LibRados_FOUND
#  LibRados_INCLUDE_DIRS
#  LibRados_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(LibRados QUIET rados)

find_path(LibRados_INCLUDE_DIR
  NAMES librados.h
  PATHS ${LibRados_INCLUDE_DIRS}
  PATH_SUFFIXES rados
)

if((NOT LibRados_INCLUDE_DIRS) AND (LibRados_INCLUDE_DIR))
  set(LibRados_INCLUDE_DIRS ${LibRados_INCLUDE_DIR})
elseif(LibRados_INCLUDE_DIRS AND LibRados_INCLUDE_DIR)
  list(APPEND LibRados_INCLUDE_DIRS ${LibRados_INCLUDE_DIR})
endif()

find_library(LibRados_LIBRARY
  NAMES ${LibRados_LIBRARIES} rados
)

if((NOT LibRados_LIBRARIES) AND (LibRados_LIBRARY))
  set(LibRados_LIBRARIES ${LibRados_LIBRARY})
elseif(LibRados_LIBRARIES AND LibRados_LIBRARY)
  list(APPEND LibRados_LIBRARIES ${LibRados_LIBRARY})
endif()

message(VERBOSE "LibRados_INCLUDE_DIRS=${LibRados_INCLUDE_DIRS}")
message(VERBOSE "LibRados_INCLUDE_DIR=${LibRados_INCLUDE_DIR}")
message(VERBOSE "LibRados_LIBRARIES=${LibRados_LIBRARIES}")
message(VERBOSE "LibRados_LIBRARY=${LibRados_LIBRARY}")

if((NOT LibRados_FOUND) AND (LibRados_INCLUDE_DIRS) AND (LibRados_LIBRARIES))
  set(LibRados_FOUND True)
endif()

if((NOT LibRados_VERSION) AND (LibRados_FOUND))
	file(STRINGS "${LibRados_INCLUDE_DIR}/librados.h" _contents REGEX "#define LIBRADOS_VER_[A-Z]+[ \t]+")
	if (_contents)
		string(REGEX REPLACE ".*#define LIBRADOS_VER_MAJOR[ \t]+([0-9]+).*" "\\1" LibRados_MAJOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define LIBRADOS_VER_MINOR[ \t]+([0-9]+).*" "\\1" LibRados_MINOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define LIBRADOS_VER_EXTRA[ \t]+([0-9]+).*" "\\1" LibRados_EXTRA_VERSION "${_contents}")

		set(LibRados_VERSION ${LibRados_MAJOR_VERSION}.${LibRados_MINOR_VERSION}.${LibRados_EXTRA_VERSION})
	endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRados
  FOUND_VAR LibRados_FOUND
  REQUIRED_VARS LibRados_FOUND LibRados_INCLUDE_DIRS LibRados_LIBRARIES
  VERSION_VAR LibRados_VERSION
)

mark_as_advanced(LibRados_INCLUDE_DIR LibRados_LIBRARY)
