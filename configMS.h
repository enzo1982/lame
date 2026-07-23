#ifndef CONFIGMS_H_INCLUDED
#define CONFIGMS_H_INCLUDED

/* Silence the MSVC CRT deprecation warning (C4996) for the standard ISO C
   library functions LAME uses.  The "secure" _s replacements MSVC suggests are
   not portable, so the code stays on the standard names.  This header is the
   first include in every translation unit, so the definition takes effect
   before any CRT header is parsed.  Guarded so a build that already defines it
   on the command line does not trigger a redefinition warning. */
#ifndef _CRT_SECURE_NO_WARNINGS
# define _CRT_SECURE_NO_WARNINGS 1
#endif

/* The number of bytes in a double.  */
#define SIZEOF_DOUBLE 8

/* The number of bytes in a float.  */
#define SIZEOF_FLOAT 4

/* The number of bytes in a int.  */
#define SIZEOF_INT 4

/* The number of bytes in a long.  */
#define SIZEOF_LONG 4

/* The number of bytes in a long double.  */
#define SIZEOF_LONG_DOUBLE 12

/* The number of bytes in a short.  */
#define SIZEOF_SHORT 2

/* The number of bytes in a unsigned int.  */
#define SIZEOF_UNSIGNED_INT 4

/* The number of bytes in a unsigned long.  */
#define SIZEOF_UNSIGNED_LONG 4

/* The number of bytes in a unsigned short.  */
#define SIZEOF_UNSIGNED_SHORT 2

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS

/* Define if you have the <errno.h> header file.  */
#define HAVE_ERRNO_H

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H

/* Name of package */
#define PACKAGE "lame"

/* Define if compiler has function prototypes */
#define PROTOTYPES 1

/* faster log implementation with less but enough precission */
#define USE_FAST_LOG 1

#define HAVE_STRCHR
#define HAVE_MEMCPY

#if defined(_MSC_VER) || defined(__BORLANDC__)
#pragma warning( disable : 4305 )
	typedef __int8  int8_t;
	typedef __int16 int16_t;
	typedef __int32 int32_t;
	typedef __int64 int64_t;

	typedef unsigned __int8  uint8_t;
	typedef unsigned __int16 uint16_t;
	typedef unsigned __int32 uint32_t;
	typedef unsigned __int64 uint64_t;

	typedef float  float32_t;
	typedef double float64_t;
#elif defined (__GNUC__)
#define __int8_t_defined
#define uint8_t unsigned char
#define uint16_t unsigned short
#define uint32_t unsigned int
#define uint64_t unsigned long long

#define int8_t signed char
#define int16_t signed short
#define int32_t signed int
#define int64_t signed long long
#endif

typedef long double ieee854_float80_t;
typedef double      ieee754_float64_t;
typedef float       ieee754_float32_t;

#ifdef HAVE_MPG123
# define DECODE_ON_THE_FLY 1
#endif

#ifdef LAME_ACM
/* memory hacking for driver purposes */
#define calloc(x,y) acm_Calloc(x,y)
#define free(x)     acm_Free(x)
#define malloc(x)   acm_Malloc(x)

#include <stddef.h>
void *acm_Calloc( size_t num, size_t size );
void *acm_Malloc( size_t size );
void acm_Free( void * mem);
#endif /* LAME_ACM */

#define LAME_LIBRARY_BUILD


/* The vector routines are available on every x86 target here: unlike GCC and
 * Clang, which reject the intrinsics unless the function opts into the
 * instruction set, this compiler accepts them whatever /arch is in force.  So
 * the routines are always built and the CPU decides at run time whether they
 * are used, exactly as on the Autotools side.
 */
#if defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64) \
 || (defined(__ICL) && (__ICL >= 450))
    #define HAVE_SSE2_INTRINSICS
#endif

#endif
