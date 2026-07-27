## $Id$

include $(top_srcdir)/Makefile.am.global

bin_PROGRAMS = @WITH_FRONTEND@ @WITH_MP3RTP@ @WITH_MP3X@
EXTRA_PROGRAMS = lame$(EXEEXT) mp3rtp$(EXEEXT) mp3x$(EXEEXT)

DEFS = @DEFS@ @CONFIG_DEFS@

common_sources = \
	main.c \
	brhist.c \
	console.c \
	get_audio.c \
	lametime.c \
	parse.c \
	timestatus.c

noinst_HEADERS = \
	brhist.h \
	console.h \
	get_audio.h \
	gtkanal.h \
	gpkplotting.h \
	mp3x_core.h \
	mp3x_plot.h \
	mp3x_session.h \
	lametime.h \
	main.h \
	parse.h \
	rtp.h \
	timestatus.h

lame_SOURCES = lame_main.c $(common_sources)
mp3rtp_SOURCES = mp3rtp.c rtp.c $(common_sources)
# The frame analyzer is now the GTK4 frontend (mp3x_ui.c + mp3x_plot.c)
# driving the toolkit-independent analyzer engine (mp3x_core.c).  The
# per-file session lifecycle (open/close/replace, async File dialog
# integration, frontend-global baseline management) lives in
# mp3x_session.c.  The program keeps its name, so
# "configure --enable-mp3x && make && ./mp3x" is unchanged for anyone
# building it.
mp3x_SOURCES = mp3x_ui.c mp3x_plot.c mp3x_core.c mp3x_session.c $(common_sources)

# The superseded GTK 1.2 implementation.  It is no longer compiled, but it
# is still shipped: naming it here keeps "make dist" including it, since
# automake only distributes files some target actually mentions.  Dropping
# this line would remove GTK1 support from the tarball by accident.
EXTRA_DIST = mp3x.c gtkanal.c gpkplotting.c README.mp3x-gtk4

CFLAGS = @CFLAGS@ @GTK_CFLAGS@ @FRONTEND_CFLAGS@ @SNDFILE_CFLAGS@
LDFLAGS = @LDFLAGS@ @FRONTEND_LDFLAGS@ @FRONTEND_UNICODE_LDFLAGS@ @SNDFILE_LIBS@

# --enable-internal exposes the developer tuning options in parse.c. The macro
# is defined here (frontend only), not globally, so other consumers of parse.c
# are unaffected. configure has already forced -static into FRONTEND_LDFLAGS,
# so the frontend can reach the internal setters, which the shared library does
# not export.
if INTERNAL_OPTS
AM_CPPFLAGS = -D_ALLOW_INTERNAL_OPTIONS
endif

INCLUDES = @INCLUDES@ -I$(top_srcdir)/libmp3lame -I$(top_srcdir)/include \
	-I$(top_srcdir) -I$(top_builddir)

LDADD = @LDADD@ \
	$(top_builddir)/libmp3lame/libmp3lame.la \
	@FRONTEND_LDADD@

mp3x_LDADD = $(LDADD) @GTK_LIBS@

CLEANFILES = lclint.txt $(EXTRA_PROGRAMS)

LCLINTFLAGS= \
	+posixlib \
	+showsummary \
	+showalluses \
	+whichlib \
	+forcehints \
	-fixedformalarray \
	+matchanyintegral \
	-Dlint

lclint.txt: ${lame_SOURCES} ${noinst_HEADERS}
	@lclint ${LCLINTFLAGS} ${INCLUDES} ${DEFS} ${lame_SOURCES} 2>&1 >lclint.txt || true

lclint: lclint.txt
	more lclint.txt
