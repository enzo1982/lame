# ABI check {#maintainer_abi}

`maintainer/abicheck.sh` checks one thing about a change: can a program built
against the previous libmp3lame still link against this one, load it and call
it?

It reads a built library and two text files from the source tree. It encodes
nothing. It needs no audio input and no second build for a comparison. So it
runs wherever the library is built, also on a machine without a reference
release.

```
make && make abicheck
```

## What the contract is

The exported interface of libmp3lame is written down twice, once per platform.
Both files are maintained by hand:

| File                     | Platform | Used by                                            |
|--------------------------|----------|----------------------------------------------------|
| `include/libmp3lame.sym` | POSIX    | libtool `-export-symbols`; all other symbols are local |
| `include/lame.def`       | Windows  | the module-definition file for linking the DLL      |

The linker exports exactly what these files list. So these files do not just
document the ABI, they define it. A symbol that is removed from a list can no
longer be called. A symbol that is added to one file and not to the other is
exported on one platform only.

## The three checks

The three checks run in one pass. Each check looks deeper than the one before,
and needs more tools. A check whose tool is missing reports `SKIP` and does not
change the exit status. A check that runs and finds a problem reports `FAIL`,
and the run exits with a non-zero status.

### contract: the two lists against each other

This check needs only the source tree, so it runs even before anything is
built.

If `lame.def` exports a name that `libmp3lame.sym` does not list, the check
always fails. The Windows DLL must not offer an entry point that the library
does not have.

The check also fails if `libmp3lame.sym` lists a name that `lame.def` does not.
The reason is how the lists are defined:

> There is **one list per operating system, not one per build**. Each list is
> the union of all configurations that we ship, because the same file is used
> for all of them. A symbol belongs in the list when its code is compiled in
> and it is part of the exported interface. Deprecated code that is still built
> is still exported, so it also belongs in the list. Only code that is not
> compiled in at all, or that is not part of the exported interface, stays out.

This has two consequences. First, a configure option that compiles code out
does **not** remove the symbol from the list. `--disable-decoder` builds the
decoding entry points as stubs and still exports every name, so the list is the
same with and without it. Second, every library function that our own
frontends call must be in the list. Otherwise a dynamically linked build of our
programs does not link.

So the comparison is exact in both directions. A name in one list and not in
the other fails the check, whichever list it is in. Only a symbol that exists on
one operating system and not on the other could justify a difference, and there
is no such symbol today. If one is ever needed, decide it here, in this document
and in the script. Do not just edit one list.

### exports: the built library against the contract

This check reads the export table of the library that was just built, and
compares it with the list for this platform. It uses `nm -D` on ELF, `nm -gU`
on Mach-O, and `dumpbin /exports` or `objdump -p` on a DLL. Names that the
linker generates (`_init`, `_end`, `DllMain`, ...) are not part of the
interface, so the check removes them before the comparison.

This check finds what the first check cannot see: the list and the library
differ because a function was renamed, made static, or compiled out by a
configure option.

In a **static-only build** (`--disable-shared`), the exported set cannot be
read at all. An archive keeps every non-static symbol, so it has no export
table. In this case the check does less, and says so: it checks that every
listed symbol is present, and reports on the same line that extra exports
cannot be found in this configuration.

### abi: inside the symbols

Two libraries can export the same names and still be incompatible. For example,
a parameter changed from `int` to `long`, a struct got a new member before an
existing one, or the values of an enum changed. `abidiff` compares the DWARF
information of the build with a baseline in `maintainer/abi/libmp3lame.abi`,
and reports such changes.

It needs two things that are not always available. Without them, it reports
SKIP:

- **libabigail.** Linux has a package (`apt install abigail-tools`), and so has
  FreeBSD (`devel/libabigail`). It is not available on native Windows. There,
  use WSL for this check.
- **Debug information in the build.** Without DWARF information, the comparison
  only sees the symbol set, which the previous check already compares, and it
  reports every type as removed. This long report means nothing. The script
  detects this case and reports SKIP instead.

```
../configure CFLAGS='-g' && make && make abicheck
```

`CFLAGS` on the configure line is added to the flags of the build. It does not
replace them. So this command only adds debug information: the optimization
level of the project stays the same, and the build tree builds and tests like
any other. You do not need to fix anything else, not the compiler and not the
optimization level. The section "The baseline" below explains why. The build
must be **shared**, though. A static-only build has no dynamic symbol table,
and the second check reports this.

## What it does not check

A passing run proves less than it seems. The check does not test the following:

**Which configuration you built.** The export lists are the union of all
configurations that we ship, with one file per operating system, not one per
build. So the check compares with the union, and cannot tell what *this* build
contains. Today this does not matter: `--disable-decoder` still defines the
decoding entry points as stubs and still exports all of them, so every
configuration exports the same set and the comparison can be exact. **This
changes if a symbol is ever really conditional.** Then the second
check must accept "no more than the list" instead of "the same set", and the
conditional names must be written down somewhere. Until then, a listed symbol
that is missing is a real failure, and the check reports it.

**Behavior.** A symbol that exists, links and does nothing passes. The decoding
entry points in a build without mpg123 are such symbols. The check tests
whether a program still *links and loads*, not whether it still works.

**The library of the other platform.** Only the first check works across
platforms, because it compares two text files. The second and third checks read
the library built here. So on a POSIX host, no check reads a DLL. An error in
`lame.def` that the first check cannot see, for example a wrong ordinal or the
name of a removed function, shows only in a Windows build.

**That our own programs still link.** The check reads the exports of the
library, not the frontends. A symbol that a frontend imports can be removed from
the list, and all three checks still pass, because the link of the frontend
fails instead.

**The C++ components.** The ACM codec and the DirectShow filter have their own
interfaces, and neither export list describes them.

**A wrong baseline.** The third check is only as good as its baseline. After a
regeneration, every ABI change passes. So the file records what was
*accepted*. It does not prove that nothing changed. For this reason, regenerate
it on purpose, in the commit that changes the ABI, and never because a run
failed.

## The baseline

`maintainer/abi/libmp3lame.abi` is an `abidw` dump of the exported interface.
It is committed to the tree and included in the distribution, so that a build
from the tarball can run the check too. It records the interface at the last
intentional ABI change. Every release after that is compared with this state.
So it is normal that the baseline stays the same for several versions. This
does not mean that it is out of date.

Regenerate it **only at an intentional ABI change**. The maintainer decides
whether a change is one, not the script and not the reviewer. When a run fails,
find out why. A failed run is never a reason to regenerate the baseline. The
command, from the top of a build tree:

```
abidw --no-corpus-path --no-show-locs --no-comp-dir-path --short-locs \
      --no-elf-needed --drop-undefined-syms --exported-interfaces-only \
      --headers-dir "$srcdir/include" \
      libmp3lame/.libs/libmp3lame.so > maintainer/abi/libmp3lame.abi
```

Each option is needed. They solve two different problems.

**Four options keep the build tree out of the file.** Without them, `abidw`
writes the absolute path of the library, the compilation directory, and a
source path for each of about 1500 type definitions. So a dump from one build
tree would differ from a dump of the same ABI from another build tree in all of
these places.

**Four options keep the *machine* out of the file.** This matters because the
file is in the release tarball, and the check should work wherever LAME builds.
Without these options, the baseline records a `DT_NEEDED` list. On a glibc host
with the `-ffast-math` of the project, this list includes `libmvec`, which does
not exist on FreeBSD. It also records declarations of libc functions that the
library only calls, and types that come from the headers of other libraries.
`--no-elf-needed` removes the dependency list. `--drop-undefined-syms` removes
what we do not define. `--exported-interfaces-only` with `--headers-dir` keeps
only what the public headers of LAME can reach. The rest describes the
libmp3lame API and nothing else.

Because of these options, **the compiler flags do not matter**. Regenerating
with or without `-ffast-math` gives byte-identical output.

Regenerating the baseline accepts an ABI change. So the regeneration belongs in
the same commit as the change, where reviewers can see it. Do not regenerate it
because the check failed: that is the mistake this file exists to prevent.

### Compare a regeneration with `abidiff`, never with `diff`

The baseline is XML. So it is tempting to compare two dumps with `diff`.
**`diff` does not tell you whether the ABI changed.** `abidiff` compares the two
dumps by meaning. It treats renumbered type ids, reordered records, and types
that come into the dump by a different route as the same interface, and this
happens often. Two dumps that differ in thousands of lines can describe the same
ABI, and a difference of one line can be a real break.

So when a regeneration gives a large diff, do not ask "what are all these
lines". Ask "what does `abidiff` report". The `abicheck` run shows this.
`abidiff` prints **nothing at all** when the two sides agree. So an empty report
with exit status zero is the clean result, not a failed run.

### One baseline for all compilers, but for one architecture

**The compiler does not matter.** A baseline from a GCC build accepts a Clang
build of the same source without any reported change, even though the two dumps
differ in more than a thousand lines of text. You need neither a second baseline
nor a fixed compiler. The previous section explains why.

**The architecture does matter.** The baseline records type sizes and layouts.
So a build for another architecture is a different ABI, and the check reports
it as one. The report starts with
`architecture changed from 'elf-amd-x86_64' to 'elf-intel-80386'` and then lists
every structure whose layout depends on the word size. The committed baseline is
for **x86-64**. On any other architecture, the third check compares word sizes,
not LAME, so ignore its result. The first two checks find the mistakes in the
export lists, and they work on every architecture.

## Reading the result

```
== LAME ABI check ==

library : libmp3lame/.libs/libmp3lame.so
contract: include/libmp3lame.sym, include/lame.def
baseline: maintainer/abi/libmp3lame.abi

[1/3] contract: the committed export lists (libmp3lame.sym, lame.def) name the same symbols
      PASS  246 symbols, named by both
[2/3] exports: the built library exports exactly the symbols the contract promises
      PASS  246 symbols, matching libmp3lame.sym
[3/3] abi: no signature, struct-layout or enum change inside those symbols
      PASS  identical to the baseline

Summary: 3 checks - 3 PASS, 0 FAIL, 0 SKIP
```

A failure names the symbols, not only their number, and says in which list they
are:

```
[1/3] contract: the committed export lists (libmp3lame.sym, lame.def) name the same symbols
      FAIL  the two lists have drifted apart
        exported on POSIX but not on Windows: lame_new_function
```

On a host without libabigail and without a build, the run can report three
`SKIP`s and exit with zero. This means that the run proved nothing, not that the
ABI is fine. The summary line says how many checks had a result. Read this
number first.

## When the check fails

The result is about the *contract*. It does not say whether the change was good.
Ask two questions, in this order:

1. **Was the ABI meant to change?** Adding a function is compatible: add it to
   both export lists and regenerate the baseline. Changing or removing a
   function that was already released is not compatible. It needs a new soname,
   not a new baseline.
2. **If it was not meant to change, what changed?** For example, a configure
   option compiled a function out, a rename reached one export list and not the
   other, or a struct in a public header got a new member.

On purpose, the check is not part of `make check` or `make all`. It needs a
built shared library, and on most hosts it can only run some of its checks. Run
it when you validate a release, and by hand after you change a public header or
an export list.
