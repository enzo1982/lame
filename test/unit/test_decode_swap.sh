#!/bin/sh
# Checks that -x swaps the bytes of the output, and not of the input, when
# lame decodes.
#
# lame --decode reads WAV input as well as MP3. Its output is a WAV file, or
# raw PCM with -t. The man page says -x swaps the output when decoding. So
# for a 16-bit and for a float WAV input:
#   - --decode -x must write the same WAV file as --decode, since -x does
#     not touch a WAV output;
#   - --decode -t -x must write the raw output of --decode -t with each pair
#     of bytes swapped.
# The float input is the case that shows a swapped input most plainly: its
# swapped samples are not finite numbers, and lame stops with an error.
#
# The control makes the result meaningful: when encoding, -x still swaps the
# input, so the MP3 of -x differs from the one without it.

set -u

: "${abs_top_builddir:?must be set by the test harness}"
: "${abs_top_srcdir:?must be set by the test harness}"

WORK=./test_decode_swap.dir
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

# ------------------------------------------------------------------- inputs
#
# le32 <n>: writes <n> as four bytes, least significant first
le32() {
    printf "\\$(printf %03o $(($1 & 255)))\\$(printf %03o $(($1 >> 8 & 255)))"
    printf "\\$(printf %03o $(($1 >> 16 & 255)))\\$(printf %03o $(($1 >> 24 & 255)))"
}
# le16 <n>: writes <n> as two bytes, least significant first
le16() {
    printf "\\$(printf %03o $(($1 & 255)))\\$(printf %03o $(($1 >> 8 & 255)))"
}

# A float WAV, stereo at 44100 Hz: a square wave of +0.25 and -0.25, 4096
# sample frames of 8 bytes. The pattern doubles from 1 frame to 4096.
printf '\000\000\200\076\000\000\200\076\000\000\200\276\000\000\200\276' > "$WORK/pcm.bin"
for i in 1 2 3 4 5 6 7 8 9 10 11; do
    cat "$WORK/pcm.bin" "$WORK/pcm.bin" > "$WORK/pcm2.bin" && mv "$WORK/pcm2.bin" "$WORK/pcm.bin"
done
DATA=$((4096 * 8))
{
    printf 'RIFF'; le32 $((36 + DATA)); printf 'WAVEfmt '; le32 16
    le16 3; le16 2; le32 44100; le32 $((44100 * 8)); le16 8; le16 32
    printf 'data'; le32 "$DATA"
    cat "$WORK/pcm.bin"
} > "$WORK/in.f32.wav"
test "$(wc -c < "$WORK/in.f32.wav")" -eq $((44 + DATA)) || die "cannot write the float WAV input"

# run <output> <args...>; sets `status`
run() {
    out=$1; shift
    rm -f "$out"
    "$LAME" --silent "$@" "$out" >/dev/null 2>"$WORK/err.txt"
    status=$?
    return 0
}

# ------------------------------------------------------------------- arms
for arm in "s16 $SRC" "f32 $WORK/in.f32.wav"; do
    set -- $arm
    name=$1; in=$2

    run "$WORK/$name.wav" --decode "$in"
    plain=$status
    run "$WORK/$name.x.wav" --decode -x "$in"
    if test "$plain" -eq 0 && test "$status" -eq 0 && test -s "$WORK/$name.wav" \
        && cmp -s "$WORK/$name.wav" "$WORK/$name.x.wav"; then
        ok "$name: --decode -x writes the WAV file of --decode"
    else
        bad "$name: --decode exited $plain, --decode -x exited $status, or the WAV files differ"
    fi

    run "$WORK/$name.raw" --decode -t "$in"
    plain=$status
    run "$WORK/$name.x.raw" --decode -t -x "$in"
    dd if="$WORK/$name.raw" of="$WORK/$name.swab.raw" conv=swab 2>/dev/null
    if test "$plain" -eq 0 && test "$status" -eq 0 && test -s "$WORK/$name.raw" \
        && cmp -s "$WORK/$name.swab.raw" "$WORK/$name.x.raw" \
        && ! cmp -s "$WORK/$name.raw" "$WORK/$name.x.raw"; then
        ok "$name: --decode -t -x writes the raw output of --decode -t, each byte pair swapped"
    else
        bad "$name: --decode -t exited $plain, --decode -t -x exited $status, or the second
      is not the first with each byte pair swapped"
    fi
done

# control: when encoding, -x swaps the input
run "$WORK/plain.mp3" "$SRC"
plain=$status
run "$WORK/x.mp3" -x "$SRC"
if test "$plain" -eq 0 && test "$status" -eq 0 && ! cmp -s "$WORK/plain.mp3" "$WORK/x.mp3"; then
    ok "control: when encoding, -x changes the MP3"
else
    bad "control: encoding exited $plain, with -x $status, or -x did not change the MP3"
fi

exit $rc
