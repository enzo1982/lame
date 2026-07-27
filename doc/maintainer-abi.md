# ABI check {#maintainer_abi}

`maintainer/abicheck.sh` answers one question about a change: can a program
built against the previous libmp3lame still be linked, loaded and called
against this one?

It reads a built library and two text files from the source tree. It encodes
nothing, needs no audio input and no second build to compare against, so it
runs anywhere the library has been built &mdash; including on a machine that
has no reference release to hand.

```
make && make abicheck
```

## What the contract is

libmp3lame's exported interface is written down twice, once per platform, and
both files are maintained by hand:

| File                     | Platform | Used by                                            |
|--------------------------|----------|----------------------------------------------------|
| `include/libmp3lame.sym` | POSIX    | libtool `-export-symbols`; everything else is local |
| `include/lame.def`       | Windows  | the module-definition file the DLL is linked with   |

Because the linker is told to export exactly what these files list, they are
not documentation of the ABI &mdash; they *are* the ABI. A symbol that stops
being listed stops being callable, and a symbol quietly added to one file and
not the other ships on one platform only.

## The three checks

They run in one pass, in increasing depth and decreasing availability. A check
whose tool is missing reports `SKIP` and does not affect the exit status; a
check that could be made and did not hold reports `FAIL` and the run exits
non-zero.

### contract &mdash; the two lists against each other

Needs nothing but the source tree, so it runs even before anything is built.

Anything `lame.def` exports that `libmp3lame.sym` does not is a failure with no
exceptions: the Windows DLL must not offer an entry point the library does not
have.

The other direction is held to the same standard, because of what the lists
are:

> There is **one list per operating system, not one per build**. Each is the
> logical OR over every configuration we ship, since the same file is used for
> all of them. A symbol belongs in it when the code is compiled in and is part
> of the exported interface &mdash; deprecated code that is still built is
> still exported and still belongs. Only what is not compiled in at all, or is
> not part of the exported interface, stays out.

Two things follow. A configure option that compiles code out does **not** mean
the symbol should leave the list: `--disable-decoder` builds the decoding entry
points as stubs and still exports all 241 names, so the list is the same either
way. And anything our own frontends call across the library boundary must be in
the list, or a dynamically linked build of what we ship does not link at all.

So the comparison is exact in both directions: a name in one list and not in
the other fails the check, whichever side it sits on. The only thing that could
justify a difference is a symbol that genuinely exists on one operating system
and not on the other, and there is nothing like that today. Should one ever
arise, it is a decision to be taken here, in this document and in the script &mdash;
not by quietly editing one list on its own.

### exports &mdash; the built library against the contract

Reads the export table back out of the library that was just built and compares
it to the list for that platform: `nm -D` on ELF, `nm -gU` on Mach-O,
`dumpbin /exports` or `objdump -p` on a DLL. Linker-generated names
(`_init`, `_end`, `DllMain`, ...) are not part of anyone's interface and are
dropped before the comparison.

This catches the case the first check cannot see: the contract and the library
disagreeing because a function was renamed, made static, or compiled out by a
configure option.

A **static-only build** (`--disable-shared`) is the one case where the exported
set cannot be observed at all &mdash; an archive keeps every non-static symbol,
so there is no export table to read. The check degrades rather than lying about
it: it confirms every promised symbol is present and says in the same line that
extra exports cannot be detected in this configuration.

### abi &mdash; inside the symbols

Two libraries can export the same names and still be incompatible: a parameter
that grew from `int` to `long`, a struct that gained a member ahead of an
existing one, an enum whose values shifted. `abidiff` compares the DWARF of the
build against a baseline committed at `maintainer/abi/libmp3lame.abi` and
reports those.

It needs two things that are not always there, and skips cleanly when they are
missing:

- **libabigail.** Packaged on Linux (`apt install abigail-tools`); not
  available on FreeBSD, macOS or native Windows. From Windows, WSL is the
  practical host for this check.
- **Debug information in the build.** Without DWARF the comparison collapses
  to the symbol set the previous check already covers, while reporting every
  type as removed &mdash; a long report that means nothing. The script detects
  this and skips instead.

```
../configure CFLAGS='-g' && make && make abicheck
```

`CFLAGS` on the configure line adds to the build's own flags rather than
replacing them, so that asks for debug information and nothing else: the
optimization stays where the project put it, and the resulting tree still
builds and tests like any other.

## What it does not check

A green run is a narrow statement, and reading it as a broad one is the way to
be caught out by it.

**It does not know which configuration you built.** The export lists are the
logical OR over every configuration we ship &mdash; one file per operating
system, not one per build &mdash; so the check compares against the union and
cannot tell you what *this* build contains. Today that costs nothing:
`--disable-decoder` still defines the decoding entry points, as stubs, and
still exports all of them, so every configuration exports the same set and the
comparison can be exact. **If a symbol is ever made genuinely conditional, that
stops being true**, and the second check has to be relaxed from "the same set"
to "no more than the contract", with the conditional names written down
somewhere. Until then, a promised symbol that is missing is a real failure and
is reported as one.

**It does not check behaviour.** A symbol that exists, links, and does nothing
passes. The decoding entry points in a build without mpg123 are exactly that.
The check is about whether a program still *links and loads*, not whether it
still works.

**It does not check the other platform's library.** Only the first check is
cross-platform, and only because it compares two text files. The second and
third read the library built here, so on a POSIX host nothing has looked at a
DLL at all: an error in `lame.def` that the first check cannot see &mdash; a
wrong ordinal, a name that no longer exists &mdash; surfaces only in a Windows
build.

**It does not check that what we ship still links.** It inspects the library's
exports, not the frontends. A symbol the frontend imports can be removed from
the list and all three checks still pass, because the failure lands in the
frontend's link step instead. That is not hypothetical: it is how the analysis
hooks were nearly dropped.

**It does not cover the C++ components.** The ACM codec and the DirectShow
filter have their own interfaces, and neither export list describes them.

**And the third check is only as honest as its baseline.** Regenerating the
baseline makes any ABI change go green, so the file is a record of what was
*accepted*, not evidence that nothing changed. That is why it is regenerated
deliberately, in the commit that changes the ABI, and not in response to a red
run.

## The baseline

`maintainer/abi/libmp3lame.abi` is an `abidw` dump of the exported interface,
committed to the tree and shipped in the distribution so that a build from the
tarball can run the check too. The ABI did not change between 4.0 and 4.1, so
the state at the time it was captured is the reference both releases are
measured against.

It is regenerated **only at an intentional ABI change**, which is a release
decision rather than a routine one. The command is:

```
abidw --no-corpus-path --no-show-locs --no-comp-dir-path --short-locs \
      libmp3lame/.libs/libmp3lame.so > maintainer/abi/libmp3lame.abi
```

The four options are not cosmetic. `abidw` otherwise writes the absolute path
of the library it read, the compilation directory, and a source path on each of
some 1500 type definitions &mdash; so a dump captured in one build tree differs
from the same ABI captured in another in every one of those places, and the
diff of an intentional one-symbol change would be unreadable. With them the
file depends on the interface and not on where it was built.

Regenerating it is how an ABI change is *accepted*, so it belongs in the same
commit as the change and should be visible in review. Reaching for it because
the check went red is the failure mode this file is meant to prevent.

## Reading the result

```
== LAME ABI check ==

library : libmp3lame/.libs/libmp3lame.so
contract: include/libmp3lame.sym, include/lame.def
baseline: maintainer/abi/libmp3lame.abi

[1/3] contract: the committed export lists (libmp3lame.sym, lame.def) name the same symbols
      PASS  241 symbols, named by both
[2/3] exports: the built library exports exactly the symbols the contract promises
      PASS  241 symbols, matching libmp3lame.sym
[3/3] abi: no signature, struct-layout or enum change inside those symbols
      PASS  identical to the baseline

Summary: 3 checks - 3 PASS, 0 FAIL, 0 SKIP
```

A failure names the symbols rather than only counting them, on the side they
were found:

```
[1/3] contract: the committed export lists (libmp3lame.sym, lame.def) name the same symbols
      FAIL  the two lists have drifted apart
        exported on POSIX but not on Windows: lame_new_function
```

Three `SKIP`s and a zero exit is a real possibility on a host with no
libabigail and nothing built &mdash; it means the run proved nothing, not that
the ABI is fine. The summary line says how many checks actually concluded, and
that is the number to read first.

## When the check goes red

The verdict is about the *contract*, never about whether the change was a good
one. Two questions in order:

1. **Was the ABI meant to change?** Adding a function is compatible: add it to
   both export lists and regenerate the baseline. Changing or removing one that
   already shipped is not, and needs a soname bump rather than a new baseline.
2. **If it was not meant to change, what moved?** A configure option that
   compiled a function out, a rename that reached one export list and not the
   other, a struct in a public header that grew a member.

The check is deliberately not part of `make check` or `make all`: it wants a
built shared library, and on most hosts it can only give a partial answer. It
is a step in validating a release, and a thing to run by hand after touching a
public header or either export list.
