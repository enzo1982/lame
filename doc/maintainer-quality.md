# Perceptual-quality comparison {#maintainer_quality}

Most changes must not change the encoded bitstream at all. For them, comparing
the two files is the whole test. Some changes are *meant* to change it: a change
to the psychoacoustic model, a quantization fix, a change to a preset. For these,
the bitstream comparison only shows that something changed. It does not show
whether the result sounds better or worse.

Two scripts measure this:

- `maintainer/setup-quality-corpus.sh`: prepares the reference audio, once.
- `maintainer/quality-compare.py`: encodes this audio with a baseline build and a
  candidate build, and compares both with the original.

## What the score is, and what it is not

The scorer implements PEAQ (ITU-R BS.1387). It returns an Objective Difference
Grade (ODG) for each track:

| ODG | Meaning                      |
|-----|------------------------------|
|  0  | imperceptible                |
| -1  | perceptible, not annoying    |
| -2  | slightly annoying            |
| -3  | annoying                     |
| -4  | very annoying                |

**Only the difference between the two builds means something here.** No free
PEAQ implementation meets the ITU conformance requirements, and the one used
here does not either. So an absolute ODG from it does not measure quality. But
the same implementation, scoring the same audio twice, compares the two encoders
fairly, and that is all that this harness needs.

PEAQ is also not a listening test. It is a filter: it finds the tracks that are
worth listening to, in a corpus much larger than anyone would listen to.

## The corpus

The reference audio is the EBU SQAM CD (Tech 3253). The EBU publishes it as one
zip file of FLAC tracks at <https://qc.ebu.io/testmaterials/523/>.

**The audio is not part of this distribution.** Its license allows testing and
evaluation, but does not clearly allow redistribution. So you download it by
hand once, and give the script its location.

The tracks have numbers, not names, and only some of them are useful for scoring
an encoder. The default selection is 20 of the 70 tracks: single instruments
that each show one psychoacoustic behavior, with more weight on the transient
and high-frequency cases that cause block switching (castanets, claves,
triangle, glockenspiel, cymbal), and also speech, solo voices, and a few real
music excerpts. `setup-quality-corpus.sh` writes a `tracks.txt` that says what
each track number is and why it was selected.

### What SQAM does not cover, and what to use instead

SQAM consists of **isolated sources** on purpose: one instrument at a time,
recorded cleanly, with very little that could mask anything. This makes it good
at finding where an encoder fails, because no artifact is hidden. But it also
makes it different from most of what LAME encodes. Dense commercial music at a
low bitrate is the opposite case: many sources at the same time, a lot of
masking, and a psychoacoustic model that makes very different decisions. A
change can be neutral on one of these and clearly not neutral on the other. So
**a quality result is only valid for the corpus it was measured on**. Name the
corpus whenever you report a number.

The second corpus used here is the sample set of the HydrogenAudio community's
public multiformat listening test at 64 kbps from 2011, published at
<https://listening-tests.hydrogenaudio.org/igorc/>. It has thirty excerpts of
real commercial music, 8 to 25 seconds each, 44.1 kHz stereo. Unlike most real
music, it can be used here, because **it includes the uncompressed FLAC
originals**. So there is a real reference to score against, not the lossy output
of another encoder. The archive also contains the codec results of that test at
64 kbps. These are the output of other encoders. Never use them as a reference.

How the two compare:

| | EBU SQAM | HydrogenAudio multiformat |
|---|---|---|
| content | single instruments, voices, speech, test signals | real commercial music, all of it dense |
| masking | very little: artifacts are exposed | a lot: the normal case |
| shows | *where does the encoder fail?* | *would a listener notice?* |
| useful for bitrates | the whole range | low, where the reports come from |
| license | testing and evaluation, redistribution unclear | **none stated at all** |

**The setup script does not download this set.** It downloads SQAM, because the
SQAM license at least allows evaluation. The HydrogenAudio page states no
license, no terms and no source for the audio. Use it only for local
evaluation: do not redistribute it, do not commit it, and do not publish audio
made from it. You can report summary scores computed from it, and these are the
only results that the harness produces.

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

The result is `DIR/ref/NN.wav` and `DIR/tracks.txt`.

```
sh maintainer/setup-quality-corpus.sh -z ~/Downloads/TECH3253_SQAM_FLAC.zip \
   -d ~/quality-corpus
```

While you work on a change, score only the two hardest cases. This takes
minutes instead of an hour:

```
sh maintainer/setup-quality-corpus.sh -z ~/Downloads/TECH3253_SQAM_FLAC.zip \
   -d ~/quality-corpus-fast -t 27,35
```

The reference is decoded to 48 kHz because the scorer resamples any other rate
internally. With 48 kHz from the start, the resampling does not affect the
measurement.

## Comparison usage

Like the speed comparison (see @ref maintainer_perf), this script compares one
cell of a baseline build matrix with the same cell of a candidate matrix. So the
compiler and the configure options are the same on both sides.

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

`-e` takes all encoder options as one value, so put more than one option into
one quoted value. Every LAME option starts with a dash, so the script attaches
the value to the flag before it parses the command line. A value after a space
works as well as an attached value:

```
python3 maintainer/quality-compare.py -o ../base-matrix -n ../cand-matrix \
        -d ~/quality-corpus -e "-V0 -m j"
```

`-f` scores a whole set of options instead of one setting, one line at a time.
The files in `test/` use exactly this format, and you can pass them directly:

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
| `test/short*.op`   | a short subset of each, for quick runs              |

A full `.op` file on the full corpus runs overnight. It needs
*tracks &times; option lines &times; 2 builds* encodes, and each encode is
followed by a decode and two PEAQ passes. While you work, use a `short*.op` file
and a corpus with fewer tracks (`-t`). Run the full set once before you submit
the change.

## Prerequisites

**ffmpeg** decodes the FLAC references, and decodes the encoded MP3 files back
to WAV. **unzip** unpacks the corpus. Both have packages on every system:

```
apt install ffmpeg unzip            # Debian/Ubuntu
pkg install ffmpeg unzip            # FreeBSD
```

**GstPEAQ** is the scorer. It has no package on any system, so build it from
source:

```
apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-base gstreamer1.0-tools gtk-doc-tools
git clone https://github.com/HSU-ANT/gstpeaq && cd gstpeaq
touch ChangeLog && autoreconf -fi && ./configure --disable-gtk-doc
make -C src && sudo make -C src install && sudo ldconfig
```

Three steps in these commands are required:

- Install `gtk-doc-tools` **before** `autoreconf`. Otherwise `configure` fails
  with a syntax error at an unexpanded `GTK_DOC_CHECK` macro.
- `touch ChangeLog` is needed because automake in GNU mode does not generate a
  `Makefile.in` without this file.
- Use `make -C src`, not `make` at the top. `doc/` needs a generated man page
  and does not build. Only `src/` is needed.

The plugin is installed in `/usr/local/lib/gstreamer-1.0`. This directory is not
in the default search path of GStreamer, so the element fails when it is used,
not when it is installed. `quality-compare.py` adds this directory to
`GST_PLUGIN_PATH` itself.

`quality-compare.py` checks all of this before it starts, and prints the
commands above if the scorer is missing. It never installs anything.

## An example: what a native build does to the audio

`--enable-native` is worth scoring, because it changes the output without
meaning to. It adds `-march=native`. On a machine with FMA, this lets the
compiler combine a multiply and an add into one instruction that rounds once
instead of twice. Then the arithmetic in the psychoacoustic model gives slightly
different results, and the encoder makes different decisions from there on. If
you encode four minutes of music at `-V2` with each build, the two files differ
in almost every byte, and do not even have the same length.

The speed comparison (see @ref maintainer_perf) reports this and fails, because
it cannot tell an intended output change from an accidental one. But that is the
wrong question here. For a native build, the question is not whether the bits
changed (they did), but whether any of this is audible:

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

Nothing changed. The largest difference on any of the twenty tracks is 0.006, in
both directions, on a scale where 1.0 is the step from "imperceptible" to
"perceptible, not annoying". The bitstream differs almost everywhere, and the
audio is the same. This is why both harnesses exist, and why a bitstream
comparison must never be read as a quality result.

The absolute column says something about the corpus, not about the builds. The
harpsichord (track 40) has by far the worst score, -0.461. Dense transients that
smear in time are the hardest case here for the encoder, and the selection
contains such cases on purpose. A track with a score near zero does not prove
that the encoder is perfect on it. It only means that this scorer finds nothing
there.

Tracks 23, 35, 49 and 50 have positive scores. An ODG cannot be above 0, because
the scale ends at "imperceptible". These values show that the implementation is
not conformant, in the one run that this page shows. They are one more reason to
read only the delta column, and to distrust every absolute number from this
tool.

## Reading the result

The report lists every track, with the baseline ODG, the candidate ODG and the
difference. A negative difference means that the candidate sounds worse. The
mean follows the lines of the tracks, and a summary line says whether any track
exceeded the tolerance.

**Read the numbers of each track, not the mean.** A regression in one hard case
disappears in the mean of twenty tracks, but this one case is the result. The
run above is a clean result because every single line is zero, not because the
mean is zero.

The harness has some noise of its own, so there is a tolerance. Do not trust the
default. Calibrate it: run the comparison with the *same* matrix on both sides,
and look at the report. Two identical builds should give `+0.000` everywhere.
Any difference between them is the noise floor, and a difference below it means
nothing.

Some differences are real and still not regressions. A change that replaces one
artifact with another can score worse at one bitrate and better at another, and
PEAQ cannot say which one a listener would prefer. A flagged track is a track to
listen to, not a verdict.

PEAQ returns `nan` for some inputs, for example a pure tone that is encoded
transparently. This means that the scorer does not grade this input. It is not
an encoder failure.

## Using it to validate a patch set

1. Build a matrix from the source without the patch, and one from the source
   with the patch.
2. Compare the two, with the same cell, corpus and options.
3. If the change was not meant to change the output, every difference should be
   `+0.000`. If one is not, the bitstream changed, and the change does more than
   it claims.
4. If the change *was* meant to change the output, look at every flagged track,
   at more than one bitrate, and listen to the tracks that changed.
5. Report the table of the tracks, the options and the corpus selection. A mean
   ODG alone is not a result that anyone can check.
