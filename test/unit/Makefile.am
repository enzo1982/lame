## $Id$

include $(top_srcdir)/Makefile.am.global

# The whole unit-test tree is gated on --enable-unit-tests. When it is off
# (the default) this file contributes no build rules at all, so a plain
# "make" / "make check" behaves exactly as it did before the harness existed.
if ENABLE_UNIT_TESTS

check_PROGRAMS = test_sanity test_get_audio_aiff test_parse_pathlen test_merge_argv
TESTS = $(check_PROGRAMS)

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

endif
