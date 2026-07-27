#!/bin/sh
#
# gen-api-docs.sh - build the API documentation and copy it into the website
# tree.
#
# Generates both sets - the public API reference and the internal developer
# documentation - and clean-copies them to webpages/API/public and
# webpages/API/internal. It never commits and never uploads: publishing the
# website is the maintainer's step, exactly like committing to SVN.
#
# Both sets are published on purpose. The public one is the API reference a
# library user needs; the internal one is for someone working on LAME itself
# and carries its own "not an API" banner. The website links them
# asymmetrically to match - see doc/maintainer-gen-api-docs.md.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")
script_dir=$(cd "$(dirname "$0")" && pwd)

usage() {
	cat <<EOF
Usage: $prog [-b DIR] [-s SRCDIR] [-w WEBDIR] [-n] [-h]

  -b DIR     Build directory to generate the documentation in. If it is
             already configured, it is reused as is; otherwise configure is
             run in it first. (default: a "doc-build" directory beside the
             website tree)
  -s SRCDIR  LAME source directory (the one containing "configure").
             (default: the parent of this script's directory)
  -w WEBDIR  The website working tree to copy into.
             (default: SRCDIR/../webpages - the sibling checkout)
  -n         Generate but do not copy. For looking at the result before it
             goes anywhere near the website tree.
  -h         Show this help and exit.

The copy is a clean copy: API/public and API/internal are removed before it,
so a page that no longer exists does not linger on the website.
EOF
}

builddir=
srcdir=
webdir=
copy=yes

while getopts "b:s:w:nh" opt; do
	case "$opt" in
		b) builddir=$OPTARG ;;
		s) srcdir=$OPTARG ;;
		w) webdir=$OPTARG ;;
		n) copy=no ;;
		h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done

# --- locate the trees -------------------------------------------------------

if [ -z "$srcdir" ]; then
	srcdir=$(cd "$script_dir/.." && pwd)
else
	srcdir=$(cd "$srcdir" 2>/dev/null && pwd) || {
		echo "$prog: source directory '$srcdir' not found" >&2
		exit 1
	}
fi
if [ ! -x "$srcdir/configure" ]; then
	echo "$prog: '$srcdir/configure' not found or not executable." >&2
	echo "$prog: run autoreconf in the source tree first, or pass -s SRCDIR." >&2
	exit 1
fi

if [ -z "$webdir" ]; then
	webdir=$srcdir/../webpages
fi
if [ "$copy" = yes ]; then
	webdir=$(cd "$webdir" 2>/dev/null && pwd) || {
		echo "$prog: website tree '$webdir' not found." >&2
		echo "$prog: it is the 'webpages' checkout beside the lame source tree;" >&2
		echo "$prog: pass -w WEBDIR, or -n to generate without copying." >&2
		exit 1
	}
	# Refuse to write into something that merely has the right name.
	if [ ! -f "$webdir/menu.html" ]; then
		echo "$prog: '$webdir' has no menu.html - that is not the LAME website tree." >&2
		exit 1
	fi
fi

if [ -z "$builddir" ]; then
	builddir=$(dirname "$webdir")/doc-build
fi
mkdir -p "$builddir" || { echo "$prog: cannot create '$builddir'" >&2; exit 1; }
builddir=$(cd "$builddir" && pwd)

echo "$prog: source  : $srcdir"
echo "$prog: build   : $builddir"
if [ "$copy" = yes ]; then
	echo "$prog: website : $webdir"
else
	echo "$prog: website : (not copying, -n given)"
fi

# --- generate ---------------------------------------------------------------

if [ ! -f "$builddir/config.status" ]; then
	echo "$prog: configuring in $builddir"
	( cd "$builddir" && "$srcdir/configure" ) >"$builddir/configure.log" 2>&1 || {
		echo "$prog: configure failed, see $builddir/configure.log" >&2
		exit 1
	}
fi

pubsrc="$builddir/doc/doxygen-output/html"
intsrc="$builddir/doc/doxygen-internal/html"

# Doxygen does not clear its output directory, and this build directory is
# reused between runs. So a run that parses nothing at all still leaves the
# PREVIOUS run's pages in place - which would sail past the emptiness check
# below and publish stale documentation as though it were fresh. Start from
# nothing, so what is published is only ever what this run produced.
rm -rf "$builddir/doc/doxygen-output" "$builddir/doc/doxygen-internal"

for t in doxygen doxygen-internal; do
	echo "$prog: make $t"
	( cd "$builddir" && make -C doc "$t" ) >"$builddir/$t.log" 2>&1 || {
		echo "$prog: 'make $t' failed, see $builddir/$t.log" >&2
		exit 1
	}
done

# A doxygen run that reads no input still writes a full set of stylesheets and
# images and exits 0 - a directory that looks entirely healthy and contains no
# documentation at all. Copying that over the website would replace a working
# reference with an empty shell, and nothing would have reported an error. So
# require a page that can only exist if a source file was really parsed.
fail=0
for pair in "$pubsrc:lame_8h_source.html" "$intsrc:lame_8h.html"; do
	d=${pair%:*}
	f=${pair#*:}
	if [ ! -d "$d" ]; then
		echo "$prog: $d was not generated" >&2
		fail=1
	elif [ ! -f "$d/$f" ]; then
		echo "$prog: $d contains no parsed source (no $f) - refusing to publish it" >&2
		fail=1
	fi
done
[ "$fail" -eq 0 ] || exit 1

echo "$prog: generated $(ls "$pubsrc" | wc -l | tr -d ' ') public and $(ls "$intsrc" | wc -l | tr -d ' ') internal file(s)"

if [ "$copy" = no ]; then
	echo
	echo "Generated only (-n). The documentation is under:"
	echo "  public   : $pubsrc"
	echo "  internal : $intsrc"
	exit 0
fi

# --- copy -------------------------------------------------------------------

api="$webdir/API"
for half in public internal; do
	dst="$api/$half"
	rm -rf "$dst"
	mkdir -p "$dst" || { echo "$prog: cannot create '$dst'" >&2; exit 1; }
done
cp -R "$pubsrc"/. "$api/public/" || { echo "$prog: copy of the public docs failed" >&2; exit 1; }
cp -R "$intsrc"/. "$api/internal/" || { echo "$prog: copy of the internal docs failed" >&2; exit 1; }

npub=$(find "$api/public" -type f | wc -l | tr -d ' ')
nint=$(find "$api/internal" -type f | wc -l | tr -d ' ')

# The link the website uses is API/<half>/index.html; if doxygen ever stops
# producing one, every link on the site would 404 and nothing here would have
# noticed.
for half in public internal; do
	[ -f "$api/$half/index.html" ] || {
		echo "$prog: WARNING: $api/$half/index.html is missing - the website links to it" >&2
	}
done

cat <<EOF

Copied into the website tree:
  $api/public    ($npub files)  <- linked from developers.php, using.php
  $api/internal  ($nint files)  <- linked from developers.php, inside.php

Nothing has been committed or published. That is the maintainer's step:
review the change in $webdir, then commit and upload it.
EOF
