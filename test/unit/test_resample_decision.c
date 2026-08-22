/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the decision to resample (libmp3lame/util.c).
 *
 * Sample rates are integers, so "does this session need resampling" is an
 * equality question: any input rate other than the output rate has to go
 * through the resampler, however close the two are.
 *
 * That was not always safe. A near-integer ratio used to be rounded to an
 * integer one, which chose the wrong filter length and let the window index
 * run past the precomputed filter table - a segmentation fault, reachable by
 * asking for 44100&nbsp;Hz output from a 44101&nbsp;Hz input. The encoder
 * carried a guard against ever reaching that path: rates agreeing to about
 * four digits were declared equal and the audio was passed through. The
 * rounding was fixed years later and the guard outlived it.
 *
 * Both halves are checked here, because the second is what makes the first
 * safe:
 *
 *   - the decision itself, over pairs that straddle the width of the old
 *     guard, including the two rates that sat exactly on its edges;
 *   - an encode across a one-hertz mismatch, which is the pairing that used
 *     to fault, run end to end through the public API.
 *
 * A pass-through session (equal rates) is encoded too. It is the arm that must
 * keep working: a decision that answered "resample" for everything would
 * satisfy the mismatch cases on its own.
 *
 * @c isResamplingNecessary() is internal and is withheld from the shared
 * library by @c include/libmp3lame.sym, so this test links the static archive -
 * the same arrangement @c test_vector_ladder.c uses.
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

/** @brief Samples per channel handed to the encoder in one call. */
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
 * @brief Every rate pair is decided by equality, not by proximity.
 *
 * The 44077 and 44122 rows are the edges of the guard this replaced: they were
 * the outermost rates it still called equal to 44100. Both are mismatches and
 * must be resampled.
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
 * @return the number of mp3 bytes produced, or -1 if the session could not be
 *         opened.
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
 * @brief A one-hertz mismatch encodes to completion.
 *
 * This is the pairing the removed guard existed to avoid. Reaching the
 * resampler at all is the point of the test; the byte count only says the
 * encode ran.
 */
static void
test_near_equal_rates_encode(LAME_UNUSED void **state)
{
    assert_true(encode_across(44101, 44100) > 0);
    assert_true(encode_across(44099, 44100) > 0);
}

/**
 * @brief Equal rates still encode, on the pass-through path.
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
