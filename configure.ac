#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AX_CONFIG_NICE
AC_INIT(mod_tile, 0.1, http://trac.openstreetmap.org)
AM_INIT_AUTOMAKE(mod_tile,0.1)
LT_INIT
AC_CONFIG_SRCDIR([convert_meta.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CC_C99

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

AC_CHECK_FT2
AX_PTHREAD(,[AC_MSG_ERROR([no])])
AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(inet_ntoa, nsl)
AC_SEARCH_LIBS(gethostbyname, resolv nsl)
AC_SEARCH_LIBS(pow,m)
AX_BOOST_BASE
AC_CHECK_ICU(4.2)
AX_LIB_MAPNIK
AC_CHECK_FUNCS([bzero gethostbyname gettimeofday inet_ntoa memset mkdir pow select socket strchr strdup strerror strrchr strstr strtol strtoul utime],[],[AC_MSG_ERROR([One of the required functions was not found])])
AC_CHECK_FUNCS([daemon getloadavg],[],[])

AC_ARG_WITH(apxs,
    [  --with-apxs=PATH        path to Apache apxs],
    [
        if test "$withval" = "yes"; then
            AC_CHECK_PROGS(APXS, apxs2 /usr/sbin/apxs2, reject)
        else
            APXS=$withval
            AC_SUBST(APXS)
        fi
    ],
    [
        AC_CHECK_PROGS(APXS, apxs2 /usr/sbin/apxs2, reject)
    ])
if test "$APXS" = "reject"; then
  AC_MSG_ERROR([Could not find apxs on the path.])
fi

AC_CONFIG_FILES(Makefile iniparser3.0b/Makefile)
AC_OUTPUT
