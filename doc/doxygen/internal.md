# LAME Internal / Development Documentation

<img src="lame_logo_full.svg" alt="LAME" style="display:block;margin:0 auto 1.5em;width:100%;max-width:360px;height:auto;">

This is the **internal** documentation for LAME. It covers the implementation:
the static internal functions of `libmp3lame` and the command-line frontend,
together with the CMocka unit-test suite (see @ref unit_tests).

It exists for people working on LAME itself. **If you use LAME as a library,
this is not the documentation you want** - read the public API documentation
instead. That is generated from the same sources but limited to the supported
public interface declared in `include/lame.h`.

This configuration enables `EXTRACT_ALL` and `EXTRACT_STATIC`, so the entities
shown here include unsupported internals that carry no stability or ABI
guarantee; do not rely on them from outside the library.

## Maintainer guides

The scripts under `maintainer/` check a change against more than the tree it
was written in. Each has a guide of its own:

- @ref maintainer_build_matrix - building LAME in every configuration at once,
  so that a change is checked against each of them before it is committed.
- @ref maintainer_perf - comparing the encoding speed of two builds, and
  telling a real difference from measurement noise.
