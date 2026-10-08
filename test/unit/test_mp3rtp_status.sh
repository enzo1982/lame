#!/bin/sh
# Checks the exit status of mp3rtp on an option error and on a read error.
#
# mp3rtp must stop with a non-zero exit status when an option value is
# refused, and when the input fails to decode part-way through. --help must
# exit 0. The packets go to the loopback address; nothing needs to receive
# them.
#
# Two controls make the result meaningful. A clean input must exit 0, so a
# build in which everything fails cannot pass. An input that does not exist
# must exit non-zero, so a build in which nothing fails cannot pass either.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_mp3rtp_status.dir
WAV=$abs_top_srcdir/testcase.wav
MP3=$abs_top_srcdir/testcase.mp3
DEST=127.0.0.1

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
skip() { echo "SKIP  $1"; exit 77; }

RTP=$abs_top_builddir/frontend/mp3rtp
test -x "$RTP" || RTP=$abs_top_builddir/frontend/mp3rtp.exe
test -x "$RTP" || skip "mp3rtp is not built here. Configure with
      --enable-mp3rtp to run this test."
test -f "$WAV" || die "no $WAV to work from"
test -f "$MP3" || die "no $MP3 to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

run() { # run <args...>; sets `status`
    "$RTP" "$DEST" "$@" >/dev/null 2>"$WORK/err.txt"
    status=$?
    return 0
}

# control: a clean encode succeeds
run --quiet "$WAV" "$WORK/clean.mp3"
if test "$status" -eq 0 && test -s "$WORK/clean.mp3"; then
    ok "control: a clean encode exits 0"
else
    bad "control: a clean encode exited $status - every other result here is
      meaningless, because this build fails on good input too"
fi

# control: a missing input is reported
run --quiet "$WORK/no-such-input.wav" "$WORK/missing.mp3"
if test "$status" -ne 0; then
    ok "control: a missing input exits non-zero ($status)"
else
    bad "control: a missing input exited 0 - this build reports success for
      everything, so the arms below cannot show anything"
fi

# the subject: a refused option value
run --quiet --scale 1e300 "$WAV" "$WORK/refused.mp3"
if test "$status" -ne 0; then
    ok "a refused option value exits non-zero ($status)"
else
    bad "a refused option value exited 0"
fi

# the subject: --help
run --help
if test "$status" -eq 0; then
    ok "--help exits 0"
else
    bad "--help exited $status: $(cat "$WORK/err.txt")"
fi

# the subject: a read error part-way through the input
cp "$MP3" "$WORK/clean-in.mp3" || die "cannot copy the test input"
run --quiet "$WORK/clean-in.mp3" "$WORK/probe.mp3"
if test "$status" -ne 0; then
    echo "SKIP  a read error: this build cannot read mp3 input (no decoder
      configured in), so there is no failing read to report"
else
    size=$(wc -c < "$WORK/clean-in.mp3" | tr -d ' ')
    third=$((size / 3))
    test "$third" -gt 0 || die "test input is too small to corrupt ($size bytes)"
    cp "$WORK/clean-in.mp3" "$WORK/corrupt.mp3" || die "cannot copy the test input"
    dd if=/dev/zero of="$WORK/corrupt.mp3" bs=1 seek="$third" count="$third" \
       conv=notrunc >/dev/null 2>&1 || die "cannot corrupt the test input"
    cmp -s "$WORK/clean-in.mp3" "$WORK/corrupt.mp3" \
        && die "the corruption step changed nothing, so the arm below would
      test a clean file"
    run --quiet "$WORK/corrupt.mp3" "$WORK/corrupt.out.mp3"
    if test "$status" -ne 0; then
        ok "a failed read exits non-zero ($status)"
    else
        bad "a failed read exited 0"
    fi
fi

exit $rc
