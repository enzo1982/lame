/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for lame_get_totalframes() length handling (set_get.c).
 *
 * lame_get_totalframes() estimates the number of MP3 frames from the caller's
 * declared num_samples. Two edge cases are checked here: the documented
 * "unknown" sentinel (2^32-1) must report 0 rather than a bogus estimate, and a
 * declared length whose frame count would exceed the int the estimate is
 * returned in must not overflow it. The latter is only reachable where
 * unsigned long is wider than int, so it is skipped elsewhere.
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
#include <limits.h>

#include <cmocka.h>

#include "lame.h"

/** @brief totalframes estimate for a stereo 44.1 kHz stream of @p ns samples. */
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

/** @brief The documented unknown sentinel (2^32-1) reports 0, not an estimate. */
static void
test_sentinel_is_unknown(void **state)
{
    (void) state;
    assert_int_equal(totalframes_for(0xFFFFFFFFUL), 0);
}

/** @brief A known finite length gives a positive estimate near samples/1152. */
static void
test_known_length_estimated(void **state)
{
    unsigned long const ns = 44100UL * 10; /* 10 s, MPEG-1: 1152 samples/frame */
    int const tf = totalframes_for(ns);
    (void) state;
    assert_true(tf > 0);
    assert_true(tf >= (int) (ns / 1152) && tf <= (int) (ns / 1152) + 4);
}

/**
 * @brief A frame count past INT_MAX yields the unknown estimate, not overflow.
 *
 * Only reachable where unsigned long is wider than int (e.g. LP64); where it is
 * not, num_samples cannot express such a length, so the case is skipped.
 */
static void
test_overflow_length_is_unknown(void **state)
{
    (void) state;
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
