#!/bin/sh
# Checks that a failed read gives a non-zero exit status.
#
# The input can fail to decode part-way through. The read functions of the
# frontend report this. The encode and decode loops must then end the process
# with a non-zero exit status. Without this, the process writes a truncated
# output file and still reports success. A caller in a script cannot notice
# the fault.
#
# The test runs both loops, because they are separate code with the same
# shape: the encoder path (MP3 in, MP3 out) and the decoder path (--decode).
#
# Two controls make the result meaningful. A clean input must exit 0, so a
# build in which everything fails cannot pass. An input that does not exist
# must exit non-zero, so a build in which nothing fails cannot pass either.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_exit_status.dir
SRC=$abs_top_srcdir/testcase.mp3

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
skip() { echo "SKIP  $1"; exit 77; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend -
      the binary this test exists to check was not produced. That is a failure
      and not a skip."
test -f "$SRC" || die "no $SRC to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# --------------------------------------------------------------- can we test?
#
# Reading an mp3 at all needs a decoder compiled in. Without one this test has
# no subject, and says so in as many words rather than passing quietly: a
# silent skip and a silent success look identical in a suite summary.
cp "$SRC" "$WORK/clean.mp3" || die "cannot copy the test input"
if ! "$LAME" --quiet --decode "$WORK/clean.mp3" "$WORK/probe.wav" 2>/dev/null; then
    skip "this build cannot decode mp3 input (no decoder configured in), so
      there is no failing read for the exit status to report. Configure with
      an mp3 decoder to run this test."
fi

# ------------------------------------------------------------- the corruption
#
# Overwrite the middle third with zero bytes, leaving the header and the tail
# in place, so the decode starts normally and fails part-way. Zeros are used
# rather than random data so that a failure is reproducible from the report
# alone; no /dev/urandom is needed, which not every platform building LAME has.
size=$(wc -c < "$WORK/clean.mp3" | tr -d ' ')
third=$((size / 3))
test "$third" -gt 0 || die "test input is too small to corrupt ($size bytes)"
cp "$WORK/clean.mp3" "$WORK/corrupt.mp3" || die "cannot copy the test input"
dd if=/dev/zero of="$WORK/corrupt.mp3" bs=1 seek="$third" count="$third" \
   conv=notrunc >/dev/null 2>&1 || die "cannot corrupt the test input"

cmp -s "$WORK/clean.mp3" "$WORK/corrupt.mp3" \
    && die "the corruption step changed nothing - dd conv=notrunc did not take
      effect, so the corrupt arms below would be testing a clean file"

# --------------------------------------------------------------------- arms
run() { # run <output> <args...>; sets `status`
    out=$1; shift
    "$LAME" --quiet "$@" >/dev/null 2>&1
    status=$?
    return 0
}

# control: a clean transcode still succeeds
run "$WORK/clean.out.mp3" "$WORK/clean.mp3" "$WORK/clean.out.mp3"
if test "$status" -eq 0; then
    ok "control: a clean transcode exits 0"
else
    bad "control: a clean transcode exited $status - every other result here is
      meaningless, because this build fails on good input too"
fi

# control: a missing input is reported
run "$WORK/missing.out.mp3" "$WORK/no-such-input.mp3" "$WORK/missing.out.mp3"
if test "$status" -ne 0; then
    ok "control: a missing input exits non-zero ($status)"
else
    bad "control: a missing input exited 0 - this build reports success for
      everything, so the arms below cannot show anything"
fi

# the subject: encode path
run "$WORK/corrupt.out.mp3" "$WORK/corrupt.mp3" "$WORK/corrupt.out.mp3"
if test "$status" -ne 0; then
    ok "a failed read during transcode exits non-zero ($status)"
else
    bad "a failed read during transcode exited 0, having written
      $(wc -c < "$WORK/corrupt.out.mp3" | tr -d ' ') bytes of a truncated file"
fi

# the subject: decode path
run "$WORK/corrupt.out.wav" --decode "$WORK/corrupt.mp3" "$WORK/corrupt.out.wav"
if test "$status" -ne 0; then
    ok "a failed read during --decode exits non-zero ($status)"
else
    bad "a failed read during --decode exited 0, having written
      $(wc -c < "$WORK/corrupt.out.wav" | tr -d ' ') bytes of a truncated file"
fi

exit $rc
