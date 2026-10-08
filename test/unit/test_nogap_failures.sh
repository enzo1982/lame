#!/bin/sh
# Checks what a --nogap run does when one of its files fails.
#
# Three files are encoded in one --nogap run, and the middle one fails:
#
# - a read error in the middle input: lame goes on with the next file, prints
#   how many files failed, and exits with a non-zero status;
# - an output that cannot be written: lame stops at once, does not encode the
#   last file, and exits with a non-zero status;
# - an input the encoder rejects (a float WAV holding NaN): the same as an
#   output failure.
#
# The output that cannot be written is a symbolic link to /dev/full. The read
# error needs an MP3 decoder. Each arm skips where its part is missing.
#
# The control makes the result meaningful. The same run with three good inputs
# must exit 0 and print no failure count.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_nogap_failures.dir
WAV=$abs_top_srcdir/testcase.wav
MP3=$abs_top_srcdir/testcase.mp3
SUMMARY="1 of 3 files failed"

rc=0
ran=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
note() { echo "SKIP  $1"; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend -
      the binary this test exists to check was not produced. That is a failure
      and not a skip."
test -f "$WAV" || die "no $WAV to work from"
test -f "$MP3" || die "no $MP3 to work from"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# nogap <outdir> <args...>: one --nogap run into <outdir>; sets `status`,
# stderr is in $WORK/err.txt. --silent keeps the error messages and the
# failure count.
nogap() {
    out=$1; shift
    rm -rf "$out"; mkdir -p "$out" || die "cannot create $out"
    "$LAME" --silent --nogapout "$out" --nogap "$@" >/dev/null 2>"$WORK/err.txt"
    status=$?
    return 0
}

# ------------------------------------------------------------------ control
for n in a b c; do
    cp "$WAV" "$WORK/$n.wav" || die "cannot copy $WAV"
done
nogap "$WORK/ok" "$WORK/a.wav" "$WORK/b.wav" "$WORK/c.wav"
if test "$status" -eq 0 && test -s "$WORK/ok/c.mp3" \
   && ! grep -F "files failed" "$WORK/err.txt" >/dev/null; then
    ok "control: three good files exit 0 and print no failure count"
else
    bad "control: three good files exited $status:
$(cat "$WORK/err.txt")"
fi

# ------------------------------------------------- read error in the middle
#
# The middle MP3 has its middle third overwritten with zero bytes, so its
# decode starts normally and fails part-way.
if "$LAME" --quiet --decode "$MP3" "$WORK/probe.wav" 2>/dev/null; then
    for n in a c; do
        cp "$MP3" "$WORK/$n.mp3" || die "cannot copy $MP3"
    done
    cp "$MP3" "$WORK/b.mp3" || die "cannot copy $MP3"
    size=$(wc -c < "$WORK/b.mp3" | tr -d ' ')
    third=$((size / 3))
    dd if=/dev/zero of="$WORK/b.mp3" bs=1 seek="$third" count="$third" \
       conv=notrunc >/dev/null 2>&1 || die "cannot damage $WORK/b.mp3"
    cmp -s "$MP3" "$WORK/b.mp3" && die "the damage to $WORK/b.mp3 changed nothing"

    nogap "$WORK/read" --mp3input "$WORK/a.mp3" "$WORK/b.mp3" "$WORK/c.mp3"
    ran=$((ran + 1))
    if test "$status" -ne 0; then
        ok "a read error in the middle file exits non-zero ($status)"
    else
        bad "a read error in the middle file exits 0"
    fi
    if test -s "$WORK/read/c.mp3"; then
        ok "a read error in the middle file: the last file is encoded"
    else
        bad "a read error in the middle file: the last file is not encoded"
    fi
    if grep -F "$SUMMARY" "$WORK/err.txt" >/dev/null; then
        ok "a read error in the middle file prints \"$SUMMARY\""
    else
        bad "a read error in the middle file does not print \"$SUMMARY\":
$(cat "$WORK/err.txt")"
    fi
else
    note "this build cannot decode mp3 input, so there is no read error to
      cause. The read error arm does not run."
fi

# ---------------------------------------- output failure in the middle file
if test -c /dev/full && ln -s /dev/full "$WORK/link" 2>/dev/null; then
    rm -f "$WORK/link"
    rm -rf "$WORK/full"; mkdir -p "$WORK/full" || die "cannot create $WORK/full"
    ln -s /dev/full "$WORK/full/b.mp3" || die "cannot link $WORK/full/b.mp3"
    "$LAME" --silent --nogapout "$WORK/full" --nogap \
        "$WORK/a.wav" "$WORK/b.wav" "$WORK/c.wav" >/dev/null 2>"$WORK/err.txt"
    status=$?
    ran=$((ran + 1))
    if test "$status" -ne 0; then
        ok "a write error in the middle file exits non-zero ($status)"
    else
        bad "a write error in the middle file exits 0"
    fi
    if test -s "$WORK/full/a.mp3" && ! test -e "$WORK/full/c.mp3"; then
        ok "a write error in the middle file stops the run before the last file"
    else
        bad "a write error in the middle file does not stop the run:
$(ls -l "$WORK/full")"
    fi
    if grep -F "$SUMMARY" "$WORK/err.txt" >/dev/null; then
        ok "a write error in the middle file prints \"$SUMMARY\""
    else
        bad "a write error in the middle file does not print \"$SUMMARY\":
$(cat "$WORK/err.txt")"
    fi
else
    note "no /dev/full or no symbolic links here, so there is no output that
      fails to write. The write error arm does not run."
fi

# --------------------------------------------- encoder error in the middle
#
# A 44.1 kHz stereo IEEE float WAV whose samples are all NaN, 4096 frames.
nan=$WORK/nan.wav
printf 'RIFF\044\200\000\000WAVEfmt \020\000\000\000\003\000\002\000' > "$nan"
printf '\104\254\000\000\040\142\005\000\010\000\040\000data\000\200\000\000' >> "$nan"
printf '\000\000\300\177\000\000\300\177' > "$WORK/nan8"
i=0
while test $i -lt 12; do
    cat "$WORK/nan8" "$WORK/nan8" > "$WORK/nan16" && mv "$WORK/nan16" "$WORK/nan8"
    i=$((i + 1))
done
cat "$WORK/nan8" >> "$nan"
test "$(wc -c < "$nan" | tr -d ' ')" -eq 32812 || die "the NaN WAV has the wrong size"
cp "$nan" "$WORK/b.wav" || die "cannot copy $nan"
nogap "$WORK/enc" "$WORK/a.wav" "$WORK/b.wav" "$WORK/c.wav"
ran=$((ran + 1))
if test "$status" -ne 0; then
    ok "an encoder error in the middle file exits non-zero ($status)"
else
    bad "an encoder error in the middle file exits 0"
fi
if test -s "$WORK/enc/a.mp3" && ! test -e "$WORK/enc/c.mp3"; then
    ok "an encoder error in the middle file stops the run before the last file"
else
    bad "an encoder error in the middle file does not stop the run:
$(ls -l "$WORK/enc")
$(cat "$WORK/err.txt")"
fi
if grep -F "$SUMMARY" "$WORK/err.txt" >/dev/null; then
    ok "an encoder error in the middle file prints \"$SUMMARY\""
else
    bad "an encoder error in the middle file does not print \"$SUMMARY\":
$(cat "$WORK/err.txt")"
fi

test "$ran" -gt 0 || die "no arm ran"
exit $rc
