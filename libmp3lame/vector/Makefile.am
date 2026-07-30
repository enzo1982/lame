## $Id$

include $(top_srcdir)/Makefile.am.global

if WITH_XMM
noinst_LTLIBRARIES = liblamevectorroutines.la
if WITH_AVX2
noinst_LTLIBRARIES += liblamevectoravx2.la
endif
if WITH_AVX512
noinst_LTLIBRARIES += liblamevectoravx512.la
endif
endif
if WITH_NEON
noinst_LTLIBRARIES = liblamevectorneon.la
endif

##liblamecpuroutines_la_LIBADD = 
##liblamecpuroutines_la_LDFLAGS =

INCLUDES = @INCLUDES@ \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/mpglib \
	-I$(top_builddir)

DEFS = @DEFS@ @CONFIG_DEFS@

# Where the compiler cannot express "this instruction set for this function
# only", configure hands the translation unit a baseline instead. Each holds
# nothing but vector routines, and their call sites check the CPU at run time,
# so the resulting binary still runs where the instruction set is absent.
# Hence one library per instruction set: a baseline applies to a whole file,
# so files that need different ones cannot share it.
xmm_sources = xmm_quantize_sub.c xmm_choose_table.c xmm_quantize_lines.c \
	xmm_calc_sfb_noise.c
avx2_sources = avx2_choose_table.c avx2_quantize_lines.c
avx512_sources = avx512_choose_table.c avx512_quantize_lines.c
neon_sources = neon_choose_table.c

if WITH_XMM
liblamevectorroutines_la_SOURCES = $(xmm_sources)
liblamevectorroutines_la_CFLAGS = $(AM_CFLAGS) @VECTOR_SSE2_FLAGS@
if WITH_AVX2
liblamevectoravx2_la_SOURCES = $(avx2_sources)
liblamevectoravx2_la_CFLAGS = $(AM_CFLAGS) @VECTOR_AVX2_FLAGS@
endif
if WITH_AVX512
liblamevectoravx512_la_SOURCES = $(avx512_sources)
liblamevectoravx512_la_CFLAGS = $(AM_CFLAGS) @VECTOR_AVX512_FLAGS@
endif
endif

# VECTOR_NEON_FLAGS is empty on AArch64, where there is nothing to ask for;
# on 32-bit ARM it carries the whole-file opt-in configure found.
if WITH_NEON
liblamevectorneon_la_SOURCES = $(neon_sources)
liblamevectorneon_la_CFLAGS = $(AM_CFLAGS) @VECTOR_NEON_FLAGS@
endif

noinst_HEADERS = lame_intrin.h

EXTRA_liblamevectorroutines_la_SOURCES = $(xmm_sources)
EXTRA_liblamevectoravx2_la_SOURCES = $(avx2_sources)
EXTRA_liblamevectoravx512_la_SOURCES = $(avx512_sources)
EXTRA_liblamevectorneon_la_SOURCES = $(neon_sources)

CLEANFILES = lclint.txt

LCLINTFLAGS= \
	+posixlib \
	+showsummary \
	+showalluses \
	+whichlib \
	+forcehints \
	-fixedformalarray \
	+matchanyintegral \
	-Dlint

lclint.txt: ${liblamecpuroutines_la_SOURCES} ${noinst_HEADERS}
	@lclint ${LCLINTFLAGS} ${INCLUDES} ${DEFS} ${liblamecpuroutines_la_SOURCES} 2>&1 >lclint.txt || true

lclint: lclint.txt
	more lclint.txt

#$(OBJECTS): libtool
#libtool: $(LIBTOOL_DEPS)
#	$(SHELL) $(top_builddir)/config.status --recheck
