## $Id$

AUTOMAKE_OPTIONS = foreign

# docdir comes from configure - it is one of the standard directory variables,
# and --docdir sets it. Assigning to it here overrode whatever the user asked
# for. The pages keep their own subdirectory below it, under a name of our own
# rather than htmldir, which is another variable configure owns and which
# already means "where the HTML documentation goes".
lamehtmldir = $(docdir)/html

lamehtml_DATA = \
    about.html \
	abr.html \
	cbr.html \
	contact.html \
	contributors.html \
	detailed.html \
	history.html \
	index.html \
	introduction.html \
	links.html \
	list.html \
	ms_stereo.html \
	usage.html \
	vbr.html

lamehtmlimagesdir = $(lamehtmldir)/images
lamehtmlimages_DATA = \
	images/logo.gif \
	images/sflogo.gif \
	images/valid-css.png \
	images/valid-html401.png \
	images/valid-xhtml10.png

lamehtmlstylesdir = $(lamehtmldir)/styles
lamehtmlstyles_DATA = \
	styles/lame.css

EXTRA_DIST = $(lamehtml_DATA) $(lamehtmlimages_DATA) $(lamehtmlstyles_DATA)
