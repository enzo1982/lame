# Build-configuration test matrix

Two generator scripts build LAME in many different configurations at once, so
that a change can be checked against every configuration it might affect before
it is committed.

- `maintainer/gen-build-matrix.sh` &mdash; the autotools (POSIX) build.
- `maintainer/gen-build-matrix.ps1` &mdash; the native Windows builds
  (nmake with `Makefile.MSVC`, and MSBuild with the `vc_solution` projects).

Each generator creates, under a *master directory*, one self-contained
out-of-tree build per configuration, plus a driver script that builds them all,
never stops on the first failure, writes every build log to a file, and prints a
summary of the failed builds together with the location of their logs.

## Why a "star" and not a full product

The interesting build options are largely independent conditional-compilation
surfaces (decoder on/off, file I/O backend, analyzer hooks, shared vs. static,
frontends on/off). Testing their full cartesian product would be dozens of
builds with little extra signal. Instead the matrix is a *star*: one
full-featured base build, plus one build per axis that flips exactly that one
axis away from the base. A regression in any single surface still shows up,
while the build count stays small enough to run routinely.

## The configuration cells (POSIX / autotools)

| Cell         | configure arguments                               | Flips (vs. base)                     |
|--------------|---------------------------------------------------|--------------------------------------|
| `full`       | `--enable-dynamic-frontends`                      | base: shared lib + dynamic frontends |
| `nodecoder`  | `--enable-dynamic-frontends --disable-decoder`    | mpg123 decoder off                   |
| `sndfile`    | `--enable-dynamic-frontends --with-fileio=sndfile`| file I/O via libsndfile              |
| `noanalyzer` | `--enable-dynamic-frontends --disable-analyzer-hooks` | analyzer hooks off (NOANALYSIS)  |
| `static`     | `--disable-shared --enable-static`                | static-only, no dynamic frontends    |
| `libonly`    | `--disable-frontend`                              | library only, no frontends           |
| `staticfe`   | `--disable-dynamic-frontends`                     | static (non-default) frontend linking|
| `nohardening`| `--enable-dynamic-frontends --disable-hardening`  | security hardening off               |
| `expopt`     | `--enable-dynamic-frontends --enable-expopt=norm` | experimental optimizations on        |

Dynamic frontend linking is the default, and the base build spells it out with
`--enable-dynamic-frontends` on purpose: linking the frontends against the shared
library means a symbol that is used but missing from the export list fails the
link here rather than silently slipping through. The `staticfe` cell covers the
opposite, `--disable-dynamic-frontends`, path.

Security hardening flags are detected and enabled by default, so the base cell
already exercises them; the `nohardening` cell covers the `--disable-hardening`
path. The `expopt` cell exercises the extra `--enable-expopt=norm` optimization
flags. Both option sets probe the compiler and keep only what it accepts, so the
exact flags differ between GCC and Clang &mdash; the matrix confirms each cell
still builds and links on every compiler. (`--enable-native` is deliberately not
a cell: native-tuned binaries are not meant to be redistributed, and the flag is
a machine-local convenience rather than something to regression-test.)

Each detected compiler gets the whole star, so the actual build count is
*compilers &times; cells*.

## The driver

The generated `build-all.sh` (POSIX) / `build-all.ps1` (Windows):

- builds every cell in turn and **does not stop when one fails**;
- writes each build's output to `logs/<compiler>-<cell>.log`
  (Windows: `logs/<toolchain>-<cell>.log`);
- prints a final summary listing each failed build next to its log file;
- exits non-zero if any build failed, zero if all passed.

## POSIX usage

```
sh maintainer/gen-build-matrix.sh [-d DIR] [-s SRCDIR] [-c LIST] [-j N]
sh DIR/build-all.sh
```

| Option      | Meaning                                                              |
|-------------|---------------------------------------------------------------------|
| `-d DIR`    | master directory to create (default `./build-matrix`)               |
| `-s SRCDIR` | source dir containing `configure` (default: parent of the script)   |
| `-c LIST`   | comma-separated compiler list instead of autodetection              |
| `-j N`      | parallel make jobs per build (default: detected CPU count)          |

Compiler autodetection tries `gcc clang cc` **on `PATH` only** (no filesystem
search), then de-duplicates by the compiler's `--version` banner, so an alias
like `cc` &rarr; `gcc` is built only once. Pass `-c` to override, e.g.
`-c gcc,clang` or `-c /opt/gcc-15/bin/gcc`.

A cell whose optional dependency is missing is **skipped** (not generated), with
a note in `matrix-info.txt`, so the driver's failure list stays meaningful:

- every cell except `nodecoder` needs **libmpg123** (the decoder is on by
  default and `configure` errors out if it is absent);
- the `sndfile` cell additionally needs **libsndfile**.

## Windows usage

```
pwsh maintainer/gen-build-matrix.ps1 [-Dir DIR] [-SrcDir DIR] `
     [-VsPath DIR] [-Mpg123Dir DIR] [-Config LIST] [-Arch LIST]
pwsh DIR\build-all.ps1
```

| Option        | Meaning                                                           |
|---------------|-------------------------------------------------------------------|
| `-Dir`        | master directory (default `.\build-matrix`)                       |
| `-SrcDir`     | LAME source directory (default: parent of the script)             |
| `-VsPath`     | Visual Studio install to use (overrides autodetection)            |
| `-Mpg123Dir`  | folder with `mpg123.h` + import lib; enables the decoder-on cells |
| `-Config`     | MSBuild configurations (default `Release,Debug`)                  |
| `-Arch`       | MSBuild platforms (default `x64`)                                 |

Visual Studio is located without an exhaustive disk crawl, in this order:
`-VsPath` &rarr; `vswhere.exe` (the standard installer query tool; finds VS on
any drive) &rarr; an already-initialized developer environment
(`%VCINSTALLDIR%` / `cl` on `PATH`). If none is found the generator explains how
to point it at the toolchain.

The Windows cells cover:

- **nmake** (`Makefile.MSVC`): a default build (decoder off), and a decoder-on
  build (`MPG123=YES MPG123_DIR=...`) that is generated only when `-Mpg123Dir`
  is given. These build **out-of-tree** inside the cell directory:
  `Makefile.MSVC` accepts `srcdir=<path>` and writes all intermediates into the
  build directory, so nothing lands in the source tree. This is the 32-bit
  build (`Makefile.MSVC`'s native target), so `-Arch` does not apply to the
  nmake cells &mdash; `-Arch` selects the platform for the MSBuild cells only.
- **MSBuild** (`vc_solution`): the product of `-Config` &times; `-Arch`, with
  the decoder-on flip (`/p:HaveMpg123=true`) generated only when `-Mpg123Dir`
  is given. Each cell points `OutDirBase`/`IntDirBase` at its own directory, so
  that cells sharing one source tree keep their binaries and objects apart. The
  configuration and platform still split the tree below those, which is what
  lets one cell hold more than one of them.

The solution still carries the `mp3x` analyzer project, but no configuration
selects it for building: it needs a GTK version no current toolchain ships. It
is kept so that the analyzer can be modernized from it later, and is loadable
in the IDE meanwhile.

The decoder-on nmake cell needs an import library beside `mpg123.h`, not just
the header; the generator says so and skips that one cell when only the header
is there. The MSBuild projects build their import library from the `.def` file
the mpg123 binary distribution ships, and so need the header alone.

libsndfile is intentionally **not** part of the Windows matrix.

## Prerequisites

**Linux (Debian/Ubuntu)**

```
apt-get install build-essential clang libmpg123-dev libsndfile1-dev libncurses-dev
```

**FreeBSD**

```
pkg install mpg123 libsndfile        # clang is in the base system
```

**MSYS2 (UCRT64)**

```
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
         mingw-w64-ucrt-x86_64-clang \
         mingw-w64-ucrt-x86_64-mpg123 \
         mingw-w64-ucrt-x86_64-libsndfile
```

**Windows (native)**

Visual Studio (any edition, incl. Build Tools) with the "Desktop development
with C++" workload. For the decoder-on cells, an unpacked libmpg123 with
`mpg123.h` and an import library; pass its folder via `-Mpg123Dir`.

## Using it to validate a patchset

1. Generate the matrix once (`gen-build-matrix.*`).
2. Run the driver (`build-all.*`) on a clean tree, note the summary.
3. Apply the patch, run the driver again.
4. Any cell that changed from pass to fail is a regression introduced by the
   patch; its log is named in the summary.

## Known issues

- **MSYS2/UCRT64, frontend cells:** the frontend links against the terminal
  library for its progress display. `configure` selects that library by probing
  `initscr` (a curses symbol), but the code actually calls the termcap
  functions (`tgetent`, ...). On UCRT64 this lands on `-lncurses` whose termcap
  entry points are declared `dllimport` in the header while the resolved archive
  is static, so the frontend link fails with `undefined reference to
  __imp_tgetent`. The `nodecoder`/frontend builds therefore may fail under
  UCRT64 until `configure`'s terminal-library probe is corrected. The autotools
  builds are unaffected on Linux and FreeBSD, and the native Windows builds do
  not use termcap at all. Remove this entry once the terminal-library probe is
  fixed.

## Keeping the matrix in step with the build system

When a change adds, removes, or renames a build option that affects what can be
compiled (a new `configure` switch, a new `Makefile.MSVC`/`vc_solution` knob),
update the cell lists in the two generators and the tables above in the same
change, so the matrix keeps covering every configuration the build system
offers.
