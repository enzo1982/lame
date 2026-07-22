#!/bin/sh
#
# coverage-run.sh - run the coverage workload against one built cell of the
# coverage matrix, capturing coverage separately for every invocation.
#
# Per-invocation capture is the whole point. A single aggregate capture says
# what is covered but not which invocation earned it, and the analysis step
# (coverage-report.py) needs exactly that attribution to work out a minimal
# set of invocations - and to tell an invocation that adds coverage from one
# that merely repeats what another already reached.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog -c CELLDIR [-o OUTDIR] [-w WORKLOAD] [-t TESTDIR] [-m MP3DIR]
             [-n MAX] [-j N] [-h]

  -c CELLDIR  Built coverage cell (a directory from gen-coverage-matrix.sh).
  -o OUTDIR   Where to write per-run coverage data.
              (default: CELLDIR/coverage-out)
  -w WORKLOAD Workload manifest. (default: alongside this script)
  -t TESTDIR  Directory holding the .op option files, whose lines are run in
              addition to the manifest. (default: SRCDIR/test)
  -m MP3DIR   Optional directory of MP3 files, used for the %MP3% entries.
              Files are read only; nothing is written back to it.
  -n MAX      At most this many MP3s from MP3DIR. (default: 10)
              Each one decodes a whole track, which dominates the run time
              while adding almost no coverage after the first few - the same
              parser runs every time. Raise it for a sanitizer sweep, where
              breadth of real-world input is the point, not for coverage.
  -j N        Parallel workers for lcov's data gathering. (default: 8)
  -h          This help.
EOF
}

celldir=
outdir=
workload=
testdir=
mp3dir=
maxmp3=10
par=8

script_dir=$(cd "$(dirname "$0")" && pwd)

while getopts "c:o:w:t:m:n:j:h" opt; do
	case "$opt" in
		c) celldir=$OPTARG ;;
		o) outdir=$OPTARG ;;
		w) workload=$OPTARG ;;
		t) testdir=$OPTARG ;;
		m) mp3dir=$OPTARG ;;
		n) maxmp3=$OPTARG ;;
		j) par=$OPTARG ;;
		h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done

if [ -z "$celldir" ]; then
	echo "$prog: -c CELLDIR is required" >&2
	usage >&2
	exit 2
fi
celldir=$(cd "$celldir" && pwd) || exit 1

lame="$celldir/frontend/lame"
if [ ! -x "$lame" ]; then
	echo "$prog: no lame binary at $lame - is the cell built?" >&2
	exit 1
fi

[ -n "$workload" ] || workload="$script_dir/coverage-workload.txt"
[ -n "$outdir" ] || outdir="$celldir/coverage-out"

# The source directory this cell was configured from, so the .op files can be
# found without being told where they are.
srcdir=$(sed -n "s/^srcdir='\(.*\)'$/\1/p" "$celldir/build.sh" 2>/dev/null)
[ -n "$testdir" ] || testdir="$srcdir/test"

work="$outdir/work"
runs="$outdir/runs"
rm -rf "$outdir"
mkdir -p "$work" "$runs"

# lcov 2.x aborts where 1.x warned, on data that is inconsistent rather than
# wrong; geninfo_unexecuted_blocks stops unexecuted blocks on a line with a
# nonzero count from being credited as covered.
LCOVOPT="--quiet --ignore-errors mismatch,unused,empty,negative,source,gcov
         --rc geninfo_unexecuted_blocks=1"
if [ "$par" -gt 1 ]; then
	LCOVOPT="$LCOVOPT --rc geninfo_parallel=$par"
fi

# --- cell capabilities ------------------------------------------------------
#
# Read from the cell's own config.h rather than re-deriving them from the
# configure arguments: the header is what the compiler actually saw.

have_decoder=no
have_analyzer=no
have_sndfileio=no
if grep -q '^#define HAVE_MPG123' "$celldir/config.h" 2>/dev/null; then
	have_decoder=yes
fi
if ! grep -q '^#define NOANALYSIS' "$celldir/config.h" 2>/dev/null; then
	have_analyzer=yes
fi
if grep -q '^#define LIBSNDFILE' "$celldir/config.h" 2>/dev/null; then
	have_sndfileio=yes
fi

# --enable-mp3rtp builds a SECOND binary. Without running it the cell
# compiles more code than any other and executes none of it, so the whole
# cell earns nothing.
mp3rtp="$celldir/frontend/mp3rtp"
have_mp3rtp=no
[ -x "$mp3rtp" ] && have_mp3rtp=yes

# Where the rtp mode streams to. Loopback ON PURPOSE and not configurable:
# the harness must never emit packets onto a real network, least of all the
# multicast addresses the mp3rtp usage text suggests. Nothing listens, so
# the datagrams are discarded by the kernel.
RTP_DEST='127.0.0.1'
RTP_PORT='19832:1'

# Optional: absent on some systems, in which case the rtp runs are simply
# unbounded rather than skipped.
if command -v timeout >/dev/null 2>&1; then
	timeout='timeout 60'
else
	timeout=''
fi

echo "cell        : $celldir"
echo "decoder     : $have_decoder"
echo "analyzer    : $have_analyzer"
echo "sndfile io  : $have_sndfileio"
echo "mp3rtp      : $have_mp3rtp"
echo "workload    : $workload"
echo "op files    : $testdir"
echo "mp3 corpus  : ${mp3dir:-<none>}"
echo "output      : $outdir"
echo

# --- inputs -----------------------------------------------------------------

inputs="$work/inputs"
mkdir -p "$inputs"
if ! python3 "$script_dir/coverage-mkinputs.py" "$inputs" > "$work/mkinputs.log" 2>&1; then
	echo "$prog: generating input files failed:" >&2
	cat "$work/mkinputs.log" >&2
	exit 1
fi

# The compiled-in line set for this cell: every line the compiler emitted,
# all at zero hits. This is the other half of the analysis - it says what
# COULD be reached here, which is what makes "never executed by anything"
# distinguishable from "not compiled in this configuration".
lcov -c -i -d "$celldir" -o "$outdir/baseline.info" $LCOVOPT >/dev/null 2>&1

# An MP3 to feed the decode entries, made with this very build so the harness
# does not depend on any MP3 happening to be lying around.
"$lame" --quiet -V9 "$inputs/stereo.wav" "$inputs/in.mp3" 2>/dev/null
echo "MP3IN	$inputs/in.mp3" >> "$inputs/inputs.map"

resolve() {
	# Replace every %NAME% token with its path from inputs.map.
	awk -v s="$1" '
		BEGIN {
			while ((getline line < ARGV[1]) > 0) {
				split(line, kv, "\t")
				gsub("%" kv[1] "%", kv[2], s)
			}
			print s
		}
	' "$inputs/inputs.map"
}

# --- run list ---------------------------------------------------------------
#
# One "id<TAB>mode<TAB>input<TAB>options<TAB>expect" record per invocation.

runlist="$work/runlist.tsv"
: > "$runlist"

# 1. the manifest, filtered by this cell's capabilities
sed -e 's/#.*$//' "$workload" | while IFS='|' read -r id mode input opts expect when; do
	id=$(echo "$id" | tr -d ' \t')
	[ -n "$id" ] || continue
	mode=$(echo "$mode" | tr -d ' \t')
	input=$(echo "$input" | tr -d ' \t')
	expect=$(echo "$expect" | tr -d ' \t')
	when=$(echo "$when" | tr -d ' \t')
	opts=$(echo "$opts" | sed -e 's/^ *//' -e 's/ *$//')

	# An empty field cannot be written to the runlist as nothing. Tab is an
	# IFS *whitespace* character, so the read-back below merges consecutive
	# tabs, the empty field disappears and every later field shifts left -
	# silently, producing a plausible-looking wrong command line. Carry a
	# sentinel instead and drop it at execution.
	[ -n "$opts" ] || opts='%NOOPTS%'

	case "$when" in
		decoder)   [ "$have_decoder" = yes ] || continue ;;
		nodecoder) [ "$have_decoder" = no ]  || continue ;;
		analyzer)  [ "$have_analyzer" = yes ] || continue ;;
		ownio)     [ "$have_sndfileio" = no ]  || continue ;;
		sndfileio) [ "$have_sndfileio" = yes ] || continue ;;
		mp3rtp)    [ "$have_mp3rtp" = yes ] || continue ;;
	esac

	if [ "$input" = "%MP3%" ]; then
		[ -n "$mp3dir" ] || continue
		n=0
		find "$mp3dir" -type f -iname '*.mp3' 2>/dev/null | sort | while read -r f; do
			n=$((n + 1))
			[ "$n" -le "$maxmp3" ] || break
			printf '%s-%03d\t%s\t%s\t%s\t%s\n' "$id" "$n" "$mode" "$f" "$opts" "$expect" >> "$runlist"
		done
		continue
	fi

	# Options are resolved too: entries like "--ti %JPG%" carry a placeholder
	# in the option list rather than in the input field.
	printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$mode" "$(resolve "$input")" "$(resolve "$opts")" "$expect" >> "$runlist"
done

# 2. the .op files: every option line becomes an encode of the canonical WAV.
#    Their exit status is not asserted - these lists predate the harness and
#    some lines are expected to be refused by some builds - so they carry
#    expect=any and contribute coverage either way.
wav=$(resolve "%WAV%")
for op in "$testdir"/*.op; do
	[ -f "$op" ] || continue
	base=$(basename "$op" .op)
	n=0
	while IFS= read -r line; do
		line=$(echo "$line" | sed -e 's/^ *//' -e 's/ *$//')
		[ -n "$line" ] || continue
		case "$line" in \#*) continue ;; esac
		n=$((n + 1))
		printf 'op-%s-%03d\t%s\t%s\t%s\t%s\n' "$base" "$n" encode "$wav" "$line" any >> "$runlist"
	done < "$op"
done

total=$(wc -l < "$runlist" | tr -d ' ')

# Guard the whole class rather than the one instance. The invariant is that
# no field may be EMPTY, not merely that there are five of them: the read
# loop below splits on IFS=tab, and tab is IFS *whitespace*, so consecutive
# tabs are merged and an empty field silently disappears, shifting every
# later field left. The result is a wrong command line and a shifted
# expectation, neither of which announces itself.
#
# Counting fields does not catch this - awk's -F'\t' is a plain separator and
# does not collapse, so a line with an empty field still reports NF=5. The
# check has to be for emptiness, which is the property that actually breaks.
malformed=$(awk -F'\t' 'NF != 5 || $1 == "" || $2 == "" || $3 == "" || $4 == "" || $5 == ""' "$runlist" | wc -l | tr -d ' ')
if [ "$malformed" -ne 0 ]; then
	echo "$prog: $malformed malformed runlist line(s) - aborting:" >&2
	awk -F'\t' 'NF != 5 || $1 == "" || $2 == "" || $3 == "" || $4 == "" || $5 == "" { print "  NF=" NF ": [" $0 "]" }' "$runlist" >&2
	exit 1
fi

echo "runs to execute: $total"
echo

# --- execute ----------------------------------------------------------------

results="$outdir/results.tsv"
printf 'id\tstatus\texpect\tverdict\trc\n' > "$results"
mismatches=0
i=0

while IFS="$(printf '\t')" read -r id mode input opts expect; do
	i=$((i + 1))
	if [ "$opts" = '%NOOPTS%' ]; then
		opts=''
	fi
	out="$work/out"
	rm -f "$out" "$out.mp3" "$out.wav"

	lcov -z -d "$celldir" $LCOVOPT >/dev/null 2>&1

	# shellcheck disable=SC2086 - options are meant to word-split
	case "$mode" in
		encode)
			"$lame" $opts "$input" "$out.mp3" >/dev/null 2>&1
			rc=$?
			;;
		decode)
			"$lame" $opts "$input" "$out.wav" >/dev/null 2>&1
			rc=$?
			;;
		stdio)
			"$lame" $opts - - < "$input" > "$out.mp3" 2>/dev/null
			rc=$?
			;;
		noinput)
			"$lame" $opts >/dev/null 2>&1
			rc=$?
			;;
		rtp)
			# Bounded: a streaming send that blocks would otherwise
			# wedge the whole run.
			$timeout "$mp3rtp" "$RTP_DEST" "$RTP_PORT" $opts \
				"$input" "$out.mp3" >/dev/null 2>&1
			rc=$?
			;;
		rtpnoargs)
			$timeout "$mp3rtp" $opts >/dev/null 2>&1
			rc=$?
			;;
		*)
			echo "$prog: unknown mode '$mode' for $id" >&2
			continue
			;;
	esac

	if [ "$rc" -eq 0 ]; then status=ok; else status=fail; fi
	case "$expect" in
		any) verdict=- ;;
		"$status") verdict=MATCH ;;
		*) verdict=MISMATCH; mismatches=$((mismatches + 1)) ;;
	esac

	lcov -c -d "$celldir" -o "$runs/$id.info" $LCOVOPT >/dev/null 2>&1

	printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$status" "$expect" "$verdict" "$rc" >> "$results"
	printf '\r  %d/%d  %-28s %s   ' "$i" "$total" "$id" "$verdict"
done < "$runlist"

echo
echo

# --- report -----------------------------------------------------------------

echo "=== expectation mismatches ==="
awk -F'\t' '$4 == "MISMATCH" { printf "  %-28s expected %-4s got %-4s (rc=%s)\n", $1, $3, $2, $5 }' "$results"
if [ "$(awk -F'\t' '$4 == "MISMATCH"' "$results" | wc -l | tr -d ' ')" = 0 ]; then
	echo "  none"
fi

echo
echo "per-run coverage data: $runs"
echo "results table        : $results"
