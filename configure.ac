dnl $Id$
dnl
dnl
dnl don't forget to set ASM_FOR_ARCH to a space delimited list of
dnl processor architectures, for which assembler routines exist
dnl
dnl
dnl Exported and configured variables:
dnl CC
dnl CFLAGS
dnl LDFLAGS
dnl LDADD
dnl NASM

dnl extra vars for frontend:
dnl FRONTEND_LDFLAGS
dnl FRONTEND_CFLAGS
dnl FRONTEND_LDADD

dnl extra vars for libmp3lame:
dnl LIBMP3LAME_LDADD

AC_PREREQ([2.73])
AC_INIT([lame],[4.1],[lame-dev@lists.sourceforge.net])
AC_CONFIG_SRCDIR([libmp3lame/lame.c])
AC_CONFIG_MACRO_DIRS([m4])
AC_LANG([C])
AC_USE_SYSTEM_EXTENSIONS

dnl check system
AC_CANONICAL_HOST

dnl automake
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AH_TOP([
#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H
])
AH_BOTTOM([#endif /* LAME_CONFIG_H */])
AM_MAINTAINER_MODE
AM_MAKE_INCLUDE
PKG_PROG_PKG_CONFIG([])

dnl for doc/Makefile's "doxygen" target - Graphviz is optional, so this must
dnl not fail configure if it's absent
AC_ARG_WITH([dot],
  [AS_HELP_STRING([--with-dot=PATH], [path to Graphviz's dot tool, used by Doxygen for diagrams (default: autodetect on PATH)])],
  [AS_IF([test "x$withval" = xno], [DOT=], [DOT="$withval"])],
  [AC_PATH_PROG([DOT], [dot])])
AC_SUBST([DOT])

dnl check environment
AC_SEARCH_LIBS([strerror],[cposix])
AC_USE_SYSTEM_EXTENSIONS
case $host_os in
  *cygwin* ) CYGWIN=yes;;
         * ) CYGWIN=no;;
esac

dnl libtool
# AC_DISABLE_SHARED
LT_INIT
AC_SUBST(LIBTOOL_DEPS)
CFLAGS="${ac_save_CFLAGS}"

# increase this when the shared lib becomes totally incompatible
LIB_MAJOR_VERSION=0

# increase this when changes are made, but they are upward compatible
# to previous versions
LIB_MINOR_VERSION=0

dnl # work around for a bug, don't know where it is exactly
if test "${ac_cv_cygwin}" = "yes"; then
	if test "${CC}" != "gcc"; then
		AC_MSG_ERROR([Please use]
			[   CC=gcc ./configure]
			[Abort this configure run and add "CC=gcc" or you will]
			[see errors and no lame.exe will be build.])
	fi
fi

dnl check programs
AC_PROG_CC()

if test "x${GCC}" = "xyes"; then
	AC_MSG_CHECKING(compiler)
	COMPILER_TYPE="`${CC} --version | head -1 | sed -e '1,$s/version.*//g'`"
	case "${COMPILER_TYPE}" in
	*gcc*)
		AC_MSG_RESULT(gcc)
		HAVE_GCC=yes
		HAVE_CLANG=no
		;;
	*clang*)
		AC_MSG_RESULT(clang)
		HAVE_CLANG=yes
		HAVE_GCC=no
		;;
	*)
		AC_MSG_RESULT(unknown)
		HAVE_GCC=no
		HAVE_CLANG=no
		;;
	esac

	if test "${HAVE_GCC}" = "yes"; then
		AC_MSG_CHECKING(version of GCC)
		GCC_version="`${CC} --version | sed -n '1s/^[[^ ]]* (.*) //;s/ .*$//;1p'`"
		case "${GCC_version}" in 
		[[0-9]]*[[0-9]]*)
			AC_MSG_RESULT(${GCC_version})
			;;
		*)
			# probably not gcc...
			AC_MSG_RESULT(unknown compiler version pattern, playing safe and disabling gcc optimisations... ${GCC_version})
			HAVE_GCC=no
			;;
		esac
	fi

	if test "${HAVE_CLANG}" = "yes"; then
		AC_MSG_CHECKING(version of clang)
		CLANG_version="`${CC} --version | sed -n 's/.*clang version //;s/ .*$//;1p'`"
		AC_MSG_RESULT(${CLANG_version})
	fi
fi


dnl LAME_CHECK_CFLAG(FLAG, VARIABLE)
dnl   Append FLAG to shell VARIABLE if the C compiler accepts it.  -Werror is
dnl   added so that a flag which only warns (e.g. clang's "unknown argument")
dnl   still counts as a rejection, and -O2 is added so that flags which are
dnl   only meaningful with optimization (e.g. _FORTIFY_SOURCE) probe correctly.
AC_DEFUN([LAME_CHECK_CFLAG],
[AC_MSG_CHECKING([whether $CC accepts $1])
lame_check_flag_save=$CFLAGS
CFLAGS="$CFLAGS -O2 $1 -Werror"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
  [lame_check_flag_ok=yes], [lame_check_flag_ok=no])
CFLAGS=$lame_check_flag_save
AC_MSG_RESULT([$lame_check_flag_ok])
AS_IF([test "x$lame_check_flag_ok" = xyes], [$2="[$]{$2} $1"])
])

dnl LAME_CHECK_LDFLAG(FLAG, VARIABLE)
dnl   Append FLAG to shell VARIABLE if the compiler driver / linker accepts it.
AC_DEFUN([LAME_CHECK_LDFLAG],
[AC_MSG_CHECKING([whether the linker accepts $1])
lame_check_flag_save=$LDFLAGS
LDFLAGS="$LDFLAGS $1 -Werror"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
  [lame_check_flag_ok=yes], [lame_check_flag_ok=no])
LDFLAGS=$lame_check_flag_save
AC_MSG_RESULT([$lame_check_flag_ok])
AS_IF([test "x$lame_check_flag_ok" = xyes], [$2="[$]{$2} $1"])
])


AC_CHECK_HEADER(dmalloc.h)
if test "${ac_cv_header_dmalloc_h}" = "yes"; then
	AM_WITH_DMALLOC
fi

dnl Checks for header files.
AC_CHECK_HEADERS( \
		 errno.h \
		 fcntl.h \
		 limits.h \
		 stdint.h \
		 string.h \
		 sys/soundcard.h \
		 sys/time.h \
		 unistd.h \
		 linux/soundcard.h)

dnl Checks for actually working SSE intrinsics
AC_MSG_CHECKING(working SSE intrinsics)
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <xmmintrin.h>]],
		[[_mm_sfence();]])],
	[AC_DEFINE([HAVE_XMMINTRIN_H], [1], [Define if SSE intrinsics work.])
	 ac_cv_header_xmmintrin_h=yes],
	[ac_cv_header_xmmintrin_h=no])
AC_MSG_RESULT(${ac_cv_header_xmmintrin_h})

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
if test ${cross_compiling} = "no"; then
	AC_C_BIGENDIAN
fi

AC_SYS_LARGEFILE

AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(unsigned short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(unsigned int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(unsigned long)
AC_CHECK_SIZEOF(long long)
AC_CHECK_SIZEOF(unsigned long long)
AC_CHECK_SIZEOF(float)
AC_CHECK_SIZEOF(double)

if test $ac_cv_sizeof_short -eq 0 \
    -o $ac_cv_sizeof_unsigned_short -eq 0 \
    -o $ac_cv_sizeof_int -eq 0 \
    -o $ac_cv_sizeof_unsigned_int -eq 0 \
    -o $ac_cv_sizeof_long -eq 0 \
    -o $ac_cv_sizeof_unsigned_long -eq 0 \
    -o $ac_cv_sizeof_long_long -eq 0 \
    -o $ac_cv_sizeof_unsigned_long_long -eq 0 \
    -o $ac_cv_sizeof_float -eq 0 \
    -o $ac_cv_sizeof_double -eq 0; then
	echo '*** I have a problem determining the size of some variable types. Either'
	echo '*** you compiler is broken, or your system+compiler combination is not'
	echo '*** supportet by the "autoconf" framework we use to generate this'
	echo '*** configure script.'
	exit 1
fi

    AC_TYPE_LONG_DOUBLE_WIDER
    ac_cv_c_long_double=$ac_cv_type_long_double_wider
    if test $ac_cv_c_long_double = yes; then
      AC_DEFINE([HAVE_LONG_DOUBLE],[1],[Define to 1 if the type `long double' works and has more range or
	 precision than `double'.])
    fi
  
if test "${ac_cv_c_long_double}" = "yes" ; then
	AC_CHECK_SIZEOF(long double)
fi

AC_CHECK_TYPES([uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t])

AH_VERBATIM([HAVE_UINT8_T],
[/* add uint8_t type */
#undef HAVE_UINT8_T
#ifndef HAVE_UINT8_T
	typedef unsigned char uint8_t;
#endif])

AH_VERBATIM([HAVE_INT8_T],
[/* add int8_t type */
#undef HAVE_INT8_T
#ifndef HAVE_INT8_T
	typedef char int8_t;
#endif])

AH_VERBATIM([HAVE_UINT16_T],
[/* add uint16_t type */
#undef HAVE_UINT16_T
#ifndef HAVE_UINT16_T
	typedef unsigned short uint16_t;
#endif])

AH_VERBATIM([HAVE_INT16_T],
[/* add int16_t type */
#undef HAVE_INT16_T
#ifndef HAVE_INT16_T
	typedef short int16_t;
#endif])

if test "${HAVE_INT32_T}" = yes; then
	AC_DEFINE(A_UINT32_T,unsigned int32_t)
else
	if test "${ac_cv_sizeof_unsigned_short}" = "4"; then
		AC_DEFINE(A_UINT32_T,unsigned short)
	else
		if test "${ac_cv_sizeof_unsigned_int}" = "4"; then
			AC_DEFINE(A_UINT32_T,unsigned int)
		else
			if test "${ac_cv_sizeof_unsigned_long}" = "4"; then
				AC_DEFINE(A_UINT32_T,unsigned long)
			else
				AC_MSG_ERROR([CHECK_TYPE_uint32_t - please report to lame-dev@lists.sourceforge.net])
			fi
		fi
	fi
fi

AH_VERBATIM([HAVE_UINT32_T],
[/* add uint32_t type */
#undef HAVE_UINT32_T
#ifndef HAVE_UINT32_T
#undef A_UINT32_T
	typedef A_UINT32_T uint32_t;
#endif])

if test "${ac_cv_sizeof_short}" = "4"; then
	AC_DEFINE(A_INT32_T,short)
else
	if test "${ac_cv_sizeof_int}" = "4"; then
		AC_DEFINE(A_INT32_T,int)
	else
		if test "${ac_cv_sizeof_long}" = "4"; then
			AC_DEFINE(A_INT32_T,long)
		else
			AC_MSG_ERROR([CHECK_TYPE_int32_t - please report to lame-dev@lists.sourceforge.net])
		fi
	fi
fi

AH_VERBATIM([HAVE_INT32_T],
[/* add int32_t type */
#undef HAVE_INT32_T
#ifndef HAVE_INT32_T
#undef A_INT32_T
	typedef A_INT32_T int32_t;
#endif])

if test "${HAVE_INT64_T}" = yes; then
        AC_DEFINE(A_UINT64_T,unsigned int64_t)
else
	if test "${ac_cv_sizeof_unsigned_int}" = "8"; then
		AC_DEFINE(A_UINT64_T,unsigned int)
	else
		if test "${ac_cv_sizeof_unsigned_long}" = "8"; then
			AC_DEFINE(A_UINT64_T,unsigned long)
		else
			if test "${ac_cv_sizeof_unsigned_long_long}" = "8"; then
				AC_DEFINE(A_UINT64_T,unsigned long long)
			else
				AC_MSG_ERROR([CHECK_TYPE_uint64_t - please report to lame-dev@lists.sourceforge.net])
			fi
		fi
	fi
fi

AH_VERBATIM([HAVE_UINT64_T],
[/* add uint64_t type */
#undef HAVE_UINT64_T
#ifndef HAVE_UINT64_T
#undef A_UINT64_T
	typedef A_UINT64_T uint64_t;
#endif])

if test "${ac_cv_sizeof_int}" = "8"; then
	AC_DEFINE(A_INT64_T,int)
else
	if test "${ac_cv_sizeof_long}" = "8"; then
		AC_DEFINE(A_INT64_T,long)
	else
		if test "${ac_cv_sizeof_long_long}" = "8"; then
			AC_DEFINE(A_INT64_T,long long)
		else
			AC_MSG_ERROR([CHECK_TYPE_int64_t - please report to lame-dev@lists.sourceforge.net])
		fi
	fi
fi

AH_VERBATIM([HAVE_INT64_T],
[/* add int64_t type */
#undef HAVE_INT64_T
#ifndef HAVE_INT64_T
#undef A_INT64_T
	typedef A_INT64_T int64_t;
#endif])

alex_IEEE854_FLOAT80
if test "${alex_cv_ieee854_float80}" = "yes" ; then
	if test "${ac_cv_c_long_double}" = "yes" ; then
		AC_CHECK_TYPES(ieee854_float80_t, long double)
		AH_VERBATIM([HAVE_IEEE854_FLOAT80_T],
[/* add ieee854_float80_t type */
#undef HAVE_IEEE854_FLOAT80_T
#ifndef HAVE_IEEE854_FLOAT80_T
	typedef long double ieee854_float80_t;
#endif])

		AC_DEFINE(HAVE_IEEE854_FLOAT80, 1, [system has 80 bit floats])
	fi
fi
AC_CHECK_TYPES([ieee754_float64_t, ieee754_float32_t])

AH_VERBATIM([HAVE_IEEE754_FLOAT64_T],
[/* add ieee754_float64_t type */
#undef HAVE_IEEE754_FLOAT64_T
#ifndef HAVE_IEEE754_FLOAT64_T
	typedef double ieee754_float64_t;
#endif])

AH_VERBATIM([HAVE_IEEE754_FLOAT32_T],
[/* add ieee754_float32_t type */
#undef HAVE_IEEE754_FLOAT32_T
#ifndef HAVE_IEEE754_FLOAT32_T
	typedef float ieee754_float32_t;
#endif])

AC_DEFINE(LAME_LIBRARY_BUILD, 1, [requested by Frank, seems to be temporary needed for a smooth transition])


if test ${cross_compiling} = "yes"; then
	AC_MSG_WARN([]
  [**************************************************************************]
  [*                                                                        *]
  [* You are cross compiling:                                               *]
  [*   - I did not have a change to determine                               *]
  [*     + the size of:                                                     *]
  [*       - short                                                          *]
  [*       - unsigned short                                                 *]
  [*       - int                                                            *]
  [*       - unsigned int                                                   *]
  [*       - long                                                           *]
  [*       - unsigned long                                                  *]
  [*       - float                                                          *]
  [*       - double                                                         *]
  [*       - long double                                                    *]
  [*     + the endianess of the system                                      *]
  [*   - You have to provide appropriate defines for them in config.h, e.g. *]
  [*     + define SIZEOF_SHORT to 2 if the size of a short is 2             *]
  [*     + define WORDS_BIGENDIAN if your system is a big endian system     *]
  [*                                                                        *]
  [**************************************************************************])
fi

AC_TYPE_SIZE_T
AC_CHECK_HEADERS_ONCE([sys/time.h])
# Obsolete code to be removed.
if test $ac_cv_header_sys_time_h = yes; then
  AC_DEFINE([TIME_WITH_SYS_TIME],[1],[Define to 1 if you can safely include both <sys/time.h>
	     and <time.h>.  This macro is obsolete.])
fi
# End of obsolete code.


dnl Checks for library functions.
AC_FUNC_ALLOCA
AC_CHECK_FUNCS(gettimeofday strtol)

if test "X${ac_cv_func_strtol}" != "Xyes"; then
	AC_MSG_ERROR([function strtol is mandatory])
fi

dnl Check if we are on a mingw system, which needs libwsock32
SOCKETFUNCTION=unknown
AC_CHECK_FUNCS(socket)
if test $ac_cv_func_socket = no; then
	# maybe it is in libsocket
	AC_CHECK_LIB(socket, socket, [AC_DEFINE(HAVE_SOCKET)
	LIBS="$LIBS -lsocket"])
	if test "X${ac_cv_lib_socket_socket}" != "Xyes"; then
		SOCKETFUNCTION=NO
	else
		case ${host_os} in
		*solaris*)
			LIBS="$LIBS -lnsl"
			;;
		esac
	fi
fi

dnl Initialize configuration variables for the Makefile
CFLAGS=${CFLAGS}
CONFIG_DEFS=${CONFIG_DEFS}
NASM=
INCLUDES="-I\$(top_srcdir)/include -I\$(srcdir)"
FRONTEND_LDFLAGS=
FRONTEND_CFLAGS=
LIB_SOURCES=
MAKEDEP="-M"
RM_F="rm -f"

AC_ARG_ENABLE(nasm,
  [  --enable-nasm           Allow the use of nasm if available],
  ASM_FOR_ARCH="i386", ASM_FOR_ARCH="")

AC_ARG_ENABLE(ieeehack,
  [  --enable-ieeehack       Enable Takhiros IEEE754 hack (speed improvement for old CPUs)],
  IEEE754_HACK="yes", IEEE754_HACK="no")
case ${IEEE754_HACK} in
	yes)
		AC_DEFINE(TAKEHIRO_IEEE754_HACK, 1, IEEE754 compatible machine)
		;;
	no)
		AC_DEFINE(TAKEHIRO_IEEE754_HACK, 0, IEEE754 compatible machine)
		;;
esac

dnl Checks for libraries.

AC_CHECK_HEADERS(termcap.h)
AC_CHECK_HEADERS(ncurses/termcap.h)

dnl The console code drives the terminal through the termcap interface,
dnl tgetent/tgetnum/tgetstr, and never calls initscr. Probing for initscr
dnl therefore accepted libraries that cannot serve the header the console code
dnl actually includes: where ncurses is packaged as a static library whose
dnl termcap header declares those entry points as DLL imports, the probe
dnl succeeded and the frontend then failed to link. Compile the same header
dnl and calls the console code does, against each candidate in turn, and keep
dnl the first that satisfies them, so that the intended termcap, curses,
dnl ncurses preference decides. Falling through all three leaves the terminal
dnl support disabled, which the console code already handles, rather than
dnl breaking the build.
HAVE_TERMCAP=""
lame_termcap_save_LIBS="${LIBS}"
for lame_termcap_lib in termcap curses ncurses; do
  AC_MSG_CHECKING([for the termcap interface in -l${lame_termcap_lib}])
  LIBS="-l${lame_termcap_lib} ${lame_termcap_save_LIBS}"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#if defined(HAVE_NCURSES_TERMCAP_H)
# include <ncurses/termcap.h>
#elif defined(HAVE_TERMCAP_H)
# include <termcap.h>
#else
# include <curses.h>
# if !defined(__bsdi__)
#  include <term.h>
# endif
#endif
    ]], [[
      char termcap_buffer[2048];
      char *area = termcap_buffer;
      if (tgetent(termcap_buffer, "vt100") == 1) {
          (void) tgetnum("li");
          (void) tgetstr("ce", &area);
      }
    ]])], [AC_MSG_RESULT(yes)
           HAVE_TERMCAP="${lame_termcap_lib}"], [AC_MSG_RESULT(no)])
  if test "x${HAVE_TERMCAP}" != "x"; then
    break
  fi
done
LIBS="${lame_termcap_save_LIBS}"
AC_CHECK_HEADERS(langinfo.h, AC_CHECK_FUNCS(nl_langinfo))

AM_ICONV

dnl math lib
AC_CHECK_LIB(m, cos, USE_LIBM="-lm")
dnl free fast math library
AC_CHECK_LIB(ffm, cos, USE_LIBM="-lffm -lm")
dnl Compaq fast math library.
AC_ARG_ENABLE(cpml,
  [  --disable-cpml          Do not use Compaq's fast Math Library],
  CONFIG_CPML="no", CONFIG_CPML="yes")
if test "${CONFIG_CPML}" = yes; then
      AC_CHECK_LIB(cpml, cos, USE_LIBM="-lcpml")
fi
CONFIG_MATH_LIB="${USE_LIBM}"

dnl configure use of features

AM_PATH_GTK(1.2.0, HAVE_GTK="yes", HAVE_GTK="no")

dnl ElectricFence malloc debugging
AC_MSG_CHECKING(use of ElectricFence malloc debugging)
AC_ARG_ENABLE(efence,
  [  --enable-efence         Use ElectricFence for malloc debugging],
  CONFIG_EFENCE="${enableval}", CONFIG_EFENCE="no")

case "${CONFIG_EFENCE}" in
yes)
	AC_CHECK_LIB(efence, EF_Print, HAVE_EFENCE="-lefence")
	if test "x${HAVE_EFENCE}" != "x-lefence"; then
		AC_MSG_RESULT(yes, but libefence not found)
	else
		LDADD="${LDADD} ${HAVE_EFENCE}"
		AC_DEFINE(HAVE_EFENCE, 1, we link against libefence)
		AC_MSG_RESULT(${CONFIG_EFENCE})
	fi
	;;
no)
	AC_MSG_RESULT(${CONFIG_EFENCE})
	;;
*)
	AC_MSG_ERROR(bad value �${CONFIG_EFENCE}� for efence option)
	;;
esac


dnl libsndfile
WARNING=
AC_ARG_WITH(fileio,
  [  --with-fileio=lame      Use lame's internal file io routines [default]]
  [             =sndfile   Use Erik de Castro Lopo's libsndfile]
  [                        (no stdin possible currently)],
  CONFIG_FILEIO="${withval}", CONFIG_FILEIO="lame")

if test "${CONFIG_FILEIO}" = "sndfile" ; then
  PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.2, HAVE_SNDFILE="yes", HAVE_SNDFILE="no")
fi

AC_MSG_CHECKING(use of file io)

if test "${CONFIG_FILEIO}" = "sndfile" ; then
  if test "${HAVE_SNDFILE}" = "yes" -o "x${SNDFILE_LIBS}" != "x" \
    -o "x${SNDFILE_CFLAGS}" != "x"; then
    SNDFILE_LIBS=`echo ${SNDFILE_LIBS}`
    SNDFILE_CFLAGS=`echo ${SNDFILE_CFLAGS}`

    if test -n "${SNDFILE_LIBS}" ; then
      FRONTEND_LDFLAGS="${SNDFILE_LIBS} ${FRONTEND_LDFLAGS}"
    fi
    FRONTEND_LDADD="-lsndfile ${FRONTEND_LDADD}"

    if test -n "${SNDFILE_CFLAGS}" ; then
      INCLUDES="${SNDFILE_CFLAGS} ${INCLUDES}"
    fi

    AC_DEFINE(LIBSNDFILE, 1, set to 1 if you have libsndfile)
  else
    # default
    CONFIG_FILEIO="lame"
    WARNING="${WARNING} Could not find any sndfile lib on system."
  fi
else
  CONFIG_FILEIO="lame"
fi
AC_MSG_RESULT(${CONFIG_FILEIO})
if test "x${WARNING}" != "x" ; then
  AC_MSG_WARN($WARNING)
fi


dnl check if we should remove hooks for analyzer code in library
dnl default library must include these hooks
AC_MSG_CHECKING(use of analyzer hooks)
AC_ARG_ENABLE(analyzer-hooks,
  [  --disable-analyzer-hooks Exclude analyzer hooks],
  CONFIG_ANALYZER="${enableval}", CONFIG_ANALYZER="yes")

case "${CONFIG_ANALYZER}" in
yes)
	;;
no)
	AC_DEFINE(NOANALYSIS, 1, build without hooks for analyzer)
	;;
*)
	AC_MSG_ERROR(bad value �${CONFIG_ANALYZER}� for analyzer-hooks option)
	;;
esac
AC_MSG_RESULT($CONFIG_ANALYZER)


dnl mpg123 decoder
PKG_CHECK_MODULES([mpg123], [libmpg123 >= 1.26.0], [HAVE_MPG123="yes"], [HAVE_MPG123="no"])

AC_MSG_CHECKING(use of external mpg123 decoder)
AC_ARG_ENABLE(decoder,
  [  --disable-decoder          Exclude mpg123 decoder],
  CONFIG_DECODER="${enableval}", CONFIG_DECODER="yes")

LIB_WITH_DECODER=false
if test "${HAVE_MPG123}" = "yes" && test "${CONFIG_DECODER}" != "no"; then
	AC_MSG_RESULT([yes])
	AC_DEFINE(HAVE_MPG123, 1, set to 1 if you have libmpg123)
	AC_DEFINE(DECODE_ON_THE_FLY, 1, allow to compute a more accurate replaygain value)
	FRONTEND_LDADD="$mpg123_LIBS $FRONTEND_LDADD"
	LIBMP3LAME_LDADD="$mpg123_LIBS $LIBMP3LAME_LDADD"
	INCLUDES="$mpg123_CFLAGS $INCLUDES"
else
	if test "${CONFIG_DECODER}" = "yes" ; then
		AC_MSG_ERROR([no, libmpg123 not found])
	else
		AC_MSG_RESULT(${CONFIG_DECODER})
	fi
fi
AM_CONDITIONAL(LIB_WITH_DECODER, ${LIB_WITH_DECODER})

AC_MSG_CHECKING(if the lame frontend should be build)
AC_ARG_ENABLE(frontend,
  [  --disable-frontend      Do not build the lame executable [default=build]],
    WITH_FRONTEND="${enableval}", WITH_FRONTEND=yes)
if test "x${WITH_FRONTEND}" = "xyes"; then
	WITH_FRONTEND=lame${ac_exeext}
	AC_MSG_RESULT(yes)
else
	WITH_FRONTEND=
	AC_MSG_RESULT(no)
fi



AC_MSG_CHECKING(if mp3x is requested)
AC_ARG_ENABLE(mp3x,
  [  --enable-mp3x           Build GTK frame analyzer [default=no]],
    WITH_MP3X="${enableval}", WITH_MP3X=no)
if test "x${WITH_MP3X}" = "xyes"; then
	WITH_MP3X=mp3x${ac_exeext}
	AC_MSG_RESULT(yes)
else
	WITH_MP3X=
	AC_MSG_RESULT(no)
fi

if test "${HAVE_GTK}" = "no"; then
	if test "x${WITH_MP3X}" = "xmp3x"; then
		AC_MSG_WARN(can't build mp3x, no GTK installed)
		WITH_MP3X=
	fi
	if test "x${CONFIG_ANALYZER}" != "xyes"; then
		AC_MSG_WARN(can't build mp3x because of disabled analyzer hooks)
		WITH_MP3X=
	fi
fi

AC_MSG_CHECKING(if mp3rtp is requested)
AC_ARG_ENABLE(mp3rtp,
  [  --enable-mp3rtp         Build mp3rtp [default=no]],
    WITH_MP3RTP="${enableval}", WITH_MP3RTP=no)
if test "x${WITH_MP3RTP}" = "xyes"; then
	if test ${SOCKETFUNCTION} = NO; then
		AC_MSG_ERROR([function socket is mandatory for mp3rtp])
	fi
	WITH_MP3RTP=mp3rtp${ac_exeext}
	AC_MSG_RESULT(yes)
else
	WITH_MP3RTP=
	AC_MSG_RESULT(no)
fi

#Checking for IPV6 support
AC_MSG_CHECKING(for IPv6 support)
AC_ARG_ENABLE(ipv6, AS_HELP_STRING([--disable-ipv6],
    [Disable IPV6 support(By default ipv6 (with ipv4) support is enabled)]),
    CONFIG_IPV6=$enableval,
    CONFIG_IPV6=yes)
if test "x$CONFIG_IPV6" = "xyes" ; then
    AC_CACHE_VAL(ac_cv_ipv6,
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <stdlib.h>
    main()
    {
        int fd;
        struct sockaddr_in6 foo;
        fd = socket(AF_INET6, SOCK_STREAM, 0);
        exit(fd >= 0 ? 0 : 1);
    }]])],[ac_cv_ipv6=yes],[ac_cv_ipv6=no],[ac_cv_ipv6=yes]))
    AC_MSG_RESULT($ac_cv_ipv6)
    if test $ac_cv_ipv6 = yes ; then
        AC_DEFINE(IPV6,1,[check if system supports IPV6])
    fi
else
    AC_MSG_RESULT($CONFIG_IPV6)	
fi
AC_MSG_CHECKING(if dynamic linking of the frontends is requested)
# Dynamic linking is the default: it lets the frontends pick up the shared
# library's optimization (see -fno-semantic-interposition below) and be built
# as position-independent executables together with the hardening flags.
# --disable-dynamic-frontends links the frontends statically into a
# self-contained binary.
AC_ARG_ENABLE(dynamic-frontends,
  [AS_HELP_STRING([--disable-dynamic-frontends],
     [link the frontends statically against libmp3lame (default: dynamic)])],
  [dynamic_frontends_given=yes], [enable_dynamic_frontends=yes])
if test "x${enable_dynamic_frontends}" = "xno"; then
	FRONTEND_LDFLAGS="${FRONTEND_LDFLAGS} -static"
fi
case "x${FRONTEND_LDFLAGS}" in
*-static*)
	AC_MSG_RESULT(no)
	;;
*)
	AC_MSG_RESULT(yes)
	;;
esac


# --enable-internal exposes the internal/developer tuning options in the
# frontend (--substep, --sfscale, --athcurve, ...). Those setters are
# deliberately withheld from the shared library's export list
# (include/libmp3lame.sym), so anything that calls them must link the static
# archive; enabling the options therefore forces static frontend linking. The
# options only make sense on an alpha version (LAME_TYPE_VERSION==0) - asking
# for them on a beta or release tree is an error, not a silent no-op.
lame_type_version=`sed -n ['s/.*LAME_TYPE_VERSION[[:space:]][[:space:]]*\([0-9]\).*/\1/p'] "$srcdir/libmp3lame/version.h"`
AC_ARG_ENABLE([internal],
  [AS_HELP_STRING([--enable-internal],
     [expose internal/developer tuning options in the frontend; alpha versions only, forces static frontend linking (default: disabled)])],
  [enable_internal="${enableval}"], [enable_internal=no])
AC_MSG_CHECKING(whether to enable internal/developer options)
AC_MSG_RESULT(${enable_internal})
internal_static_note=
if test "x${enable_internal}" = "xyes"; then
	if test "x${lame_type_version}" != "x0"; then
		AC_MSG_ERROR([--enable-internal requires an alpha version (LAME_TYPE_VERSION==0); this tree is version type "${lame_type_version}". The internal/developer tuning options are not available in beta or release builds.])
	fi
	if test "x${enable_static}" = "xno"; then
		AC_MSG_ERROR([--enable-internal needs the static libmp3lame archive, but --disable-static was given. The internal setters are not exported from the shared library, so the frontend must link statically. Re-run without --disable-static.])
	fi
	# _ALLOW_INTERNAL_OPTIONS is defined per-target (frontend and the set_get
	# unit test) via the INTERNAL_OPTS automake conditional, NOT globally: a
	# global define would pull the internal-option code path of parse.c into the
	# other unit tests that compile parse.c only for its argument helpers, and
	# they link the shared library where the internal setters are absent.
	case "x${FRONTEND_LDFLAGS}" in
	*-static*)
		: ;; # already static (e.g. --disable-dynamic-frontends)
	*)
		FRONTEND_LDFLAGS="${FRONTEND_LDFLAGS} -static"
		if test "x${dynamic_frontends_given}" = "xyes"; then
			internal_static_note="static frontend linking forced ON, overriding the requested --enable-dynamic-frontends: the internal setters are not exported from the shared library, so the frontend must link the static archive"
		else
			internal_static_note="static frontend linking forced ON (the default is dynamic): the internal setters are not exported from the shared library, so the frontend must link the static archive"
		fi
		;;
	esac
fi
AM_CONDITIONAL([INTERNAL_OPTS], [test "x${enable_internal}" = "xyes"])


#
# this is from vorbis
#
dnl check GLIBC
case $host in
*86-*-linux*)
	# glibc < 2.1.3 has a serious FP bug in the math inline header
	# that will cripple Vorbis.  Look to see if the magic FP stack
	# clobber is missing in the mathinline header, thus indicating
	# the buggy version

	AC_EGREP_CPP(log10.*fldlg2.*fxch,[
		#define __LIBC_INTERNAL_MATH_INLINES 1
	     	#define __OPTIMIZE__
		#include <math.h>
		],bad=maybe,bad=no)

	AC_MSG_CHECKING(glibc mathinline bug)
	if test ${bad} = "maybe" ;then
	      AC_EGREP_CPP(log10.*fldlg2.*fxch.*st\([[0123456789]]*\),
				[
				#define __LIBC_INTERNAL_MATH_INLINES 1
			     	#define __OPTIMIZE__
				#include <math.h>
				],bad=no,bad=yes)
	fi
	AC_MSG_RESULT(${bad})
	if test ${bad} = "yes" ;then
 AC_MSG_WARN([                                                        ])
 AC_MSG_WARN([********************************************************])
 AC_MSG_WARN([* The glibc headers on this machine have a serious bug *])
 AC_MSG_WARN([* in /usr/include/bits/mathinline.h  This bug affects  *])
 AC_MSG_WARN([* all floating point code, not only LAME, but all code *])
 AC_MSG_WARN([* built on this machine. Upgrading to glibc 2.1.3 is   *])
 AC_MSG_WARN([* strongly urged to correct the problem.               *])
 AC_MSG_WARN([*Note: that upgrading glibc will not fix any previously*])
 AC_MSG_WARN([* built programs; this is a compile-time bug.          *])
 AC_MSG_WARN([* To work around the problem for this build of LAME,   *])
 AC_MSG_WARN([* autoconf is disabling all math inlining.  This will  *])
 AC_MSG_WARN([* hurt LAME performace but is necessary for LAME to    *])
 AC_MSG_WARN([* work correctly.  Once glibc is upgraded, rerun       *])
 AC_MSG_WARN([* configure and make to build with inlining.           *])
 AC_MSG_WARN([********************************************************])
 AC_MSG_WARN([                                                        ])

	AC_DEFINE(__NO_MATH_INLINES, 1, work around a glibc bug)
	fi;;
esac


dnl configure use of VBR bitrate histogram
dnl todo: always use yes as default, use simulation instead ?
AC_MSG_CHECKING(for termcap)
if test "x${HAVE_TERMCAP}" != "x"; then
  FRONTEND_LDADD="-l${HAVE_TERMCAP} ${FRONTEND_LDADD}"
  AC_DEFINE(HAVE_TERMCAP, 1, have termcap)
  TERMCAP_DEFAULT="yes"
else
  TERMCAP_DEFAULT="no"
fi
AC_MSG_RESULT(${TERMCAP_DEFAULT})


dnl ### processor specific options ###
WITH_VECTOR=no
case $host_cpu in
x86_64|amd64)
	CPUTYPE="no"
	if test $ac_cv_header_xmmintrin_h = yes ; then
		WITH_XMM=yes
		WITH_VECTOR=yes
	fi

	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
*86)
	CPUTYPE="i386"
	if test $ac_cv_header_xmmintrin_h = yes ; then
		WITH_XMM=yes
		WITH_VECTOR=yes
	fi

	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
powerpc)
	CPUTYPE="no"

	# The following should not get enabled on a G5. HOWTO check for a G5?
	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
*)
	CPUTYPE="no"
	;;
esac

# which vector code do we support to build on this machine?
AM_CONDITIONAL(WITH_XMM, test "x${WITH_XMM}" = "xyes")

# needs to be defined to link in the internal vector lib
AM_CONDITIONAL(WITH_VECTOR, test "x${WITH_VECTOR}" = "xyes")
AC_MSG_CHECKING(if I have to build the internal vector lib)
AC_MSG_RESULT(${WITH_VECTOR})


AC_PATH_PROG(NASM, nasm, no)
case "${NASM}" in
no)
	;;
*)
	AC_MSG_CHECKING(for assembler routines for this processor type)
	for recurse_over in ${ASM_FOR_ARCH}
	do
		if test "${CPUTYPE}" = "${recurse_over}"; then
			include_asm_routines="yes"
		fi

		case $host_os in
		*darwin*)
			# currently we have problems because of a wrong
			# libtool hack in the darwin case (for nasm code)
			include_asm_routines="no"
			;;
		esac
	done
	if test "x${include_asm_routines}" = "xyes"; then
		AC_DEFINE(HAVE_NASM, 1, have nasm)
		AC_DEFINE(MMX_choose_table, 1, use MMX version of choose_table)
	else
		include_asm_routines="no"
		NASM="no"
	fi
	AC_MSG_RESULT(${include_asm_routines})
	;;
esac
AM_CONDITIONAL(HAVE_NASM, test "${NASM}" != "no")

case $host_os in
	*cygwin*|*mingw32*)
		CYGWIN=yes
		NASM_FORMAT="-f win32 -DWIN32"
		;;
	*darwin*)
		NASM_FORMAT="-f macho"
		;;
	*os2-emx*)
		NASM_FORMAT="-f aout -DAOUT"
		;;
	*)
		CYGWIN=no
		NASM_FORMAT="-f elf"
		;;
esac

#
# Optimization flags.
#
# Probe the compiler for the optimization flags we want and keep only the ones
# it accepts.
#
# --enable-expopt adds extra, less universally beneficial flags on top of the
# always-on base set.
#
AC_MSG_CHECKING(for additional optimizations)
AC_ARG_ENABLE(expopt,
  [  --enable-expopt=norm    Whether to enable experimental optimizations]
  [                          [default=no]],
    CONFIG_EXPOPT="${enableval}", CONFIG_EXPOPT="no")
AC_MSG_RESULT(${CONFIG_EXPOPT})

# Security hardening is on by default; --disable-hardening opts out.
AC_ARG_ENABLE([hardening],
  [AS_HELP_STRING([--disable-hardening],
     [do not add compiler/linker security hardening flags (default: enabled)])],
  [], [enable_hardening=yes])

# Machine-specific tuning is opt-in: it can produce binaries that do not run
# on other CPUs of the same architecture.
AC_ARG_ENABLE([native],
  [AS_HELP_STRING([--enable-native],
     [tune the build for this exact machine, not portable (default: disabled)])],
  [], [enable_native=no])

if test "x$HAVE_GCC" = "xyes" -o "x$HAVE_CLANG" = "xyes"; then
	OPTIMIZATION=
	HARDENING_CFLAGS=
	HARDENING_LDFLAGS=
	HARDENING_FE_CFLAGS=
	HARDENING_FE_LDFLAGS=

	# Warnings, and a faster compile pipe where the linker supports it.
	# (Per the gcc manual, "there is no reason to use -pedantic".)
	if test "x${with_gnu_ld}" = "xyes"; then
		CFLAGS="-Wall -pipe ${CFLAGS}"
	else
		# some vendor ld's don't like '-pipe'
		CFLAGS="-Wall ${CFLAGS}"
	fi

	case "${CONFIG_EXPOPT}" in
	no|norm|yes)
		;;
	*)
		AC_MSG_ERROR([bad value ${CONFIG_EXPOPT} for --enable-expopt])
		;;
	esac

	# Base optimization, applied unless the user passed their own CFLAGS.
	#   -O3                          aggressive optimization
	#   -fomit-frame-pointer         free a register (implied by -O on most
	#                                modern targets, useful on older ones)
	#   -ffast-math                  relax IEEE semantics - LAME's DSP code
	#                                is validated with this and relies on
	#                                the speed
	#   -fno-semantic-interposition  let the shared libmp3lame inline its
	#                                own calls instead of going through the
	#                                PLT (gcc; harmless no-op elsewhere)
	if test "x${ac_save_CFLAGS}" = "x"; then
		LAME_CHECK_CFLAG([-O3], [OPTIMIZATION])
		LAME_CHECK_CFLAG([-fomit-frame-pointer], [OPTIMIZATION])
		LAME_CHECK_CFLAG([-ffast-math], [OPTIMIZATION])
		LAME_CHECK_CFLAG([-fno-semantic-interposition], [OPTIMIZATION])

		case "${CONFIG_EXPOPT}" in
		norm|yes)
			# extra, experimental optimization
			LAME_CHECK_CFLAG([-funroll-loops], [OPTIMIZATION])
			LAME_CHECK_CFLAG([-fschedule-insns2], [OPTIMIZATION])
			;;
		esac
	fi

	# --enable-native tuning.
	if test "x${enable_native}" = "xyes"; then
		LAME_CHECK_CFLAG([-march=native], [OPTIMIZATION])
		LAME_CHECK_CFLAG([-mtune=native], [OPTIMIZATION])
	fi

	# Security hardening (default on).  Every flag is probed, so any that
	# the toolchain does not support is simply skipped.
	if test "x${enable_hardening}" != "xno"; then
		# Compile-time hardening, applied to the whole build:
		#   _FORTIFY_SOURCE          runtime checks for common libc buffer
		#                            overflows (needs optimization, which
		#                            the probe supplies)
		#   -fstack-protector-strong stack-smashing canaries
		#   -fstack-clash-protection guard against stack-clash attacks
		#   -fcf-protection=full     control-flow integrity (x86 CET)
		LAME_CHECK_CFLAG([-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2], [HARDENING_CFLAGS])
		LAME_CHECK_CFLAG([-fstack-protector-strong], [HARDENING_CFLAGS])
		LAME_CHECK_CFLAG([-fstack-clash-protection], [HARDENING_CFLAGS])
		LAME_CHECK_CFLAG([-fcf-protection=full], [HARDENING_CFLAGS])

		# Link-time hardening:
		#   -z relro / -z now        full RELRO (relocations read-only,
		#                            resolved at load time)
		#   -z noexecstack           non-executable stack
		LAME_CHECK_LDFLAG([-Wl,-z,relro], [HARDENING_LDFLAGS])
		LAME_CHECK_LDFLAG([-Wl,-z,now], [HARDENING_LDFLAGS])
		LAME_CHECK_LDFLAG([-Wl,-z,noexecstack], [HARDENING_LDFLAGS])

		# Build the frontend programs as position-independent executables,
		# but only when they are dynamically linked: PIE is incompatible
		# with the statically-linked default frontend (its libmp3lame
		# archive objects are not built PIC).  The shared library itself
		# is already built -fPIC by libtool and needs nothing here.
		case "x${FRONTEND_LDFLAGS}" in
		*-static*)
			;;
		*)
			LAME_CHECK_CFLAG([-fPIE], [HARDENING_FE_CFLAGS])
			LAME_CHECK_LDFLAG([-fPIE -pie], [HARDENING_FE_LDFLAGS])
			;;
		esac

		CFLAGS="${CFLAGS} ${HARDENING_CFLAGS}"
		LDFLAGS="${LDFLAGS} ${HARDENING_LDFLAGS}"
		FRONTEND_CFLAGS="${FRONTEND_CFLAGS} ${HARDENING_FE_CFLAGS}"
		FRONTEND_LDFLAGS="${FRONTEND_LDFLAGS} ${HARDENING_FE_LDFLAGS}"
	fi
fi





AC_MSG_CHECKING(for debug options)
AC_ARG_ENABLE(debug,
  [  --enable-debug=alot,norm Enable debugging (disables optimizations)]
  [                         [default=no]],
    CONFIG_DEBUG="${enableval}", CONFIG_DEBUG="no")

if test "x$GCC" = "xyes"; then
	DEBUG_NORM_OPT="-O -g -Wall"
	DEBUG_ANOYING="-Wbad-function-cast -Wcast-align \
		-Wcast-qual -Wchar-subscripts -Wconversion \
		-Wmissing-prototypes -Wnested-externs -Wpointer-arith \
		-Wredundant-decls -Wshadow -Wstrict-prototypes \
		-Wwrite-strings -Winline \
		-Wformat -Wswitch -Waggregate-return -Wmissing-noreturn \
		-Wimplicit-int -fno-builtin"

	case "${CONFIG_DEBUG}" in
	no)
dnl	Comment out the NDEBUG on release build, they check for issues
dnl	which should also be rejected in a release build.
dnl	The real solution would be to do user friendly error messages
dnl	instead of a core dump. This is a quick and user-unfriendly fix.
dnl		AC_DEFINE(NDEBUG, 1, no debug build)
		;;
	norm|yes)
		AC_DEFINE(ABORTFP, 1, debug define)
		OPTIMIZATION="${DEBUG_NORM_OPT}"
		;;
	anoying)
		AC_DEFINE(ABORTFP, 1, debug define)
		OPTIMIZATION="${DEBUG_NORM_OPT} ${DEBUG_ANOYING}"
		;;
	alot)
		AC_DEFINE(ABORTFP, 1, debug define)
		AC_DEFINE(DEBUG, 1, alot of debug output)
		OPTIMIZATION="${DEBUG_NORM_OPT}"
		;;
	*)
		AC_MSG_ERROR(bad value �${CONFIG_DEBUG}� for debug option)
	esac

	AC_MSG_RESULT(${CONFIG_DEBUG})
else
	AC_MSG_RESULT(no)
fi



dnl ###  system specific options  ###

##########################################################################
# LINUX on Digital/Compaq Alpha CPUs
##########################################################################
case $host in
alpha*-*-linux*)

################################################################
#### Check if 'ccc' is in our path
################################################################
if test "`which ccc 2>/dev/null | grep -c ccc`" != "0" ; then
	# Compaq's C Compiler
	CC=ccc

################################################################
#### set 'OPTIMIZATION = -arch host -tune host'
####              to generate/tune instructions for this machine
####     'OPTIMIZATION += -migrate -fast -inline speed -unroll 0'
####              tweak to run as fast as possible :)
####     'OPTIMIZATION += -w0'
####              set warning and linking flags
################################################################
	OPTIMIZATION="-arch host -tune host"
	OPTIMIZATION="-migrate -fast -inline speed -unroll 0 $OPTIMIZATION"
	OPTIMIZATION="-w0 $OPTIMIZATION"


################################################################
#### to debug, uncomment
################################################################
	# For Debugging
	#OPTIMIZATION="-g3 $OPTIMIZATION"

################################################################
#### define __DECALPHA__ (i was getting re-declaration warnings
####   in machine.h
################################################################
	# Define DEC Alpha
	AC_DEFINE(__DECALPHA__, 1, we're on DEC Alpha)
fi  #  gcc or ccc?
;; # alpha


##########################################################################
# SunOS
##########################################################################
sparc-*-sunos4*)
	if test CC = "cc"; then
		OPTIMIZATION="-O -xCC"
		MAKEDEP="-xM"
		# for gcc, use instead:
		#   CC="gcc"
		#   OPTIMIZATION="-O"
		#   MAKEDEP="-M"
AC_MSG_WARN([Please contact lame@lists.sourceforge.net with the output of the configure run and the file config.cache. Thank you for your cooperation.])
	fi
;; #SunOS

##########################################################################
# SGI
##########################################################################
*-sgi-irix*)
	if test CC = "cc"; then
		OPTIMIZATION="-O3 -woff all"
	fi
;; # SGI

##########################################################################
# Compaq Alpha running Dec Unix (OSF)
##########################################################################
alpha*-dec-osf*)
	if test CC = "cc"; then
		OPTIMIZATION="-fast -O3 -std -g3 -non_shared"
	fi
;; #OSF
esac

# todo: include the following tests in the case-list
UNAME=`uname`
ARCH=`uname -m`

###########################################################################
# MOSXS (Rhapsody PPC)
###########################################################################
if test "$UNAME" = "Rhapsody"; then
#   CC="cc"   # should be handled already by autoconf
   MAKEDEP="-make"
fi

###########################################################################
# MAC OSX  Darwin PPC
###########################################################################
if test "$UNAME" = "Darwin"; then
   MAKEDEP="-make"
   CFLAGS="$CFLAGS -fno-common"
fi


##########################################################################
# OS/2
##########################################################################
# Properly installed EMX runtime & development package is a prerequisite.
# tools I used: make 3.76.1, uname 1.12, sed 2.05, PD-ksh 5.2.13
#
##########################################################################
if test "$UNAME" = "OS/2"; then
   SHELL=sh
   #CC=gcc # should already be handled by configure

   # file extension should already be handled by automake (I don't know,
   # please  give feedback!
   #FILE_EXTENSION=".exe"

# Uncomment & inspect the GTK lines to use MP3x GTK frame analyzer.
# Properly installed XFree86/devlibs & GTK+ is a prerequisite.
# The following works for me using Xfree86/OS2 3.3.5 and GTK+ 1.2.3:
#   AC_DEFINE(HAVE_GTK, 1, have GTK)
#   AC_DEFINE(__ST_MT_ERRNO__, 1)
#   INCLUDES="-IC:/XFree86/include/gtk12 -IC:/XFree86/include/glib12 \
#             -IC:/XFree86/include $INCLUDES"
#   FRONTEND_LDFLAGS="-LC:/XFree86/lib -lgtk12 -lgdk12 -lgmodule -lglib12 \
#             -lXext -lX11 -lshm -lbsd -lsocket -lm $FRONTEND_LDFLAGS"
#   FRONTEND_CFLAGS="-Zmtd -Zsysv-signals -Zbin-files $FRONTEND_CFLAGS"
fi

###########################################################################
# AmigaOS
###########################################################################
# Type 'Make ARCH=PPC' for PowerUP and 'Make ARCH=WOS' for WarpOS
#
###########################################################################
if test "$UNAME" = "AmigaOS" ; then
	CC="gcc -noixemul"
	OPTIMIZATION="$OPTIMIZATION -m68020-60 -m68881"
	MAKEDEP="-MM"
	if test "$ARCH" = "WOS"; then
		CC="ppc-amigaos-gcc -warpup"
		OPTIMIZATION="$OPTIMIZATION -mmultiple -mcpu=603e"
		AR="ppc-amigaos-ar"
		RANLIB="ppc-amigaos-ranlib"
	fi
	if test "$ARCH",PPC; then
		CC="ppc-amigaos-gcc"
		OPTIMIZATION="$OPTIMIZATION -mmultiple -mcpu=603e"
		AR="ppc-amigaos-ar"
		RANLIB="ppc-amigaos-ranlib"
	fi
fi


CFLAGS="${OPTIMIZATION} ${CFLAGS}"

dnl In maintainer mode, build under stricter warnings and make them fatal so
dnl regressions are caught before release.  This runs after all feature checks
dnl so -Werror cannot break autoconf's test compilations, and LAME_CHECK_CFLAG
dnl drops any flag the compiler does not accept (so no compiler gate is needed).
dnl End-user builds are unaffected: maintainer mode is off by default.
if test "x$USE_MAINTAINER_MODE" = xyes; then
	LAME_CHECK_CFLAG([-Wextra], [CFLAGS])
	LAME_CHECK_CFLAG([-Wshadow], [CFLAGS])
	LAME_CHECK_CFLAG([-Werror], [CFLAGS])
fi

LDADD="${LDADD}"
FRONTEND_CFLAGS="${INCICONV} ${FRONTEND_CFLAGS}"
FRONTEND_LDADD="${FRONTEND_LDADD} ${LTLIBICONV} ${CONFIG_MATH_LIB}"


AC_SUBST(INCLUDES)

AC_SUBST(FRONTEND_LDFLAGS)
AC_SUBST(FRONTEND_CFLAGS)
AC_SUBST(FRONTEND_LDADD)
AC_SUBST(LIBMP3LAME_LDADD)
AC_SUBST(CONFIG_MATH_LIB)
AC_SUBST(LDADD)

AC_SUBST(LIB_MAJOR_VERSION)
AC_SUBST(LIB_MINOR_VERSION)

AC_SUBST(NASM)
AC_SUBST(NASM_FORMAT)

AC_SUBST(MAKEDEP)
AC_SUBST(RM_F)

AC_SUBST(LIBTOOL_DEPS)

AC_SUBST(WITH_FRONTEND)
AC_SUBST(WITH_MP3X)
AC_SUBST(WITH_MP3RTP)

AC_SUBST(CPUTYPE)
AC_SUBST(CPUCCODE)

AC_SUBST(CONFIG_DEFS)

dnl ### unit tests (CMocka) ###
dnl Off by default: a plain build is completely unaffected. When explicitly
dnl requested, require CMocka via pkg-config and hard-fail if it is missing,
dnl rather than silently configuring a test target that builds nothing.
AC_MSG_CHECKING(whether to build unit tests)
AC_ARG_ENABLE(unit-tests,
  [AS_HELP_STRING([--enable-unit-tests],
    [build the CMocka-based unit tests, run with "make check" [default=no]])],
  [enable_unit_tests="${enableval}"], [enable_unit_tests=no])
AC_MSG_RESULT(${enable_unit_tests})
if test "x${enable_unit_tests}" = "xyes"; then
	PKG_CHECK_MODULES([CMOCKA], [cmocka], [],
	  [AC_MSG_ERROR([--enable-unit-tests was given, but the CMocka unit-test library was not found by pkg-config. Install CMocka (e.g. libcmocka-dev / sysutils/cmocka / mingw-w64-cmocka) or configure without --enable-unit-tests.])])
fi
AM_CONDITIONAL([ENABLE_UNIT_TESTS], [test "x${enable_unit_tests}" = "xyes"])


AC_CONFIG_FILES([Makefile \
		 test/Makefile \
		 test/unit/Makefile \
		 libmp3lame/Makefile \
		 libmp3lame/i386/Makefile \
		 libmp3lame/lame.pc \
		 libmp3lame/vector/Makefile \
		 frontend/Makefile \
		 mpglib/Makefile \
		 doc/Makefile \
		 doc/html/Makefile \
		 doc/man/Makefile \
		 include/Makefile \
		 Dll/Makefile \
		 misc/Makefile \
		 dshow/Makefile \
		 ACM/Makefile \
		 ACM/ADbg/Makefile \
		 ACM/ddk/Makefile \
		 ACM/tinyxml/Makefile \
		 lame.spec \
		 mac/Makefile \
		 vc_solution/Makefile])

AC_OUTPUT


# ---------------------------------------------------------------------------
# Configuration summary. Anything that was forced to a non-default value, or
# that overrode a user-supplied option, is reported with the reason so the
# result is never surprising.
# ---------------------------------------------------------------------------
case "${lame_type_version}" in
  0) lame_version_type_name=alpha ;;
  1) lame_version_type_name=beta ;;
  2) lame_version_type_name=release ;;
  *) lame_version_type_name="unknown (${lame_type_version})" ;;
esac
case "x${FRONTEND_LDFLAGS}" in
  *-static*) frontend_link_kind=static ;;
  *)         frontend_link_kind=dynamic ;;
esac
# libsndfile has three outcomes, not two: not asked for, in use, or asked for
# and not found - in which case the file io silently falls back to lame's own
# routines, which is easy to miss among the rest of the configure output.
if test "${CONFIG_FILEIO}" = "sndfile"; then
  sndfile_state=yes
elif test "x${HAVE_SNDFILE}" = "xno"; then
  sndfile_state="requested, not found (using lame)"
else
  sndfile_state="not requested"
fi
if test "x${HAVE_TERMCAP}" = "x"; then
  termcap_state=none
else
  termcap_state="${HAVE_TERMCAP}"
fi
if test "${HAVE_MPG123}" = "yes" && test "${CONFIG_DECODER}" != "no"; then
  decoder_state=yes
else
  decoder_state=no
fi
if test -n "${WITH_FRONTEND}"; then prog_lame=yes; else prog_lame=no; fi
if test -n "${WITH_MP3X}"; then prog_mp3x=yes; else prog_mp3x=no; fi
if test -n "${WITH_MP3RTP}"; then prog_mp3rtp=yes; else prog_mp3rtp=no; fi
# The vector routines are named by the instruction sets they are BUILT for,
# not by what the host CPU happens to support. Only SSE is probed today.
lame_vector_sets=
if test "x${WITH_XMM}" = "xyes"; then
  lame_vector_sets="SSE"
fi
if test -z "${lame_vector_sets}"; then
  lame_vector_sets=none
fi
echo ""
echo "  LAME ${PACKAGE_VERSION} configuration summary"
echo "  -------------------------------------------"
echo "    version type .......... ${lame_version_type_name}"
echo "    frontend linking ...... ${frontend_link_kind}"
echo "    internal options ...... ${enable_internal:-no}"
echo "    unit tests ............ ${enable_unit_tests:-no}"
echo ""
echo "    optional libraries"
echo "      libmpg123 ........... ${HAVE_MPG123:-no}"
echo "      libsndfile .......... ${sndfile_state}"
echo "      terminal library .... ${termcap_state}"
echo "      GTK 1.2 ............. ${HAVE_GTK:-no}"
echo ""
echo "    programs"
echo "      lame ................ ${prog_lame}"
echo "      mp3x ................ ${prog_mp3x}"
echo "      mp3rtp .............. ${prog_mp3rtp}"
echo ""
echo "    features"
echo "      decoder ............. ${decoder_state}"
echo "      file io ............. ${CONFIG_FILEIO}"
echo "      analyzer hooks ...... ${CONFIG_ANALYZER:-no}"
echo "      vector routines ..... ${lame_vector_sets}"
echo "      hardening ........... ${enable_hardening:-no}"
echo "      native tuning ....... ${enable_native:-no}"
echo "      debug ............... ${CONFIG_DEBUG:-no}"
if test -n "${internal_static_note}"; then
  echo ""
  echo "    NOTE: ${internal_static_note}."
fi
echo ""
