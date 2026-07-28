#!/bin/sh
#
# check-dist.sh - validate a LAME distribution tarball.
#
# Takes a tarball and a target directory, extracts it, builds it on this
# system through the build-configuration matrix, runs the unit tests in each
# cell, and reports every step as a named check with a one-line description
# and a PASS / FAIL / SKIP / N/A verdict. It never stops at the first failure:
# a release candidate is more useful fully surveyed than abandoned early.
#
# What it answers is "is this tarball a usable release", not "is this a good
# encoder". Encoding quality is a separate question with its own harness; see
# doc/maintainer-quality.md.
#
# Everything it prints also lands in a log in the target directory, and the
# exit status is non-zero if any check FAILed. A check that could not be made
# reports SKIP (a prerequisite is missing here) or N/A (it does not apply to
# this platform at all) and never affects the exit status - the distinction
# matters when reading a log from someone else's machine.
#
# The native Windows build (nmake / MSBuild) is not reachable from here; use
# the PowerShell sibling check-dist.ps1 for those cells.
#
# See doc/maintainer-check-dist.md for the full guide.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog [options] TARBALL TARGETDIR

  TARBALL     the distribution tarball to validate (.tar.gz / .tar.bz2 / .tar.xz)
  TARGETDIR   a directory to work in; it is created if missing. The extracted
              source, the build matrix and the log all go inside it.

Options:
  -q, --quick         one default configuration instead of the full matrix,
                      plus its unit tests and distcheck. For a fast look; a
                      release acceptance run wants the default full matrix.
  -j N                parallel make jobs per build (default: detected CPUs)
      --maintainer-mode
                      add a --enable-maintainer-mode cell, which turns
                      warnings into errors. Off by default: a warning a new
                      compiler version introduces is not a reason to reject a
                      tarball that was correct when it was rolled.
      --audio-dir DIR encode every file in DIR through a built cell and
                      require success and a non-empty MP3. A robustness smoke
                      test, not a quality measurement - nothing is compared
                      against a reference.
      --coverage      add a coverage run (slow).
      --sanitizers    add an ASan/UBSan run (slow).
  -h, --help          show this help and exit.

Exit status: 0 if no check FAILed, 1 otherwise, 2 for a usage error.
EOF
}

# --- logging ----------------------------------------------------------------
#
# The whole run is tee'd to a log in the target directory. That has to happen
# before anything prints, so it happens before the options are parsed: the
# target directory is the last operand, which is all this needs to know. A
# malformed command line is left to the inner pass, which prints the usage and
# exits 2 without creating anything.
#
# The log is named after the version, which is not known until the tarball has
# been extracted and the first check has already printed. So it is written
# under a timestamped name and renamed at the end from what the inner pass
# leaves in a version file.

if [ -z "${CHECK_DIST_INNER:-}" ] && [ $# -ge 2 ]; then
	for a in "$@"; do outer_target=$a; done
	# If the last word looks like an option the command line is malformed;
	# leave it to the inner pass rather than creating a directory named
	# "--quick" on the way to printing the usage.
	case "$outer_target" in -*) outer_target= ;; esac
	if [ -n "$outer_target" ] && mkdir -p "$outer_target" 2>/dev/null; then
		outer_target=$(cd "$outer_target" && pwd)
		stamp=$(date '+%Y%m%d-%H%M%S')
		log="$outer_target/check-dist-$stamp.log"
		verfile="$outer_target/.check-dist-version"
		st="$outer_target/.check-dist-status"
		rm -f "$verfile" "$st"
		CHECK_DIST_INNER=1
		export CHECK_DIST_INNER
		{ sh "$0" "$@"; echo $? > "$st"; } 2>&1 | tee "$log"
		rc=$(cat "$st" 2>/dev/null || echo 1)
		rm -f "$st"
		if [ -s "$verfile" ]; then
			v=$(cat "$verfile")
			if mv -f "$log" "$outer_target/check-dist-$v-$stamp.log" 2>/dev/null; then
				log="$outer_target/check-dist-$v-$stamp.log"
			fi
			rm -f "$verfile"
		fi
		echo
		echo "log: $log"
		exit "$rc"
	fi
fi

# ---------------------------------------------------------------------------
# From here on this is the inner pass: its output is being tee'd.
# ---------------------------------------------------------------------------

# --- options ----------------------------------------------------------------

quick=no
jobs=
maintainer_mode=no
audio_dir=
do_coverage=no
do_sanitizers=no

while [ $# -gt 0 ]; do
	case "$1" in
		-q|--quick)          quick=yes ;;
		-j)                  shift; jobs=${1:-} ;;
		--maintainer-mode)   maintainer_mode=yes ;;
		--audio-dir)         shift; audio_dir=${1:-} ;;
		--coverage)          do_coverage=yes ;;
		--sanitizers)        do_sanitizers=yes ;;
		-h|--help)           usage; exit 0 ;;
		--)                  shift; break ;;
		-*)                  echo "$prog: unknown option '$1'" >&2; usage >&2; exit 2 ;;
		*)                   break ;;
	esac
	shift
done

if [ $# -ne 2 ]; then
	usage >&2
	exit 2
fi
tarball=$1
target=$2

[ -f "$tarball" ] || { echo "$prog: '$tarball' is not a file" >&2; exit 2; }
tarball=$(cd "$(dirname "$tarball")" && pwd)/$(basename "$tarball")
mkdir -p "$target" || { echo "$prog: cannot create '$target'" >&2; exit 2; }
target=$(cd "$target" && pwd)

if [ -n "$audio_dir" ]; then
	[ -d "$audio_dir" ] || { echo "$prog: --audio-dir '$audio_dir' is not a directory" >&2; exit 2; }
	audio_dir=$(cd "$audio_dir" && pwd)
fi

if [ -z "$jobs" ]; then
	jobs=$(nproc 2>/dev/null \
		|| sysctl -n hw.ncpu 2>/dev/null \
		|| getconf _NPROCESSORS_ONLN 2>/dev/null \
		|| echo 1)
fi

verfile="$target/.check-dist-version"

# --- check bookkeeping ------------------------------------------------------
#
# The counters live in files, not shell variables: several of the loops below
# run inside pipelines, where a subshell would drop an increment silently and
# the summary would under-report rather than fail.

countdir="$target/.check-dist-counts"
rm -rf "$countdir"
mkdir -p "$countdir"
: > "$countdir/pass"; : > "$countdir/fail"; : > "$countdir/skip"; : > "$countdir/na"

check_begin() {
	printf '\n[ CHECK ] %s\n' "$1"
	printf '          %s\n' "$2"
}

# check_end <verdict> <name> [detail]
check_end() {
	verdict=$1
	name=$2
	detail=${3:-}
	case "$verdict" in
		PASS) echo "$name" >> "$countdir/pass" ;;
		FAIL) echo "$name" >> "$countdir/fail" ;;
		SKIP) echo "$name" >> "$countdir/skip" ;;
		N/A)  echo "$name" >> "$countdir/na"   ;;
		*)    echo "$prog: internal error: bad verdict '$verdict'" >&2; exit 3 ;;
	esac
	if [ -n "$detail" ]; then
		printf '[ %-4s ] %s - %s\n' "$verdict" "$name" "$detail"
	else
		printf '[ %-4s ] %s\n' "$verdict" "$name"
	fi
}

have() { command -v "$1" >/dev/null 2>&1; }

echo "============================================================"
echo " LAME distribution check"
echo "   tarball : $tarball"
echo "   target  : $target"
echo "   system  : $(uname -s -r -m 2>/dev/null || echo unknown)"
echo "   started : $(date '+%Y-%m-%d %H:%M:%S %z')"
echo "============================================================"

# --- check: extract ---------------------------------------------------------

srcparent="$target/src"
check_begin "extract" \
	"the tarball unpacks, and into exactly one top-level directory"
rm -rf "$srcparent"
mkdir -p "$srcparent"
if tar -x -f "$tarball" -C "$srcparent" 2>"$target/extract.err"; then
	tops=$(ls "$srcparent" | wc -l | tr -d ' ')
	if [ "$tops" -eq 1 ]; then
		srcdir="$srcparent/$(ls "$srcparent")"
		check_end PASS extract "unpacked into $(basename "$srcdir")"
	else
		srcdir=
		check_end FAIL extract "$tops top-level entries, expected 1 (a tarbomb)"
	fi
else
	srcdir=
	check_end FAIL extract "tar failed: $(head -1 "$target/extract.err" 2>/dev/null)"
fi

# Without a source tree nothing else can run. Report the rest rather than
# exiting quietly, so the summary still says how much was not checked.
if [ -z "$srcdir" ] || [ ! -f "$srcdir/configure" ]; then
	if [ -n "$srcdir" ]; then
		check_begin "usable-tree" "the extracted tree carries a configure script"
		check_end FAIL usable-tree "no configure in $srcdir - not an autotools distribution"
	fi
	echo
	echo "cannot continue without an extracted source tree."
	echo "checks not run: version-consistency, build, unit-tests, distcheck,"
	echo "                doxygen, manpage, abi"
	echo
	echo "============================================================"
	echo " summary: 0 PASS, $(wc -l <"$countdir/fail" | tr -d ' ') FAIL, 0 SKIP"
	echo "============================================================"
	rm -rf "$countdir"
	exit 1
fi

# --- version banner ---------------------------------------------------------

vh="$srcdir/libmp3lame/version.h"
getdef() { sed -n "s/^# *define  *$1  *\([0-9][0-9]*\).*/\1/p" "$vh" | head -1; }
v_major=$(getdef LAME_MAJOR_VERSION)
v_minor=$(getdef LAME_MINOR_VERSION)
v_type=$(getdef LAME_TYPE_VERSION)
v_patch=$(getdef LAME_PATCH_VERSION)
case "${v_type:-}" in
	0) v_kind=alpha ;;
	1) v_kind=beta ;;
	2) v_kind=release ;;
	*) v_kind="unknown type '${v_type:-}'" ;;
esac
version="${v_major:-?}.${v_minor:-?}"
echo "$version" > "$verfile"

echo
echo "------------------------------------------------------------"
echo " version under test: LAME $version ($v_kind, patch level ${v_patch:-?})"
echo "------------------------------------------------------------"

# --- check: version-consistency ---------------------------------------------

check_begin "version-consistency" \
	"the tarball name, configure's package version and version.h agree"
cfg_version=$(sed -n 's/^AC_INIT(\[*lame\]*,[ ]*\[*\([^],)]*\).*/\1/p' \
	"$srcdir/configure.ac" 2>/dev/null | head -1)
[ -n "$cfg_version" ] || cfg_version=$(sed -n "s/^PACKAGE_VERSION='\(.*\)'/\1/p" \
	"$srcdir/configure" 2>/dev/null | head -1)
# One plain expression per suffix rather than a \(a\|b\) alternation: \| is a
# GNU extension to basic regular expressions, so on a BSD sed the suffix was
# never stripped and this check reported every tarball as misnamed.
tar_version=$(basename "$tarball" | sed 's/^lame-//
	s/\.tar\.gz$//
	s/\.tar\.bz2$//
	s/\.tar\.xz$//
	s/\.tar\.Z$//
	s/\.tgz$//')
echo "          tarball name : $tar_version"
echo "          configure    : ${cfg_version:-<not found>}"
echo "          version.h    : $version"
if [ -z "$v_major" ] || [ -z "$v_minor" ]; then
	check_end FAIL version-consistency "could not read version.h"
elif [ -z "$cfg_version" ]; then
	check_end FAIL version-consistency "could not read a package version from configure.ac or configure"
elif [ "$cfg_version" != "$version" ]; then
	check_end FAIL version-consistency "configure says $cfg_version, version.h says $version"
elif [ "$tar_version" != "$version" ]; then
	check_end FAIL version-consistency "tarball is named $tar_version but contains $version"
else
	check_end PASS version-consistency "all three say $version"
fi

# --- prerequisites ----------------------------------------------------------

pkgconf=
for p in pkg-config pkgconf; do
	have "$p" && { pkgconf=$p; break; }
done

unit_tests=no
unit_reason="CMocka not found"
if [ -n "$pkgconf" ] && "$pkgconf" --exists cmocka 2>/dev/null; then
	unit_tests=yes
elif [ -z "$pkgconf" ]; then
	unit_reason="neither pkg-config nor pkgconf found, so CMocka cannot be probed"
fi

echo
echo "prerequisites: unit tests $(if [ $unit_tests = yes ]; then echo available; else echo "unavailable ($unit_reason)"; fi)"
echo "               doxygen $(if have doxygen; then doxygen --version; else echo "not found"; fi)"
echo "               groff $(if have groff; then echo present; else echo "not found"; fi)"
echo "               abidw $(if have abidw; then echo present; else echo "not found"; fi)"

# --- build the matrix -------------------------------------------------------

matrix="$target/build"
rm -rf "$matrix"
gen="$srcdir/maintainer/gen-build-matrix.sh"
extra=
[ "$unit_tests" = yes ] && extra="--enable-unit-tests"
if [ "$maintainer_mode" = yes ]; then
	extra="${extra:+$extra }--enable-maintainer-mode"
fi

check_begin "matrix-generate" \
	"the shipped build harness can lay out this tarball's configurations"
if [ ! -f "$gen" ]; then
	check_end FAIL matrix-generate "$gen is missing from the tarball"
	cells=""
elif [ "$quick" = yes ]; then
	# One cell, laid out by hand rather than by the harness: the harness's
	# reason for existing is breadth, which --quick is explicitly giving up.
	mkdir -p "$matrix/default/quick"
	{
		echo "#!/bin/sh"
		echo "set -e"
		echo 'cd "$(dirname "$0")"'
		echo "\"$srcdir/configure\" --enable-dynamic-frontends $extra"
		echo "make -j$jobs"
	} > "$matrix/default/quick/build.sh"
	chmod +x "$matrix/default/quick/build.sh"
	check_end PASS matrix-generate "--quick: 1 cell"
	cells="$matrix/default/quick"
else
	# Two spellings rather than ${extra:+-x "$extra"}: that expansion is
	# unquoted, so a two-option string would word-split and only the first
	# would reach -x.
	if { if [ -n "$extra" ]; then
			sh "$gen" -d "$matrix" -s "$srcdir" -j "$jobs" -x "$extra"
		 else
			sh "$gen" -d "$matrix" -s "$srcdir" -j "$jobs"
		 fi; } >"$target/matrix-gen.log" 2>&1; then
		n=$(find "$matrix" -mindepth 3 -maxdepth 3 -name build.sh 2>/dev/null | wc -l | tr -d ' ')
		if [ "$n" -eq 0 ]; then
			check_end FAIL matrix-generate "the harness generated no cells at all (see matrix-gen.log)"
		else
			check_end PASS matrix-generate "$n cell(s)"
		fi
	else
		check_end FAIL matrix-generate "the harness failed (see matrix-gen.log)"
		n=0
	fi
	cells=$(find "$matrix" -mindepth 3 -maxdepth 3 -name build.sh 2>/dev/null \
		| sed 's|/build.sh$||' | sort)
fi

# --- per-cell: build, then unit tests ---------------------------------------

for cell in $cells; do
	tag=$(echo "${cell#$matrix/}" | tr '/' '-')

	check_begin "build[$tag]" \
		"this configuration compiles and links from the extracted tarball"
	if ( cd "$cell" && sh ./build.sh ) >"$cell/build.log" 2>&1; then
		check_end PASS "build[$tag]"
		built=yes
	else
		check_end FAIL "build[$tag]" "see $cell/build.log"
		built=no
	fi

	check_begin "unit-tests[$tag]" \
		"the CMocka suite passes in this configuration"
	if [ "$unit_tests" != yes ]; then
		check_end SKIP "unit-tests[$tag]" "$unit_reason"
	elif [ "$built" != yes ]; then
		check_end SKIP "unit-tests[$tag]" "the build failed, so there is nothing to test"
	elif ( cd "$cell" && make check ) >"$cell/check.log" 2>&1; then
		check_end PASS "unit-tests[$tag]"
	else
		check_end FAIL "unit-tests[$tag]" "see $cell/check.log"
	fi
done

# The first successfully built cell is what the whole-tree checks below use.
firstbuilt=
for cell in $cells; do
	if [ -f "$cell/libmp3lame/.libs/libmp3lame.so" ] \
		|| [ -f "$cell/libmp3lame/.libs/libmp3lame.dylib" ] \
		|| [ -f "$cell/libmp3lame/.libs/libmp3lame.a" ]; then
		firstbuilt=$cell
		break
	fi
done

# --- check: distcheck -------------------------------------------------------

check_begin "distcheck" \
	"make distcheck: a VPATH build, install, installcheck, uninstall and re-dist"
if [ -z "$firstbuilt" ]; then
	check_end SKIP distcheck "no cell built, so there is no build tree to run it in"
elif ( cd "$firstbuilt" && make distcheck ) >"$target/distcheck.log" 2>&1; then
	check_end PASS distcheck
else
	check_end FAIL distcheck "see $target/distcheck.log"
fi

# --- check: doxygen ---------------------------------------------------------

check_begin "doxygen" \
	"both documentation sets build, and produce more than an empty theme"
if ! have doxygen; then
	check_end SKIP doxygen "doxygen not found"
elif [ -z "$firstbuilt" ]; then
	check_end SKIP doxygen "no cell built, so the Doxyfiles were never generated"
else
	dox_ok=yes
	( cd "$firstbuilt" && make -C doc doxygen && make -C doc doxygen-internal ) \
		>"$target/doxygen.log" 2>&1 || dox_ok=no
	# A doxygen run that reads no input still writes a full set of stylesheet
	# and image files and exits 0, so the file count proves nothing. Demand a
	# documented symbol from the installed header instead - a page name would
	# only test the settings the render happened to be made with.
	pub="$firstbuilt/doc/doxygen-output/html"
	int="$firstbuilt/doc/doxygen-internal/html"
	if [ "$dox_ok" != yes ]; then
		check_end FAIL doxygen "the build failed, see $target/doxygen.log"
	elif ! grep -l lame_init "$pub"/*.html >/dev/null 2>&1; then
		check_end FAIL doxygen "the public set documents nothing (no lame_init)"
	elif ! grep -l lame_init "$int"/*.html >/dev/null 2>&1; then
		check_end FAIL doxygen "the internal set documents nothing (no lame_init)"
	else
		check_end PASS doxygen "both sets document the API"
	fi
fi

# --- check: manpage ---------------------------------------------------------

check_begin "manpage" \
	"the shipped man pages render without a formatting complaint"
if ! have groff; then
	check_end SKIP manpage "groff not found"
else
	man_err="$target/manpage.log"
	: > "$man_err"
	man_missing=
	for m in doc/man/lame.1 doc/man/mp3rtp.1 doc/man/mp3x.1; do
		if [ -f "$srcdir/$m" ]; then
			groff -man -ww -z "$srcdir/$m" >>"$man_err" 2>&1
		else
			man_missing="$man_missing $m"
		fi
	done
	if [ -n "$man_missing" ]; then
		check_end FAIL manpage "missing from the tarball:$man_missing"
	elif [ -s "$man_err" ]; then
		check_end FAIL manpage "$(wc -l <"$man_err" | tr -d ' ') complaint(s), see $man_err"
	else
		check_end PASS manpage
	fi
fi

# --- check: abi -------------------------------------------------------------

check_begin "abi" \
	"the built library's exported interface still matches the committed contract"
if [ ! -f "$srcdir/maintainer/abicheck.sh" ]; then
	check_end N/A abi "this tarball does not carry the ABI check"
elif [ -z "$firstbuilt" ]; then
	check_end SKIP abi "no cell built, so there is no library to inspect"
elif ( cd "$firstbuilt" && sh "$srcdir/maintainer/abicheck.sh" \
		-i "$srcdir/include" -a "$srcdir/maintainer/abi/libmp3lame.abi" ) \
		>"$target/abicheck.log" 2>&1; then
	# Pass its own summary line through rather than recounting: it is one
	# line, it is the script's own wording, and a private recount here would
	# be a second implementation to keep in step.
	check_end PASS abi "$(sed -n 's/^Summary: //p' "$target/abicheck.log" 2>/dev/null | head -1)"
else
	check_end FAIL abi "see $target/abicheck.log"
fi

# --- check: audio smoke (opt-in) --------------------------------------------

if [ -n "$audio_dir" ]; then
	check_begin "audio-smoke" \
		"every file in the supplied directory encodes to a non-empty MP3"
	lame_bin=
	[ -n "$firstbuilt" ] && [ -x "$firstbuilt/frontend/lame" ] && lame_bin="$firstbuilt/frontend/lame"
	if [ -z "$lame_bin" ]; then
		check_end SKIP audio-smoke "no built frontend to encode with"
	else
		out="$target/audio-smoke"
		rm -rf "$out"; mkdir -p "$out"
		bad=0; total=0
		for f in "$audio_dir"/*; do
			[ -f "$f" ] || continue
			total=$((total + 1))
			o="$out/$(basename "$f").mp3"
			if "$lame_bin" --quiet "$f" "$o" >>"$out/encode.log" 2>&1 && [ -s "$o" ]; then
				:
			else
				bad=$((bad + 1))
				echo "  failed: $f" >> "$out/encode.log"
			fi
		done
		if [ "$total" -eq 0 ]; then
			check_end SKIP audio-smoke "no files in $audio_dir"
		elif [ "$bad" -eq 0 ]; then
			check_end PASS audio-smoke "$total file(s)"
		else
			check_end FAIL audio-smoke "$bad of $total failed, see $out/encode.log"
		fi
	fi
fi

# --- check: coverage / sanitizers (opt-in) ----------------------------------

if [ "$do_coverage" = yes ]; then
	check_begin "coverage" "the coverage harness runs against this tarball"
	if [ ! -f "$srcdir/maintainer/coverage-run.sh" ]; then
		check_end N/A coverage "this tarball does not carry the coverage harness"
	elif sh "$srcdir/maintainer/gen-coverage-matrix.sh" -d "$target/coverage" -s "$srcdir" \
			>"$target/coverage.log" 2>&1; then
		check_end PASS coverage "see $target/coverage.log"
	else
		check_end FAIL coverage "see $target/coverage.log"
	fi
fi

if [ "$do_sanitizers" = yes ]; then
	check_begin "sanitizers" "a build with ASan and UBSan configures, builds and tests clean"
	sdir="$target/sanitize"
	mkdir -p "$sdir"
	if ( cd "$sdir" \
			&& CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
			   LDFLAGS="-fsanitize=address,undefined" \
			   "$srcdir/configure" --enable-dynamic-frontends $extra \
			&& make -j"$jobs" \
			&& { [ "$unit_tests" != yes ] || make check ; } ) \
			>"$target/sanitizers.log" 2>&1; then
		check_end PASS sanitizers
	else
		check_end FAIL sanitizers "see $target/sanitizers.log"
	fi
fi

# --- summary ----------------------------------------------------------------

np=$(wc -l <"$countdir/pass" | tr -d ' ')
nf=$(wc -l <"$countdir/fail" | tr -d ' ')
ns=$(wc -l <"$countdir/skip" | tr -d ' ')
nn=$(wc -l <"$countdir/na"   | tr -d ' ')
nt=$((np + nf + ns + nn))

echo
echo "============================================================"
echo " LAME $version ($v_kind, patch level ${v_patch:-?})"
echo " $nt check(s): $np PASS, $nf FAIL, $ns SKIP, $nn N/A"
if [ "$nf" -gt 0 ]; then
	echo
	echo " failed:"
	sed 's/^/   /' "$countdir/fail"
fi
if [ "$ns" -gt 0 ]; then
	echo
	echo " skipped (a prerequisite is missing on this machine):"
	sed 's/^/   /' "$countdir/skip"
fi
if [ "$nn" -gt 0 ]; then
	echo
	echo " not applicable here:"
	sed 's/^/   /' "$countdir/na"
fi
echo " finished: $(date '+%Y-%m-%d %H:%M:%S %z')"
echo "============================================================"

rm -rf "$countdir"
[ "$nf" -eq 0 ] || exit 1
exit 0
