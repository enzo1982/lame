## $Id$

include $(top_srcdir)/Makefile.am.global

SUBDIRS = html man

# API documentation via Doxygen, generated on demand only (not part of all/check).
#
# Two configurations share a common base (Doxyfile-common), each @INCLUDE-ing it
# and overriding only what differs:
#   Doxyfile          - public API documentation       ("make doxygen")
#   Doxyfile-internal - internals plus the unit tests   ("make doxygen-internal")
DOT = @DOT@

# Expand the @...@ placeholders in a Doxyfile template. The template to read and
# the output redirection are appended by each rule below.
edit_doxyfile = \
	if test -n "$(DOT)"; then have_dot=YES; dot_path=`dirname "$(DOT)"`; \
	else have_dot=NO; dot_path=; fi; \
	strip_path=`dirname "$(top_srcdir)"`; \
	sed -e 's|@top_srcdir@|$(top_srcdir)|g' \
	    -e "s|@HAVE_DOT@|$$have_dot|g" \
	    -e "s|@DOT_PATH@|$$dot_path|g" \
	    -e "s|@STRIP_FROM_PATH@|$$strip_path|g" \
	    -e 's|@PROJECT_NUMBER@|$(PACKAGE_VERSION)|g'

Doxyfile-common: $(srcdir)/Doxyfile-common.in Makefile
	$(edit_doxyfile) $(srcdir)/Doxyfile-common.in > $@

Doxyfile: $(srcdir)/Doxyfile.in Doxyfile-common Makefile
	$(edit_doxyfile) $(srcdir)/Doxyfile.in > $@

Doxyfile-internal: $(srcdir)/Doxyfile-internal.in Doxyfile-common Makefile
	$(edit_doxyfile) $(srcdir)/Doxyfile-internal.in > $@

doxygen: Doxyfile
	doxygen Doxyfile

doxygen-internal: Doxyfile-internal
	doxygen Doxyfile-internal

CLEANFILES = Doxyfile Doxyfile-common Doxyfile-internal

EXTRA_DIST = Doxyfile.in Doxyfile-common.in Doxyfile-internal.in \
	maintainer-build-matrix.md \
	doxygen/mainpage.md \
	doxygen/internal.md \
	doxygen/unit_tests.dox \
	doxygen/lame-theme.css \
	doxygen/doxygen-awesome-css/doxygen-awesome.css \
	doxygen/logo/lame_logo.svg \
	doxygen/logo/lame_logo_full.svg

.PHONY: doxygen doxygen-internal
