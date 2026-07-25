/* Unused-parameter marker for the unit tests.
 *
 * Several tests here compile against libmp3lame's PUBLIC API only - they do
 * not pull in any internal header. libmp3lame's own LAME_UNUSED lives in
 * machine.h, an internal header; including it just to silence a warning would
 * couple these public-API-only tests to the library's internals. This local
 * copy keeps that coupling out while giving every test the same spelling as
 * the library. The definition is intentionally identical to machine.h's; if
 * that ever changes, change it here too.
 */
#ifndef LAME_TEST_UNUSED_H
#define LAME_TEST_UNUSED_H

#if defined(__GNUC__) || defined(__clang__)
# define LAME_UNUSED __attribute__((unused))
#else
# define LAME_UNUSED
#endif

#endif /* LAME_TEST_UNUSED_H */
