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
    MAPNIK_LDFLAGS=""
    MAPNIK_VERSION=""

    dnl
    dnl Check mapnik libraries (libmapnik)
    dnl

    if test "$want_libmapnik" = "yes"; then

        if test -z "$MAPNIK_CONFIG" -o test; then
            AC_PATH_PROG([MAPNIK_CONFIG], [mapnik-config], [])
        fi

        if test "$MAPNIK_CONFIG" != "no"; then
            AC_MSG_CHECKING([for mapnik libraries])

            MAPNIK_CFLAGS="`$MAPNIK_CONFIG --cflags`"
            MAPNIK_LDFLAGS="`$MAPNIK_CONFIG --libs` `$MAPNIK_CONFIG --ldflags` `$MAPNIK_CONFIG --dep-libs`"
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

    AC_SUBST([MAPNIK_VERSION])
    AC_SUBST([MAPNIK_CFLAGS])
    AC_SUBST([MAPNIK_LDFLAGS])
])

