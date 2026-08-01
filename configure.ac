dnl $Id$
dnl
dnl
dnl Exported and configured variables:
dnl CC
dnl CFLAGS
dnl LDFLAGS
dnl LDADD

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

dnl LAME_SSE2_PROBE(RESULT-VARIABLE)
dnl   Compile the vector routines' own construct: an SSE2 intrinsic inside a
dnl   function carrying the per-function target attribute they use.  Testing a
dnl   bare intrinsic instead would ask "is SSE2 the compiler's default here",
dnl   which is a stricter question than the source asks and answers no on
dnl   32-bit x86.  Used below, after CFLAGS is final - see there for why.
AC_DEFUN([LAME_SSE2_PROBE],
[AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <xmmintrin.h>
		  #if defined (__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 2)))
		  #define SSE2_TARGET __attribute__((target("sse2")))
		  #else
		  #define SSE2_TARGET
		  #endif
		  static float probe_out;
		  SSE2_TARGET static void probe(void)
		  {
		      __m128 v = _mm_set_ps(1.f, 2.f, 3.f, 4.f);
		      _mm_store_ss(&probe_out, _mm_sqrt_ps(_mm_mul_ps(v, v)));
		  }]],
		[[probe();]])],
	[$1=yes], [$1=no])])

dnl LAME_NEON_PROBE(RESULT-VARIABLE)
dnl   Compile the ARM vector routine's own constructs.  Three of them, because
dnl   they do not all answer alike: the pairwise add over register halves is
dnl   the one spelling of "fold adjacent lanes" that exists on both 32- and
dnl   64-bit ARM, the lane read is how an index reaches a general register,
dnl   and the compare-and-subtract is how the clamp counter is kept.  A probe
dnl   on a bare vld1q would accept a toolchain that cannot build the file.
dnl   No array subscripts anywhere in here.  A `[' is m4's quote character, so
dnl   an int m[4] inside AC_LANG_PROGRAM comes out of the macro expansion as
dnl   literal [[4]] and the probe fails to compile for a reason that has
dnl   nothing to do with NEON - which reads exactly like "this toolchain has no
dnl   NEON".  The quadrigraphs would work; not needing them is simpler.
AC_DEFUN([LAME_NEON_PROBE],
[AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <arm_neon.h>
		  static int probe_out;
		  static void probe(const int *p, int xlen)
		  {
		      int32x4_t v = vmulq_s32(vld1q_s32(p), vdupq_n_s32(xlen));
		      int32x4_t c = vdupq_n_s32(0);
		      int32x2_t h;
		      c = vsubq_s32(c, vreinterpretq_s32_u32(vcgtq_s32(v, vdupq_n_s32(14))));
		      v = vminq_s32(v, vdupq_n_s32(15));
		      h = vpadd_s32(vget_low_s32(v), vget_high_s32(v));
		      probe_out = vget_lane_s32(h, 0) + vgetq_lane_s32(c, 0);
		  }]],
		[[probe((const int *) 0, 16);]])],
	[$1=yes], [$1=no])])

dnl LAME_AVX2_PROBE(RESULT-VARIABLE)
dnl   The same question one tier up.  Both an integer and a floating point
dnl   construct are compiled, because they do not answer alike: GCC on 32-bit
dnl   x86 with -ffast-math accepts the integer one under the target attribute
dnl   and rejects the float one, so probing integers alone would promise a
dnl   tier the build cannot finish.
AC_DEFUN([LAME_AVX2_PROBE],
[AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <immintrin.h>
		  #if defined (__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 2)))
		  #define AVX2_TARGET __attribute__((target("avx2")))
		  #else
		  #define AVX2_TARGET
		  #endif
		  static const float probe_tbl[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
		  static int probe_i;
		  static float probe_f;
		  AVX2_TARGET static void probe(void)
		  {
		      __m256i i = _mm256_set1_epi32(1);
		      __m256i m = _mm256_max_epi16(_mm256_packs_epi32(i, i), _mm256_setzero_si256());
		      probe_i = _mm256_extract_epi16(m, 0);
		      _mm_store_ss(&probe_f, _mm256_castps256_ps128(_mm256_i32gather_ps(probe_tbl, i, 4)));
		  }]],
		[[probe();]])],
	[$1=yes], [$1=no])])

dnl LAME_AVX512_PROBE(RESULT-VARIABLE)
dnl   One tier further up, and asking for four subsets rather than one: the
dnl   kernels narrow to sixteen bits (BW), use the packed integer forms (DQ)
dnl   and address the shorter widths on the same encodings (VL), so a compiler
dnl   that offers only the foundation cannot build them.  Integer and floating
dnl   point are both compiled here for the reason given above.
AC_DEFUN([LAME_AVX512_PROBE],
[AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <immintrin.h>
		  #if defined (__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 2)))
		  #define AVX512_TARGET __attribute__((target("avx512f,avx512vl,avx512bw,avx512dq")))
		  #else
		  #define AVX512_TARGET
		  #endif
		  static const float probe_tbl@<:@16@:>@ = {
		      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
		  };
		  static const int probe_itbl@<:@16@:>@ = {
		      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
		  };
		  static int probe_i;
		  static float probe_f;
		  AVX512_TARGET static void probe(void)
		  {
		      __m512i i = _mm512_set1_epi32(1);
		      __m512i g = _mm512_i32gather_epi32(i, probe_itbl, 4);
		      __mmask16 m = _mm512_cmpgt_epu32_mask(i, _mm512_setzero_si512());
		      __m512i p = _mm512_packs_epi32(g, g);
		      probe_i = _mm512_reduce_max_epi32(p) + (int) m;
		      _mm_store_ss(&probe_f,
		                   _mm512_castps512_ps128(
		                       _mm512_i32gather_ps(i, probe_tbl, 4)));
		  }]],
		[[probe();]])],
	[$1=yes], [$1=no])])

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

dnl For the ARM capability probe in libmp3lame/util.c.  The header and the
dnl function are separate questions - a system can have <sys/auxv.h> and only
dnl one of the two ways of reading it - so both are asked, and util.c includes
dnl what the answers say exists rather than guessing from platform macros.
AC_CHECK_HEADERS(sys/auxv.h machine/elf.h asm/hwcap.h)
AC_CHECK_FUNCS(elf_aux_info getauxval sysctlbyname)

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
INCLUDES="-I\$(top_srcdir)/include -I\$(srcdir)"
FRONTEND_LDFLAGS=
FRONTEND_CFLAGS=
LIB_SOURCES=
MAKEDEP="-M"
RM_F="rm -f"

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

dnl AM_ICONV runs further down, once the optional libraries have been located.
dnl It has to see the same header and library search paths the frontend is
dnl built with, and here it would run before any of them exist.

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

dnl The frame analyzer (mp3x) is a GTK4 program.  An absent GTK4 must stay
dnl non-fatal - it only disables mp3x, checked below - hence the explicit
dnl not-found action.
PKG_CHECK_MODULES([GTK], [gtk4 >= 4.10], [HAVE_GTK="yes"], [HAVE_GTK="no"])

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
AC_ARG_ENABLE(mp3x,
  [  --enable-mp3x           Build GTK4 frame analyzer [default=no]],
    enable_mp3x="${enableval}", enable_mp3x=no)

PKG_CHECK_MODULES([mpg123], [libmpg123 >= 1.26.0], [HAVE_MPG123="yes"], [HAVE_MPG123="no"])

AC_ARG_ENABLE(decoder,
  [  --disable-decoder          Exclude mpg123 decoder],
  CONFIG_DECODER="${enableval}", CONFIG_DECODER="yes")

if test "x${enable_mp3x}" = "xyes"; then
	if test "${HAVE_MPG123}" != "yes" || test "${CONFIG_DECODER}" = "no"; then
		AC_MSG_ERROR([--enable-mp3x requires enabled libmpg123 decoder support (libmpg123 >= 1.26.0)])
	fi
	MP3X_MAN=mp3x.1
else
	MP3X_MAN=
fi

AC_MSG_CHECKING(use of external mpg123 decoder)
LIB_WITH_DECODER=false
if test "${HAVE_MPG123}" = "yes" && test "${CONFIG_DECODER}" != "no"; then
	AC_MSG_RESULT([yes])
	AC_DEFINE(HAVE_MPG123, 1, set to 1 if you have libmpg123)
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
if test "x${enable_mp3x}" = "xyes"; then
	WITH_MP3X=mp3x${ac_exeext}
	AC_MSG_RESULT(yes)
else
	WITH_MP3X=
	AC_MSG_RESULT(no)
fi

if test "x${WITH_MP3X}" != "x" && test "${HAVE_GTK}" = "no"; then
	AC_MSG_WARN([can't build mp3x, no GTK installed])
	WITH_MP3X=
fi
if test "x${WITH_MP3X}" != "x" && test "x${CONFIG_ANALYZER}" != "xyes"; then
	AC_MSG_WARN([can't build mp3x because of disabled analyzer hooks])
	WITH_MP3X=
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
	MP3RTP_MAN=mp3rtp.1
	AC_MSG_RESULT(yes)
else
	WITH_MP3RTP=
	MP3RTP_MAN=
	AC_MSG_RESULT(no)
fi

dnl MinGW-w64 uses wmain for the Windows Unicode command-line contract.
dnl Apply -municode only to the frontend programs; library-only builds do
dnl not need a Windows executable startup object.
FRONTEND_UNICODE_LDFLAGS=
case "${host_os}" in
mingw*)
	if test -n "${WITH_FRONTEND}${WITH_MP3X}${WITH_MP3RTP}"; then
		AC_MSG_CHECKING([whether ${CC} supports MinGW Unicode startup])
		lame_save_LDFLAGS="${LDFLAGS}"
		LDFLAGS="${LDFLAGS} -municode"
		AC_LINK_IFELSE(
			[AC_LANG_SOURCE([[
#include <wchar.h>
int wmain(int argc, wchar_t **argv)
{
	return argc > 0 && argv != 0 ? 0 : 1;
}
			]])],
			[FRONTEND_UNICODE_LDFLAGS="-municode"],
			[FRONTEND_UNICODE_LDFLAGS=])
		LDFLAGS="${lame_save_LDFLAGS}"
		if test -n "${FRONTEND_UNICODE_LDFLAGS}"; then
			AC_MSG_RESULT(yes)
		else
			AC_MSG_RESULT(no)
			AC_MSG_ERROR([MinGW frontend programs require compiler support for -municode and wmain])
		fi
	fi
	;;
esac

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
lame_cpu_x86=no
lame_cpu_arm=no
lame_cpu_arm32=no
case $host_cpu in
aarch64|arm64)
	lame_cpu_arm=yes
	;;
arm*)
	lame_cpu_arm=yes
	lame_cpu_arm32=yes
	;;
esac
case $host_cpu in
x86_64|amd64)
	lame_cpu_x86=yes

	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
*86)
	lame_cpu_x86=yes

	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
powerpc)
	# The following should not get enabled on a G5. HOWTO check for a G5?
	AC_DEFINE(USE_FAST_LOG, 1, faster log implementation with less but enough precission)
	;;
*)
	;;
esac

# The vector decision itself is made further down, once CFLAGS is final -
# see "vector routines" there. AM_CONDITIONAL and AC_MSG_RESULT for it live
# with the decision rather than here.


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

	# Base optimization, always applied.
	#   -O3                          aggressive optimization
	#   -fomit-frame-pointer         free a register (implied by -O on most
	#                                modern targets, useful on older ones)
	#   -ffast-math                  relax IEEE semantics - LAME's DSP code
	#                                is validated with this and relies on
	#                                the speed. It also asserts that no NaN
	#                                or infinity occurs, so a test for one
	#                                may be deleted however it is spelled;
	#                                code validating a caller's floating
	#                                point data has to survive that
	#   -fno-semantic-interposition  let the shared libmp3lame inline its
	#                                own calls instead of going through the
	#                                PLT (gcc; harmless no-op elsewhere)
	#
	# A CFLAGS given on the configure line is placed AFTER these rather than
	# instead of them (see the CFLAGS assignment further down), so it adds to
	# the build and overrides only what it actually names: CFLAGS='-g' yields
	# debug information at -O3, CFLAGS='-O2 -g' yields both at -O2, and either
	# keeps the arithmetic settings the DSP code is validated with. Suppressing
	# this block instead, as it once did, meant a single flag on the command
	# line silently dropped -ffast-math and changed the encoder's output.
	# Anything here can still be countermanded by naming its opposite, since
	# the later flag wins.
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
		# but only when they will link against the shared library.  PIE
		# needs position-independent objects, and the libmp3lame archive
		# is not built PIC - so both --disable-dynamic-frontends (which
		# links that archive on purpose) and --disable-shared (which
		# leaves nothing else to link) have to skip it.  The shared
		# library itself is already built -fPIC by libtool and needs
		# nothing here.
		lame_fe_pie=yes
		case "x${FRONTEND_LDFLAGS}" in
		*-static*)
			lame_fe_pie=no
			;;
		esac
		if test "x${enable_shared}" = "xno"; then
			lame_fe_pie=no
		fi
		if test "x${lame_fe_pie}" = "xyes"; then
			LAME_CHECK_CFLAG([-fPIE], [HARDENING_FE_CFLAGS])
			LAME_CHECK_LDFLAG([-fPIE -pie], [HARDENING_FE_LDFLAGS])
		fi

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

dnl ### vector routines ###
dnl
dnl Probed here, not with the other feature checks, because the answer depends
dnl on the optimization flags chosen above and those are only settled now. The
dnl combination that forces it: GCC on 32-bit x86 with -ffast-math rejects the
dnl per-function target attribute the routines rely on, so a probe run with
dnl bare CFLAGS says the routines are available and the build then fails to
dnl compile them. That is how 32-bit x86 lost these routines in 3.100, and a
dnl probe that does not use the real flags would reintroduce it.
dnl
dnl Where the attribute cannot be used, the routines can still be built by
dnl giving that one translation unit an SSE2 baseline of its own. It contains
dnl nothing but the vector code, and its callers test the CPU at run time, so
dnl a binary built this way still runs on machines without SSE2.
VECTOR_SSE2_FLAGS=
if test "x${lame_cpu_x86}" = xyes; then
	AC_MSG_CHECKING(working SSE2 intrinsics)
	LAME_SSE2_PROBE([lame_sse2_intrinsics])
	AC_MSG_RESULT(${lame_sse2_intrinsics})
	if test "x${lame_sse2_intrinsics}" = xno; then
		AC_MSG_CHECKING(working SSE2 intrinsics with a per-file SSE2 baseline)
		lame_save_CFLAGS=$CFLAGS
		CFLAGS="$CFLAGS -msse2"
		LAME_SSE2_PROBE([lame_sse2_baseline])
		CFLAGS=$lame_save_CFLAGS
		AC_MSG_RESULT(${lame_sse2_baseline})
		if test "x${lame_sse2_baseline}" = xyes; then
			lame_sse2_intrinsics=yes
			VECTOR_SSE2_FLAGS="-msse2"
		fi
	fi
	if test "x${lame_sse2_intrinsics}" = xyes; then
		AC_DEFINE([HAVE_SSE2_INTRINSICS], [1], [Define if SSE2 intrinsics work.])
		WITH_XMM=yes
		WITH_VECTOR=yes
	fi
fi

dnl The tiers are cumulative, so a wider one is only offered where the one
dnl below it was accepted, and it is probed the same way for the same reason.
VECTOR_AVX2_FLAGS=
if test "x${WITH_XMM}" = xyes; then
	AC_MSG_CHECKING(working AVX2 intrinsics)
	LAME_AVX2_PROBE([lame_avx2_intrinsics])
	AC_MSG_RESULT(${lame_avx2_intrinsics})
	if test "x${lame_avx2_intrinsics}" = xno; then
		AC_MSG_CHECKING(working AVX2 intrinsics with a per-file AVX2 baseline)
		lame_save_CFLAGS=$CFLAGS
		CFLAGS="$CFLAGS -mavx2"
		LAME_AVX2_PROBE([lame_avx2_baseline])
		CFLAGS=$lame_save_CFLAGS
		AC_MSG_RESULT(${lame_avx2_baseline})
		if test "x${lame_avx2_baseline}" = xyes; then
			lame_avx2_intrinsics=yes
			VECTOR_AVX2_FLAGS="-mavx2"
		fi
	fi
	if test "x${lame_avx2_intrinsics}" = xyes; then
		AC_DEFINE([HAVE_AVX2_INTRINSICS], [1], [Define if AVX2 intrinsics work.])
		WITH_AVX2=yes
	fi
fi

dnl There is deliberately no --enable-avx512, for the same reason there is none
dnl for SSE2 or AVX2: what the compiler can build is built, and the CPU decides
dnl at run time, so one binary runs correctly everywhere and no build is
dnl half-enabled.  The top rung carries only the constant-bitrate kernels; the
dnl variable-bitrate one was measured and rejected - see @ref vector_dispatch.
VECTOR_AVX512_FLAGS=
if test "x${WITH_XMM}" = xyes; then
	AC_MSG_CHECKING(working AVX-512 intrinsics)
	LAME_AVX512_PROBE([lame_avx512_intrinsics])
	AC_MSG_RESULT(${lame_avx512_intrinsics})
	if test "x${lame_avx512_intrinsics}" = xno; then
		AC_MSG_CHECKING(working AVX-512 intrinsics with a per-file AVX-512 baseline)
		lame_save_CFLAGS=$CFLAGS
		CFLAGS="$CFLAGS -mavx512f -mavx512vl -mavx512bw -mavx512dq"
		LAME_AVX512_PROBE([lame_avx512_baseline])
		CFLAGS=$lame_save_CFLAGS
		AC_MSG_RESULT(${lame_avx512_baseline})
		if test "x${lame_avx512_baseline}" = xyes; then
			lame_avx512_intrinsics=yes
			VECTOR_AVX512_FLAGS="-mavx512f -mavx512vl -mavx512bw -mavx512dq"
		fi
	fi
	if test "x${lame_avx512_intrinsics}" = xyes; then
		AC_DEFINE([HAVE_AVX512_INTRINSICS], [1], [Define if AVX-512 intrinsics work.])
		WITH_AVX512=yes
	fi
fi

dnl The ARM rung.  Two differences from the x86 ones above, both from the same
dnl fact - Advanced SIMD is architectural on AArch64 and optional on ARMv7:
dnl
dnl   - there is no per-function opt-in to test for.  On AArch64 there is
dnl     nothing to opt into, and on ARMv7 the switch is a floating-point ABI
dnl     choice that applies to a whole translation unit.  So the probe asks
dnl     the plain question and the fallback gives the file its own baseline,
dnl     which is the second half of what the x86 probes do anyway.
dnl   - it is not gated on a lower rung, because it is the only one.
dnl
dnl Probed here rather than with the other feature checks for the reason given
dnl above: the answer depends on the optimization flags, and those are only
dnl settled at this point.
VECTOR_NEON_FLAGS=
if test "x${lame_cpu_arm}" = xyes; then
	AC_MSG_CHECKING(working NEON intrinsics)
	LAME_NEON_PROBE([lame_neon_intrinsics])
	AC_MSG_RESULT(${lame_neon_intrinsics})
	dnl -mfpu= is a 32-bit ARM switch; on AArch64 clang rejects it outright
	dnl ("unsupported option '-mfpu=' for target aarch64-..."), so trying it
	dnl there produces two failures that look like a missing instruction set.
	if test "x${lame_neon_intrinsics}" = xno && test "x${lame_cpu_arm32}" = xyes; then
		for lame_neon_try in "-mfpu=neon" "-mfpu=neon -mfloat-abi=softfp"; do
			AC_MSG_CHECKING(working NEON intrinsics with ${lame_neon_try})
			lame_save_CFLAGS=$CFLAGS
			CFLAGS="$CFLAGS ${lame_neon_try}"
			LAME_NEON_PROBE([lame_neon_baseline])
			CFLAGS=$lame_save_CFLAGS
			AC_MSG_RESULT(${lame_neon_baseline})
			if test "x${lame_neon_baseline}" = xyes; then
				lame_neon_intrinsics=yes
				VECTOR_NEON_FLAGS="${lame_neon_try}"
				break
			fi
		done
	fi
	if test "x${lame_neon_intrinsics}" = xyes; then
		AC_DEFINE([HAVE_NEON_INTRINSICS], [1], [Define if ARM NEON intrinsics work.])
		WITH_NEON=yes
		WITH_VECTOR=yes
	fi
fi
AC_SUBST(VECTOR_SSE2_FLAGS)
AC_SUBST(VECTOR_AVX2_FLAGS)
AC_SUBST(VECTOR_AVX512_FLAGS)
AC_SUBST(VECTOR_NEON_FLAGS)

# which vector code do we support to build on this machine?
AM_CONDITIONAL(WITH_XMM, test "x${WITH_XMM}" = "xyes")
AM_CONDITIONAL(WITH_AVX2, test "x${WITH_AVX2}" = "xyes")
AM_CONDITIONAL(WITH_AVX512, test "x${WITH_AVX512}" = "xyes")
AM_CONDITIONAL(WITH_NEON, test "x${WITH_NEON}" = "xyes")

# needs to be defined to link in the internal vector lib
AM_CONDITIONAL(WITH_VECTOR, test "x${WITH_VECTOR}" = "xyes")
AC_MSG_CHECKING(if I have to build the internal vector lib)
AC_MSG_RESULT(${WITH_VECTOR})

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

dnl iconv, deliberately last.
dnl
dnl Every frontend translation unit is compiled with the include paths
dnl pkg-config reported for GTK, the decoder and libsndfile - frontend's
dnl Makefile.am applies them to the whole directory, not just to the analyzer.
dnl Where those paths lead to a GNU libiconv installed beside the C library's
dnl iconv, its <iconv.h> renames iconv_open to libiconv_open, and the frontend
dnl then needs that library.  AM_ICONV must therefore be asked the question the
dnl frontend actually asks, which means running it after those paths are known
dnl and with them in effect, not before they exist.
dnl
dnl The search directories are taken from what pkg-config reported, never
dnl assumed: an installation prefix is configurable and is not always the
dnl conventional one, so the -L options that located GTK and the decoder are
dnl also the ones that locate the iconv beside them.  Only -I and -L options
dnl are carried over.  The link options are not: they are written for libtool,
dnl which links the frontend, and include flags such as -R that a bare compiler
dnl rejects outright - which would turn a working iconv into a configure error.
dnl
dnl Where no such paths exist the loops below contribute nothing and the check
dnl behaves exactly as it always did.  That is the self-limiting property worth
dnl keeping: nothing can drag in a foreign <iconv.h> without also telling us
dnl where its library lives.
lame_iconv_incs=
for lame_iconv_opt in ${GTK_CFLAGS} ${mpg123_CFLAGS} ${SNDFILE_CFLAGS}; do
	case ${lame_iconv_opt} in
	-I*)
		case " ${lame_iconv_incs} " in
		*" ${lame_iconv_opt} "*) ;;
		*) lame_iconv_incs="${lame_iconv_incs} ${lame_iconv_opt}" ;;
		esac
		;;
	esac
done
lame_iconv_libdirs=
for lame_iconv_opt in ${GTK_LIBS} ${mpg123_LIBS} ${SNDFILE_LIBS}; do
	case ${lame_iconv_opt} in
	-L*)
		case " ${lame_iconv_libdirs} " in
		*" ${lame_iconv_opt} "*) ;;
		*) lame_iconv_libdirs="${lame_iconv_libdirs} ${lame_iconv_opt}" ;;
		esac
		;;
	esac
done

lame_iconv_save_CPPFLAGS="${CPPFLAGS}"
lame_iconv_save_LDFLAGS="${LDFLAGS}"
CPPFLAGS="${CPPFLAGS}${lame_iconv_incs}"
LDFLAGS="${LDFLAGS}${lame_iconv_libdirs}"
AM_ICONV
CPPFLAGS="${lame_iconv_save_CPPFLAGS}"
LDFLAGS="${lame_iconv_save_LDFLAGS}"

if test "x${am_func_iconv}" = "xyes"; then
	if test "x${am_cv_lib_iconv}" = "xyes"; then
		iconv_state="yes (separate library)"
	else
		iconv_state="yes (C library)"
	fi
else
	iconv_state="no - tags are not transliterated"
fi

LDADD="${LDADD}"
FRONTEND_CFLAGS="${INCICONV} ${FRONTEND_CFLAGS}"
FRONTEND_LDADD="${FRONTEND_LDADD} ${LTLIBICONV} ${CONFIG_MATH_LIB}"


AC_SUBST(INCLUDES)

AC_SUBST(FRONTEND_LDFLAGS)
AC_SUBST(FRONTEND_UNICODE_LDFLAGS)
AC_SUBST(FRONTEND_CFLAGS)
AC_SUBST(FRONTEND_LDADD)
AC_SUBST(LIBMP3LAME_LDADD)
AC_SUBST(CONFIG_MATH_LIB)
AC_SUBST(LDADD)

AC_SUBST(LIB_MAJOR_VERSION)
AC_SUBST(LIB_MINOR_VERSION)

AC_SUBST(MAKEDEP)
AC_SUBST(RM_F)

AC_SUBST(LIBTOOL_DEPS)

AC_SUBST(WITH_FRONTEND)
AC_SUBST(WITH_MP3X)
AC_SUBST(WITH_MP3RTP)
AC_SUBST(MP3RTP_MAN)
AC_SUBST(MP3X_MAN)

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
# The vector routines are named by the instruction sets they are BUILT for.
# Whether they are used is decided per CPU at run time, and "lame --verbose"
# reports what the running machine actually got.
lame_vector_sets=
if test "x${WITH_XMM}" = "xyes"; then
  lame_vector_sets="SSE2"
fi
if test "x${WITH_AVX2}" = "xyes"; then
  lame_vector_sets="${lame_vector_sets} AVX2"
fi
if test "x${WITH_AVX512}" = "xyes"; then
  lame_vector_sets="${lame_vector_sets} AVX-512"
fi
dnl Assigned rather than appended, like SSE2 above and for the same reason:
dnl it is the first rung of its architecture, and a host is x86 or ARM, never
dnl both.
if test "x${WITH_NEON}" = "xyes"; then
  lame_vector_sets="NEON"
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
echo "      GTK4 (>= 4.10) ...... ${HAVE_GTK:-no}"
echo "      iconv ............... ${iconv_state}"
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
