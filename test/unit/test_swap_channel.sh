#!/bin/sh
# Checks that --swap-channel is rejected for an input with one channel.
#
# The option swaps the two channels of the input. A mono input has only one,
# so lame must stop with an error and a non-zero exit status, and write no
# output file. The test covers the four mono paths: WAV and MP3 input, each
# encoded and decoded.
#
# The controls make the result meaningful. The same four commands without the
# option must succeed, so a build that fails on mono input cannot pass. A stereo
# input with the option must succeed, so a build that rejects the option itself
# cannot pass.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_swap_channel.dir
SRC=$abs_top_srcdir/testcase.mp3
MESSAGE='--swap-channel needs an input with two channels'

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

# ------------------------------------------------------------------- inputs
#
# All inputs are made from the test MP3, so the test needs a decoder. Without
# one it says so rather than passing quietly.
if ! "$LAME" --quiet --decode "$SRC" "$WORK/stereo.wav" 2>/dev/null; then
    skip "this build cannot decode mp3 input (no decoder configured in), so
      the mono inputs cannot be made. Configure with an mp3 decoder to run
      this test."
fi
"$LAME" --quiet -m m "$WORK/stereo.wav" "$WORK/mono.mp3" \
    || die "cannot encode the mono MP3 input"
"$LAME" --quiet --decode "$WORK/mono.mp3" "$WORK/mono.wav" \
    || die "cannot decode the mono MP3 input to a mono WAV"

# ------------------------------------------------------------------- arms
run() { # run <output> <args...>; sets `status` and `err`
    out=$1; shift
    rm -f "$out"
    "$LAME" --silent "$@" "$out" >/dev/null 2>"$WORK/err.txt"
    status=$?
    err=$(cat "$WORK/err.txt")
    return 0
}

# control: a stereo input with the option is accepted
run "$WORK/stereo.swap.mp3" --swap-channel "$WORK/stereo.wav"
if test "$status" -eq 0 && test -s "$WORK/stereo.swap.mp3"; then
    ok "control: --swap-channel on a stereo input exits 0"
else
    bad "control: --swap-channel on a stereo input exited $status - the option
      itself fails, so the mono arms below cannot show anything"
fi

for arm in "mono.wav mp3" "mono.mp3 mp3" "mono.wav wav --decode" "mono.mp3 wav --decode"; do
    set -- $arm
    in=$1; suffix=$2; shift 2
    name="$in${1:+ $1}"

    # control: the same command without the option succeeds
    run "$WORK/plain.$suffix" "$@" "$WORK/$in"
    if test "$status" -eq 0 && test -s "$WORK/plain.$suffix"; then
        ok "control: $name exits 0"
    else
        bad "control: $name exited $status - this build fails on mono input
          without the option, so the arm below proves nothing"
    fi

    # the subject: the option is rejected
    run "$WORK/swap.$suffix" --swap-channel "$@" "$WORK/$in"
    if test "$status" -eq 0; then
        bad "$name --swap-channel exited 0"
    elif ! printf '%s\n' "$err" | grep -q -e "$MESSAGE"; then
        bad "$name --swap-channel exited $status without the message; it said:
          $err"
    elif test -e "$WORK/swap.$suffix"; then
        bad "$name --swap-channel exited $status but wrote an output file"
    else
        ok "$name --swap-channel exits $status with the message"
    fi
done

exit $rc
