#!/bin/sh
# Checks that an MP3 input of unknown length is encoded as one of unknown
# length.
#
# An MP3 file without an Info tag, read from a pipe, has no length that the
# decoder can know. lame must then write no play length (TLEN) into the ID3v2
# tag. The control: the same file read from the file system has a known
# length, and its output carries TLEN, so the search can find the frame.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_pipe_length.dir
WAV=$abs_top_srcdir/testcase.wav

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
skip() { echo "SKIP  $1"; exit 77; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend"
test -f "$WAV" || die "no $WAV to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# An MP3 without an Info tag (-t).
"$LAME" --quiet -t "$WAV" "$WORK/untagged.mp3" ||
    die "cannot encode the input MP3"
# MP3 input needs the decoder; a build without it refuses --mp3input.
"$LAME" --quiet --mp3input "$WORK/untagged.mp3" "$WORK/probe.mp3" 2>/dev/null ||
    skip "this lame cannot decode MP3 input"

"$LAME" --quiet --mp3input --add-id3v2 --tt Title "$WORK/untagged.mp3" "$WORK/file.mp3" ||
    die "encoding from the file failed"
if grep -q TLEN "$WORK/file.mp3"; then
    ok "from a file, the length is known and TLEN is written"
else
    bad "from a file, no TLEN: the search cannot see the frame"
fi

cat "$WORK/untagged.mp3" |
    "$LAME" --quiet --mp3input --add-id3v2 --tt Title - "$WORK/pipe.mp3" ||
    die "encoding from the pipe failed"
if grep -q TLEN "$WORK/pipe.mp3"; then
    bad "from a pipe, TLEN is written for a length nobody knows"
else
    ok "from a pipe, no TLEN"
fi

exit $rc
