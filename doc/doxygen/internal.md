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

## Implementation notes

Longer-form notes on how a subsystem is built and why it is built that way:

- @ref vector_dispatch - the run-time SIMD dispatch ladder for the hot
  quantization loops, and why it stops where it does: no SSE4.1/SSE4.2 rung,
  no AVX2 tier for the variable-bitrate noise estimate, and no AVX-512 tier
  above AVX2 - the last one built, measured on three machines and rejected.

## Maintainer guides

The scripts under `maintainer/` check a change against more than the tree it
was written in. Each has a guide of its own:

- @ref maintainer_build_matrix - building LAME in every configuration at once,
  so that a change is checked against each of them before it is committed.
- @ref maintainer_perf - comparing the encoding speed of two builds, and
  telling a real difference from measurement noise.
- @ref maintainer_quality - scoring what a change did to the encoded audio,
  for the changes that are meant to alter it.
- @ref maintainer_coverage - measuring which source lines the test material
  reaches, and which configurations and invocations are worth running under
  the sanitizers.
- @ref maintainer_abi - checking the library's exported interface against the
  contract committed in the source tree, so that a change to it is a decision
  rather than an accident.
- @ref maintainer_check_dist - validating a finished distribution tarball
  before it is announced: does it unpack, build in every configuration this
  machine can build, pass its tests, and still describe itself correctly.
- @ref maintainer_gen_api_docs - building this documentation and the public
  API reference, and putting both where the website can serve them.
