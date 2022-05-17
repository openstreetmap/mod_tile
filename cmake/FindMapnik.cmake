# - Find Mapnik
# Find the Mapnik includes and libraries.
# This module defines:
#  Mapnik_FOUND
#  Mapnik_INCLUDE_DIRS
#  Mapnik_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(Mapnik QUIET mapnik)

find_path(Mapnik_INCLUDE_DIR
  NAMES version.hpp
  PATHS ${Mapnik_INCLUDE_DIRS}
  PATH_SUFFIXES mapnik
)

if((NOT Mapnik_INCLUDE_DIRS) AND (Mapnik_INCLUDE_DIR))
  set(Mapnik_INCLUDE_DIRS ${Mapnik_INCLUDE_DIR})
endif()

find_library(Mapnik_LIBRARY
  NAMES ${Mapnik_LIBRARIES} mapnik
)

if((NOT Mapnik_LIBRARIES) AND (Mapnik_LIBRARY))
  set(Mapnik_LIBRARIES ${Mapnik_LIBRARY})
endif()

message(VERBOSE "Mapnik_INCLUDE_DIRS=${Mapnik_INCLUDE_DIRS}")
message(VERBOSE "Mapnik_INCLUDE_DIR=${Mapnik_INCLUDE_DIR}")
message(VERBOSE "Mapnik_LIBRARIES=${Mapnik_LIBRARIES}")
message(VERBOSE "Mapnik_LIBRARY=${Mapnik_LIBRARY}")

if((NOT Mapnik_FOUND) AND (Mapnik_INCLUDE_DIRS) AND (Mapnik_LIBRARIES))
  set(Mapnik_FOUND True)
endif()

if((NOT Mapnik_VERSION) AND (Mapnik_FOUND))
	file(STRINGS "${Mapnik_INCLUDE_DIR}/version.hpp" _contents REGEX "#define MAPNIK_[A-Z]+_VERSION[ \t]+")
	if (_contents)
		string(REGEX REPLACE ".*#define MAPNIK_MAJOR_VERSION[ \t]+([0-9]+).*" "\\1" Mapnik_MAJOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define MAPNIK_MINOR_VERSION[ \t]+([0-9]+).*" "\\1" Mapnik_MINOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define MAPNIK_PATCH_VERSION[ \t]+([0-9]+).*" "\\1" Mapnik_PATCH_VERSION "${_contents}")

		set(Mapnik_VERSION ${Mapnik_MAJOR_VERSION}.${Mapnik_MINOR_VERSION}.${Mapnik_PATCH_VERSION})
	endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mapnik
  FOUND_VAR Mapnik_FOUND
  REQUIRED_VARS Mapnik_FOUND Mapnik_INCLUDE_DIRS Mapnik_LIBRARIES
  VERSION_VAR Mapnik_VERSION
)

mark_as_advanced(Mapnik_INCLUDE_DIR Mapnik_LIBRARY)
