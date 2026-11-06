# - Find APR
# Find the APR includes and libraries.
# This module defines:
#  APR_FOUND        - True if APR is found
#  APR_INCLUDE_DIRS - Include directories for APR
#  APR_LIBRARIES    - Libraries to link against for APR
#  APR_LIBRARY_DIRS - Library search directories for APR
#  APR_VERSION      - Version of APR found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_APR QUIET apr-1)
endif()

# Search for headers
find_path(APR_INCLUDE_DIR
  NAMES apr.h
  HINTS ${PC_APR_INCLUDE_DIRS}
  PATH_SUFFIXES apr-1 apr-1.0
)

# Search for library binary
find_library(APR_LIBRARY
  NAMES apr-1 libapr-1
  HINTS ${PC_APR_LIBRARY_DIRS}
)

# Assemble include and library lists
set(APR_INCLUDE_DIRS ${APR_INCLUDE_DIR})
if(PC_APR_INCLUDE_DIRS)
  list(APPEND APR_INCLUDE_DIRS ${PC_APR_INCLUDE_DIRS})
endif()
list(REMOVE_DUPLICATES APR_INCLUDE_DIRS)

set(APR_LIBRARIES ${APR_LIBRARY})
if(PC_APR_LIBRARIES AND NOT APR_LIBRARY)
  set(APR_LIBRARIES ${PC_APR_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(APR_LIBRARY_DIRS ${PC_APR_LIBRARY_DIRS})
if(APR_LIBRARY)
  get_filename_component(APR_LIB_DIR "${APR_LIBRARY}" DIRECTORY)
  list(APPEND APR_LIBRARY_DIRS "${APR_LIB_DIR}")
endif()
if(APR_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES APR_LIBRARY_DIRS)
endif()

# Version detection from pkg-config
if(PC_APR_VERSION)
  set(APR_VERSION ${PC_APR_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APR
  REQUIRED_VARS APR_LIBRARY APR_INCLUDE_DIR
  VERSION_VAR APR_VERSION
)

mark_as_advanced(APR_INCLUDE_DIR APR_LIBRARY)
