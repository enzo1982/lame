#!/bin/sh
#
# setup-quality-corpus.sh - lay out the reference audio the quality harness
# scores against.
#
# The corpus is the EBU SQAM CD (Tech 3253), which the EBU publishes as one
# zip of FLAC tracks at https://qc.ebu.io/testmaterials/523/. The audio is not
# part of this distribution: its licence permits testing and evaluation but
# does not clearly permit redistribution, so it is fetched by hand once and
# pointed at from here.
#
# The tracks are numbered, not named. The default selection is the one worth
# scoring an encoder against: single instruments that isolate one
# psychoacoustic behaviour each, weighted towards the transient and
# high-frequency cases that drive block switching, plus speech and a few real
# musical excerpts. Pass -a for all 70.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog -z ZIP [-d DIR] [-a] [-t LIST] [-r RATE] [-h]

  -z ZIP     The SQAM zip, as downloaded from
             https://qc.ebu.io/testmaterials/523/ (TECH3253_SQAM_FLAC.zip).
  -d DIR     Where to build the corpus. (default: ./quality-corpus)
  -a         Decode all 70 tracks instead of the default selection.
  -t LIST    Decode these track numbers instead, comma-separated, e.g. "27,35".
  -r RATE    Sample rate of the decoded reference, in Hz. (default: 48000)
             The scorer resamples anything else internally; matching it here
             keeps that out of the measurement.
  -h         Show this help and exit.

The result is DIR/ref/NN.wav plus a tracks.txt naming what each one is.
EOF
}

# Track number|what it is|why it is in the default selection
#
# From the Tech 3253 booklet. The "why" is what the track exercises in an
# encoder, and is what the default selection is chosen on.
tracks() {
	cat <<'EOF'
07|Frere Jacques, electronic tune|synthetic tune, no masking to hide behind
10|Violoncello|sustained low strings
15|Bassoon|low woodwind
21|Trumpet|brass, strong harmonics
23|Horn|brass, soft attack
25|Harp|plucked strings, decaying transients
26|Claves|sharp transient, near-silence around it
27|Castanets|the pre-echo case: dense sharp attacks
31|Cymbal|broadband high frequency, long decay
32|Triangle|high frequency, sparse transients
35|Glockenspiel|high frequency transients plus tonal decay
40|Harpsichord|dense transients, temporal smearing
44|Soprano|solo voice, high register
47|Bass|solo voice, low register
48|Quartet|multiple voices at once
49|Female speech, English|speech, sparse spectrum
50|Male speech, English|speech, low pitch
60|Orchestra, Strauss|dense orchestral
66|Wind ensemble, Mozart|mid-band ensemble
69|Pop music|modern production, heavily compressed
EOF
}

# --- option parsing ---------------------------------------------------------

zip=
dir=./quality-corpus
all=no
want=
rate=48000

while getopts "z:d:at:r:h" opt; do
	case "$opt" in
		z)
			zip=$OPTARG
			;;
		d)
			dir=$OPTARG
			;;
		a)
			all=yes
			;;
		t)
			want=$OPTARG
			;;
		r)
			rate=$OPTARG
			;;
		h)
			usage
			exit 0
			;;
		*)
			usage >&2
			exit 2
			;;
	esac
done

# --- prerequisites ----------------------------------------------------------

missing=no
for tool in unzip ffmpeg; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "$prog: $tool not found." >&2
		missing=yes
	fi
done
if [ "$missing" = yes ]; then
	echo "$prog: ffmpeg decodes the FLAC tracks to the WAV the harness reads;" >&2
	echo "$prog: unzip unpacks the CD image." >&2
	echo "$prog: Debian/Ubuntu: apt install ffmpeg unzip" >&2
	echo "$prog: FreeBSD: pkg install ffmpeg" >&2
	echo "$prog: MSYS2: pacman -S mingw-w64-ucrt-x86_64-ffmpeg unzip" >&2
	exit 1
fi

if [ -z "$zip" ]; then
	echo "$prog: -z ZIP is required." >&2
	echo "$prog: download TECH3253_SQAM_FLAC.zip from https://qc.ebu.io/testmaterials/523/" >&2
	exit 2
fi
if [ ! -r "$zip" ]; then
	echo "$prog: cannot read '$zip'." >&2
	exit 1
fi

# --- selection --------------------------------------------------------------

if [ -n "$want" ]; then
	sel=$(printf '%s' "$want" | tr ',' ' ')
elif [ "$all" = yes ]; then
	sel=$(seq 1 70)
else
	sel=$(tracks | cut -d'|' -f1)
fi

# --- build ------------------------------------------------------------------

mkdir -p "$dir/ref" || {
	echo "$prog: cannot create '$dir/ref'." >&2
	exit 1
}
dir=$(cd "$dir" && pwd)

work=$(mktemp -d 2>/dev/null || mktemp -d -t lameqc)
trap 'rm -rf "$work"' 0 1 2 3 15

n=0
failed=0
for t in $sel; do
	# The zip names tracks with a leading zero.
	nn=$(printf '%02d' "$t" 2>/dev/null || echo "$t")
	if ! unzip -o -q -j "$zip" "$nn.flac" -d "$work" 2>/dev/null; then
		echo "  [skip] track $nn is not in the zip" >&2
		failed=$((failed + 1))
		continue
	fi
	if ffmpeg -nostdin -loglevel error -i "$work/$nn.flac" \
		-ar "$rate" -ac 2 -c:a pcm_s16le -y "$dir/ref/$nn.wav" 2>/dev/null; then
		n=$((n + 1))
	else
		echo "  [fail] track $nn did not decode" >&2
		failed=$((failed + 1))
	fi
	rm -f "$work/$nn.flac"
done

# What each track is, so that a per-track score means something to read.
{
	echo "# SQAM (EBU Tech 3253) tracks in this corpus"
	echo "# track|content|what it exercises"
	for t in $sel; do
		nn=$(printf '%02d' "$t" 2>/dev/null || echo "$t")
		if [ ! -f "$dir/ref/$nn.wav" ]; then
			continue
		fi
		line=$(tracks | grep "^$nn|" || true)
		if [ -n "$line" ]; then
			echo "$line"
		else
			echo "$nn|(see the booklet)|"
		fi
	done
} > "$dir/tracks.txt"

echo "Decoded $n reference track(s) into: $dir/ref"
echo "Track list: $dir/tracks.txt"
if [ "$failed" -gt 0 ]; then
	echo "$prog: $failed track(s) could not be prepared." >&2
	exit 1
fi
echo
echo "To score two builds against it:"
echo "  quality-compare.py -o OLD_MATRIX -n NEW_MATRIX -c CELL -d $dir"
