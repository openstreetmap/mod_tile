# - Find GLIB
# Find the GLIB includes and libraries.
# This module defines:
#  GLIB_FOUND        - True if GLIB is found
#  GLIB_INCLUDE_DIRS - Include directories for GLIB (including glibconfig.h)
#  GLIB_LIBRARIES    - Libraries to link against for GLIB
#  GLIB_LIBRARY_DIRS - Library search directories for GLIB
#  GLIB_VERSION      - Version of GLIB found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_GLIB QUIET glib-2.0)
endif()

# Search for main header (glib.h)
find_path(GLIB_INCLUDE_DIR
  NAMES glib.h
  HINTS ${PC_GLIB_INCLUDE_DIRS}
  PATH_SUFFIXES glib-2.0
)

# Search for architecture-specific header (glibconfig.h)
find_path(GLIB_CONFIG_INCLUDE_DIR
  NAMES glibconfig.h
  HINTS ${PC_GLIB_INCLUDE_DIRS} ${PC_GLIB_LIBRARY_DIRS}
  PATH_SUFFIXES glib-2.0/include lib/glib-2.0/include
)

# Search for library binary
find_library(GLIB_LIBRARY
  NAMES glib-2.0 libglib-2.0
  HINTS ${PC_GLIB_LIBRARY_DIRS}
)

# Assemble include and library lists
set(GLIB_INCLUDE_DIRS ${GLIB_INCLUDE_DIR})
if(GLIB_CONFIG_INCLUDE_DIR)
  list(APPEND GLIB_INCLUDE_DIRS ${GLIB_CONFIG_INCLUDE_DIR})
endif()
if(PC_GLIB_INCLUDE_DIRS)
  list(APPEND GLIB_INCLUDE_DIRS ${PC_GLIB_INCLUDE_DIRS})
endif()
list(REMOVE_DUPLICATES GLIB_INCLUDE_DIRS)

set(GLIB_LIBRARIES ${GLIB_LIBRARY})
if(PC_GLIB_LIBRARIES AND NOT GLIB_LIBRARY)
  set(GLIB_LIBRARIES ${PC_GLIB_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(GLIB_LIBRARY_DIRS ${PC_GLIB_LIBRARY_DIRS})
if(GLIB_LIBRARY)
  get_filename_component(GLIB_LIB_DIR "${GLIB_LIBRARY}" DIRECTORY)
  list(APPEND GLIB_LIBRARY_DIRS "${GLIB_LIB_DIR}")
endif()
if(GLIB_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES GLIB_LIBRARY_DIRS)
endif()

# Version detection from pkg-config
if(PC_GLIB_VERSION)
  set(GLIB_VERSION ${PC_GLIB_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLIB
  REQUIRED_VARS GLIB_LIBRARY GLIB_INCLUDE_DIR GLIB_CONFIG_INCLUDE_DIR
  VERSION_VAR GLIB_VERSION
)

mark_as_advanced(GLIB_INCLUDE_DIR GLIB_CONFIG_INCLUDE_DIR GLIB_LIBRARY)
