## $Id$

include $(top_srcdir)/Makefile.am.global

# What configure found: the include path, and the flags for the
# optional libraries. A per-target CPPFLAGS setting replaces this one
# rather than adding to it, so the targets below that have their own
# name it explicitly.
AM_CPPFLAGS = @LAME_CPPFLAGS@

EXTRA_PROGRAMS = abx ath scalartest

CLEANFILES = $(EXTRA_PROGRAMS)

EXTRA_SCRIPTS = \
	auenc \
	lameid3.pl \
	mugeco.sh \
	mlame \
	mk_mp3.sh

EXTRA_DIST = \
	$(EXTRA_SCRIPTS) \
	Lame.vbs \
	lameGUI.html \
	lame4dos.bat \
	mlame_corr.c

abx_SOURCES = abx.c

ath_SOURCES = ath.c

scalartest_SOURCES = scalartest.c

