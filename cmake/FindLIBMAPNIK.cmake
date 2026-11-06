# - Find LIBMAPNIK
# Find the LIBMAPNIK includes and libraries.
# This module defines:
#  LIBMAPNIK_FOUND        - True if LIBMAPNIK is found
#  LIBMAPNIK_INCLUDE_DIRS - Include directories for LIBMAPNIK
#  LIBMAPNIK_LIBRARIES    - Libraries to link against for LIBMAPNIK
#  LIBMAPNIK_LIBRARY_DIRS - Library search directories for LIBMAPNIK
#  LIBMAPNIK_VERSION      - Version of LIBMAPNIK found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBMAPNIK QUIET libmapnik mapnik)
endif()

# Search for main header (mapnik/version.hpp or version.hpp)
find_path(LIBMAPNIK_INCLUDE_DIR
  NAMES mapnik/version.hpp version.hpp
  HINTS ${PC_LIBMAPNIK_INCLUDE_DIRS}
  PATH_SUFFIXES mapnik
)

# Search for library binary
find_library(LIBMAPNIK_LIBRARY
  NAMES mapnik libmapnik
  HINTS ${PC_LIBMAPNIK_LIBRARY_DIRS}
)

# Assemble include and library lists
set(LIBMAPNIK_INCLUDE_DIRS ${LIBMAPNIK_INCLUDE_DIR})
if(PC_LIBMAPNIK_INCLUDE_DIRS)
  list(APPEND LIBMAPNIK_INCLUDE_DIRS ${PC_LIBMAPNIK_INCLUDE_DIRS})
endif()
if(LIBMAPNIK_INCLUDE_DIRS)
  list(REMOVE_DUPLICATES LIBMAPNIK_INCLUDE_DIRS)
endif()

set(LIBMAPNIK_LIBRARIES ${LIBMAPNIK_LIBRARY})
if(PC_LIBMAPNIK_LIBRARIES AND NOT LIBMAPNIK_LIBRARY)
  set(LIBMAPNIK_LIBRARIES ${PC_LIBMAPNIK_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(LIBMAPNIK_LIBRARY_DIRS ${PC_LIBMAPNIK_LIBRARY_DIRS})
if(LIBMAPNIK_LIBRARY)
  get_filename_component(LIBMAPNIK_LIB_DIR "${LIBMAPNIK_LIBRARY}" DIRECTORY)
  list(APPEND LIBMAPNIK_LIBRARY_DIRS "${LIBMAPNIK_LIB_DIR}")
endif()
if(LIBMAPNIK_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES LIBMAPNIK_LIBRARY_DIRS)
endif()

# Version detection
if(LIBMAPNIK_INCLUDE_DIR)
  set(_version_hpp "")
  if(EXISTS "${LIBMAPNIK_INCLUDE_DIR}/mapnik/version.hpp")
    set(_version_hpp "${LIBMAPNIK_INCLUDE_DIR}/mapnik/version.hpp")
  elseif(EXISTS "${LIBMAPNIK_INCLUDE_DIR}/version.hpp")
    set(_version_hpp "${LIBMAPNIK_INCLUDE_DIR}/version.hpp")
  endif()

  if(_version_hpp)
    file(STRINGS "${_version_hpp}" _mapnik_major_str REGEX "#define[ \t]+MAPNIK_MAJOR_VERSION[ \t]+[0-9]+")
    file(STRINGS "${_version_hpp}" _mapnik_minor_str REGEX "#define[ \t]+MAPNIK_MINOR_VERSION[ \t]+[0-9]+")
    file(STRINGS "${_version_hpp}" _mapnik_patch_str REGEX "#define[ \t]+MAPNIK_PATCH_VERSION[ \t]+[0-9]+")

    if(_mapnik_major_str AND _mapnik_minor_str AND _mapnik_patch_str)
      string(REGEX REPLACE ".*#define[ \t]+MAPNIK_MAJOR_VERSION[ \t]+([0-9]+).*" "\\1" LIBMAPNIK_MAJOR_VERSION "${_mapnik_major_str}")
      string(REGEX REPLACE ".*#define[ \t]+MAPNIK_MINOR_VERSION[ \t]+([0-9]+).*" "\\1" LIBMAPNIK_MINOR_VERSION "${_mapnik_minor_str}")
      string(REGEX REPLACE ".*#define[ \t]+MAPNIK_PATCH_VERSION[ \t]+([0-9]+).*" "\\1" LIBMAPNIK_PATCH_VERSION "${_mapnik_patch_str}")

      set(LIBMAPNIK_VERSION "${LIBMAPNIK_MAJOR_VERSION}.${LIBMAPNIK_MINOR_VERSION}.${LIBMAPNIK_PATCH_VERSION}")
    endif()
  endif()
endif()

if(NOT LIBMAPNIK_VERSION AND PC_LIBMAPNIK_VERSION)
  set(LIBMAPNIK_VERSION ${PC_LIBMAPNIK_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBMAPNIK
  REQUIRED_VARS LIBMAPNIK_LIBRARY LIBMAPNIK_INCLUDE_DIR
  VERSION_VAR LIBMAPNIK_VERSION
)

mark_as_advanced(LIBMAPNIK_INCLUDE_DIR LIBMAPNIK_LIBRARY)
