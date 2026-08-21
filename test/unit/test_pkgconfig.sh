#!/bin/sh
# Does the installed pkg-config file describe what libmp3lame actually needs?
#
# A consumer that links libmp3lame statically gets its libraries from
# `pkg-config --static --libs lame`. If lame.pc understates them the link
# fails with undefined references; if it overstates them every consumer drags
# in libraries it has no use for, and on a cross-build those may not exist at
# all. Both directions are checked here.
#
# The oracle is libtool's own record of the link, libmp3lame.la's
# dependency_libs. That is written by the step that linked the library, so it
# is independent of the configure variables lame.pc is generated from -
# comparing lame.pc against those would put the same substitution on both
# sides of the comparison and could not fail.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
PKG_CONFIG="${PKG_CONFIG:-pkg-config}"

LA=$abs_top_builddir/libmp3lame/libmp3lame.la
PC=$abs_top_builddir/libmp3lame/lame.pc
WORK=./test_pkgconfig.dir

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

test -f "$PC" || die "no generated lame.pc at $PC"
test -f "$LA" || die "no libmp3lame.la at $LA - the library was not built, so
      there is nothing to compare lame.pc against. This is a failure and not a
      skip: an unbuilt library cannot show that lame.pc is right."

# ---------------------------------------------------------------- the subject
#
# Query a private copy under a name nothing else can own. Pointing
# PKG_CONFIG_PATH at the build directory would still let an installed lame.pc
# answer if the search order ever changed; a package name that exists only
# here cannot be answered by anything else. pkg-config takes the package name
# from the file name, so the copy needs no editing.
UUT=lame-buildtree-uut
cp "$PC" "$WORK/$UUT.pc" || die "cannot copy lame.pc"
PKG_CONFIG_PATH="$WORK${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH

$PKG_CONFIG --exists "$UUT" \
	|| die "pkg-config cannot use the generated lame.pc:
      $($PKG_CONFIG --print-errors --exists "$UUT" 2>&1)"

# An unsubstituted @FOO@ is the failure mode a human reading the file would
# miss and every consumer would hit.
if grep -q '@[A-Za-z_][A-Za-z0-9_]*@' "$PC"; then
	bad "lame.pc still contains an unsubstituted variable:
      $(grep -n '@[A-Za-z_][A-Za-z0-9_]*@' "$PC")"
else
	ok "every configure variable in lame.pc was substituted"
fi

# ----------------------------------------------------------------- the oracle
deps_raw=$(sed -n "s/^dependency_libs='\(.*\)'$/\1/p" "$LA")
lists() { printf '%s\n' "$1" | tr ' \t' '\n\n' | grep '^-l' | sort -u; }

DEP=$(lists "$deps_raw")
if test -z "$DEP"; then
	die "libmp3lame.la records no libraries at all (dependency_libs='$deps_raw').
      Every assertion below would then hold vacuously, so this is a failure of
      the test's own premise rather than a result."
fi
ok "libtool recorded $(printf '%s\n' "$DEP" | wc -l | tr -d ' ') librar(y/ies) for libmp3lame"

STATIC=$(lists "$($PKG_CONFIG --static --libs "$UUT")")
SHARED=$(lists "$($PKG_CONFIG --libs "$UUT")")

# ------------------------------------------------- 1. nothing is understated
#
# Through files rather than process substitution, which is not in POSIX sh and
# would leave this comparing nothing on a shell that lacks it.
printf '%s\n' "$DEP" >"$WORK/dep"
printf '%s\n' "$STATIC" >"$WORK/static"
missing=$(comm -23 "$WORK/dep" "$WORK/static")
if test -z "$missing"; then
	ok "every library libmp3lame links is reachable from --static --libs"
else
	bad "libraries libmp3lame links are missing from lame.pc: $(echo $missing)"
fi

# -------------------------------------------------- 2. nothing is overstated
#
# What a consumer is allowed to be given: libmp3lame itself, the libraries
# libmp3lame links, and whatever the packages named in Requires.private
# declare for themselves. That last part is not slack - it is the whole point
# of naming a package instead of a library, and leaving it out would make this
# fail wherever libmpg123 grows a private dependency of its own.
allowed=$WORK/allowed
{
	echo "-lmp3lame"
	printf '%s\n' "$DEP"
	for pkg in $(sed -n 's/^Requires\.private: *//p' "$PC" | tr ',' ' '); do
		case $pkg in
		[0-9]* | '<'* | '>'* | '='* | '!'*) continue ;;   # version operands
		esac
		lists "$($PKG_CONFIG --static --libs "$pkg" 2>/dev/null)"
	done
} | sort -u >"$allowed"
extra=$(comm -23 "$WORK/static" "$allowed")
if test -z "$extra"; then
	ok "--static --libs names nothing beyond libmp3lame and its own dependencies"
else
	bad "lame.pc offers libraries libmp3lame does not link: $(echo $extra)"
fi

# ------------------- 3. the frontend's libraries are not in the library's file
#
# Checked against the text of lame.pc rather than the expanded link line: this
# is about what LAME declares, and a third party's file must not be able to
# make it fail. libsndfile, GTK, the terminal and socket libraries belong to
# the command line tools, which nobody links against.
decl=$(sed -n 's/^\(Requires\|Requires\.private\|Libs\|Libs\.private\): *//p' "$PC")
frontend_only=
for w in sndfile gtk-4 gtk4 ncurses termcap curses iconv efence vorbis ogg; do
	case " $decl " in *"$w"*) frontend_only="$frontend_only $w" ;; esac
done
if test -z "$frontend_only"; then
	ok "lame.pc declares no library that belongs to the frontend"
else
	bad "lame.pc declares frontend-only librar(y/ies):$frontend_only"
fi

# ---------------------------------------- 4. private stays out of the shared line
if test "$SHARED" = "-lmp3lame"; then
	ok "--libs names libmp3lame alone, so the private fields stay private"
else
	bad "--libs should name libmp3lame alone, it names: $(echo $SHARED)"
fi

# ------------------------------------- 5. every package named can be resolved
unresolved=
for pkg in $(sed -n 's/^Requires\.private: *//p' "$PC" | tr ',' ' '); do
	case $pkg in
	[0-9]* | '<'* | '>'* | '='* | '!'*) continue ;;
	esac
	$PKG_CONFIG --exists "$pkg" || unresolved="$unresolved $pkg"
done
if test -z "$unresolved"; then
	ok "every package named in Requires.private resolves"
else
	bad "Requires.private names package(s) pkg-config cannot find:$unresolved"
fi

# --------------------------- 6. the version agrees with the library's own header
hdr=${abs_top_srcdir:-$abs_top_builddir}/libmp3lame/version.h
if test -f "$hdr"; then
	maj=$(sed -n 's/^# *define  *LAME_MAJOR_VERSION  *\([0-9][0-9]*\).*/\1/p' "$hdr")
	min=$(sed -n 's/^# *define  *LAME_MINOR_VERSION  *\([0-9][0-9]*\).*/\1/p' "$hdr")
	pcv=$($PKG_CONFIG --modversion "$UUT")
	if test -z "$maj" || test -z "$min"; then
		bad "could not read the version out of $hdr - the check compared nothing"
	elif expr "$pcv" : "$maj\\.$min" >/dev/null; then
		ok "lame.pc reports $pcv, agreeing with version.h ($maj.$min)"
	else
		bad "lame.pc reports $pcv but version.h says $maj.$min"
	fi
else
	bad "no version.h at $hdr - the version could not be cross-checked"
fi

exit $rc
