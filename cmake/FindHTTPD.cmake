# - Find HTTPD
# Find the HTTPD includes and libraries.
# This module defines:
#  HTTPD_FOUND
#  HTTPD_INCLUDE_DIRS

find_package(PkgConfig QUIET)
pkg_check_modules(HTTPD QUIET httpd)

find_path(HTTPD_INCLUDE_DIR
  NAMES httpd.h
  PATHS ${HTTPD_INCLUDE_DIRS}
  PATH_SUFFIXES apache2 apache24 httpd
)

if((NOT HTTPD_INCLUDE_DIRS) AND (HTTPD_INCLUDE_DIR))
  set(HTTPD_INCLUDE_DIRS ${HTTPD_INCLUDE_DIR})
elseif(HTTPD_INCLUDE_DIRS AND HTTPD_INCLUDE_DIR)
  list(APPEND HTTPD_INCLUDE_DIRS ${HTTPD_INCLUDE_DIR})
endif()

message(VERBOSE "HTTPD_INCLUDE_DIRS=${HTTPD_INCLUDE_DIRS}")
message(VERBOSE "HTTPD_INCLUDE_DIR=${HTTPD_INCLUDE_DIR}")

if((NOT HTTPD_FOUND) AND (HTTPD_INCLUDE_DIRS))
  set(HTTPD_FOUND True)
endif()

if((NOT HTTPD_VERSION) AND (HTTPD_FOUND))
	file(STRINGS "${HTTPD_INCLUDE_DIR}/ap_release.h" _contents REGEX "#define AP_SERVER_[A-Z]+_NUMBER[ \t]+")
	if (_contents)
		string(REGEX REPLACE ".*#define AP_SERVER_MAJORVERSION_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_MAJOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define AP_SERVER_MINORVERSION_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_MINOR_VERSION "${_contents}")
		string(REGEX REPLACE ".*#define AP_SERVER_PATCHLEVEL_NUMBER[ \t]+([0-9]+).*" "\\1" HTTPD_PATCH_VERSION "${_contents}")

		set(HTTPD_VERSION ${HTTPD_MAJOR_VERSION}.${HTTPD_MINOR_VERSION}.${HTTPD_PATCH_VERSION})
	endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HTTPD
  FOUND_VAR HTTPD_FOUND
  REQUIRED_VARS HTTPD_FOUND HTTPD_INCLUDE_DIRS
  VERSION_VAR HTTPD_VERSION
)

mark_as_advanced(HTTPD_INCLUDE_DIR)
