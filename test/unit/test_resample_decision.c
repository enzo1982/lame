/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the decision to resample (libmp3lame/util.c).
 *
 * Sample rates are integers. So the question "does this session need
 * resampling" is a test for equality. Any input rate other than the output
 * rate goes through the resampler, even when the two rates are very close.
 *
 * Close rates give a resampling ratio near an integer, for example
 * 44101&nbsp;Hz input and 44100&nbsp;Hz output. The resampler must not round
 * such a ratio to an integer. If it does, it chooses the wrong filter length.
 * The window index then runs past the precomputed filter table, and the
 * encoder crashes with a segmentation fault.
 *
 * The tests check both parts, because the second part makes the first safe:
 *
 *   - the decision itself, over rate pairs on both sides of a tolerance of
 *     about 0.05 percent, including the two rates on its edges.
 *   - an encode across a one-hertz mismatch, end to end through the public
 *     API.
 *
 * The tests also encode a pass-through session with equal rates. This case
 * must keep working. A decision that returns "resample" for every pair would
 * pass the mismatch cases alone.
 *
 * @c isResamplingNecessary() is internal. @c include/libmp3lame.sym does not
 * export it from the shared library. So this test links the static archive,
 * as @c test_vector_ladder.c does.
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

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"

/** @brief Samples per channel that the test passes to the encoder in one call. */
#define NSAMPLES 4608
/** @brief Size of the output buffer, per the worst case in lame.h. */
#define MP3BUF_SIZE (NSAMPLES * 5 / 4 + 7200)

/** @brief One row of the decision table. */
struct rate_pair {
    int     in;              /**< input sample rate, Hz */
    int     out;             /**< output sample rate, Hz */
    int     expected;        /**< 1 if the session must resample */
    const char *what;        /**< what the row stands for */
};

/**
 * @brief Checks that the decision for each rate pair uses equality, not
 *        proximity.
 *
 * The 44077 and 44122 rows are the outermost rates that a tolerance of about
 * 0.05 percent calls equal to 44100. Both are mismatches and must be
 * resampled.
 */
static void
test_decision_is_exact(LAME_UNUSED void **state)
{
    static const struct rate_pair pairs[] = {
        { 44100, 44100, 0, "equal rates need no resampling" },
        { 44101, 44100, 1, "one hertz above the output rate" },
        { 44099, 44100, 1, "one hertz below the output rate" },
        { 44122, 44100, 1, "the upper edge of the old tolerance" },
        { 44077, 44100, 1, "the lower edge of the old tolerance" },
        { 48000, 44100, 1, "an ordinary downsample" },
        { 22050, 44100, 1, "an ordinary upsample" },
        {  8000,  8000, 0, "equal rates at the low end of the table" },
        {  8001,  8000, 1, "one hertz apart at the low end of the table" }
    };
    size_t  i;

    for (i = 0; i < dimension_of(pairs); ++i) {
        SessionConfig_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        cfg.samplerate_in = pairs[i].in;
        cfg.samplerate_out = pairs[i].out;

        assert_int_equal(isResamplingNecessary(&cfg), pairs[i].expected);
    }
}

/**
 * @brief Encodes silence through a session with the given rates.
 * @param in_rate   input sample rate, Hz.
 * @param out_rate  output sample rate, Hz.
 * @return the number of MP3 bytes, or a negative value on failure. The value
 *         is -1 if the session cannot be set up. A failed encode call returns
 *         its own negative code.
 */
static int
encode_across(int in_rate, int out_rate)
{
    static short int pcm[2 * NSAMPLES];
    unsigned char mp3buf[MP3BUF_SIZE];
    lame_global_flags *gf;
    int     total = 0, n, i;

    gf = lame_init();
    if (gf == 0) {
        return -1;
    }
    lame_set_num_channels(gf, 2);
    lame_set_in_samplerate(gf, in_rate);
    lame_set_out_samplerate(gf, out_rate);
    lame_set_brate(gf, 128);
    lame_set_quality(gf, 7);
    if (lame_init_params(gf) < 0) {
        lame_close(gf);
        return -1;
    }

    memset(pcm, 0, sizeof(pcm));
    for (i = 0; i < 3; ++i) {
        n = lame_encode_buffer_interleaved(gf, pcm, NSAMPLES, mp3buf, sizeof(mp3buf));
        if (n < 0) {
            lame_close(gf);
            return n;
        }
        total += n;
    }
    n = lame_encode_flush(gf, mp3buf, sizeof(mp3buf));
    if (n > 0) {
        total += n;
    }
    lame_close(gf);
    return total;
}

/**
 * @brief Checks that an encode with a one-hertz mismatch runs to the end.
 *
 * This pair goes through the resampler with a ratio near 1. The purpose of the
 * test is to run the resampler. The byte count only shows that the encode
 * ran.
 */
static void
test_near_equal_rates_encode(LAME_UNUSED void **state)
{
    assert_true(encode_across(44101, 44100) > 0);
    assert_true(encode_across(44099, 44100) > 0);
}

/**
 * @brief Checks that equal rates encode on the pass-through path.
 */
static void
test_equal_rates_encode(LAME_UNUSED void **state)
{
    assert_true(encode_across(44100, 44100) > 0);
}

/**
 * @brief Runs the resampling-decision tests.
 * @return the number of failed tests.
 */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_decision_is_exact),
        cmocka_unit_test(test_near_equal_rates_encode),
        cmocka_unit_test(test_equal_rates_encode)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
