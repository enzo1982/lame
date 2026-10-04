# Encoding-speed comparison {#maintainer_perf}

`maintainer/perf-compare.sh` checks one thing about a change: is the encoder
faster, slower, or the same after the change?

It encodes one input file with a baseline build and a candidate build, times
both, and reports a confidence interval for the difference. It also reports
whether the two builds produced the same bitstream.

## Why two build matrices

The script does not build anything. It takes one cell from each of two matrices
that `maintainer/gen-build-matrix.sh` generated (see
@ref maintainer_build_matrix): one built from the baseline source, and one built
from the candidate source. It compares a cell with the same cell of the other
matrix, so the compiler and the configure options are the same on both sides,
and only the source change is different.

With two matrices, you can run the baseline again at any time without a rebuild.

## Speed alone is not the result

A faster encoder that writes different bits is a bug, not an optimization,
unless the change is meant to change the output. So before it times anything,
the script encodes once with each build and compares the two files. A different
bitstream is reported next to the timings, and the run fails. The script still
prints the timings, because they help to decide what to do next.

## Usage

```
sh maintainer/perf-compare.sh -o DIR -n DIR -i FILE [-c CELL] [-e ARGS] [-r N]
sh maintainer/perf-compare.sh -o DIR -n DIR -l
```

| Option    | Meaning                                                             |
|-----------|---------------------------------------------------------------------|
| `-o DIR`  | baseline build matrix                                               |
| `-n DIR`  | candidate build matrix, laid out the same way                       |
| `-i FILE` | input audio; several minutes of real music, not a short clip        |
| `-c CELL` | cell to compare, e.g. `gcc/full` (default: `gcc/full`)              |
| `-e ARGS` | encoder arguments (default: `-V2`)                                  |
| `-r N`    | hot runs, at least 10 (default: 10)                                 |
| `-l`      | list the cells both matrices have in common, and exit               |

`-c` also takes a cell name without a compiler. `-c static` works when only one
compiler has this cell. When several compilers have it, the script lists them
and does not guess.

`-e` takes **all** encoder arguments as one value, and splits it at the spaces.
So put more than one argument into one quoted value:

```
sh maintainer/perf-compare.sh -o ../base-matrix -n ../cand-matrix \
   -i ~/audio/album.wav -e "-V0 -m j"
```

### Examples

List the cells that can be compared, before you choose one:

```
sh maintainer/perf-compare.sh -o ../base-matrix -n ../cand-matrix -l
```

The default comparison: `gcc/full`, `-V2`, ten hot runs:

```
sh maintainer/perf-compare.sh -o ../base-matrix -n ../cand-matrix \
   -i ~/audio/album.wav
```

A different compiler and configuration, with CBR, and more runs:

```
sh maintainer/perf-compare.sh -o ../base-matrix -n ../cand-matrix \
   -c clang/static -i ~/audio/album.wav -e "-b 192" -r 20
```

Run it on one core. Of all things you can do, this improves the numbers most:

```
taskset -c 2 sh maintainer/perf-compare.sh ...     # Linux
cpuset -l 1  sh maintainer/perf-compare.sh ...     # FreeBSD
```

### Comparing build flags instead of a source change

The two matrices do not have to come from two different sources. To measure how
a build option changes the encoding speed, keep the source the same and change
the flags.

`--enable-native` has no cell of its own, because a binary tuned for one machine
is not meant to be distributed, and the matrix tests what is distributed. But
the script finds a cell by its `config.status`. So a cell that you build by hand
in the same `<matrix>/<compiler>/<cell>` layout is compared like any other:

```
mkdir -p ~/base-matrix/gcc/full ~/native-matrix/gcc/full

cd ~/base-matrix/gcc/full
CC=gcc ~/lame/configure --enable-dynamic-frontends && make -j16

cd ~/native-matrix/gcc/full
CC=gcc ~/lame/configure --enable-dynamic-frontends --enable-native && make -j16

taskset -c 2 sh maintainer/perf-compare.sh \
   -o ~/base-matrix -n ~/native-matrix -i ~/audio/music.wav
```

## Prerequisites

**hyperfine** runs and times the encoder. It is required.

**ministat** computes a confidence interval from the two sets of hot runs. If it
is missing, the script says so, reports the comparison of hyperfine instead,
and the run is still valid.

```
apt install hyperfine ministat                          # Debian/Ubuntu
pkg install hyperfine                                   # FreeBSD (ministat is in base)
pacman -S mingw-w64-ucrt-x86_64-hyperfine               # MSYS2 (UCRT64)
```

There is no version of this script for native Windows. A timing comparison needs
two builds that differ only in the source, and the POSIX matrix gives that.

## Reading the result

The whole report of the `--enable-native` comparison above:

```
baseline : /home/u/base-matrix/gcc/full/frontend/.libs/lame
candidate: /home/u/native-matrix/gcc/full/frontend/.libs/lame
input    : /home/u/audio/music.wav
arguments: -V2
hot runs : 10 (after 1 warmup)

baseline :    2.195 s  +/- 0.016  (CV 0.72%)
candidate:    2.029 s  +/- 0.010  (CV 0.49%)

--- ministat (baseline vs candidate, 95% CI) ---
x /tmp/tmp.eYOfnOWoPh/old.times
+ /tmp/tmp.eYOfnOWoPh/new.times
+--------------------------------------------------------------------------+
|   +  +                                                 xx                |
|++++  +++ +                                             xx  xx xx        x|
| |___AM_|                                              |____A_____|       |
+--------------------------------------------------------------------------+
    N           Min           Max        Median           Avg        Stddev
x  10     2.1801255      2.231948     2.1934367     2.1945536   0.015715418
+  10      2.014966     2.0458651      2.033675     2.0289309   0.010010465
Difference at 95.0% confidence
	-0.165623 +/- 0.0123796
	-7.54699% +/- 0.564105%
	(Student's t, pooled s = 0.0131754)

bitstream: DIFFERENT
  The two builds do not encode to the same bits. Unless this change is
  meant to alter the output, that is a bug and outranks any speedup.

measurement: CV 0.72% - trustworthy.
```

### The sign

Every number in the report is a **time**. So it moves in the opposite direction
to the speed. The difference is the candidate minus the baseline:

- **negative: the candidate is faster.** It took less time. This is usually the
  result you want, and the minus sign shows it.
- **positive: the candidate is slower.** It took more time.

Here, `-7.54699%` means that the native build encodes this file 7.5% faster than
the default build, not 7.5% slower.

`ministat` marks the baseline with `x` and the candidate with `+`, both in the
legend and in the plot. The plot shows the two sets of runs on one axis: `A` is
the average, `M` is the median when it is far enough from the average to be
drawn separately, and the bracket around them is the confidence interval. When
the two brackets do not overlap, the plot shows the difference that the numbers
below it state.

### The two "+/-" mean different things

They look the same, but they mean different things:

- `2.195 s +/- 0.016` on the line of each build is a **standard deviation**: how
  much the ten runs of *this* build varied.
- `-0.165623 +/- 0.0123796` below `Difference at 95.0% confidence` is **half the
  width of the confidence interval** of the difference between the two builds.
  It says that the true difference is between -0.178 s and -0.153 s, with 95%
  confidence.

Quote the second one. The first one is only used for the CV.

When there is no difference, ministat says so directly:
`No difference proven at 95.0% confidence`. This is a result: on this machine,
with this input, the change did not change the encoding speed measurably. It
does not mean "a little faster", and it does not mean "too few runs".

### The bitstream line, when the flags changed

The run above **fails** (exit 1) with `bitstream: DIFFERENT`, and here this is
expected, not a bug. `--enable-native` adds `-march=native`. On any machine with
FMA, this lets the compiler combine a multiply and an add into one instruction
that rounds once instead of twice. Then the arithmetic in the psychoacoustic
model gives slightly different results, and the encoder makes different
decisions later. For a file of four minutes, almost every byte after the first
50 differs, and the file length changes by a few bytes.

Each build still gives the same output every time it runs, and both files are
valid MP3 files.

The script cannot tell an expected output change from an accidental one. So it
reports the difference and fails in both cases. The person who reads the report
decides which case it is. When the flags are what changed, the bitstream line is
only information. When the source changed and the change was not meant to change
the output, the difference is a bug, and it matters more than the timings.

This script cannot tell whether a bitstream difference is *audible*. That is a
different question. A comparison of these two builds on a reference corpus (see
@ref maintainer_quality) reports no perceptual difference at all: with this
change, the bits change everywhere, but the audio does not.

### The measurement

The coefficient of variation (CV) decides whether the run means anything at all.
Check it before you look at any difference between the two builds:

| CV        | What it means                                                     |
|-----------|-------------------------------------------------------------------|
| &le; 2%   | trustworthy                                                       |
| 2% &ndash; 5% | borderline; re-run with `-r 20`                               |
| &gt; 5%   | the run is rejected and the script fails                          |

Above 5%, more runs do not help. The cause is the machine, not the number of
runs. The usual causes are frequency scaling, turbo boost, thermal throttling, a
power plan that is not the performance plan, or other programs that are running.
Try running on one core first (`taskset` or `cpuset`). This often lowers the CV
from almost 10% to well below one percent.

## Using it to validate a patch set

1. Generate and build a matrix from the source without the patch.
2. Generate and build a second matrix from the source with the patch.
3. Compare the base cell, on a quiet machine, on one core.
4. Check the bitstream line first. If it says `DIFFERENT` and the change was not
   meant to change the output, stop. In this case, the timings do not matter.
5. Repeat this for every cell that the change is specific to. For example,
   measure a change for `--disable-decoder` in `nodecoder`, not only in `full`.

Report the confidence interval, the CV and the input, not a single percentage. A
speedup on one file at one bitrate is only a speedup on one file at one bitrate.
