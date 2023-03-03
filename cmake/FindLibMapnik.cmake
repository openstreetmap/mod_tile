# - Find LibMapnik
# Find the LibMapnik includes and libraries.
# This module defines:
#  LibMapnik_FOUND
#  LibMapnik_INCLUDE_DIRS
#  LibMapnik_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(LibMapnik QUIET libmapnik)

find_path(LibMapnik_INCLUDE_DIR
  NAMES version.hpp
  PATHS ${LibMapnik_INCLUDE_DIRS}
  PATH_SUFFIXES mapnik
)

if((NOT LibMapnik_INCLUDE_DIRS) AND (LibMapnik_INCLUDE_DIR))
  set(LibMapnik_INCLUDE_DIRS ${LibMapnik_INCLUDE_DIR})
elseif(LibMapnik_INCLUDE_DIRS AND LibMapnik_INCLUDE_DIR)
  list(APPEND LibMapnik_INCLUDE_DIRS ${LibMapnik_INCLUDE_DIR})
endif()

find_library(LibMapnik_LIBRARY
  NAMES ${LibMapnik_LIBRARIES} mapnik
)

if((NOT LibMapnik_LIBRARIES) AND (LibMapnik_LIBRARY))
  set(LibMapnik_LIBRARIES ${LibMapnik_LIBRARY})
elseif(LibMapnik_LIBRARIES AND LibMapnik_LIBRARY)
  list(APPEND LibMapnik_LIBRARIES ${LibMapnik_LIBRARY})
endif()

message(VERBOSE "LibMapnik_INCLUDE_DIRS=${LibMapnik_INCLUDE_DIRS}")
message(VERBOSE "LibMapnik_INCLUDE_DIR=${LibMapnik_INCLUDE_DIR}")
message(VERBOSE "LibMapnik_LIBRARIES=${LibMapnik_LIBRARIES}")
message(VERBOSE "LibMapnik_LIBRARY=${LibMapnik_LIBRARY}")

if((NOT LibMapnik_FOUND) AND (LibMapnik_INCLUDE_DIRS) AND (LibMapnik_LIBRARIES))
  set(LibMapnik_FOUND True)
endif()

if((NOT LibMapnik_VERSION) AND (LibMapnik_FOUND))
	file(STRINGS "${LibMapnik_INCLUDE_DIR}/version.hpp" _contents REGEX "#define MAPNIK_[A-Z]+_VERSION[ \t]+")
	if (_contents)
		string(REGEX REPLACE ".*#define MAPNIK_MAJOR_VERSION[ \t]+([0-9]+).*" "\\1" LibMapnik_MAJOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define MAPNIK_MINOR_VERSION[ \t]+([0-9]+).*" "\\1" LibMapnik_MINOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define MAPNIK_PATCH_VERSION[ \t]+([0-9]+).*" "\\1" LibMapnik_PATCH_VERSION "${_contents}")

		set(LibMapnik_VERSION ${LibMapnik_MAJOR_VERSION}.${LibMapnik_MINOR_VERSION}.${LibMapnik_PATCH_VERSION})
	endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMapnik
  FOUND_VAR LibMapnik_FOUND
  REQUIRED_VARS LibMapnik_FOUND LibMapnik_INCLUDE_DIRS LibMapnik_LIBRARIES
  VERSION_VAR LibMapnik_VERSION
)

mark_as_advanced(LibMapnik_INCLUDE_DIR LibMapnik_LIBRARY)
