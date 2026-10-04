# Build-configuration test matrix {#maintainer_build_matrix}

Two generator scripts build LAME in many configurations at once. So you can
check a change in every configuration it might affect before you commit it.

- `maintainer/gen-build-matrix.sh`: the autotools (POSIX) build.
- `maintainer/gen-build-matrix.ps1`: the native Windows builds (nmake with
  `Makefile.MSVC`, and MSBuild with the `vc_solution` projects).

Each generator creates a *master directory*. In it, there is one separate
out-of-tree build for each configuration, and a driver script. The driver
builds all configurations, does not stop at the first failure, writes every
build log to a file, and prints a summary of the failed builds with the
location of their logs.

## A star, not every combination

Most build options switch independent parts of the code on or off: the
decoder, the file I/O backend, the analyzer hooks, shared or static linking,
the frontends. Testing every combination would need dozens of builds and would
find little more. So the matrix is a *star*: one base build with all features,
and one build per option that changes only this option. A regression in any
single part still shows, and the number of builds stays small enough to run
often.

## The configuration cells (POSIX / autotools)

| Cell         | configure arguments                               | Change from the base                 |
|--------------|---------------------------------------------------|--------------------------------------|
| `full`       | `--enable-dynamic-frontends`                      | base: shared lib + dynamic frontends |
| `nodecoder`  | `--enable-dynamic-frontends --disable-decoder`    | mpg123 decoder off                   |
| `nodecstrict`| `--enable-dynamic-frontends --disable-decoder --enable-maintainer-mode` | decoder off *and* warnings are errors |
| `sndfile`    | `--enable-dynamic-frontends --with-fileio=sndfile`| file I/O via libsndfile              |
| `noanalyzer` | `--enable-dynamic-frontends --disable-analyzer-hooks` | analyzer hooks off (NOANALYSIS)  |
| `static`     | `--disable-shared --enable-static`                | static-only, no dynamic frontends    |
| `libonly`    | `--disable-frontend`                              | library only, no frontends           |
| `staticfe`   | `--disable-dynamic-frontends`                     | frontends linked statically (not the default) |
| `nohardening`| `--enable-dynamic-frontends --disable-hardening`  | security hardening off               |
| `expopt`     | `--enable-dynamic-frontends --enable-expopt=norm` | experimental optimizations on        |
| `mp3x`       | `--enable-dynamic-frontends --enable-mp3x`        | GTK 4 frame analyzer frontend on     |

Dynamic linking of the frontends is the default. The base build still gives
`--enable-dynamic-frontends` on purpose. When the frontends link against the
shared library, a symbol that they use but that is missing from the export list
makes the link fail here. The `staticfe` cell tests the other case,
`--disable-dynamic-frontends`.

The security hardening flags are detected and enabled by default, so the base
cell already uses them. The `nohardening` cell tests `--disable-hardening`. The
`expopt` cell tests the extra optimization flags of `--enable-expopt=norm`.
For both option sets, configure tests the compiler and keeps only the flags it
accepts. So the exact flags differ between GCC and Clang. The matrix checks
that each cell still builds and links with every compiler. `--enable-native` is
not a cell. Binaries tuned for one machine are not meant to be distributed, and
the option is a convenience for the local machine.

`nodecstrict` changes two options at once, on purpose. Some code is compiled
only when the decoder is disabled. The `full` cell does not compile this code,
so it cannot warn about it. The `nodecoder` cell compiles it, but does not treat
warnings as errors. Only a cell with both options finds a new warning in this
code. For the same reason, the unit tests are not a cell:
`-x --enable-unit-tests` adds them to *every* cell, including this one, which
covers more combinations with an option that already exists.

This cell fails on any warning, unlike all other cells. That is its purpose. So
when the matrix reports a failure, look at this cell first, because its
requirements are stricter than those of the other cells.

Each compiler that the generator finds gets the whole star. So the number of
builds is *compilers &times; cells*.

## The driver

The generated `build-all.sh` (POSIX) or `build-all.ps1` (Windows):

- builds every cell, one after the other, and **does not stop when one fails**;
- writes the output of each build to `logs/<compiler>-<cell>.log`
  (Windows: `logs/<toolchain>-<cell>.log`);
- at the end, prints a summary with each failed build and its log file;
- exits with a non-zero status if any build failed, and with zero if all
  passed.

## POSIX usage

```
sh maintainer/gen-build-matrix.sh [-d DIR] [-s SRCDIR] [-c LIST] [-j N] [-x ARGS]
sh DIR/build-all.sh
```

| Option      | Meaning                                                              |
|-------------|---------------------------------------------------------------------|
| `-d DIR`    | master directory to create (default `./build-matrix`)               |
| `-s SRCDIR` | source dir containing `configure` (default: parent of the script)   |
| `-c LIST`   | comma-separated compiler list instead of autodetection              |
| `-j N`      | parallel make jobs per build (default: detected CPU count)          |
| `-x ARGS`   | extra `configure` arguments appended to every cell                  |

`-x` adds the same options to all cells, after the own arguments of each cell.
It was written for `-x --enable-unit-tests`, so that every configuration is
tested and not only built. @ref maintainer_check_dist uses it in this way. The
generator does not check these options. If an option needs a library that is not
installed, `configure` fails in **every** cell, and no cell is skipped. So check
the requirement before you pass the option.

To find compilers, the generator tries `gcc clang cc` **on `PATH` only**. It
does not search the file system. Then it removes duplicates by the `--version`
output of each compiler, so an alias such as `cc` for `gcc` is built only once.
Use `-c` to choose the compilers, for example `-c gcc,clang` or
`-c /opt/gcc-15/bin/gcc`.

If a cell needs an optional dependency that is missing, the cell is **skipped**
(not generated), with a note in `matrix-info.txt`. So the list of failures of
the driver contains only real failures:

- every cell that does not disable the decoder needs **libmpg123**. All cells
  except `nodecoder` and `nodecstrict` enable the decoder, and `configure`
  fails if the library is missing.
- the `nodecstrict` cell needs **the automake version that generated the
  tree**. This is a tool, not a library. `--enable-maintainer-mode` enables the
  Autotools rebuild rules. These rules call the tools with the version suffix
  that the generated Makefiles name (`aclocal-1.18` and the others). On a
  machine with a different automake, this tool is missing, and `make` stops with
  status 127. This failure is about the host, not about the source. The
  generator reads the required version from the `configure` of the tree, and
  skips the cell when this version is not installed. So nobody has to update the
  version by hand.
- the `sndfile` cell also needs **libsndfile**.
- the `mp3x` cell also needs **GTK 4 &ge; 4.10**. The generator tests with the
  same version requirement as `configure`. So a GTK 4 that is too old for the
  analyzer counts as missing here, instead of creating a cell that cannot
  configure.

Only the `mp3x` cell compiles and links the analyzer sources. So without GTK 4,
the analyzer frontend is not built at all, and `matrix-info.txt` says so.

## Windows usage

```
pwsh maintainer/gen-build-matrix.ps1 [-Dir DIR] [-SrcDir DIR] `
     [-VsPath DIR] [-Mpg123Dir DIR] [-LibsndfileDir DIR] [-GtkDir DIR] `
     [-DShowBaseClassesDir DIR] [-Config LIST] [-Arch LIST]
pwsh DIR\build-all.ps1
```

| Option        | Meaning                                                           |
|---------------|-------------------------------------------------------------------|
| `-Dir`        | master directory (default `.\build-matrix`)                       |
| `-SrcDir`     | LAME source directory (default: parent of the script)             |
| `-VsPath`     | Visual Studio install to use (overrides autodetection)            |
| `-Mpg123Dir`  | folder with `mpg123.h` + import lib; enables the decoder-on cells |
| `-LibsndfileDir` | libsndfile folder with `lib\sndfile.lib`; enables the libsndfile cells |
| `-GtkDir`     | GTK 4 install prefix with `lib\gtk-4.lib`; enables the mp3x cell  |
| `-DShowBaseClassesDir` | DirectShow base class sources (`streams.h`); enables the DirectShow filter cell |
| `-Config`     | MSBuild configurations (default `Release,Debug`)                  |
| `-Arch`       | MSBuild platforms (default `x64`)                                 |

The generator finds Visual Studio without searching the whole disk, in this
order: `-VsPath`, then `vswhere.exe` (the standard installer query tool, which
finds Visual Studio on any drive), then a developer environment that is already
set up (`%VCINSTALLDIR%`, or `cl` on `PATH`). If it finds none, it explains how
to give it the path of the toolchain.

The Windows cells are:

- **nmake** (`Makefile.MSVC`): a default build, and one build for each optional
  library that is present: the decoder (`MPG123=YES`) and libsndfile
  (`SNDFILE=YES`). There is no analyzer cell: `Makefile.MSVC` builds 32-bit
  binaries, GTK 4 is published for x64 only, so `Makefile.MSVC` does not build
  mp3x. These cells build **out of tree**, in the cell directory:
  `Makefile.MSVC` accepts `srcdir=<path>` and writes all intermediate files into
  the build directory, so nothing is written into the source tree. These are
  32-bit builds (the native target of `Makefile.MSVC`), so `-Arch` does not
  apply to them. `-Arch` selects the platform for the MSBuild cells only.
- **MSBuild** (`vc_solution`): every combination of `-Config` and `-Arch`, and
  for each optional library that is present, one more build of the base
  configuration with this library (`/p:HaveMpg123=true`,
  `/p:HaveLibsndfile=true`, and the mp3x project with `/p:HaveGtk=true`). Each
  cell sets `OutDirBase` and `IntDirBase` to its own directory, so that cells
  that share one source tree keep their binaries and objects apart. Below these
  directories, the configuration and the platform have their own
  subdirectories, so one cell can hold more than one of them.
- **The client components** (`vs_lame_clients.slnx`, Win32 only): one cell for
  the ACM codec, which needs only Visual Studio and so is always generated, and
  one cell for the DirectShow filter, which is generated only when the base class
  sources are present. Both cells run `smoke-clients.ps1` after the build, and
  the exit status of this script is the result of the cell. The two components
  are DLLs that Windows loads into the process of another program. So the script
  loads each one, finds the entry points it must provide, and reads its import
  table for any DLL that is not part of Windows and would have to be shipped with
  it. A clean build only shows that the component linked. When the 32-bit mpg123
  is present, a third cell builds the clients with `/p:HaveMpg123=true` and runs
  the same checks without `libmpg123-0.dll` next to them. The clients link the
  library that is built without the decoder, so they must load without it.

The generator looks for the optional libraries under `vc_solution`, where
`setup-windows-deps.ps1` puts them, or in the directory that `-Mpg123Dir`,
`-LibsndfileDir`, `-GtkDir` or `-DShowBaseClassesDir` names. A library that is
missing only removes its cells, so the matrix runs with what is installed.

The solution contains the `mp3x` analyzer project, but does not select it,
because it needs a GTK build that LAME does not ship. When GTK is present, the
generator builds the project on its own (x64 only), not through the solution.
So the analyzer is tested, and nobody else needs GTK.

`mp3x` is the same GTK 4 program with every toolchain. On Windows, only the
Visual Studio solution builds it. It is **x64 only**: gvsbuild publishes GTK 4
for x64 and for no other architecture, so a 32-bit analyzer would have nothing
to link against. For the same reason, `Makefile.MSVC`, whose native target is
32-bit x86, does not build it.

You do not need to build GTK 4 for MSVC yourself. The gvsbuild releases page
publishes the finished x64 build as an archive whose root is already an install
prefix. Unpack it into `vc_solution\gtk4\x64`, in the same place and the same
way as mpg123 and libsndfile, and the setup is complete.
`setup-windows-deps.ps1` does exactly this.

The nmake cell with the decoder needs an import library next to `mpg123.h`, not
only the header. If only the header is there, the generator says so and skips
this one cell. The MSBuild projects build their import library from the `.def`
file in the mpg123 binary distribution, so they need only the header.

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

Visual Studio (any edition, including Build Tools) with the "Desktop
development with C++" workload. Each cell for an optional library needs this
library under `vc_solution`. `setup-windows-deps.ps1` puts it there from
archives that you have downloaded. `vc_solution\README.vs.txt` says where to
get each one. A missing library only removes its cells.

## Using it to validate a patch set

1. Generate the matrix once (`gen-build-matrix.*`).
2. Run the driver (`build-all.*`) on a clean tree, and note the summary.
3. Apply the patch, and run the driver again.
4. Any cell that passed before and fails now is a regression from the patch.
   The summary names its log.

## Keeping the matrix in step with the build system

When a change adds, removes or renames a build option that changes what can be
compiled (a new `configure` option, a new setting in `Makefile.MSVC` or
`vc_solution`), update the cell lists in the two generators and the tables
above in the same change. So the matrix keeps covering every configuration that
the build system offers.
