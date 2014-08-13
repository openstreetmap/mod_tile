# SYNOPSIS
#
#   AX_LIB_MAPNIK()
#
# DESCRIPTION
#
#   This macro provides tests of availability of mapnik 'libmapnik' library
#   of particular version or newer.
#
#   
#
#   The --with-mapnik option takes one of three possible values:
#
#   no - do not check for mapnik library
#
#   yes - do check for mapnik library in standard locations (xml2-config
#   should be in the PATH)
#
#   path - complete path to mapnik-config utility, use this option if mapnik-config
#   can't be found in the PATH
#
#   This macro calls:
#
#     AC_SUBST(MAPNIK_CFLAGS)
#     AC_SUBST(MAPNIK_LDFLAGS)
#
#   And sets:
#
#     HAVE_MAPNIK
#
# LICENSE
#
#   Copyright (c) 2012
#   Copyright (c) 2009 Hartmut Holzgraefe <hartmut@php.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_LIB_MAPNIK],
[
    AC_ARG_WITH([libmapnik],
        AC_HELP_STRING([--with-libmapnik=@<:@ARG@:>@],
            [use mapnik library @<:@default=yes@:>@, optionally specify path to mapnik-config]
        ),
        [
        if test "$withval" = "no"; then
            want_libmapnik="no"
        elif test "$withval" = "yes"; then
            want_libmapnik="yes"
        else
            want_libmapnik="yes"
            MAPNIK_CONFIG="$withval"
        fi
        ],
        [want_libmapnik="yes"]
    )

    MAPNIK_CFLAGS=""
    MAPNIK_INCLUDES=""
    MAPNIK_LDFLAGS=""
    MAPNIK_VERSION=""

    dnl
    dnl Check mapnik libraries (libmapnik)
    dnl

    if test "$want_libmapnik" = "yes"; then

        if test -z "$MAPNIK_CONFIG" -o test; then
            AC_PATH_PROG([MAPNIK_CONFIG], [mapnik-config], [])
        fi

        if test ! -x "$MAPNIK_CONFIG"; then
            # mapnik version < 2.0 did not have mapnik-config, so need to manually figure out the configuration
            AC_CHECK_HEADER(mapnik/version.hpp,[],[AC_MSG_ERROR([Did not find mapnik])])
            AC_CHECK_LIB([mapnik],[main],[MAPNIK_LDFLAGS="-lmapnik" found_libmapnik="yes"])
            AC_CHECK_LIB([mapnik2],[main],[MAPNIK_LDFLAGS="-lmapnik2" found_libmapnik="yes"])
            if test "$found_libmapnik" = "yes"; then
                AC_MSG_CHECKING([for mapnik libraries])			
                AC_MSG_RESULT([yes])
            else
                AC_MSG_ERROR([Did not find usable mapnik library])
            fi
        else
            if test "$MAPNIK_CONFIG" != "no"; then
	            AC_MSG_CHECKING([for mapnik libraries])

        	    MAPNIK_CFLAGS="`$MAPNIK_CONFIG --cflags`"
                MAPNIK_INCLUDES="`$MAPNIK_CONFIG --includes`"
                if test $? -ne 0; then
                    MAPNIK_INCLUDES=""
                fi
                    MAPNIK_INCLUDES="$MAPNIK_INCLUDES `$MAPNIK_CONFIG --dep-includes`"
        	    MAPNIK_LDFLAGS="`$MAPNIK_CONFIG --libs`"

        	    MAPNIK_VERSION=`$MAPNIK_CONFIG --version`

        	    AC_DEFINE([HAVE_MAPNIK], [1],
        	        [Define to 1 if mapnik libraries are available])

        	    found_libmapnik="yes"
	            AC_MSG_RESULT([yes])
        	else
	            found_libmapnik="no"
	            AC_MSG_RESULT([no])
	        fi
        fi
    fi

    AC_SUBST([MAPNIK_VERSION])
    AC_SUBST([MAPNIK_CFLAGS])
    AC_SUBST([MAPNIK_INCLUDES])
    AC_SUBST([MAPNIK_LDFLAGS])
])

