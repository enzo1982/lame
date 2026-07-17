## $Id$

AUTOMAKE_OPTIONS = foreign

docdir = $(datadir)/doc
pkgdocdir = $(docdir)/$(PACKAGE)
htmldir = $(docdir)/html
pkghtmldir = $(pkgdocdir)/html

pkghtml_DATA = \
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

pkghtmlimagesdir = $(pkghtmldir)/images
pkghtmlimages_DATA = \
	images/logo.gif \
	images/sflogo.gif \
	images/valid-css.png \
	images/valid-html401.png \
	images/valid-xhtml10.png

pkghtmlstylesdir = $(pkghtmldir)/styles
pkghtmlstyles_DATA = \
	styles/lame.css

EXTRA_DIST = $(pkghtml_DATA) $(pkghtmlimages_DATA) $(pkghtmlstyles_DATA)
