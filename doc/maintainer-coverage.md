# Code coverage harness {#maintainer_coverage}

A maintainer tool. You do not need it to build or use LAME.

The harness measures which source lines the test material runs. It helps with
two questions that look like one:

1. **Which build configurations are worth testing?** Because of conditional
   compilation, a line can be missing from the binary. No command line can run
   `#ifdef HAVE_MPG123` code in a build configured with `--disable-decoder`. So
   the configuration is part of the coverage, not a separate question.
2. **Which command lines are worth running?** Of the lines that a configuration
   compiles, which lines does a given command line run? And which command lines
   add coverage that no other command line already gives?

The second question is the reason to automate this. Sanitizer runs
(AddressSanitizer, UndefinedBehaviorSanitizer) only report problems in code that
runs, and they are too slow to run every combination of options. So the harness
produces a short, ranked list of command lines to run under the sanitizers.

## Prerequisites

- **gcc.** The coverage data is gcov data, which lcov reads. Clang writes a
  different profile format that lcov cannot read. A coverage run with clang
  would need llvm-cov and a different reader.
- **lcov** (1.x or 2.x) and **genhtml** for the HTML report.
- **python3** for the input generator and the analysis.
- Optional, each needed for some cells: **libmpg123**, **libsndfile**,
  **cmocka** (unit tests), **GTK 4** (>= 4.10, for the mp3x analyzer frontend,
  found with pkg-config).

## Running it

```sh
# 1. generate instrumented builds, one per configuration cell
sh maintainer/gen-coverage-matrix.sh -d /tmp/cov -s "$PWD"
sh /tmp/cov/build-all.sh

# 2. run the workload against each built cell
sh maintainer/coverage-run.sh -c /tmp/cov/full
sh maintainer/coverage-run.sh -c /tmp/cov/nodecoder
# ... and so on for the cells of interest

# 3. analyze
python3 maintainer/coverage-report.py \
        /tmp/cov/*/coverage-out -o /tmp/cov/report -r "$PWD"
```

With `coverage-run.sh -m DIR`, the runner also decodes and re-encodes every MP3
in `DIR`. Real files have tag layouts, bitrate changes and truncations that
generated input does not have. The runner only reads the directory and writes
nothing into it.

## What it runs

The harness uses two sources, and keeps them separate on purpose:

- **`test/*.op`**: the existing option lists, with one encoder option line
  each, already selected to cover the encoder settings. The runner reads them
  directly, so they are maintained in one place only. Their exit status is not
  checked. They are older than this harness, and some builds reject some lines.
  This is fine: a rejected command line still covers the code that rejects it.
- **`maintainer/coverage-workload.txt`**: everything that the `.op` format
  cannot express, because an `.op` line always encodes one fixed WAV file:
  decoding, stdin and stdout, tagging, other input formats, the usage and
  argument error paths, and command lines that *must* fail.

The last group is more important than it looks. The code that rejects bad input
(a truncated header, an inconsistent format chunk, an album art file that cannot
be read) runs only in a command line that exits with a non-zero status. A
harness that counts a non-zero exit as its own failure misses exactly the error
handling that a sanitizer run should test most. So each entry states the
expected result, and the harness reports a mismatch in either direction.

`maintainer/coverage-mkinputs.py` generates the input files: mono and stereo
PCM, 32-bit float, AIFF, raw PCM without a header, `WAVE_FORMAT_EXTENSIBLE`
both valid and with inconsistent fields, and headers that end at each point
where the next field read fails. Each file exists to run one specific branch.

## Reading the output

- `summary.txt`: the totals, and the **contribution of each cell**: the lines
  that this configuration runs and no other configuration runs. A cell without
  such lines does not need its own sanitizer run, even if its configure line
  looks very different.
- `cover-set.txt`: the command lines, sorted by how much *new* coverage each one
  adds, with a running total. Run this set under the sanitizers. The column with
  the added coverage shows where to stop.
- `uncovered.txt`: the lines that some cell compiles but no command line runs,
  grouped by file. Each is either a gap that a new workload entry could close,
  or code that cannot run at all. Both are worth knowing.

One limit: a line inside a preprocessor conditional that **no** cell enables
has no gcov record anywhere, so this report cannot show it. The report lists a
whole file that is missing, but not a missing region inside a compiled file.
Only more cells can close this gap.

## Why the cells differ from the build matrix

`maintainer/gen-build-matrix.sh` checks that every configuration still builds.
So it has cells that only change flags or linking (static, library only,
hardening off). They compile the same source, so here they would only cost
build time.

This matrix finds which lines can run at all. So its cells are the switches of
conditional compilation: the decoder, the file I/O backend, the analyzer hooks,
the experimental optimizations, the IEEE754 fast path, and the frontends that
are not built by default. Several of these have no cell in the build matrix, and
nothing else compiles them.
