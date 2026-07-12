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
