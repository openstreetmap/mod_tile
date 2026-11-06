# - Find CAIRO
# Find the CAIRO includes and libraries.
# This module defines:
#  CAIRO_FOUND        - True if CAIRO is found
#  CAIRO_INCLUDE_DIRS - Include directories for CAIRO
#  CAIRO_LIBRARIES    - Libraries to link against for CAIRO
#  CAIRO_LIBRARY_DIRS - Library search directories for CAIRO
#  CAIRO_VERSION      - Version of CAIRO found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_CAIRO QUIET cairo)
endif()

# Search for headers
find_path(CAIRO_INCLUDE_DIR
  NAMES cairo.h
  HINTS ${PC_CAIRO_INCLUDE_DIRS}
  PATH_SUFFIXES cairo
)

# Search for library binary
find_library(CAIRO_LIBRARY
  NAMES cairo libcairo
  HINTS ${PC_CAIRO_LIBRARY_DIRS}
)

# Assemble include and library lists
set(CAIRO_INCLUDE_DIRS ${CAIRO_INCLUDE_DIR})
if(PC_CAIRO_INCLUDE_DIRS)
  list(APPEND CAIRO_INCLUDE_DIRS ${PC_CAIRO_INCLUDE_DIRS})
endif()
list(REMOVE_DUPLICATES CAIRO_INCLUDE_DIRS)

set(CAIRO_LIBRARIES ${CAIRO_LIBRARY})
if(PC_CAIRO_LIBRARIES AND NOT CAIRO_LIBRARY)
  set(CAIRO_LIBRARIES ${PC_CAIRO_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(CAIRO_LIBRARY_DIRS ${PC_CAIRO_LIBRARY_DIRS})
if(CAIRO_LIBRARY)
  get_filename_component(CAIRO_LIB_DIR "${CAIRO_LIBRARY}" DIRECTORY)
  list(APPEND CAIRO_LIBRARY_DIRS "${CAIRO_LIB_DIR}")
endif()
if(CAIRO_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES CAIRO_LIBRARY_DIRS)
endif()

# Version detection from pkg-config
if(PC_CAIRO_VERSION)
  set(CAIRO_VERSION ${PC_CAIRO_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CAIRO
  REQUIRED_VARS CAIRO_LIBRARY CAIRO_INCLUDE_DIR
  VERSION_VAR CAIRO_VERSION
)

mark_as_advanced(CAIRO_INCLUDE_DIR CAIRO_LIBRARY)
