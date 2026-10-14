#!/bin/sh
# Checks that --replaygain-id3v2 fills in the gain at the largest ID3v2
# padding, --pad-id3v2-size 128000.
#
# The frontend writes the ID3v2 tag with a placeholder gain before the audio,
# and rewrites it with the measured gain after the encode. The rewrite must
# keep the tag's length, so the frontend reserves some padding beyond the
# user's for the gain to grow into. At the largest padding that reservation
# must still fit the library's maximum, or the rewrite changes the length and
# the placeholder stays in the file.
#
# The control encodes the same input with a small padding: its gain must not
# be the placeholder's, so a placeholder left in the file can be told apart,
# and the gain at the largest padding must be the same as the control's.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_replaygain_pad_max.dir
SRC=$abs_top_srcdir/testcase.wav
PLACEHOLDER="0.00 dB"

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

# gain <mp3>: prints the value of the REPLAYGAIN_TRACK_GAIN frame, such as
# "-1.23 dB". The frame's text is the description, a NUL, then the value.
gain() {
    dd if="$1" bs=1024 count=140 2>/dev/null | tr '\000' '|' \
        | grep -a -o 'REPLAYGAIN_TRACK_GAIN|[-+0-9.]* dB' | head -n 1 \
        | sed 's/^REPLAYGAIN_TRACK_GAIN|//'
}

# encode <padding> <mp3>: sets `status`; stderr goes to <mp3>.err
encode() {
    rm -f "$2"
    "$LAME" --replaygain-id3v2 --pad-id3v2-size "$1" "$SRC" "$2" \
        >/dev/null 2>"$2.err"
    status=$?
}

encode 128 "$WORK/small.mp3"
small_status=$status
small=$(gain "$WORK/small.mp3")
if test "$small_status" -eq 0 && test -n "$small" && test "$small" != "$PLACEHOLDER"; then
    ok "control: with a small padding the gain is measured ($small)"
else
    die "control: exited $small_status, gain \"$small\" - without a measured gain
      in the control this test cannot tell a placeholder from a value"
fi

encode 128000 "$WORK/max.mp3"
max=$(gain "$WORK/max.mp3")
if test "$status" -eq 0 && test "$max" = "$small" \
    && ! grep -q "ReplayGain tag frames\|not writing ReplayGain" "$WORK/max.mp3.err"; then
    ok "--pad-id3v2-size 128000: the gain is filled in ($max)"
else
    bad "--pad-id3v2-size 128000: exited $status, gain \"$max\", expected \"$small\";
      stderr: $(cat "$WORK/max.mp3.err")"
fi

exit $rc
