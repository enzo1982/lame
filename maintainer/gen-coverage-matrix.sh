#!/bin/sh
#
# gen-coverage-matrix.sh - generate an instrumented out-of-tree build matrix
# for measuring code coverage of the autotools (POSIX) build of LAME.
#
# It creates, under a master directory, one coverage-instrumented build
# directory per configuration cell, each with a self-contained build.sh, plus a
# build-all.sh driver - the same shape as gen-build-matrix.sh, which this is
# modelled on.
#
# The two matrices answer different questions and therefore have different cell
# lists. gen-build-matrix.sh asks "does every configuration still build?", so
# its cells include ones that only change flags or linkage (static, libonly,
# nohardening, ...). This one asks "which source lines can be reached at all?",
# so it covers every axis of *conditional compilation* instead: a line inside a
# #ifdef that no cell enables cannot be executed by any test, no matter how the
# test is written. Cells that merely change flags compile the same source and
# would only cost build time here, so they are deliberately absent.
#
# See doc/maintainer-coverage.md for the workflow this fits into.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog [-d DIR] [-s SRCDIR] [-c CC] [-j N] [-h]

Generates coverage-instrumented builds of the autotools (POSIX) configurations.

  -d DIR     Master directory to create the build tree in.
             (default: ./coverage-matrix)
  -s SRCDIR  LAME source directory (the one containing "configure").
             (default: the parent of this script's directory)
  -c CC      C compiler to use. (default: gcc)
  -j N       Parallel make jobs per build. (default: detected CPU count)
  -h         Show this help and exit.

The compiler must be gcc, or gcc-compatible in its coverage output: the
analysis step reads gcov data with lcov. Clang writes a different profile
format that lcov cannot consume, so a clang coverage run would need llvm-cov
and a separate reader; sanitizer runs, which do use clang, are a separate job.
EOF
}

# --- option parsing ---------------------------------------------------------

master=./coverage-matrix
srcdir=
cc=gcc
jobs=

while getopts "d:s:c:j:h" opt; do
	case "$opt" in
		d)
			master=$OPTARG
			;;
		s)
			srcdir=$OPTARG
			;;
		c)
			cc=$OPTARG
			;;
		j)
			jobs=$OPTARG
			;;
		h)
			usage
			exit 0
			;;
		*)
			usage >&2
			exit 2
			;;
	esac
done

# --- source directory -------------------------------------------------------

script_dir=$(cd "$(dirname "$0")" && pwd)
if [ -z "$srcdir" ]; then
	srcdir=$(cd "$script_dir/.." && pwd)
else
	srcdir=$(cd "$srcdir" && pwd) || {
		echo "$prog: source directory '$srcdir' not found" >&2
		exit 1
	}
fi
if [ ! -x "$srcdir/configure" ]; then
	echo "$prog: '$srcdir/configure' not found or not executable." >&2
	echo "$prog: run autoreconf in the source tree first, or pass -s SRCDIR." >&2
	exit 1
fi

# --- jobs -------------------------------------------------------------------

if [ -z "$jobs" ]; then
	jobs=$(nproc 2>/dev/null \
		|| sysctl -n hw.ncpu 2>/dev/null \
		|| getconf _NPROCESSORS_ONLN 2>/dev/null \
		|| echo 1)
fi

# --- compiler ---------------------------------------------------------------

cc_path=$(command -v "$cc" 2>/dev/null)
if [ -z "$cc_path" ]; then
	echo "$prog: compiler '$cc' not found on PATH." >&2
	exit 1
fi
cc_banner=$("$cc_path" --version 2>/dev/null | head -1)
case "$cc_banner" in
	*clang*)
		echo "$prog: WARNING: '$cc' looks like clang. Its profile format is not" >&2
		echo "$prog:          gcov data and lcov cannot read it; the analysis step" >&2
		echo "$prog:          will find nothing. Use gcc for coverage." >&2
		;;
esac

# Coverage instrumentation.
#
#   -O0                 optimisation reorders, folds and inlines code, which
#                       makes line coverage report lines that no longer exist
#                       as such. Coverage builds are therefore NOT warning
#                       builds and no warning conclusions are drawn from them.
#   -g                  line tables, so gcov can map counters back to source.
#   --coverage          instrument (compile) and link the gcov runtime.
#   -fprofile-abs-path  record absolute source paths. These are out-of-tree
#                       builds, and without this gcov emits paths relative to
#                       the build dir that lcov then cannot resolve.
#
# Passed via CFLAGS because configure.ac merges user CFLAGS *after* its own
# ${OPTIMIZATION} (configure.ac:1251), so -O0 wins over the tree's default -O3
# without patching configure.
cov_cflags='-O0 -g --coverage -fprofile-abs-path'
cov_ldflags='--coverage'

# --- optional dependency probing --------------------------------------------

pkgconf=
for p in pkg-config pkgconf; do
	if command -v "$p" >/dev/null 2>&1; then
		pkgconf=$p
		break
	fi
done

have_mpg123=unknown
have_sndfile=unknown
have_cmocka=unknown
if [ -n "$pkgconf" ]; then
	"$pkgconf" --exists libmpg123 2>/dev/null && have_mpg123=yes || have_mpg123=no
	"$pkgconf" --exists sndfile 2>/dev/null && have_sndfile=yes || have_sndfile=no
	"$pkgconf" --exists cmocka 2>/dev/null && have_cmocka=yes || have_cmocka=no
else
	echo "$prog: WARNING: neither pkg-config nor pkgconf found - cannot probe" >&2
	echo "$prog:          optional dependencies; generating all cells." >&2
fi

# GTK for the mp3x analyzer frontend. configure.ac uses AM_PATH_GTK(1.2.0),
# the GTK *1.2* macro, which discovers through gtk-config rather than
# pkg-config - probing for a pkg-config gtk+-N.0 package answers a different
# question and always answers it wrongly.
have_gtk=no
if command -v gtk-config >/dev/null 2>&1; then
	have_gtk=yes
fi

# --- configuration cells ----------------------------------------------------
#
# name | configure arguments
#
# Every cell flips exactly one conditional-compilation surface, so that a
# difference in coverage is attributable to that surface alone.
#
# full        base: decoder on, analyzer hooks on. Also the only cell that
#             compiles the DECODE_ON_THE_FLY code (--replaygain-accurate,
#             --clipdetect), which configure defines in the same branch as
#             HAVE_MPG123 - the two are one condition under two names.
# nodecoder   decoder off: the HAVE_MPG123 / DECODE_ON_THE_FLY blocks vanish.
#             Also the only cell MSan can use, since MSan needs every linked
#             library instrumented and libmpg123 is not.
# sndfile     file I/O through libsndfile instead of the builtin readers.
# noanalyzer  analyzer hooks off (NOANALYSIS) - removes the pinfo paths.
# expopt      experimental optimizations compiled in.
# ieeehack    the IEEE754 fast path (TAKEHIRO_IEEE754_HACK), which is off by
#             default in every build on every platform, so it is otherwise
#             never exercised anywhere.
# mp3rtp      the mp3rtp frontend, not built by default.
# mp3x        the GTK frame analyzer, not built by default. Needs GTK 1.2 and
#             the analyzer hooks, so it is mutually exclusive with noanalyzer.
# debug       debug-only code paths (--enable-debug=alot).
#
# --disable-hardening everywhere: hardening adds -D_FORTIFY_SOURCE=2, which
# glibc warns about at -O0 ("requires compiling with optimization"), once per
# translation unit. It changes no source, only flags, so dropping it costs no
# coverage and keeps the build log readable.
cells() {
	cat <<'EOF'
full|--disable-hardening
nodecoder|--disable-hardening --disable-decoder
sndfile|--disable-hardening --with-fileio=sndfile
noanalyzer|--disable-hardening --disable-analyzer-hooks
expopt|--disable-hardening --enable-expopt=norm
ieeehack|--disable-hardening --enable-ieeehack
mp3rtp|--disable-hardening --enable-mp3rtp
mp3x|--disable-hardening --enable-mp3x
debug|--disable-hardening --enable-debug=alot
EOF
}

# A cell needs libmpg123 unless it disables the decoder.
cell_needs_mpg123() {
	case " $1 " in
		*" --disable-decoder "*)
			return 1
			;;
		*)
			return 0
			;;
	esac
}

cell_needs_sndfile() {
	case " $1 " in
		*"--with-fileio=sndfile"*)
			return 0
			;;
		*)
			return 1
			;;
	esac
}

cell_needs_gtk() {
	case " $1 " in
		*"--enable-mp3x"*)
			return 0
			;;
		*)
			return 1
			;;
	esac
}

# --- generate ---------------------------------------------------------------

mkdir -p "$master" || {
	echo "$prog: cannot create '$master'" >&2
	exit 1
}
master_abs=$(cd "$master" && pwd)
mkdir -p "$master_abs/logs"

# The unit tests are worth having in a coverage build: they reach error paths
# that the command-line workload cannot trigger from outside.
unit_arg=
if [ "$have_cmocka" != no ]; then
	unit_arg=" --enable-unit-tests"
fi

info="$master_abs/matrix-info.txt"
{
	echo "# LAME coverage build matrix"
	echo "# generated: $(date '+%Y-%m-%d %H:%M:%S %z') by $prog"
	echo "srcdir   = $srcdir"
	echo "system   = $(uname -s -m 2>/dev/null)"
	echo "compiler = $cc_path"
	echo "           $cc_banner"
	echo "jobs     = $jobs"
	echo "CFLAGS   = $cov_cflags"
	echo "LDFLAGS  = $cov_ldflags"
	echo "libmpg123 present  = $have_mpg123"
	echo "libsndfile present = $have_sndfile"
	echo "cmocka present     = $have_cmocka"
	echo "GTK 1.2 (gtk-config) present = $have_gtk"
	echo "cells:"
} > "$info"

cells | while IFS='|' read -r cell args; do
	if [ -z "$cell" ]; then
		continue
	fi

	skip_reason=""
	if cell_needs_mpg123 "$args" && [ "$have_mpg123" = no ]; then
		skip_reason="libmpg123 missing"
	elif cell_needs_sndfile "$args" && [ "$have_sndfile" = no ]; then
		skip_reason="libsndfile missing"
	elif cell_needs_gtk "$args" && [ "$have_gtk" = no ]; then
		skip_reason="GTK 1.2 missing (AM_PATH_GTK/gtk-config); this frontend is then not coverable on this host at all"
	fi
	if [ -n "$skip_reason" ]; then
		echo "  [skip] $cell ($skip_reason)" >> "$info"
		continue
	fi

	celldir="$master_abs/$cell"
	mkdir -p "$celldir"
	{
		echo "#!/bin/sh"
		echo "# Auto-generated by $prog - DO NOT EDIT BY HAND."
		echo "# cell: $cell"
		echo "# configure args: $args$unit_arg"
		echo "set -e"
		echo "# Configure and build inside this cell's own directory (where this"
		echo "# script lives), so every cell is a genuine isolated out-of-tree"
		echo "# build rather than sharing - and clobbering - the master directory."
		echo 'cd "$(dirname "$0")"'
		echo "srcdir='$srcdir'"
		echo "export CC='$cc_path'"
		echo "export CFLAGS='$cov_cflags'"
		echo "export LDFLAGS='$cov_ldflags'"
		echo "\"\$srcdir/configure\" $args$unit_arg"
		echo "make -j$jobs"
	} > "$celldir/build.sh"
	chmod +x "$celldir/build.sh"
	echo "  [gen]  $cell : $args$unit_arg" >> "$info"
done

# The cell loop runs in a subshell (right-hand side of a pipe), so count the
# generated builds from the filesystem afterwards.
generated=$(find "$master_abs" -mindepth 2 -maxdepth 2 -name build.sh 2>/dev/null | wc -l | tr -d ' ')

# --- driver -----------------------------------------------------------------

driver="$master_abs/build-all.sh"
cat > "$driver" <<'DRIVER'
#!/bin/sh
#
# build-all.sh - build every cell of the LAME coverage matrix.
# Auto-generated by gen-coverage-matrix.sh - DO NOT EDIT BY HAND.
#
# Builds each cell in turn, never stopping on failure. Each build's output is
# shown and saved to logs/<cell>.log. Prints a summary listing the failed
# builds together with their log files, and exits non-zero if any failed.
#
set -u
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"
mkdir -p logs
fails="$here/logs/.failures"
: > "$fails"
total=0

for build in */build.sh; do
	if [ ! -f "$build" ]; then
		continue
	fi
	dir=$(dirname "$build")
	log="logs/$dir.log"
	total=$((total + 1))
	printf '\n==== building %s ====\n' "$dir"
	st_file="$dir/.exit-status"
	( sh "$build"; echo $? > "$st_file" ) 2>&1 | tee "$log"
	st=$(cat "$st_file" 2>/dev/null || echo 1)
	rm -f "$st_file"
	if [ "$st" -eq 0 ]; then
		printf '     PASS  %s\n' "$dir"
	else
		printf '     FAIL  %s  (log: %s)\n' "$dir" "$log"
		echo "$dir|$log" >> "$fails"
	fi
done

echo
echo '=================== coverage-matrix summary ==================='
printf 'total builds: %s\n' "$total"
if [ -s "$fails" ]; then
	echo 'FAILED builds:'
	while IFS='|' read -r d l; do
		printf '  %-32s log: %s\n' "$d" "$l"
	done < "$fails"
	rm -f "$fails"
	echo '=============================================================='
	exit 1
fi
rm -f "$fails"
echo 'all builds passed'
echo '=============================================================='
exit 0
DRIVER
chmod +x "$driver"

# --- report -----------------------------------------------------------------

echo "Generated $generated coverage cell(s) under: $master_abs"
echo "Driver: $driver"
echo "Matrix info: $info"

if [ "$have_gtk" = no ]; then
	echo
	echo "NOTE: GTK 1.2 not found (configure uses AM_PATH_GTK/gtk-config)."
	echo "      The mp3x frontend cannot be built or covered on this host."
fi

echo
echo "To build the matrix:"
echo "  sh $driver"
