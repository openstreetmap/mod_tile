# - Find GLib
# Find the GLib includes and libraries.
# This module defines:
#  GLib_FOUND
#  GLib_INCLUDE_DIRS
#  GLib_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(GLib QUIET glib-2.0)

find_path(GLib_INCLUDE_DIR
  NAMES glib.h
  PATHS ${GLib_INCLUDE_DIRS}
  PATH_SUFFIXES glib-2.0
)

if((NOT GLib_INCLUDE_DIRS) AND (GLib_INCLUDE_DIR))
  set(GLib_INCLUDE_DIRS ${GLib_INCLUDE_DIR})
elseif(GLib_INCLUDE_DIRS AND GLib_INCLUDE_DIR)
  list(APPEND GLib_INCLUDE_DIRS ${GLib_INCLUDE_DIR})
endif()

find_library(GLib_LIBRARY
  NAMES ${GLib_LIBRARIES} glib-2.0
)

if((NOT GLib_LIBRARIES) AND (GLib_LIBRARY))
  set(GLib_LIBRARIES ${GLib_LIBRARY})
elseif(GLib_LIBRARIES AND GLib_LIBRARY)
  list(APPEND GLib_LIBRARIES ${GLib_LIBRARY})
endif()

message(VERBOSE "GLib_INCLUDE_DIRS=${GLib_INCLUDE_DIRS}")
message(VERBOSE "GLib_INCLUDE_DIR=${GLib_INCLUDE_DIR}")
message(VERBOSE "GLib_LIBRARIES=${GLib_LIBRARIES}")
message(VERBOSE "GLib_LIBRARY=${GLib_LIBRARY}")

if((NOT GLib_FOUND) AND (GLib_INCLUDE_DIRS) AND (GLib_LIBRARIES))
  set(GLib_FOUND True)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLib
  FOUND_VAR GLib_FOUND
  REQUIRED_VARS GLib_FOUND GLib_INCLUDE_DIRS GLib_LIBRARIES
  VERSION_VAR GLib_VERSION
)

mark_as_advanced(GLib_INCLUDE_DIR GLib_LIBRARY)
