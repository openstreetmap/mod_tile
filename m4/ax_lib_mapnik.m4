# SYNOPSIS
#
#   AX_LIB_MAPNIK([MINIMUM-VERSION])
#
# DESCRIPTION
#
#   This macro provides tests of availability of mapnik 'libmapnik' library
#   of particular version or newer.
#
#   AX_LIB_MAPNIK macro takes only one argument which is optional. If
#   there is no required version passed, then macro does not run version
#   test.
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
#     AC_SUBST(MAPNIK_VERSION)
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

        if test ! -x "$MAPNIK_CONFIG"; then
            AC_MSG_ERROR([$MAPNIK_CONFIG does not exist or it is not an exectuable file])
            MAPNIK_CONFIG="no"
            found_libmapnik="no"
        fi

        if test "$MAPNIK_CONFIG" != "no"; then
            AC_MSG_CHECKING([for mapnik libraries])

            MAPNIK_CFLAGS="`$MAPNIK_CONFIG --cflags`"
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

    dnl
    dnl Check if required version of mapnik is available
    dnl


    libmapnik_version_req=ifelse([$1], [], [], [$1])


    if test "$found_libmapnik" = "yes" -a -n "$libmapnik_version_req"; then

        AC_MSG_CHECKING([if mapnik version is >= $libmapnik_version_req])

        dnl Decompose required version string of mapnik
        dnl and calculate its number representation
        libxml2_version_req_major=`expr $libxml2_version_req : '\([[0-9]]*\)'`
        libxml2_version_req_minor=`expr $libxml2_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
        libxml2_version_req_micro=`expr $libxml2_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
        if test "x$libxml2_version_req_micro" = "x"; then
            libxml2_version_req_micro="0"
        fi

        libxml2_version_req_number=`expr $libxml2_version_req_major \* 1000000 \
                                   \+ $libxml2_version_req_minor \* 1000 \
                                   \+ $libxml2_version_req_micro`

        dnl Decompose version string of installed PostgreSQL
        dnl and calculate its number representation
        libxml2_version_major=`expr $XML2_VERSION : '\([[0-9]]*\)'`
        libxml2_version_minor=`expr $XML2_VERSION : '[[0-9]]*\.\([[0-9]]*\)'`
        libxml2_version_micro=`expr $XML2_VERSION : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
        if test "x$libxml2_version_micro" = "x"; then
            libxml2_version_micro="0"
        fi

        libxml2_version_number=`expr $libxml2_version_major \* 1000000 \
                                   \+ $libxml2_version_minor \* 1000 \
                                   \+ $libxml2_version_micro`

        libxml2_version_check=`expr $libxml2_version_number \>\= $libxml2_version_req_number`
        if test "$libxml2_version_check" = "1"; then
            AC_MSG_RESULT([yes])
        else
            AC_MSG_RESULT([no])
        fi
    fi

    AC_SUBST([MAPNIK_VERSION])
    AC_SUBST([MAPNIK_CFLAGS])
    AC_SUBST([MAPNIK_LDFLAGS])
])

