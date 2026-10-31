#!/bin/sh
# Checks what lame says when the album art of --ti cannot be measured.
#
# The frontend reads the image whole, so it needs its size first. A pipe has
# none. lame must then stop with a message that says so, and exit non-zero.
# The control is a regular image file, which must give exit 0.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_albumart_pipe.dir
SRC=$abs_top_srcdir/testcase.wav

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
skip() { echo "SKIP  $1"; exit 77; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend"
test -f "$SRC" || die "no $SRC to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# A GIF that the image check accepts: its signature and some bytes.
printf 'GIF89a' > "$WORK/art.gif"
head -c 2000 /dev/zero >> "$WORK/art.gif"

"$LAME" --silent --ti "$WORK/art.gif" "$SRC" "$WORK/file.mp3" 2> "$WORK/file.err"
st=$?
if test "$st" -eq 0; then
    ok "control: album art from a regular file exits 0"
else
    bad "control: album art from a regular file exited $st: $(head -c 100 "$WORK/file.err")"
fi

mkfifo "$WORK/art.pipe" 2> /dev/null || skip "no mkfifo here"
case $(uname -s) in
MINGW* | MSYS* | CYGWIN*) skip "a native build cannot open this shell's named pipe" ;;
esac
cat "$WORK/art.gif" > "$WORK/art.pipe" &
writer=$!
"$LAME" --silent --ti "$WORK/art.pipe" "$SRC" "$WORK/pipe.mp3" 2> "$WORK/pipe.err"
st=$?
if test "$st" -ne 0 && grep -q 'size is not known' "$WORK/pipe.err"; then
    ok "album art from a pipe stops with a message that says why"
else
    bad "album art from a pipe: exit $st, message: $(head -c 100 "$WORK/pipe.err")"
fi
# If lame did not open the pipe, the writer still waits for a reader.
kill "$writer" 2> /dev/null
wait "$writer" 2> /dev/null

exit $rc
