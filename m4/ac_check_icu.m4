dnl @synopsis AC_CHECK_ICU(version, action-if, action-if-not)
dnl
dnl @summary check for ICU of sufficient version by looking at icu-config
dnl
dnl Defines ICU_LIBS, ICU_CFLAGS, ICU_CXXFLAGS. See icu-config(1) man
dnl page.
dnl
dnl modified 2012-01-22 for mod_tile
dnl
dnl @category InstalledPackages
dnl @author Akos Maroy <darkeye@tyrell.hu>
dnl @version 2005-09-20
dnl @license AllPermissive

AC_DEFUN([AC_CHECK_ICU], [
  succeeded=no

  AC_ARG_WITH([icu],
        AC_HELP_STRING([--with-icu=@<:@ARG@:>@],
            [use mapnik library @<:@default=yes@:>@, optionally specify path to icu-config]
        ),
        [
        if test "$withval" = "no"; then
            want_icu="no"
        elif test "$withval" = "yes"; then
            want_icu="yes"
        else
            want_libmapnik="yes"
            ICU_CONFIG="$withval"
        fi
        ],
        [want_icu="yes"]
    )

  if test -z "$ICU_CONFIG"; then
    AC_PATH_PROG(ICU_CONFIG, icu-config, no)
  fi

  if test "$ICU_CONFIG" = "no" ; then
    echo "*** The icu-config script could not be found. Make sure it is"
    echo "*** in your path, and that taglib is properly installed."
    echo "*** Or see http://ibm.com/software/globalization/icu/"
  else
    ICU_VERSION=`$ICU_CONFIG --version`
    AC_MSG_CHECKING(for ICU >= $1)
        VERSION_CHECK=`expr $ICU_VERSION \>\= $1`
        if test "$VERSION_CHECK" = "1" ; then
            AC_MSG_RESULT(yes)
            succeeded=yes

            AC_MSG_CHECKING(ICU_CPPFLAGS)
            ICU_CPPFLAGS=`$ICU_CONFIG --cppflags`
            AC_MSG_RESULT($ICU_CPPFLAGS)

            AC_MSG_CHECKING(ICU_LIBS)
            ICU_LIBS=`$ICU_CONFIG --ldflags`
            AC_MSG_RESULT($ICU_LIBS)
        else
            ICU_CPPFLAGS=""
            ICU_LIBS=""
            ## If we have a custom action on failure, don't print errors, but
            ## do set a variable so people can do so.
            ifelse([$3], ,echo "can't find ICU >= $1",)
        fi

        AC_SUBST(ICU_CPPFLAGS)
        AC_SUBST(ICU_LIBS)
  fi

  if test $succeeded = yes; then
     ifelse([$2], , :, [$2])
  else
     ifelse([$3], , AC_MSG_ERROR([Library requirements (ICU) not met.]), [$3])
  fi
])
