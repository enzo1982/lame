## $Id$

include $(top_srcdir)/Makefile.am.global

# The whole unit-test tree is gated on --enable-unit-tests. When it is off
# (the default) this file contributes no build rules at all, so a plain
# "make" / "make check" behaves exactly as it did before the harness existed.
if ENABLE_UNIT_TESTS

check_PROGRAMS = test_sanity
TESTS = $(check_PROGRAMS)

test_sanity_SOURCES = test_sanity.c
test_sanity_CPPFLAGS = $(CMOCKA_CFLAGS)
test_sanity_LDADD = $(CMOCKA_LIBS)

endif
