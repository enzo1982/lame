/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the length handling of lame_get_totalframes()
 *        (set_get.c).
 *
 * lame_get_totalframes() estimates the number of MP3 frames. It uses the
 * num_samples value that the caller sets. The tests check two edge cases:
 * - The documented "unknown" value (2^32-1) returns 0, not a wrong estimate.
 * - A length with more frames than an int can store does not overflow the
 *   return value.
 *
 * The second case needs an unsigned long that is wider than int. On other
 * platforms the test is skipped.
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API directly.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <limits.h>

#include <cmocka.h>

#include "test_unused.h"

#include "lame.h"

/** @brief Returns the frame estimate for @p ns samples of stereo 44.1 kHz. */
static int
totalframes_for(unsigned long ns)
{
    lame_t gfp = lame_init();
    int    tf;
    lame_set_in_samplerate(gfp, 44100);
    lame_set_num_channels(gfp, 2);
    lame_set_num_samples(gfp, ns);
    lame_init_params(gfp);
    tf = lame_get_totalframes(gfp);
    lame_close(gfp);
    return tf;
}

/** @brief Checks that the documented "unknown" value (2^32-1) returns 0. */
static void
test_sentinel_is_unknown(LAME_UNUSED void **state)
{
    assert_int_equal(totalframes_for(0xFFFFFFFFUL), 0);
}

/** @brief Checks that a known length gives an estimate near samples/1152. */
static void
test_known_length_estimated(LAME_UNUSED void **state)
{
    unsigned long const ns = 44100UL * 10; /* 10 s, MPEG-1: 1152 samples/frame */
    int const tf = totalframes_for(ns);
    assert_true(tf > 0);
    assert_true(tf >= (int) (ns / 1152) && tf <= (int) (ns / 1152) + 4);
}

/**
 * @brief Checks that a frame count above INT_MAX returns 0 ("unknown") and does
 *        not overflow.
 *
 * The case needs an unsigned long that is wider than int, for example on LP64.
 * Otherwise num_samples cannot express such a length, and the test is skipped.
 */
static void
test_overflow_length_is_unknown(LAME_UNUSED void **state)
{
    if (sizeof(unsigned long) <= sizeof(int)) {
        skip();
        return;
    }
    /* (INT_MAX + margin) frames' worth of samples overflows the int estimate. */
    assert_int_equal(totalframes_for(((unsigned long) INT_MAX + 1000UL) * 1152UL), 0);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sentinel_is_unknown),
        cmocka_unit_test(test_known_length_estimated),
        cmocka_unit_test(test_overflow_length_is_unknown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
