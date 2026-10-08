#!/bin/sh
# Checks that --nogap encodes every input file it is given.
#
# Three copies of the test WAV are encoded in one --nogap run, once beside the
# inputs and once into a --nogapout directory. Each input must give its own
# non-empty MP3 file under the name derived from the input name.
#
# The control makes the result meaningful. A file in the middle of a --nogap
# run is encoded without the final flush, so it must differ from a plain
# encode of the same input. A build that encodes each file on its own cannot
# pass.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_nogap_files.dir
SRC=$abs_top_srcdir/testcase.wav
NAMES="a b c"

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
rm -rf "$WORK"; mkdir -p "$WORK/out" || die "cannot create $WORK"
for n in $NAMES; do
    cp "$SRC" "$WORK/$n.wav" || die "cannot copy $SRC to $WORK/$n.wav"
done

# check_outputs <directory> <label>: every input has its MP3 in <directory>
check_outputs() {
    for n in $NAMES; do
        if test -s "$1/$n.mp3"; then
            ok "$2: $n.wav gives a non-empty $n.mp3"
        else
            bad "$2: no $1/$n.mp3 for $n.wav"
        fi
    done
}

# the subject: one --nogap run, outputs beside the inputs
"$LAME" --quiet --nogap "$WORK/a.wav" "$WORK/b.wav" "$WORK/c.wav" >/dev/null 2>"$WORK/err.txt"
status=$?
if test "$status" -eq 0; then
    ok "--nogap with three files exits 0"
else
    bad "--nogap with three files exited $status:
$(cat "$WORK/err.txt")"
fi
check_outputs "$WORK" "--nogap"

# the subject: one --nogap run, outputs in the --nogapout directory
"$LAME" --quiet --nogapout "$WORK/out" --nogap "$WORK/a.wav" "$WORK/b.wav" "$WORK/c.wav" \
    >/dev/null 2>"$WORK/err.txt"
status=$?
if test "$status" -eq 0; then
    ok "--nogapout with three files exits 0"
else
    bad "--nogapout with three files exited $status:
$(cat "$WORK/err.txt")"
fi
check_outputs "$WORK/out" "--nogapout"

# control: the middle file of the run is not a plain encode
"$LAME" --quiet "$WORK/b.wav" "$WORK/plain.mp3" >/dev/null 2>&1 \
    || die "a plain encode of $WORK/b.wav fails"
if test -s "$WORK/b.mp3" && ! cmp -s "$WORK/b.mp3" "$WORK/plain.mp3"; then
    ok "control: the middle file of the --nogap run differs from a plain encode"
else
    bad "control: the middle file of the --nogap run is missing or equal to a
      plain encode, so the run did not encode the files as one --nogap chain"
fi

exit $rc
