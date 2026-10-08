#!/bin/sh
# Checks the range that the error message of --preset names for an ABR bitrate.
#
# The smallest bitrate --preset accepts is 8 kbps. A smaller one is rejected,
# and the message must name 8 as the lower end of the range.
#
# Two controls make the result meaningful. --preset 8 must succeed, so 8 is
# the real lower end. --preset 7 must fail, so the message comes from this
# check and not from somewhere else.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_preset_range.dir
SRC=$abs_top_srcdir/testcase.wav
RANGE='between "8" and "320"'

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend -
      the binary this test exists to check was not produced. That is a failure
      and not a skip."
test -f "$SRC" || die "no $SRC to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# control: the lower end is accepted
"$LAME" --quiet --preset 8 "$SRC" "$WORK/8.mp3" >/dev/null 2>&1
status=$?
if test "$status" -eq 0; then
    ok "control: --preset 8 exits 0"
else
    bad "control: --preset 8 exited $status"
fi

# the subject: one below is rejected, and the message names the range
"$LAME" --quiet --preset 7 "$SRC" "$WORK/7.mp3" >/dev/null 2>"$WORK/err.txt"
status=$?
if test "$status" -ne 0; then
    ok "control: --preset 7 exits non-zero ($status)"
else
    bad "control: --preset 7 exited 0"
fi
if grep -F "$RANGE" "$WORK/err.txt" >/dev/null; then
    ok "the message names the range $RANGE"
else
    bad "the message does not name the range $RANGE:
$(cat "$WORK/err.txt")"
fi

exit $rc
