## $Id$

include $(top_srcdir)/Makefile.am.global

if WITH_XMM
noinst_LTLIBRARIES = liblamevectorroutines.la
if WITH_AVX2
noinst_LTLIBRARIES += liblamevectoravx2.la
endif
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
xmm_sources = xmm_quantize_sub.c xmm_choose_table.c xmm_quantize_lines.c
avx2_sources = avx2_choose_table.c avx2_quantize_lines.c

if WITH_XMM
liblamevectorroutines_la_SOURCES = $(xmm_sources)
liblamevectorroutines_la_CFLAGS = $(AM_CFLAGS) @VECTOR_SSE2_FLAGS@
if WITH_AVX2
liblamevectoravx2_la_SOURCES = $(avx2_sources)
liblamevectoravx2_la_CFLAGS = $(AM_CFLAGS) @VECTOR_AVX2_FLAGS@
endif
endif

noinst_HEADERS = lame_intrin.h

EXTRA_liblamevectorroutines_la_SOURCES = $(xmm_sources)
EXTRA_liblamevectoravx2_la_SOURCES = $(avx2_sources)

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
