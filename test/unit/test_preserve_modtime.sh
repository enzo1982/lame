#!/bin/sh
# Checks that --preserve-modtime gives the output the times of the input only
# when the output was written completely.
#
# The input is dated 2001. With a limit on the size of a file (ulimit -f),
# writing the output fails part of the way, and lame exits non-zero. The
# truncated output must keep the time it was written at, so that it does not
# look like a finished file as old as the input. Encoding and decoding are
# checked.
#
# The controls make the result meaningful. Without the limit, the output gets
# the time of the input, so a build that never sets times cannot pass. With
# the limit, lame must exit non-zero. Where the limit does not stop the write,
# or the platform cannot set file times, the test cannot run and says so.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_preserve_modtime.dir
SRC=$abs_top_srcdir/testcase.wav

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

cp "$SRC" "$WORK/in.wav" && touch -t 200101010000 "$WORK/in.wav" \
    || die "cannot make the dated input"

# same_time <a> <b>: whether the two files have the same modification time
same_time() {
    ! test "$1" -nt "$2" && ! test "$1" -ot "$2"
}

for arm in "encode mp3" "decode wav --decode"; do
    set -- $arm
    name=$1; suffix=$2; shift 2

    # control: a complete output gets the time of the input
    rm -f "$WORK/ok.$suffix"
    "$LAME" --silent --preserve-modtime "$@" "$WORK/in.wav" "$WORK/ok.$suffix" >/dev/null 2>&1
    status=$?
    if test "$status" -ne 0; then
        bad "control: $name exited $status without a limit"
        continue
    fi
    same_time "$WORK/ok.$suffix" "$WORK/in.wav" \
        || skip "this platform does not set file times, so --preserve-modtime has
      nothing to show"
    ok "control: $name: a complete output gets the time of the input"

    # the subject: the write fails part of the way. The limit is a quarter of
    # the complete output in blocks of 1024 bytes, so at most a quarter in the
    # blocks of 512 bytes that some shells count in.
    blocks=$(($(wc -c < "$WORK/ok.$suffix") / 4096))
    test "$blocks" -gt 0 || die "the complete $name output is too small to cut short"
    rm -f "$WORK/bad.$suffix"
    ( trap '' XFSZ; ulimit -f "$blocks" || exit 99
      exec "$LAME" --silent --preserve-modtime "$@" "$WORK/in.wav" "$WORK/bad.$suffix" ) \
        >/dev/null 2>&1
    status=$?
    test "$status" -ne 99 || skip "ulimit -f is not available here"
    test "$status" -ne 0 || skip "ulimit -f does not stop the write of a file here"
    if test -e "$WORK/bad.$suffix" && ! same_time "$WORK/bad.$suffix" "$WORK/in.wav"; then
        ok "$name: an output whose writing failed (exit $status) keeps its own time"
    else
        bad "$name: an output whose writing failed (exit $status) got the time of the input"
    fi
done

exit $rc
