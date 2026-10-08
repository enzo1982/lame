#!/bin/sh
# Checks the row of the bitrate histogram for a CBR encode.
#
# lame prints the histogram on stderr while it encodes. A CBR encode has one
# bitrate, so the histogram must show one row for it, with a frame count above
# zero. The test covers a table bitrate (-b 128) and a free format bitrate
# that is not in the table (--freeformat -b 136).
#
# The controls make the result meaningful. A VBR encode and an ABR encode must
# show rows too, so the pattern finds rows where they exist. With --nohist the
# row must be absent, so the pattern does not match the other lines of the
# display. A free format encode with --nohist must not print an error.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_brhist_cbr.dir
SRC=$abs_top_srcdir/testcase.wav

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

# encode <args...>: encodes the test WAV, the display goes to $WORK/display.txt
# with every carriage return turned into a line break; sets `status`
encode() {
    "$LAME" "$@" "$SRC" "$WORK/out.mp3" >/dev/null 2>"$WORK/err.txt"
    status=$?
    tr '\r' '\n' < "$WORK/err.txt" > "$WORK/display.txt"
    return 0
}

# rows <kbps>: the number of histogram rows for <kbps> with a frame count above 0
rows() {
    grep -E -c "^ *$1 \[ *[1-9][0-9]*\]" "$WORK/display.txt"
}

# the subject: a CBR encode shows its bitrate
encode -b 128
if test "$status" -eq 0 && test "$(rows 128)" -gt 0; then
    ok "-b 128 shows a histogram row for 128 kbps"
else
    bad "-b 128 (exit $status) shows no histogram row for 128 kbps"
fi

# the subject: a free format encode shows its bitrate
encode --freeformat -b 136
if test "$status" -eq 0 && test "$(rows 136)" -gt 0; then
    ok "--freeformat -b 136 shows a histogram row for 136 kbps"
else
    bad "--freeformat -b 136 (exit $status) shows no histogram row for 136 kbps"
fi

# control: VBR and ABR show rows
for args in "-V 2" "--abr 128"; do
    # shellcheck disable=SC2086
    encode $args
    if test "$status" -eq 0 && test "$(grep -E -c '^ *[0-9]+ \[ *[1-9][0-9]*\]' "$WORK/display.txt")" -gt 0; then
        ok "control: $args shows histogram rows"
    else
        bad "control: $args (exit $status) shows no histogram row, so the
      pattern does not find rows that exist"
    fi
done

# control: no row with --nohist
encode --nohist -b 128
if test "$status" -eq 0 && test "$(rows 128)" -eq 0; then
    ok "control: --nohist -b 128 shows no histogram row"
else
    bad "control: --nohist -b 128 (exit $status) shows a histogram row, so the
      pattern matches more than the histogram"
fi

# control: free format with --nohist prints no error
encode --nohist --freeformat -b 136
if test "$status" -eq 0 && ! grep -i "error" "$WORK/display.txt" >/dev/null; then
    ok "control: --nohist --freeformat -b 136 prints no error"
else
    bad "control: --nohist --freeformat -b 136 (exit $status) prints:
$(grep -i "error" "$WORK/display.txt")"
fi

exit $rc
