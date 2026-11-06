# - Find INIPARSER
# Find the INIPARSER includes and libraries.
# This module defines:
#  INIPARSER_FOUND        - True if iniparser is found
#  INIPARSER_INCLUDE_DIRS - Include directories for iniparser
#  INIPARSER_LIBRARIES    - Libraries to link against for iniparser
#  INIPARSER_LIBRARY_DIRS - Library search directories for iniparser
#  INIPARSER_VERSION      - Version of iniparser found
#  INIPARSER_USES_CONST   - True if API signatures accept 'const dictionary *'

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_INIPARSER QUIET iniparser)
endif()

# Search for headers
find_path(INIPARSER_INCLUDE_DIR
  NAMES iniparser.h
  HINTS ${PC_INIPARSER_INCLUDE_DIRS}
  PATH_SUFFIXES iniparser
)

# Search for library binary
find_library(INIPARSER_LIBRARY
  NAMES iniparser libiniparser
  HINTS ${PC_INIPARSER_LIBRARY_DIRS}
)

# Assemble include and library lists
set(INIPARSER_INCLUDE_DIRS ${INIPARSER_INCLUDE_DIR})
if(PC_INIPARSER_INCLUDE_DIRS)
  list(APPEND INIPARSER_INCLUDE_DIRS ${PC_INIPARSER_INCLUDE_DIRS})
endif()
if(INIPARSER_INCLUDE_DIRS)
  list(REMOVE_DUPLICATES INIPARSER_INCLUDE_DIRS)
endif()

set(INIPARSER_LIBRARIES ${INIPARSER_LIBRARY})
if(PC_INIPARSER_LIBRARIES AND NOT INIPARSER_LIBRARY)
  set(INIPARSER_LIBRARIES ${PC_INIPARSER_LIBRARIES})
endif()

# Extract library search directories
set(INIPARSER_LIBRARY_DIRS ${PC_INIPARSER_LIBRARY_DIRS})
if(INIPARSER_LIBRARY)
  get_filename_component(INIPARSER_LIB_DIR "${INIPARSER_LIBRARY}" DIRECTORY)
  list(APPEND INIPARSER_LIBRARY_DIRS "${INIPARSER_LIB_DIR}")
endif()
if(INIPARSER_LIBRARY_DIRS)
  list(REMOVE_DUPLICATES INIPARSER_LIBRARY_DIRS)
endif()

# Version detection
if(INIPARSER_INCLUDE_DIR)
  set(_iniparser_header "")
  if(EXISTS "${INIPARSER_INCLUDE_DIR}/version.h")
    set(_iniparser_header "${INIPARSER_INCLUDE_DIR}/version.h")
  elseif(EXISTS "${INIPARSER_INCLUDE_DIR}/iniparser/version.h")
    set(_iniparser_header "${INIPARSER_INCLUDE_DIR}/iniparser/version.h")
  elseif(EXISTS "${INIPARSER_INCLUDE_DIR}/iniparser.h")
    set(_iniparser_header "${INIPARSER_INCLUDE_DIR}/iniparser.h")
  endif()

  if(_iniparser_header)
    file(STRINGS "${_iniparser_header}" _iniparser_ver_str REGEX "#define[ \t]+INIPARSER_VERSION[ \t]+\".*\"")
    if(_iniparser_ver_str)
      string(REGEX REPLACE ".*#define[ \t]+INIPARSER_VERSION[ \t]+\"([^\"]+)\".*" "\\1" INIPARSER_VERSION "${_iniparser_ver_str}")
    else()
      file(STRINGS "${_iniparser_header}" _iniparser_major_str REGEX "#define[ \t]+INIPARSER_MAJOR_VERSION[ \t]+[0-9]+")
      file(STRINGS "${_iniparser_header}" _iniparser_minor_str REGEX "#define[ \t]+INIPARSER_MINOR_VERSION[ \t]+[0-9]+")
      file(STRINGS "${_iniparser_header}" _iniparser_patch_str REGEX "#define[ \t]+INIPARSER_PATCH_VERSION[ \t]+[0-9]+")

      if(_iniparser_major_str AND _iniparser_minor_str AND _iniparser_patch_str)
        string(REGEX REPLACE ".*#define[ \t]+INIPARSER_MAJOR_VERSION[ \t]+([0-9]+).*" "\\1" INIPARSER_MAJOR_VERSION "${_iniparser_major_str}")
        string(REGEX REPLACE ".*#define[ \t]+INIPARSER_MINOR_VERSION[ \t]+([0-9]+).*" "\\1" INIPARSER_MINOR_VERSION "${_iniparser_minor_str}")
        string(REGEX REPLACE ".*#define[ \t]+INIPARSER_PATCH_VERSION[ \t]+([0-9]+).*" "\\1" INIPARSER_PATCH_VERSION "${_iniparser_patch_str}")

        set(INIPARSER_VERSION "${INIPARSER_MAJOR_VERSION}.${INIPARSER_MINOR_VERSION}.${INIPARSER_PATCH_VERSION}")
      endif()
    endif()
  endif()
endif()

if(NOT INIPARSER_VERSION AND PC_INIPARSER_VERSION)
  set(INIPARSER_VERSION ${PC_INIPARSER_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(INIPARSER
  REQUIRED_VARS INIPARSER_LIBRARY INIPARSER_INCLUDE_DIR
  VERSION_VAR INIPARSER_VERSION
)

# Compile check for 'const' API signature
if(INIPARSER_FOUND)
  include(CheckCSourceCompiles)

  set(CMAKE_REQUIRED_FLAGS "-Werror")
  set(CMAKE_REQUIRED_INCLUDES ${INIPARSER_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${INIPARSER_LIBRARIES})

  if(INIPARSER_LIBRARY_DIRS)
    set(CMAKE_REQUIRED_LINK_OPTIONS "-L${INIPARSER_LIBRARY_DIRS}")
  endif()

  check_c_source_compiles("
      #include <iniparser.h>
      int main(void) {
          const dictionary *d = NULL;
          return iniparser_getnsec(d);
      }
  " INIPARSER_USES_CONST)

  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_INCLUDES)
  unset(CMAKE_REQUIRED_LIBRARIES)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
endif()

mark_as_advanced(INIPARSER_INCLUDE_DIR INIPARSER_LIBRARY)
