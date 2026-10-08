/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for lame_init_params() when the decoder that measures the
 *        encoded output cannot start (libmp3lame/lame.c).
 *
 * The test compiles lame.c in, with a hip_decode_init() that fails when the
 * test asks it to. The rest of the library comes from libmp3lame as usual.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"
#include "test_report.h"

#include "lame.h"

#ifdef HAVE_MPG123
/** Set to 1 to make the next hip_decode_init() of lame.c fail. */
static int fail_next_decoder = 0;

/**
 * @brief The hip_decode_init() that lame.c calls in this test.
 * @return NULL once after the test sets @c fail_next_decoder, else what
 *         hip_decode_init() returns.
 */
static hip_t
test_hip_decode_init(void)
{
    if (fail_next_decoder) {
        fail_next_decoder = 0;
        return NULL;
    }
    return hip_decode_init();
}
#endif

#define hip_decode_init() test_hip_decode_init()
#include "lame.c"
#undef hip_decode_init

#ifdef HAVE_MPG123
/**
 * @brief Prepares an encoder that decodes its own output, and runs
 *        lame_init_params() on it.
 * @param fail 1 to make the decoder fail to start.
 * @return the result of lame_init_params(). The function closes the instance.
 */
static int
init_with_decoder(int fail)
{
    lame_t  gfp = lame_init();
    int     rc;

    assert_non_null(gfp);
    report_reset();
    assert_int_equal(lame_set_errorf(gfp, report_capture), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_decode_on_the_fly(gfp, 1), 0);
    fail_next_decoder = fail;
    rc = lame_init_params(gfp);
    assert_int_equal(fail_next_decoder, 0);
    (void) lame_close(gfp);
    return rc;
}
#endif

/**
 * @brief Checks that lame_init_params() fails, with a message, when the
 *        decoder cannot start.
 * @param state cmocka fixture state (unused).
 */
static void
test_a_decoder_that_cannot_start_fails_init(LAME_UNUSED void **state)
{
#ifdef HAVE_MPG123
    assert_int_equal(init_with_decoder(1), -1);
    assert_true(report_calls > 0);
    /* the control: with a decoder, the same settings work */
    assert_int_equal(init_with_decoder(0), 0);
    assert_int_equal(report_calls, 0);
#else
    skip();
#endif
}

/** @brief Registers the decoder start test and runs it. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_a_decoder_that_cannot_start_fails_init),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
