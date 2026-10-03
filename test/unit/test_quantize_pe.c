/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for turning perceptual entropy into bits
 *        (libmp3lame/quantize_pvt.c).
 *
 * The encoder scales the perceptual entropy by a smoothed value that can come
 * out near zero, so the entropy the bit allocation sees can lie far outside
 * anything an int holds. These reach internal symbols, so the test links the
 * static archive.
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

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_global_flags.h"
#include "quantize_pvt.h"
#include "reservoir.h"

/**
 * @brief bits_in_range() truncates a count inside the range and clamps one
 *        outside it, however far outside.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_bits_in_range(LAME_UNUSED void **state)
{
    assert_int_equal(bits_in_range(12.9f, 0, 100), 12);
    assert_int_equal(bits_in_range(99.99f, 0, 100), 99);
    assert_int_equal(bits_in_range(100.0f, 0, 100), 100);
    assert_int_equal(bits_in_range(-0.5f, 0, 100), 0);
    assert_int_equal(bits_in_range(5.5f, 10, 20), 10);
    assert_int_equal(bits_in_range(1e10f, 0, 100), 100);
    assert_int_equal(bits_in_range(-1e10f, 0, 100), 0);
}

/**
 * @brief on_pe() gives a channel the most extra bits it allows for an entropy
 *        far above any real one, and none for one far below.
 *
 * 700 is the entropy on_pe() adds nothing for, so it is the reference: a far
 * higher entropy must add bits, a far lower one must not take any away.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_on_pe_extreme_entropy(LAME_UNUSED void **state)
{
    static const FLOAT neutral[2][2] = { {700, 700}, {700, 700} };
    static const FLOAT high[2][2] = { {1e10f, 1e10f}, {1e10f, 1e10f} };
    static const FLOAT low[2][2] = { {-1e10f, -1e10f}, {-1e10f, -1e10f} };
    lame_t  gf = lame_init();
    lame_internal_flags *gfc;
    int     mean_bits = 0, ch;
    int     t_neutral[2], t_high[2], t_low[2];

    assert_non_null(gf);
    assert_int_equal(lame_set_in_samplerate(gf, 44100), 0);
    assert_int_equal(lame_set_num_channels(gf, 2), 0);
    assert_int_equal(lame_set_brate(gf, 128), 0);
    assert_true(lame_init_params(gf) >= 0);
    gfc = gf->internal_flags;
    (void) ResvFrameBegin(gfc, &mean_bits);

    (void) on_pe(gfc, neutral, t_neutral, mean_bits, 0, 1);
    (void) on_pe(gfc, high, t_high, mean_bits, 0, 1);
    (void) on_pe(gfc, low, t_low, mean_bits, 0, 1);
    for (ch = 0; ch < 2; ++ch) {
        assert_true(t_high[ch] > t_neutral[ch]);
        assert_true(t_high[ch] <= MAX_BITS_PER_CHANNEL);
        assert_int_equal(t_low[ch], t_neutral[ch]);
    }
    lame_close(gf);
}

/** @brief Runs the perceptual entropy tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bits_in_range),
        cmocka_unit_test(test_on_pe_extreme_entropy),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
