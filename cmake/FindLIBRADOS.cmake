# - Find LIBRADOS
# Find the LIBRADOS includes and libraries.
# This module defines:
#  LIBRADOS_FOUND        - True if LIBRADOS is found
#  LIBRADOS_INCLUDE_DIRS - Include directories for LIBRADOS
#  LIBRADOS_LIBRARIES    - Libraries to link against for LIBRADOS
#  LIBRADOS_LIBRARY_DIRS - Library search directories for LIBRADOS
#  LIBRADOS_VERSION      - Version of LIBRADOS found

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBRADOS QUIET rados librados)
endif()

# Search for main header (rados/librados.h or librados.h)
find_path(LIBRADOS_INCLUDE_DIR
  NAMES rados/librados.h librados.h
  HINTS ${PC_LIBRADOS_INCLUDE_DIRS}
  PATH_SUFFIXES rados
)

# Search for library binary
find_library(LIBRADOS_LIBRARY
  NAMES rados librados
  HINTS ${PC_LIBRADOS_LIBRARY_DIRS}
)

# Assemble include and library lists
set(LIBRADOS_INCLUDE_DIRS ${LIBRADOS_INCLUDE_DIR})
if(PC_LIBRADOS_INCLUDE_DIRS)
  list(APPEND LIBRADOS_INCLUDE_DIRS ${PC_LIBRADOS_INCLUDE_DIRS})
endif()
if(LIBRADOS_INCLUDE_DIRS)
  list(REMOVE_DUPLICATES LIBRADOS_INCLUDE_DIRS)
endif()

set(LIBRADOS_LIBRARIES ${LIBRADOS_LIBRARY})
if(PC_LIBRADOS_LIBRARIES AND NOT LIBRADOS_LIBRARY)
  set(LIBRADOS_LIBRARIES ${PC_LIBRADOS_LIBRARIES})
endif()

# Extract library search directories for linker safety
set(LIBRADOS_LIBRARY_DIRS ${PC_LIBRADOS_LIBRARY_DIRS})
if(LIBRADOS_LIBRARY)
  get_filename_component(LIBRADOS_LIB_DIR "${LIBRADOS_LIBRARY}" DIRECTORY)
  list(APPEND LIBRADOS_LIBRARY_DIRS "${LIBRADOS_LIB_DIR}")
endif()
if(LIBRADOS_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES LIBRADOS_LIBRARY_DIRS)
endif()

# Version detection
if(LIBRADOS_INCLUDE_DIR)
  set(_rados_h "")
  if(EXISTS "${LIBRADOS_INCLUDE_DIR}/rados/librados.h")
    set(_rados_h "${LIBRADOS_INCLUDE_DIR}/rados/librados.h")
  elseif(EXISTS "${LIBRADOS_INCLUDE_DIR}/librados.h")
    set(_rados_h "${LIBRADOS_INCLUDE_DIR}/librados.h")
  endif()

  if(_rados_h)
    file(STRINGS "${_rados_h}" _rados_major_str REGEX "#define[ \t]+LIBRADOS_VER_MAJOR[ \t]+[0-9]+")
    file(STRINGS "${_rados_h}" _rados_minor_str REGEX "#define[ \t]+LIBRADOS_VER_MINOR[ \t]+[0-9]+")
    file(STRINGS "${_rados_h}" _rados_extra_str REGEX "#define[ \t]+LIBRADOS_VER_EXTRA[ \t]+[0-9]+")

    if(_rados_major_str AND _rados_minor_str AND _rados_extra_str)
      string(REGEX REPLACE ".*#define[ \t]+LIBRADOS_VER_MAJOR[ \t]+([0-9]+).*" "\\1" LIBRADOS_MAJOR_VERSION "${_rados_major_str}")
      string(REGEX REPLACE ".*#define[ \t]+LIBRADOS_VER_MINOR[ \t]+([0-9]+).*" "\\1" LIBRADOS_MINOR_VERSION "${_rados_minor_str}")
      string(REGEX REPLACE ".*#define[ \t]+LIBRADOS_VER_EXTRA[ \t]+([0-9]+).*" "\\1" LIBRADOS_EXTRA_VERSION "${_rados_extra_str}")

      set(LIBRADOS_VERSION "${LIBRADOS_MAJOR_VERSION}.${LIBRADOS_MINOR_VERSION}.${LIBRADOS_EXTRA_VERSION}")
    endif()
  endif()
endif()

if(NOT LIBRADOS_VERSION AND PC_LIBRADOS_VERSION)
  set(LIBRADOS_VERSION ${PC_LIBRADOS_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBRADOS
  REQUIRED_VARS LIBRADOS_LIBRARY LIBRADOS_INCLUDE_DIR
  VERSION_VAR LIBRADOS_VERSION
)

mark_as_advanced(LIBRADOS_INCLUDE_DIR LIBRADOS_LIBRARY)
