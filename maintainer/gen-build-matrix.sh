#!/bin/sh
#
# gen-build-matrix.sh - generate an out-of-tree build-configuration test
# matrix for the autotools (POSIX) build of LAME.
#
# It creates, under a master directory, one out-of-tree build directory per
# (compiler x configuration) combination, each with a self-contained build.sh
# that records the exact configure invocation, plus a build-all.sh driver that
# builds every combination, never stopping on failure, writing each build log
# to logs/, and printing a summary of the failed builds with their log
# locations.
#
# The configuration axis is a "star" matrix: a full-featured base plus one
# single-flip variant per interesting conditional-compilation surface, rather
# than the full cartesian product. See doc/maintainer-build-matrix.md for the
# rationale, the per-platform prerequisites and the Windows counterpart
# (gen-build-matrix.ps1).
#
# This handles the POSIX / autoconf targets. For the native Windows builds
# (nmake / MSBuild) use gen-build-matrix.ps1 instead.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog [-d DIR] [-s SRCDIR] [-c LIST] [-j N] [-h]

This builds the autotools (POSIX) configurations, MSYS2 included. For the
native Windows builds (nmake / MSBuild) use gen-build-matrix.ps1 instead.

  -d DIR     Master directory to create the build tree in.
             (default: ./build-matrix)
  -s SRCDIR  LAME source directory (the one containing "configure").
             (default: the parent of this script's directory)
  -c LIST    Comma-separated compiler list to use instead of autodetection,
             e.g. "gcc,clang" or "/opt/gcc-15/bin/gcc". Each entry is used
             as CC; the directory is named after its basename.
  -j N       Parallel make jobs per build. (default: detected CPU count)
  -h         Show this help and exit.

Compiler autodetection tries "gcc clang cc" on PATH (no filesystem search)
and de-duplicates by compiler version, so an alias like cc->gcc is only
built once.
EOF
}

# --- option parsing ---------------------------------------------------------

master=./build-matrix
srcdir=
compilers_override=
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
			compilers_override=$OPTARG
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

# --- compiler detection -----------------------------------------------------

# Print the PATH location of a command, or nothing if it is not found.
# Deliberately not symlink-resolved: a compiler may be reached through a
# wrapper symlink whose name selects the real compiler - for example a ccache
# wrapper, as the maintainer uses on the FreeBSD build host - and there the
# wrapper path is exactly what CC has to be. De-duplication keys on the
# --version banner instead, so no canonicalization is needed here.
# (Used once, but kept as a named helper so that rationale stays attached to it
# rather than to a bare command-substitution below.)
resolve() {
	command -v "$1" 2>/dev/null
}

if [ -n "$compilers_override" ]; then
	cand=$(printf '%s' "$compilers_override" | tr ',' ' ')
else
	cand="gcc clang cc"
fi

seen_sigs="|"
seen_names=" "
detected=""      # space-separated "name=ccpath" tokens
for c in $cand; do
	path=$(resolve "$c")
	if [ -z "$path" ]; then
		echo "  compiler '$c' not on PATH - skipped" >&2
		continue
	fi
	# De-duplicate by compiler identity rather than by path: an alias such as
	# cc is often a distinct binary that is nonetheless the same compiler, so
	# key on the --version banner with the leading program name removed.
	sig=$("$path" --version 2>/dev/null | head -1 | sed 's/^[^ ]*[ ]*//')
	if [ -z "$sig" ]; then
		sig=$path
	fi
	case "$seen_sigs" in
		*"|$sig|"*)
			continue
			;;
	esac
	seen_sigs="$seen_sigs$sig|"
	name=$(basename "$c")
	# Guard against two different compilers wanting the same directory name.
	base=$name
	i=1
	while : ; do
		case "$seen_names" in
			*" $name "*)
				name="${base}-${i}"
				i=$((i + 1))
				;;
			*)
				break
				;;
		esac
	done
	seen_names="$seen_names$name "
	detected="$detected $name=$path"
done

if [ -z "$detected" ]; then
	echo "$prog: no usable compiler found (tried: $cand)." >&2
	exit 1
fi

# --- optional dependency probing --------------------------------------------
#
# Probe with pkgconf / pkg-config. If neither is present the state is
# "unknown": every cell is generated rather than skipped and configure is left
# to be the judge, but the situation is reported.

pkgconf=
for p in pkg-config pkgconf; do
	if command -v "$p" >/dev/null 2>&1; then
		pkgconf=$p
		break
	fi
done

have_mpg123=unknown
have_sndfile=unknown
if [ -n "$pkgconf" ]; then
	if "$pkgconf" --exists libmpg123 2>/dev/null; then
		have_mpg123=yes
	else
		have_mpg123=no
	fi
	if "$pkgconf" --exists sndfile 2>/dev/null; then
		have_sndfile=yes
	else
		have_sndfile=no
	fi
else
	echo "$prog: WARNING: neither pkg-config nor pkgconf found - cannot probe" >&2
	echo "$prog:          optional dependencies; generating all cells (some may" >&2
	echo "$prog:          fail to configure if libmpg123 / libsndfile are absent)." >&2
fi

if [ "$have_mpg123" = no ]; then
	echo "$prog: WARNING: libmpg123 not found via $pkgconf - decoder-on cells" >&2
	echo "$prog:          will be skipped (they cover most of the code base)." >&2
fi
if [ "$have_sndfile" = no ]; then
	echo "$prog: WARNING: libsndfile not found via $pkgconf - the sndfile cell" >&2
	echo "$prog:          will be skipped." >&2
fi

# --- configuration cells (the "star") ---------------------------------------
#
# name | configure arguments
#
# full        base: shared build + dynamic frontends (exercises the export
#             list; a missing symbol breaks the frontend link here)
# nodecoder   flip the mpg123 decoder off (the only cell not needing libmpg123)
# sndfile     flip file I/O to libsndfile instead of the builtin reader
# noanalyzer  flip the analyzer hooks off (NOANALYSIS)
# static      flip to a static-only build (no shared lib, no dyn frontends)
# libonly     flip the frontends off, library only
# staticfe    flip the (default-on) dynamic frontends off (static frontends)
# nohardening flip the (default-on) security hardening off
# expopt      flip the experimental optimizations on (--enable-expopt=norm)
#
cells() {
	cat <<'EOF'
full|--enable-dynamic-frontends
nodecoder|--enable-dynamic-frontends --disable-decoder
sndfile|--enable-dynamic-frontends --with-fileio=sndfile
noanalyzer|--enable-dynamic-frontends --disable-analyzer-hooks
static|--disable-shared --enable-static
libonly|--disable-frontend
staticfe|--disable-dynamic-frontends
nohardening|--enable-dynamic-frontends --disable-hardening
expopt|--enable-dynamic-frontends --enable-expopt=norm
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

# A cell needs libsndfile only when it selects the sndfile file I/O backend.
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

# --- generate ---------------------------------------------------------------

mkdir -p "$master" || {
	echo "$prog: cannot create '$master'" >&2
	exit 1
}
master_abs=$(cd "$master" && pwd)
mkdir -p "$master_abs/logs"

info="$master_abs/matrix-info.txt"
{
	echo "# LAME build-configuration test matrix"
	echo "# generated: $(date '+%Y-%m-%d %H:%M:%S %z') by $prog"
	echo "srcdir  = $srcdir"
	echo "system  = $(uname -s -m 2>/dev/null)"
	echo "jobs    = $jobs"
	echo "libmpg123 present  = $have_mpg123"
	echo "libsndfile present = $have_sndfile"
	echo "compilers:"
	for d in $detected; do
		echo "  ${d%%=*} -> ${d#*=}"
	done
	echo "cells:"
} > "$info"

for d in $detected; do
	cname=${d%%=*}
	cpath=${d#*=}
	cells | while IFS='|' read -r cell args; do
		# Defensive: with the current cells() list this never triggers, but it
		# stops a stray blank line there from emitting a nameless build with no
		# configure arguments.
		if [ -z "$cell" ]; then
			continue
		fi
		# Prerequisite gating: only emit locally buildable combinations.
		skip_reason=""
		if cell_needs_mpg123 "$args" && [ "$have_mpg123" = no ]; then
			skip_reason="libmpg123 missing"
		elif cell_needs_sndfile "$args" && [ "$have_sndfile" = no ]; then
			skip_reason="libsndfile missing"
		fi
		if [ -n "$skip_reason" ]; then
			echo "  [skip] $cname/$cell ($skip_reason)" >> "$info"
			continue
		fi
		celldir="$master_abs/$cname/$cell"
		mkdir -p "$celldir"
		{
			echo "#!/bin/sh"
			echo "# Auto-generated by $prog - DO NOT EDIT BY HAND."
			echo "# cell: $cell   compiler: $cname ($cpath)"
			echo "# configure args: $args"
			echo "set -e"
			echo "# Configure and build inside this cell's own directory (where"
			echo "# this script lives) so every matrix cell is a genuine isolated"
			echo "# out-of-tree build rather than sharing - and clobbering - the"
			echo "# master directory. srcdir and CC below are absolute."
			echo 'cd "$(dirname "$0")"'
			echo "srcdir='$srcdir'"
			echo "export CC='$cpath'"
			echo "\"\$srcdir/configure\" $args"
			echo "make -j$jobs"
		} > "$celldir/build.sh"
		chmod +x "$celldir/build.sh"
		echo "  [gen]  $cname/$cell : $args" >> "$info"
	done
done

# The cell loop above runs in a subshell (it is the right-hand side of a pipe),
# so count the generated builds from the filesystem afterwards.
generated=$(find "$master_abs" -mindepth 3 -maxdepth 3 -name build.sh 2>/dev/null | wc -l | tr -d ' ')

# --- driver -----------------------------------------------------------------

driver="$master_abs/build-all.sh"
cat > "$driver" <<'DRIVER'
#!/bin/sh
#
# build-all.sh - build every cell of the LAME build-configuration matrix.
# Auto-generated by gen-build-matrix.sh - DO NOT EDIT BY HAND.
#
# Builds each combination in turn, never stopping on failure. Each build's
# output is shown and saved to logs/<compiler>-<cell>.log. Prints a summary
# listing the failed builds together with their log files, and exits non-zero
# if any build failed.
#
set -u
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"
mkdir -p logs
fails="$here/logs/.failures"
: > "$fails"
total=0

for build in */*/build.sh; do
	if [ ! -f "$build" ]; then
		continue
	fi
	dir=$(dirname "$build")
	tag=$(echo "$dir" | tr '/' '-')
	log="logs/$tag.log"
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
echo '==================== build-matrix summary ===================='
printf 'total builds: %s\n' "$total"
if [ -s "$fails" ]; then
	echo 'FAILED builds:'
	while IFS='|' read -r d l; do
		printf '  %-32s log: %s\n' "$d" "$l"
	done < "$fails"
	rm -f "$fails"
	echo '============================================================='
	exit 1
fi
rm -f "$fails"
echo 'all builds passed'
echo '============================================================='
exit 0
DRIVER
chmod +x "$driver"

# --- report -----------------------------------------------------------------

echo "Generated $generated build cell(s) under: $master_abs"
echo "Driver: $driver"
echo "Matrix info: $info"

if [ "$have_mpg123" = no ]; then
	echo
	echo "NOTE: libmpg123 not found - the decoder-on cells were skipped."
	echo "      Install it to enable them (they cover most of the code base)."
fi
if [ "$have_sndfile" = no ]; then
	echo
	echo "NOTE: libsndfile not found - the sndfile cell was skipped."
fi

echo
echo "To run the matrix:"
echo "  sh $driver"
