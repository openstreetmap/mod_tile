#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AX_CONFIG_NICE
AC_INIT(mod_tile, 0.1, http://trac.openstreetmap.org)
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT
AC_CONFIG_SRCDIR([src/convert_meta.c])
AC_CONFIG_HEADERS([includes/config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CC_C99

dnl Find C++ compiler 
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no) 
if test "$HAVE_CXX" = "no"  
then 
  AC_MSG_ERROR([Could not find a c++ compiler]); 
fi

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h syslog.h unistd.h utime.h paths.h sys/cdefs.h sys/loadavg.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME

AX_PTHREAD(,[AC_MSG_ERROR([no])])
AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(inet_ntoa, nsl)
AC_SEARCH_LIBS(gethostbyname, resolv nsl)
AC_SEARCH_LIBS(pow,m)
AC_SEARCH_LIBS(clock_gettime,[rt posix4])
AX_LIB_MAPNIK
AX_ENABLE_LIBMEMCACHED
LIBCURL_CHECK_CONFIG
AC_CHECK_LIB(rados, rados_version, [
    AC_DEFINE([HAVE_LIBRADOS], [1], [Have found librados])
    LIBRADOS_LDFLAGS='-lrados'
    AC_SUBST(LIBRADOS_LDFLAGS)
][])

AC_ARG_WITH([libs3],
    [AS_HELP_STRING([--with-libs3[=DIR]],[path to libs3])],
    [
    libs3_dir="$withval"
    if test "$libs3_dir" != "no"; then
      if test "$libs3_dir" != "yes"; then
        AC_MSG_CHECKING([for libs3])
        if test -f "$libs3_dir/include/libs3.h"; then
          AC_DEFINE([HAVE_LIBS3], [1], [Have found libs3])
          LIBS3_LDFLAGS="-L$libs3_dir/lib -ls3"
          AC_SUBST(LIBS3_LDFLAGS)
          LIBS3_CFLAGS="-I$libs3_dir/include"
          AC_SUBST(LIBS3_CFLAGS)
          AC_MSG_RESULT($libs3_dir)
        else
          AC_MSG_ERROR([no libs3 found at $libs3_dir])
        fi
      else
	AC_CHECK_LIB(s3, S3_deinitialize,
        [
            AC_DEFINE([HAVE_LIBS3], [1], [Have found libs3])
	    LIBS3_LDFLAGS='-ls3'
            AC_SUBST(LIBS3_LDFLAGS)
        ],
        [
            AC_MSG_ERROR([no libs3 found])
        ])
      fi
    fi
    ],
    [
	AC_CHECK_LIB(s3, S3_deinitialize,
        [
            AC_DEFINE([HAVE_LIBS3], [1], [Have found libs3])
	    LIBS3_LDFLAGS='-ls3'
            AC_SUBST(LIBS3_LDFLAGS)
        ],
        [])
    ])

AC_CHECK_FUNCS([bzero gethostbyname gettimeofday inet_ntoa memset mkdir pow select socket strchr strdup strerror strrchr strstr strtol strtoul utime],[],[AC_MSG_ERROR([One of the required functions was not found])])
AC_CHECK_FUNCS([daemon getloadavg],[],[])

AC_ARG_WITH(apxs,
    [  --with-apxs=PATH        path to Apache apxs],
    [
        if test "$withval" = "yes"; then
            AC_CHECK_PROGS(APXS, apxs apxs2 /opt/local/apache2/bin/apxs, reject)
        else
            APXS=$withval
            AC_SUBST(APXS)
        fi
    ],
    [
        AC_CHECK_PROGS(APXS, apxs apxs2 /opt/local/apache2/bin/apxs, reject)
    ])
if test "$APXS" = "reject"; then
  AC_MSG_ERROR([Could not find apxs on the path.])
fi

AC_SEARCH_LIBS([iniparser_load], [iniparser], [
	AC_CHECK_HEADERS([iniparser.h], [have_system_iniparser=yes])
])
AM_CONDITIONAL([SYSTEM_LIBINIPARSER], [test "x$have_system_iniparser" = "xyes"])
if test "x$have_system_iniparser" = "xyes"; then
    AC_SUBST(SYSTEM_LIBINIPARSER, 1)
    AC_MSG_NOTICE([Using installed iniparser])
else
    AC_SUBST(SYSTEM_LIBINIPARSER, 0)
    AC_CONFIG_FILES(iniparser3.0b/Makefile)
    AC_MSG_NOTICE([Building iniparser])
fi
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
