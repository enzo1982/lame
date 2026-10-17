#!/bin/sh
# Checks what lame does with an MP3 that holds fewer samples than the encoder
# delay and padding in its LAME tag.
#
# The input is an MP3 of 1000 sample frames, cut after its first audio frame.
# The tag still gives the delay and padding of the whole file (576 + 728), and
# the one frame left holds 1152 samples. Decoding and transcoding such a file
# give no samples. lame finishes the output, prints an error and exits
# non-zero. The control is the whole file, which decodes to its 1000 frames
# and exits 0.

set -u

: "${abs_top_builddir:?must be set by the test harness}"

WORK=./test_short_mp3.dir
FRAMES=1000
# At 48 kHz and 128 kbit/s every MP3 frame has 384 bytes.
MP3_FRAME_BYTES=384

rc=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; rc=1; }
die()  { echo "FAIL  $1"; exit 1; }
skip() { echo "SKIP  $1"; exit 77; }

LAME=$abs_top_builddir/frontend/lame
test -x "$LAME" || LAME=$abs_top_builddir/frontend/lame.exe
test -x "$LAME" || die "no built lame frontend at $abs_top_builddir/frontend"

trap 'rm -rf "$WORK"' EXIT HUP INT TERM
rm -rf "$WORK"; mkdir -p "$WORK" || die "cannot create $WORK"

# byte N: one byte of value N
byte() {
    printf "\\$(printf %03o "$1")"
}
le32() {
    byte $(($1 & 255)); byte $(($1 >> 8 & 255)); byte $(($1 >> 16 & 255)); byte $(($1 >> 24 & 255))
}
le16() {
    byte $(($1 & 255)); byte $(($1 >> 8 & 255))
}
# wav_data_size FILE: the data size field of a 44-byte WAV header, read byte by
# byte so that the result does not depend on the byte order of the host
wav_data_size() {
    set -- $(od -A n -t u1 -j 40 -N 4 "$1")
    test $# -eq 4 || { echo none; return; }
    echo $(($1 + 256 * $2 + 65536 * $3 + 16777216 * $4))
}

# 1000 stereo sample frames at 48000 Hz, 16 bit
printf '\000\020\000\360' > "$WORK/pcm.bin"
for i in 1 2 3 4 5 6 7 8 9 10; do
    cat "$WORK/pcm.bin" "$WORK/pcm.bin" > "$WORK/pcm2.bin" && mv "$WORK/pcm2.bin" "$WORK/pcm.bin"
done
DATA=$((FRAMES * 4))
{
    printf 'RIFF'; le32 $((36 + DATA)); printf 'WAVEfmt '; le32 16
    le16 1; le16 2; le32 48000; le32 $((48000 * 4)); le16 4; le16 16
    printf 'data'; le32 "$DATA"
    head -c "$DATA" "$WORK/pcm.bin"
} > "$WORK/in.wav"
test "$(wc -c < "$WORK/in.wav" | tr -d ' ')" -eq $((44 + DATA)) || die "cannot write the WAV input"

"$LAME" --silent -b 128 "$WORK/in.wav" "$WORK/full.mp3" || die "cannot encode the input"
test "$(wc -c < "$WORK/full.mp3" | tr -d ' ')" -eq $((3 * MP3_FRAME_BYTES)) \
    || die "full.mp3 is not the tag frame and two audio frames"

if ! "$LAME" --silent --decode "$WORK/full.mp3" "$WORK/probe.wav" 2>/dev/null; then
    skip "this build cannot decode MP3 input"
fi

# The tag frame and the first audio frame.
head -c $((2 * MP3_FRAME_BYTES)) "$WORK/full.mp3" > "$WORK/short.mp3"
test "$(wc -c < "$WORK/short.mp3" | tr -d ' ')" -eq $((2 * MP3_FRAME_BYTES)) || die "cannot cut the input"

"$LAME" --silent --decode "$WORK/full.mp3" "$WORK/full.wav" 2> "$WORK/full.err"
st=$?
size=$(wav_data_size "$WORK/full.wav")
if test "$st" -eq 0 && test "$size" = $((FRAMES * 4)) && test ! -s "$WORK/full.err"; then
    ok "control: the whole file decodes to its $FRAMES sample frames"
else
    bad "control: the whole file: exit $st, WAV data size $size, not $((FRAMES * 4))"
fi

"$LAME" --silent --decode "$WORK/short.mp3" "$WORK/short.wav" 2> "$WORK/short.err"
st=$?
size=$(wav_data_size "$WORK/short.wav")
if test "$st" -ne 0 && test "$size" = 0 && test "$(wc -c < "$WORK/short.wav" | tr -d ' ')" -eq 44 \
    && grep -q 'LAME tag' "$WORK/short.err"; then
    ok "--decode of the short file writes a WAV of no samples and fails"
else
    bad "--decode of the short file: exit $st, WAV data size $size, message: $(head -c 100 "$WORK/short.err")"
fi

"$LAME" --silent --mp3input -b 128 "$WORK/short.mp3" "$WORK/short.out.mp3" 2> "$WORK/short.out.err"
st=$?
if test "$st" -ne 0 && test -s "$WORK/short.out.mp3" && grep -q 'LAME tag' "$WORK/short.out.err" \
    && "$LAME" --silent --decode "$WORK/short.out.mp3" "$WORK/short.out.wav" 2> "$WORK/short.out.wav.err" \
    && test "$(wav_data_size "$WORK/short.out.wav")" = 0; then
    ok "--mp3input of the short file writes an MP3 of no samples and fails"
else
    bad "--mp3input of the short file: exit $st, output $(wc -c < "$WORK/short.out.mp3" | tr -d ' ') bytes"
fi

exit $rc
