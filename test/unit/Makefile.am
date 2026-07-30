## $Id$

include $(top_srcdir)/Makefile.am.global

# The whole unit-test tree is gated on --enable-unit-tests. When it is off
# (the default) this file contributes no build rules at all, so a plain
# "make" / "make check" behaves exactly as it did before the harness existed.
if ENABLE_UNIT_TESTS

check_PROGRAMS = test_sanity test_get_audio_aiff test_parse_pathlen test_merge_argv \
	test_id3tag test_parse_id3 test_encode_nonfinite test_encode_mono_interleaved \
	test_totalframes test_set_get test_lib_info test_vector_routines

# The vectorised routines exist only where the vector code was built, so there
# is nothing to test otherwise.
if WITH_XMM
check_PROGRAMS += test_choose_table_vector test_quantize_lines_vector \
	test_calc_sfb_noise_vector
endif
if WITH_NEON
check_PROGRAMS += test_choose_table_neon
endif

TESTS = $(check_PROGRAMS)

# Every test here links libmp3lame, and every one of them has to load the copy
# this build just produced - not one the host happens to have installed. That
# is not automatic. Libtool's Unix wrapper is a shell script and exports
# LD_LIBRARY_PATH itself, but on Windows it generates a wrapper executable that
# does nothing equivalent for PATH, so the suite there loads the installed DLL
# instead and reports on a library nobody asked about. Naming the directory
# here settles it the same way on every platform rather than leaving it to
# libtool. Both variables are set because the two families of systems consult
# different ones, and neither is harmful where it is not used.
AM_TESTS_ENVIRONMENT = \
	LD_LIBRARY_PATH="$(abs_top_builddir)/libmp3lame/.libs$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}"; \
	export LD_LIBRARY_PATH; \
	PATH="$(abs_top_builddir)/libmp3lame/.libs:$$PATH"; \
	export PATH;

test_sanity_SOURCES = test_sanity.c
test_sanity_CPPFLAGS = $(CMOCKA_CFLAGS)
test_sanity_LDADD = $(CMOCKA_LIBS)

# The AIFF header parser lives in frontend/get_audio.c and is static, so the
# test compiles it in directly (hence the frontend/libmp3lame/include search
# paths). fread is wrapped to trap the underflow spin the guard prevents.
test_get_audio_aiff_SOURCES = test_get_audio_aiff.c test_stubs.c
test_get_audio_aiff_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/frontend \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_get_audio_aiff_LDFLAGS = -Wl,--wrap=fread
test_get_audio_aiff_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la

# The positional-filename length guard set_path_arg() lives in
# frontend/parse.c and is static, so the test compiles that file in directly;
# parse_test_stubs.c supplies the console/file helpers it references and
# libmp3lame provides the lame_* API.
test_parse_pathlen_SOURCES = test_parse_pathlen.c parse_test_stubs.c
test_parse_pathlen_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/frontend \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_parse_pathlen_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	$(LTLIBICONV) -lm

# merge_argv() is likewise static in frontend/parse.c; same include/link setup.
test_merge_argv_SOURCES = test_merge_argv.c parse_test_stubs.c
test_merge_argv_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/frontend \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_merge_argv_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	$(LTLIBICONV) -lm

# The id3tag_* API is public, so this test links libmp3lame and calls it
# directly - no frontend translation unit compiled in.
test_id3tag_SOURCES = test_id3tag.c
test_id3tag_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_id3tag_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# The frontend ID3 dispatch (id3_tag/set_id3v2tag_*) is static in parse.c, so
# this test compiles parse.c in directly; same include/link setup as the other
# parse.c tests.
test_parse_id3_SOURCES = test_parse_id3.c parse_test_stubs.c
test_parse_id3_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/frontend \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_parse_id3_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	$(LTLIBICONV) -lm

# The encoding entry points are public, so this test links libmp3lame and calls
# them directly - no frontend translation unit compiled in.
test_encode_nonfinite_SOURCES = test_encode_nonfinite.c
test_encode_nonfinite_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_encode_nonfinite_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# The interleaved entry points are public, so this test links libmp3lame and
# calls them directly - no frontend translation unit compiled in.
test_encode_mono_interleaved_SOURCES = test_encode_mono_interleaved.c
test_encode_mono_interleaved_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_encode_mono_interleaved_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# lame_get_totalframes() is public, so this test links libmp3lame and calls it
# directly - no frontend translation unit compiled in.
test_totalframes_SOURCES = test_totalframes.c
test_totalframes_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_totalframes_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# The version/build-information functions and the two table accessors are
# public and take no encoder instance, so this test links libmp3lame and calls
# them directly - no frontend translation unit compiled in.
test_lib_info_SOURCES = test_lib_info.c
test_lib_info_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_lib_info_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# The vector-routines API is public and architecture-neutral by design - it
# reports whatever this build carries - so this test links libmp3lame and is
# built everywhere, with no WITH_XMM/WITH_NEON gate. A build with no vector
# routines at all is one of the configurations it has to hold for.
test_vector_routines_SOURCES = test_vector_routines.c
test_vector_routines_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_vector_routines_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# Exercises the parameter API in set_get.c. The exported getters/setters are
# tested in every build; the internal-only tuning setters are additionally
# tested when configured with --enable-internal (the same gate the frontend
# uses), which defines _ALLOW_INTERNAL_OPTIONS and pulls in the private
# libmp3lame/set_get.h - hence the extra include path. Those internal symbols
# are withheld from the shared library by include/libmp3lame.sym, so under
# --enable-internal this test links the STATIC archive (-static) to reach them;
# a normal build tests only the exported interface and links the shared library.
test_set_get_SOURCES = test_set_get.c
test_set_get_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_builddir)
if INTERNAL_OPTS
test_set_get_CPPFLAGS += -D_ALLOW_INTERNAL_OPTIONS
test_set_get_LDFLAGS = -static
endif
test_set_get_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	-lm

# The table-search routines are internal to the vector convenience libraries
# and are not part of libmp3lame's exported interface, so this test links
# those libraries directly. It needs nothing else from the encoder: each
# routine is a function of its arguments, tables included.
if WITH_XMM
test_choose_table_vector_SOURCES = test_choose_table_vector.c
test_choose_table_vector_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_choose_table_vector_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/vector/liblamevectorroutines.la
if WITH_AVX2
test_choose_table_vector_LDADD += $(top_builddir)/libmp3lame/vector/liblamevectoravx2.la
endif
if WITH_AVX512
test_choose_table_vector_LDADD += $(top_builddir)/libmp3lame/vector/liblamevectoravx512.la
endif

# Same arrangement for the quantization loop, and for the same reason.
test_quantize_lines_vector_SOURCES = test_quantize_lines_vector.c
test_quantize_lines_vector_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_quantize_lines_vector_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/vector/liblamevectorroutines.la \
	-lm
if WITH_AVX2
test_quantize_lines_vector_LDADD += $(top_builddir)/libmp3lame/vector/liblamevectoravx2.la
endif
if WITH_AVX512
test_quantize_lines_vector_LDADD += $(top_builddir)/libmp3lame/vector/liblamevectoravx512.la
endif

# The VBR scalefactor-band noise gets the same arrangement, but deliberately no
# AVX-512 line: that tier was written, measured and rejected, so there is no
# liblamevectoravx512 contribution here to link.  See @ref vector_dispatch.
test_calc_sfb_noise_vector_SOURCES = test_calc_sfb_noise_vector.c
test_calc_sfb_noise_vector_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_calc_sfb_noise_vector_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/vector/liblamevectorroutines.la \
	-lm
if WITH_AVX2
test_calc_sfb_noise_vector_LDADD += $(top_builddir)/libmp3lame/vector/liblamevectoravx2.la
endif
endif

# The ARM tier carries one routine, so it gets one test program rather than an
# arm of the x86 file - that one names the SSE2, AVX2 and AVX-512 routines
# throughout, so there is no shared body to add to.
#
# OUTSIDE the WITH_XMM block above, which is the whole point: the first version
# of this sat inside it, so on an ARM host - where WITH_XMM is false - the
# _SOURCES and _LDADD never expanded while the program stayed in
# check_PROGRAMS. automake emitted a link command with no objects and no
# libraries at all, and the failure was `undefined symbol: main`, which names
# neither the program nor the conditional.
if WITH_NEON
test_choose_table_neon_SOURCES = test_choose_table_neon.c
test_choose_table_neon_CPPFLAGS = $(CMOCKA_CFLAGS) \
	-I$(top_srcdir)/libmp3lame \
	-I$(top_srcdir)/include \
	-I$(top_builddir)
test_choose_table_neon_LDADD = $(CMOCKA_LIBS) \
	$(top_builddir)/libmp3lame/vector/liblamevectorneon.la
endif

endif

# Kept in the tarball whether or not this build configured the vector code,
# so a dist made on one architecture still carries the tests for another.
EXTRA_DIST = test_choose_table_vector.c test_quantize_lines_vector.c \
	test_calc_sfb_noise_vector.c test_choose_table_neon.c

# Header-only unused-parameter marker shared by the tests; not a _SOURCES file,
# so it has to be named for the tarball explicitly.
EXTRA_DIST += test_unused.h
