# - Find INIPARSER
# Find the INIPARSER includes and libraries.
# This module defines:
#  INIPARSER_FOUND        - True if iniparser is found
#  INIPARSER_INCLUDE_DIRS - Include directories for iniparser
#  INIPARSER_LIBRARIES    - Libraries to link against for iniparser
#  INIPARSER_USES_CONST   - True if API signatures accept 'const dictionary *'

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_INIPARSER QUIET iniparser)
endif()

# 1. Search for headers
find_path(INIPARSER_INCLUDE_DIR
  NAMES iniparser.h
  HINTS ${PC_INIPARSER_INCLUDE_DIRS}
  PATH_SUFFIXES iniparser
)

# 2. Search for library binary
find_library(INIPARSER_LIBRARY
  NAMES iniparser libiniparser
  HINTS ${PC_INIPARSER_LIBRARY_DIRS}
)

# 3. Assemble include and library lists
set(INIPARSER_INCLUDE_DIRS ${INIPARSER_INCLUDE_DIR})
if(PC_INIPARSER_INCLUDE_DIRS)
  list(APPEND INIPARSER_INCLUDE_DIRS ${PC_INIPARSER_INCLUDE_DIRS})
endif()
list(REMOVE_DUPLICATES INIPARSER_INCLUDE_DIRS)

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

# 4. Standard validation check
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(INIPARSER
  REQUIRED_VARS INIPARSER_LIBRARY INIPARSER_INCLUDE_DIR
)

# 5. Compile check for 'const' API signature
if(INIPARSER_FOUND)
  include(CheckCSourceCompiles)

  set(CMAKE_REQUIRED_FLAGS "-Werror")
  set(CMAKE_REQUIRED_INCLUDES ${INIPARSER_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${INIPARSER_LIBRARIES})

  # Pass link search directories so the test linker finds -liniparser
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
