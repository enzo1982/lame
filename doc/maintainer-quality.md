# Perceptual-quality comparison {#maintainer_quality}

Most changes must not alter the encoded bitstream at all, and comparing the two
files is then the whole test. Some changes are *meant* to alter it: a
psychoacoustic tweak, a quantization fix, a change to a preset. For those the
bitstream check says only that something moved, not whether the result sounds
better or worse.

Two scripts answer that question:

- `maintainer/setup-quality-corpus.sh` &mdash; lay out the reference audio,
  once.
- `maintainer/quality-compare.py` &mdash; encode that audio with a baseline and
  a candidate build, and score both against the original.

## What the score is, and what it is not

The scorer implements PEAQ (ITU-R BS.1387), which returns an Objective
Difference Grade per track:

| ODG | Meaning                      |
|-----|------------------------------|
|  0  | imperceptible                |
| -1  | perceptible, not annoying    |
| -2  | slightly annoying            |
| -3  | annoying                     |
| -4  | very annoying                |

**Only the difference between the two builds is meaningful here.** No free PEAQ
implementation meets the ITU conformance requirements, the one used here
included, so an absolute ODG from it is not a measurement of quality. The same
implementation scoring the same audio twice does compare the two encoders
fairly, which is all that is asked of it.

PEAQ is also not a listening test. It is a filter: it finds the tracks worth
listening to, on a corpus far larger than anyone will sit through.

## The corpus

The reference audio is the EBU SQAM CD (Tech 3253), which the EBU publishes as
one zip of FLAC tracks at <https://qc.ebu.io/testmaterials/523/>.

**The audio is not part of this distribution.** Its licence permits testing and
evaluation but does not clearly permit redistribution, so it is downloaded by
hand once and pointed at.

The tracks are numbered rather than named, and only some are worth scoring an
encoder against. The default selection is 20 of the 70: single instruments that
isolate one psychoacoustic behaviour each, weighted towards the transient and
high-frequency cases that drive block switching (castanets, claves, triangle,
glockenspiel, cymbal), plus speech, solo voices, and a few real musical
excerpts. `setup-quality-corpus.sh` writes a `tracks.txt` recording what each
number is and why it was picked.

### Setup usage

```
sh maintainer/setup-quality-corpus.sh -z ZIP [-d DIR] [-a] [-t LIST] [-r RATE]
```

| Option    | Meaning                                                             |
|-----------|---------------------------------------------------------------------|
| `-z ZIP`  | the downloaded `TECH3253_SQAM_FLAC.zip`                             |
| `-d DIR`  | where to build the corpus (default: `./quality-corpus`)             |
| `-a`      | decode all 70 tracks instead of the default selection               |
| `-t LIST` | decode these track numbers instead, comma-separated                 |
| `-r RATE` | sample rate of the decoded reference, in Hz (default: 48000)        |

The result is `DIR/ref/NN.wav` plus `DIR/tracks.txt`.

```
sh maintainer/setup-quality-corpus.sh -z ~/Downloads/TECH3253_SQAM_FLAC.zip \
   -d ~/quality-corpus
```

Score against the two hardest cases alone while iterating on a change, which is
minutes rather than an hour:

```
sh maintainer/setup-quality-corpus.sh -z ~/Downloads/TECH3253_SQAM_FLAC.zip \
   -d ~/quality-corpus-fast -t 27,35
```

The reference is decoded to 48 kHz because the scorer resamples anything else
internally; matching it up front keeps that out of the measurement.

## Comparison usage

Like the speed comparison (see @ref maintainer_perf), this compares one cell of
a baseline build matrix against the same cell of a candidate one, so that the
compiler and the configure options stay out of the difference.

```
python3 maintainer/quality-compare.py -o DIR -n DIR -d DIR
        [-c CELL] [-e ARGS | -f FILE] [-t TOL]
```

| Option                | Meaning                                                 |
|-----------------------|---------------------------------------------------------|
| `-o`, `--old`         | baseline build matrix                                   |
| `-n`, `--new`         | candidate build matrix                                  |
| `-d`, `--corpus`      | corpus from `setup-quality-corpus.sh`                   |
| `-c`, `--cell`        | matrix cell to compare (default: `gcc/full`)            |
| `-e`, `--encoder-options` | encoder options to score at (default: `-V2`)        |
| `-f`, `--options-file`| score every option line of this file instead            |
| `-t`, `--tolerance`   | flag a track whose ODG drops by more than this (default: 0.05) |

`-e` takes all the encoder options as its single value, so more than one goes in
one quoted argument. Because every LAME option starts with a dash, the value has
to be attached to the flag rather than separated by a space &mdash; the script
accepts either form and joins them for you:

```
python3 maintainer/quality-compare.py -o ../base-matrix -n ../cand-matrix \
        -d ~/quality-corpus -e "-V0 -m j"
```

`-f` scores a whole option set instead of one setting, one line at a time. The
files under `test/` are written in exactly that format and can be passed
straight in:

```
python3 maintainer/quality-compare.py -o ../base-matrix -n ../cand-matrix \
        -d ~/quality-corpus -f test/shortVBR.op
```

| File               | Covers                                              |
|--------------------|-----------------------------------------------------|
| `test/VBR.op`      | the VBR settings, default engine                    |
| `test/VBRold.op`   | the same, through `--vbr-old`                       |
| `test/CBRABR.op`   | the CBR and ABR settings                            |
| `test/misc.op`     | the ATH, filter, block and preset options           |
| `test/nores.op`    | `--nores`                                           |
| `test/short*.op`   | a short subset of each, for iterating               |

A full `.op` file against the full corpus is an overnight run: it is
*tracks &times; option lines &times; 2 builds* encodes, each followed by a
decode and two PEAQ passes. Use a `short*.op` and a `-t`-narrowed corpus while
working, and the full product once before submitting.

## Prerequisites

**ffmpeg** decodes the FLAC references and the encoded MP3s back to WAV.
**unzip** unpacks the corpus. Both are packaged everywhere:

```
apt install ffmpeg unzip            # Debian/Ubuntu
pkg install ffmpeg unzip            # FreeBSD
```

**GstPEAQ** is the scorer, and is not packaged anywhere. Build it from source:

```
apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-base gstreamer1.0-tools gtk-doc-tools
git clone https://github.com/HSU-ANT/gstpeaq && cd gstpeaq
touch ChangeLog && autoreconf -fi && ./configure --disable-gtk-doc
make -C src && sudo make -C src install && sudo ldconfig
```

Three things about that recipe are not optional:

- `gtk-doc-tools` has to be installed **before** `autoreconf`, or `configure`
  fails with a syntax error at an unexpanded `GTK_DOC_CHECK` macro.
- `touch ChangeLog` satisfies automake's GNU strictness, which refuses to
  generate a `Makefile.in` without one.
- `make -C src`, not a top-level `make`: `doc/` wants a generated man page and
  does not build. Only `src/` is needed.

The plugin installs under `/usr/local/lib/gstreamer-1.0`, which is not on
GStreamer's default search path, and the element then fails to instantiate at
run time rather than at install time. `quality-compare.py` adds that directory
to `GST_PLUGIN_PATH` itself.

`quality-compare.py` checks for all of this up front and prints the recipe
above if the scorer is missing; it never installs anything.

## A worked example: what a native build does to the audio

`--enable-native` is worth scoring, because it is a change that alters the
output without meaning to. It adds `-march=native`, which on a machine with FMA
lets the compiler contract a multiply and an add into a single instruction that
rounds once instead of twice. The arithmetic feeding the psychoacoustic model is
then no longer bit-identical, and the encoder makes different decisions from
there on: encoding four minutes of music at `-V2` with each build produces files
that differ in nearly every byte and are not even the same length.

The speed comparison (see @ref maintainer_perf) reports exactly that and fails
on it, because it cannot tell an intended output change from an accidental one.
It is the wrong question to ask here. What one wants to know about a native
build is not whether the bits moved &mdash; they did &mdash; but whether any of
it is audible:

```
python3 maintainer/quality-compare.py -o ~/base-matrix -n ~/native-matrix \
        -d ~/quality-corpus -e "-V2"
```

```
baseline : /home/u/base-matrix/gcc/full/frontend/.libs/lame
candidate: /home/u/native-matrix/gcc/full/frontend/.libs/lame
corpus   : /home/u/quality-corpus/ref (20 tracks)
scorer   : /usr/local/bin/peaq

=== -V2 ===
  track      baseline candidate    delta
  07            0.000    0.000   +0.000
  10           -0.162   -0.162   +0.000
  15           -0.241   -0.242   -0.001
  21           -0.077   -0.077   +0.000
  23            0.019    0.019   +0.000
  25           -0.034   -0.028   +0.006
  26           -0.100   -0.100   +0.000
  27           -0.015   -0.015   +0.000
  31           -0.128   -0.128   +0.000
  32           -0.168   -0.169   -0.001
  35            0.022    0.022   +0.000
  40           -0.461   -0.461   +0.000
  44           -0.117   -0.117   +0.000
  47           -0.010   -0.009   +0.001
  48           -0.091   -0.091   +0.000
  49            0.093    0.093   +0.000
  50            0.086    0.086   +0.000
  60           -0.076   -0.076   +0.000
  66           -0.021   -0.021   +0.000
  69           -0.044   -0.044   +0.000
                                 +0.000  (mean of 20)

No track dropped by more than 0.050.
```

Nothing moved. The largest delta on any of the twenty tracks is 0.006, in both
directions, on a scale where 1.0 is the step from "imperceptible" to
"perceptible but not annoying". A bitstream that differs almost everywhere and
audio that is the same audio: that is the whole reason the two harnesses exist
side by side, and it is why a bitstream comparison must not be read as a quality
verdict.

The absolute column is worth a second look for what it says about the corpus
rather than about the builds. The harpsichord (track 40) scores worst by a
distance at -0.461 &mdash; dense transients smearing in time is the hardest
thing here for the encoder, and the selection is picked to contain such cases.
Tracks that score near zero are not evidence that the encoder is perfect on
them; they are cases where this scorer has nothing to say.

Note the positive scores on tracks 23, 35, 49 and 50. An ODG cannot exceed 0:
the scale stops at "imperceptible". Those values are the implementation being
non-conformant, out in the open, on the one run this page shows. They are
another reason to read only the delta column and to distrust any absolute number
this tool produces.

## Reading the result

Every track is reported, with the baseline ODG, the candidate ODG, and the
delta. A negative delta means the candidate sounds worse. The mean follows the
per-track lines, and a summary line says whether anything exceeded the
tolerance.

**Read the per-track numbers, not the mean.** A regression in one hard case
averages away across twenty tracks; that one case is the result. The run above
means what it says only because every individual line is at zero, not because
the mean is.

The tolerance exists because the harness has noise of its own. Calibrate it
rather than trusting the default: run the comparison with the *same* matrix on
both sides and see what it reports. Two identical builds should come back at
`+0.000` everywhere, and anything they do not agree on is the floor below which
a delta means nothing.

Some deltas are real and still not regressions. A change that trades one
artefact for another can score worse at one bitrate and better at another, and
PEAQ has no opinion about which a listener would rather have. A flagged track is
a track to listen to, not a verdict.

PEAQ returns `nan` on degenerate input &mdash; a pure tone encoded
transparently, for instance. That is the scorer declining to grade, not a
failure of the encoder.

## Using it to validate a patchset

1. Build a matrix from the unpatched source, and one from the patched source.
2. Score the two against each other with the same cell, corpus, and options.
3. If the change was not meant to alter the output, every delta should be
   `+0.000`. One that is not means the bitstream moved and the change did more
   than it claims.
4. If the change *was* meant to alter the output, look at every flagged track,
   at more than one bitrate, and listen to the ones that moved.
5. Report the per-track table, the options, and the corpus selection. A mean
   ODG on its own is not a result anyone can check.
