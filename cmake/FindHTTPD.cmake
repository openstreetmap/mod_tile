# - Find HTTPD
# Find the HTTPD includes and header paths.
# This module defines:
#  HTTPD_FOUND        - True if HTTPD headers are found
#  HTTPD_INCLUDE_DIRS - Include directories for HTTPD
#  HTTPD_VERSION      - Version of HTTPD found (parsed from ap_release.h)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_HTTPD QUIET httpd)
endif()

# Search for main header (httpd.h)
find_path(HTTPD_INCLUDE_DIR
  NAMES httpd.h
  HINTS ${PC_HTTPD_INCLUDE_DIRS}
  PATH_SUFFIXES apache2 apache24 httpd
)

# Assemble include list
set(HTTPD_INCLUDE_DIRS ${HTTPD_INCLUDE_DIR})
if(PC_HTTPD_INCLUDE_DIRS)
  list(APPEND HTTPD_INCLUDE_DIRS ${PC_HTTPD_INCLUDE_DIRS})
endif()
if(HTTPD_INCLUDE_DIRS)
  list(REMOVE_DUPLICATES HTTPD_INCLUDE_DIRS)
endif()

# Version detection
if(HTTPD_INCLUDE_DIR AND EXISTS "${HTTPD_INCLUDE_DIR}/ap_release.h")
  file(STRINGS "${HTTPD_INCLUDE_DIR}/ap_release.h" _ap_major_str REGEX "#define[ \t]+AP_SERVER_MAJORVERSION_NUMBER[ \t]+[0-9]+")
  file(STRINGS "${HTTPD_INCLUDE_DIR}/ap_release.h" _ap_minor_str REGEX "#define[ \t]+AP_SERVER_MINORVERSION_NUMBER[ \t]+[0-9]+")
  file(STRINGS "${HTTPD_INCLUDE_DIR}/ap_release.h" _ap_patch_str REGEX "#define[ \t]+AP_SERVER_PATCHLEVEL_NUMBER[ \t]+[0-9]+")

  if(_ap_major_str AND _ap_minor_str AND _ap_patch_str)
    string(REGEX REPLACE ".*#define[ \t]+AP_SERVER_MAJORVERSION_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_MAJOR_VERSION "${_ap_major_str}")
    string(REGEX REPLACE ".*#define[ \t]+AP_SERVER_MINORVERSION_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_MINOR_VERSION "${_ap_minor_str}")
    string(REGEX REPLACE ".*#define[ \t]+AP_SERVER_PATCHLEVEL_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_PATCH_VERSION "${_ap_patch_str}")

    set(HTTPD_VERSION "${HTTPD_MAJOR_VERSION}.${HTTPD_MINOR_VERSION}.${HTTPD_PATCH_VERSION}")
  endif()
endif()

if(NOT HTTPD_VERSION AND PC_HTTPD_VERSION)
  set(HTTPD_VERSION ${PC_HTTPD_VERSION})
endif()

# Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HTTPD
  REQUIRED_VARS HTTPD_INCLUDE_DIR
  VERSION_VAR HTTPD_VERSION
)

mark_as_advanced(HTTPD_INCLUDE_DIR)
