# LAME Internal / Development Documentation

<img src="lame_logo_full.svg" alt="LAME" style="display:block;margin:0 auto 1.5em;width:100%;max-width:360px;height:auto;">

This is the **internal** documentation for LAME. It describes the
implementation: the static internal functions of `libmp3lame` and of the
command-line frontend, and the CMocka unit tests (see @ref unit_tests).

It is for people who work on LAME itself. **If you use LAME as a library, read
the public API documentation instead.** The public documentation is generated
from the same sources, but it shows only the supported public interface in
`include/lame.h`.

This configuration enables `EXTRACT_ALL` and `EXTRACT_STATIC`. So this
documentation also shows internal functions and types that are not supported.
They have no stability or ABI guarantee. Do not use them from outside the
library.

## Implementation notes

Longer notes on how a part of LAME works, and why it works that way:

- @ref vector_dispatch - the tiers of SIMD routines that LAME selects at run
  time, and why each tier has the routines it has. There is no SSE4.1 or
  SSE4.2 tier, the VBR noise estimate has an SSE2 version only, and the
  AVX-512 tier has one routine, the quantization loop of `count_bits()`.
  @ref vector_dispatch_record has the measurements behind these decisions.

## Maintainer guides

The scripts in `maintainer/` check a change in more ways than a normal build
does. Each script has its own guide:

- @ref maintainer_build_matrix - building LAME in every configuration at once,
  to check a change in each of them before it is committed.
- @ref maintainer_perf - comparing the encoding speed of two builds, and
  telling a real difference from measurement noise.
- @ref maintainer_quality - measuring how a change affects the encoded audio,
  for changes that are meant to change it.
- @ref maintainer_coverage - measuring which source lines the test material
  runs, and finding the configurations and command lines that are worth
  running under the sanitizers.
- @ref maintainer_abi - checking the exported interface of the library against
  the list in the source tree, so that every change to it is made on purpose.
- @ref maintainer_check_dist - checking a finished distribution tarball before
  it is announced: does it unpack, build in every configuration this machine
  can build, pass its tests, and show the right version information.
- @ref maintainer_gen_api_docs - building this documentation and the public
  API reference, and copying both to where the website serves them.
