/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the decoder handle's public entry points.
 *
 * The analysis hooks are the part of this API a frontend wires up without
 * necessarily having a decoder to wire it to: a library built without libmpg123
 * hands back no handle at all, and a frontend that plots what the decoder saw
 * may install no block. Both calls therefore have to survive being handed
 * nothing, which is what the header undertakes and what is checked here.
 *
 * Reaching the end of a case is the assertion in the two hook tests: the failure
 * they guard against is a dereference, so a regressed build leaves the case on a
 * signal rather than on a failed comparison.
 *
 * Library-level tests: they link libmp3lame and call the exported API directly.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>

#include <cmocka.h>

#include "test_unused.h"

#include "lame.h"

/**
 * @brief Neither analysis hook dereferences a handle it was not given.
 *
 * A caller that never obtained a decoder - which is every caller on a library
 * built without libmpg123, since hip_decode_init() then returns NULL - still
 * reaches the frontend's shutdown path, and the header promises both calls do
 * nothing rather than that they are unreachable.
 */
static void
test_analysis_hooks_tolerate_a_null_handle(LAME_UNUSED void **state)
{
    hip_set_pinfo(NULL, NULL);
    hip_finish_pinfo(NULL);
}

/**
 * @brief hip_finish_pinfo() does nothing when no block was installed.
 *
 * The other half of the same promise, on a handle that really exists. Skipped
 * where the library cannot hand one out, rather than passing on the strength of
 * a call that was never made.
 */
static void
test_finish_pinfo_without_a_block(LAME_UNUSED void **state)
{
    hip_t   hip = hip_decode_init();

    if (hip == NULL) {
        skip();         /* no decoder in this build */
    }
    hip_finish_pinfo(hip);
    assert_int_equal(hip_decode_exit(hip), 0);
}

/**
 * @brief Releasing a handle that was never obtained reports success.
 *
 * hip_decode_exit() accepts NULL, so a frontend can call it unconditionally on
 * a path where the handle may never have been created - including the one where
 * the library has no decoder to hand out.
 */
static void
test_decode_exit_accepts_a_null_handle(LAME_UNUSED void **state)
{
    assert_int_equal(hip_decode_exit(NULL), 0);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_analysis_hooks_tolerate_a_null_handle),
        cmocka_unit_test(test_finish_pinfo_without_a_block),
        cmocka_unit_test(test_decode_exit_accepts_a_null_handle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
