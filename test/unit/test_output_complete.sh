#!/bin/sh
# Checks that lame exits 0 only when its output is complete.
#
# With a limit on the size of a file (ulimit -f), writing the output fails
# part of the way. The last buffered bytes leave at a seek or at the close of
# the output, so a limit inside the last buffer makes only that write fail.
# For each limit from 16 kB below the complete size up to it, lame must exit
# non-zero or write the whole file: encoding, decoding, and encoding to
# standard output redirected to a file.
#
# The controls make the result meaningful. Without a limit, each run exits 0.
# At least one limit must stop the write, or the limit does not work here and
# the test cannot run. An encode to a pipe exits 0 and writes the stream, as a
# seek on a pipe fails without a write error.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_output_complete.dir
SRC=$abs_top_srcdir/testcase.wav
# How far below the complete size the limits start, in bytes: more than the
# buffer of the C library.
SWEEP_BYTES=16384

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
cp "$SRC" "$WORK/in.wav" || die "cannot copy the input"

# The block of ulimit -f: 512 bytes in some shells, 1024 in others.
( trap '' XFSZ; ulimit -f 1 || exit 99
  dd if=/dev/zero of="$WORK/block" bs=1024 count=1 ) >/dev/null 2>&1
test $? -ne 99 || skip "ulimit -f is not available here"
block=$(wc -c < "$WORK/block")
test "$block" -eq 512 || test "$block" -eq 1024 \
    || skip "ulimit -f does not stop the write of a file here ($block bytes)"

# run <limit> <output> <arguments...>: lame under a file size limit, its status
run() {
    limit=$1; output=$2; shift 2
    rm -f "$output"
    if test "$output" = "-stdout"; then
        ( trap '' XFSZ; ulimit -f "$limit"
          exec "$LAME" --silent "$@" "$WORK/in.wav" - > "$WORK/out" ) >/dev/null 2>&1
    else
        ( trap '' XFSZ; ulimit -f "$limit"
          exec "$LAME" --silent "$@" "$WORK/in.wav" "$output" ) >/dev/null 2>&1
    fi
}

for arm in "encode out.mp3" "decode out.wav --decode" "stdout -stdout"; do
    set -- $arm
    name=$1; output=$2; shift 2
    file=$output
    test "$file" = "-stdout" && file=$WORK/out || file=$WORK/$output
    test "$output" = "-stdout" || output=$file

    # control: without a limit the run is complete
    rm -f "$file"
    if test "$output" = "-stdout"; then
        "$LAME" --silent "$@" "$WORK/in.wav" - > "$file" 2>/dev/null
    else
        "$LAME" --silent "$@" "$WORK/in.wav" "$file" >/dev/null 2>&1
    fi
    status=$?
    test "$status" -eq 0 || { bad "control: $name exited $status without a limit"; continue; }
    full=$(wc -c < "$file")

    first=$(( (full - SWEEP_BYTES) / block ))
    test "$first" -ge 1 || first=1
    last=$(( full / block + 1 ))
    stopped=0; short=0; k=$first
    while test "$k" -le "$last"; do
        run "$k" "$output" "$@"
        status=$?
        size=0
        test -f "$file" && size=$(wc -c < "$file")
        if test "$status" -ne 0; then
            stopped=$((stopped + 1))
        elif test "$size" -ne "$full"; then
            short=$((short + 1))
            echo "      limit $k blocks of $block bytes: exit 0 with $size of $full bytes"
        fi
        k=$((k + 1))
    done
    test "$stopped" -gt 0 || skip "no limit stopped the $name write"
    if test "$short" -eq 0; then
        ok "$name: limits $first to $last: exit non-zero ($stopped) or the whole $full bytes"
    else
        bad "$name: $short limit(s) exit 0 with a short output"
    fi
done

# control: a pipe takes the whole stream, and the seek that fails on it is
# no write error
{ "$LAME" --silent "$WORK/in.wav" - 2>/dev/null; echo $? > "$WORK/status"; } | cat > "$WORK/pipe.mp3"
status=$(cat "$WORK/status")
size=$(wc -c < "$WORK/pipe.mp3")
if test "$status" -eq 0 && test "$size" -gt 0; then
    ok "control: an encode to a pipe exits 0 with $size bytes"
else
    bad "control: an encode to a pipe exits $status with $size bytes"
fi

exit $rc
