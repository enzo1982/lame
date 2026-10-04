# Distribution check {#maintainer_check_dist}

`maintainer/check-dist.sh` checks one thing about a finished tarball: if someone
downloads it, will it work? It takes the tarball and a scratch directory, unpacks
the tarball, builds it in every configuration that the machine can build, runs
the tests, and reports each step as a named check.

```
sh maintainer/check-dist.sh lame-4.1.tar.gz /tmp/distcheck
```

The script reads nothing from the working tree that the tarball was made from.
Everything, including the build harness, the ABI check, the man pages and the
source, comes from the tarball. A file that is missing from the tarball is
exactly the kind of error that this script looks for.

A POSIX shell cannot run the native Windows build. Use
`maintainer/check-dist.ps1` for those cells. The two scripts are separate on
purpose: most checks apply to only one of them.

## What it does not check

It does not measure encoding quality. That has its own harness and its own
corpus, see @ref maintainer_quality. A tarball can pass every check here and
still encode badly. A release needs both runs, and neither one replaces the
other.

It is also not a check for development. Option checks and the `.op` bitstream
comparison belong to the change to the encoder, not to the tarball that contains
it.

## The checks

Each check prints its name, one line that says what it checks, and then the
result.

| Check | What it checks |
|---|---|
| `extract` | the tarball unpacks, into exactly one top-level directory |
| `version-consistency` | the tarball name, the package version of `configure` and `libmp3lame/version.h` all agree |
| `matrix-generate` | the build harness in the tarball can create the configurations for this machine (@ref maintainer_build_matrix) |
| `build[cell]` | this configuration compiles and links; one check per cell |
| `unit-tests[cell]` | the CMocka tests pass in this configuration; one check per cell |
| `distcheck` | `make distcheck`: a VPATH build, install, installcheck, uninstall and a new dist |
| `doxygen` | both documentation sets build, and contain parsed source, not only the empty theme |
| `manpage` | the man pages in the tarball render without a formatting warning |
| `abi` | the exported interface of the built library matches the list in the tree (@ref maintainer_abi) |

The build and the unit tests are **two checks per cell**, not one, on purpose. A
configuration that builds but cannot run its tests is a different problem from
one that does not build. A summary that combined them would hide which problem
it is.

Optional checks, off by default:

| Flag | Adds |
|---|---|
| `--maintainer-mode` | a `--enable-maintainer-mode` cell, where a warning is an error |
| `--audio-dir DIR` | encodes every file in `DIR` and requires success and a non-empty MP3 |
| `--coverage` | a coverage run (@ref maintainer_coverage) |
| `--sanitizers` | an ASan/UBSan build, and its tests |

`--maintainer-mode` is off by default because a warning from a newer compiler is
no reason to reject a tarball that was correct when it was made. Use it when you
want to know whether the source is clean, not whether the tarball can be
released.

`--quick` replaces the whole matrix with one default configuration. Use it while
you fix a packaging problem. For a release, use the default.

## The four results

- **PASS**: the check ran and found no problem.
- **FAIL**: the check ran and found a problem. Any FAIL makes the run exit with
  a non-zero status.
- **SKIP**: the check could not run *on this machine*, because something it needs
  is missing. After installing it, the check gives a real result.
- **N/A**: the check does not apply here at all, and no installation would change
  that. For example, the native Windows build has no `configure`, so `distcheck`
  is N/A there, not SKIP.

This difference matters when you read a log from another machine. Many SKIPs
mean that the run proved less than it seems. Many N/As mean that this part was
never meant to be checked.

**A configuration that the harness could not create on this machine also
reports SKIP**, with the reason from `matrix-info.txt`
(@ref maintainer_build_matrix). Such a cell has no build directory. The report
lists it as SKIP, so you can see which configurations were not checked.

Neither SKIP nor N/A changes the exit status. So a run on a machine with nothing
installed exits with 0, after checking almost nothing. **Read the counts in the
summary, not only the exit status.**

## Prerequisites, and what is lost without each

| Missing | Effect |
|---|---|
| CMocka (via `pkg-config`) | every `unit-tests[cell]` reports SKIP. In this case the cells are *not* configured with `--enable-unit-tests`, because `configure` fails when the option is given and the library is missing. |
| `libmpg123` | the harness does not generate the cells with the decoder. These cells cover most of the code, so this is the largest single loss. |
| `libsndfile` | the `sndfile` cell is not generated |
| the automake version that generated the tree | the `nodecstrict` cell is not generated, so no check here treats warnings as errors. This cell enables maintainer mode, which runs the tools with the version suffix that the generated Makefiles name (@ref maintainer_build_matrix) |
| `doxygen` | the `doxygen` check reports SKIP |
| `groff` | the `manpage` check reports SKIP |
| `libabigail` | the deepest part of the ABI check reports SKIP, and the `abi` check still passes |

## Reading the result

At the end, the run prints the version, the total number of checks, and the
counts. It also writes the full output to
`<target>/check-dist-<version>-<timestamp>.log`. Each failing check names the
log file with the details: the `build.log` or `check.log` of a cell,
`distcheck.log`, `abicheck.log`, and so on.

The version in the log name comes from the tarball, not from the tree that the
script was run from. So the logs of several candidate tarballs sort correctly,
and you can tell them apart. `check-dist.ps1` writes
`<target>/check-dist-windows-<version>-<timestamp>.log`. This is the same name,
with the platform in it, so both scripts can use the same target directory.

## Known failures that come from the tree, not from the tarball

Some cells fail their unit tests for reasons that have nothing to do with the
tarball. The unit tests compile frontend source files in configurations that
these files were not written for. The `--disable-decoder` cell is the usual
example. When `unit-tests[...]` fails in one cell and the same tests pass in the
other cells, check whether this cell is one of these before you treat it as a
release blocker.
