## $Id$

include $(top_srcdir)/Makefile.am.global

SUBDIRS = unit

# Hand-maintained test material that predates the automated harness, and the
# tests for the Windows client components under clients/. Neither is built
# here: the clients are MSBuild-only, since ACM/ and dshow/ are EXTRA_DIST
# themselves and this build never compiles them. List them so "make dist"
# keeps them, which is what puts them in the hands of whoever unpacks the
# tarball with a Visual Studio.
EXTRA_DIST = \
	clients/ctest.h \
	clients/acm_test.cpp \
	lame_test.cpp \
	lametest.py \
	cvscheck.sh \
	CBRABR.op \
	VBR.op \
	VBRold.op \
	misc.op \
	nores.op \
	shortCBRABR.op \
	shortVBR.op
