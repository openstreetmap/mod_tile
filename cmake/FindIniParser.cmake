# - Find IniParser
# Find the IniParser includes and libraries.
# This module defines:
#  IniParser_FOUND
#  IniParser_INCLUDE_DIRS
#  IniParser_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(IniParser QUIET iniparser)

find_path(IniParser_INCLUDE_DIR
  NAMES iniparser.h
  PATHS ${IniParser_INCLUDE_DIRS}
  PATH_SUFFIXES iniparser
)

if((NOT IniParser_INCLUDE_DIRS) AND (IniParser_INCLUDE_DIR))
  set(IniParser_INCLUDE_DIRS ${IniParser_INCLUDE_DIR})
elseif(IniParser_INCLUDE_DIRS AND IniParser_INCLUDE_DIR)
  list(APPEND IniParser_INCLUDE_DIRS ${IniParser_INCLUDE_DIR})
endif()

find_library(IniParser_LIBRARY
  NAMES ${IniParser_LIBRARIES} iniparser
)

if((NOT IniParser_LIBRARIES) AND (IniParser_LIBRARY))
  set(IniParser_LIBRARIES ${IniParser_LIBRARY})
elseif(IniParser_LIBRARIES AND IniParser_LIBRARY)
  list(APPEND IniParser_LIBRARIES ${IniParser_LIBRARY})
endif()

message(VERBOSE "IniParser_INCLUDE_DIRS=${IniParser_INCLUDE_DIRS}")
message(VERBOSE "IniParser_INCLUDE_DIR=${IniParser_INCLUDE_DIR}")
message(VERBOSE "IniParser_LIBRARIES=${IniParser_LIBRARIES}")
message(VERBOSE "IniParser_LIBRARY=${IniParser_LIBRARY}")

if((NOT IniParser_FOUND) AND (IniParser_INCLUDE_DIRS) AND (IniParser_LIBRARIES))
  set(IniParser_FOUND True)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IniParser
  FOUND_VAR IniParser_FOUND
  REQUIRED_VARS IniParser_FOUND IniParser_INCLUDE_DIRS IniParser_LIBRARIES
)

mark_as_advanced(IniParser_INCLUDE_DIR IniParser_LIBRARY)
