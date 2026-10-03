/**
 * @file
 * @ingroup unit_tests
 * @brief ReplayGain analysis of mono input (libmp3lame/gain_analysis.c).
 *
 * Mono input gives the same result as stereo input whose two channels are
 * that one channel. Checked over 3 s of signal and a part window, fed in blocks of several sizes,
 * including blocks shorter than the filter order and blocks that cross a
 * 50 ms analysis window.
 *
 * The analysis is internal and the shared library does not export it, so this
 * test links the static archive, as test_resample_decision.c does.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "lame.h"
#include "machine.h"
#include "gain_analysis.h"

/** @brief The sample rate analysed. */
#define RATE 44100
/** @brief Samples per channel: 3 s and 1000 more, so the last window is not
 *         complete and its sums are still there to compare. */
#define NSAMPLES (3 * RATE + 1000)

static Float_t left[NSAMPLES], right[NSAMPLES];

/**
 * @brief Fills the left channel with a tone and noise, and the right channel
 *        with a different tone and noise.
 */
static void
make_signal(void)
{
    unsigned int seed = 12345u;
    int     i;
    for (i = 0; i < NSAMPLES; ++i) {
        seed = seed * 1103515245u + 12345u;
        left[i] = (Float_t) (8000.0 * sin(i * 0.031) + (int) ((seed >> 16) % 2000) - 1000);
        seed = seed * 1103515245u + 12345u;
        right[i] = (Float_t) (6000.0 * sin(i * 0.017 + 1.0) + (int) ((seed >> 16) % 3000) - 1500);
    }
}

/**
 * @brief Analyses the signal in blocks of one size.
 *
 * @param channels  1 for mono (the left channel), 2 for stereo.
 * @param l         the left channel.
 * @param r         the right channel; ignored for mono.
 * @param block     samples per call.
 * @return the analysis state after the last block, before the title gain is
 *         taken. The caller frees it.
 */
static replaygain_t *
analyse(int channels, const Float_t *l, const Float_t *r, int block)
{
    replaygain_t *rg = calloc(1, sizeof *rg);
    int     done;
    assert_non_null(rg);
    assert_int_equal(InitGainAnalysis(rg, RATE), INIT_GAIN_ANALYSIS_OK);
    for (done = 0; done < NSAMPLES; done += block) {
        int const n = NSAMPLES - done < block ? NSAMPLES - done : block;
        assert_int_equal(AnalyzeSamples(rg, l + done, r + done, (size_t) n, channels),
                         GAIN_ANALYSIS_OK);
    }
    return rg;
}

/**
 * @brief Whether two analysis states hold the same result: the histogram, the
 *        sums of the window in progress, and the title gain, bit for bit.
 *
 * @param a  one state.
 * @param b  the other.
 * @return nonzero when they agree.
 */
static int
same_result(replaygain_t *a, replaygain_t *b)
{
    Float_t ga, gb;
    if (memcmp(a->A, b->A, sizeof a->A) != 0 || a->totsamp != b->totsamp)
        return 0;
    if (memcmp(&a->lsum, &b->lsum, sizeof a->lsum) != 0
        || memcmp(&a->rsum, &b->rsum, sizeof a->rsum) != 0)
        return 0;
    ga = GetTitleGain(a);
    gb = GetTitleGain(b);
    return memcmp(&ga, &gb, sizeof ga) == 0;
}

/** @brief Block sizes: shorter than the filter order, a granule, a frame,
 *         and one that crosses the 2205-sample window. */
static const int blocks[] = { 5, 576, 1152, 2300 };

/**
 * @brief Mono gives the result of stereo with both channels equal to it.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_equals_identical_stereo(void **state)
{
    size_t  k;
    (void) state;
    make_signal();
    for (k = 0; k < sizeof blocks / sizeof *blocks; ++k) {
        replaygain_t *mono = analyse(1, left, right, blocks[k]);
        replaygain_t *stereo = analyse(2, left, left, blocks[k]);
        if (!same_result(mono, stereo))
            fail_msg("block %d: mono and identical stereo disagree", blocks[k]);
        free(mono);
        free(stereo);
    }
}

/**
 * @brief The comparison can fail: mono of the left channel and stereo with a
 *        different right channel disagree.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_comparison_sees_a_difference(void **state)
{
    replaygain_t *mono, *stereo;
    (void) state;
    make_signal();
    mono = analyse(1, left, right, 1152);
    stereo = analyse(2, left, right, 1152);
    assert_false(same_result(mono, stereo));
    free(mono);
    free(stereo);
}

/**
 * @brief In the window in progress, the two sums of mono input are equal.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_sums_are_equal(void **state)
{
    replaygain_t *mono;
    (void) state;
    make_signal();
    mono = analyse(1, left, right, 1000);
    assert_true(mono->totsamp > 0);
    assert_memory_equal(&mono->lsum, &mono->rsum, sizeof mono->lsum);
    free(mono);
}

/** @brief Registers and runs the ReplayGain analysis tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mono_equals_identical_stereo),
        cmocka_unit_test(test_comparison_sees_a_difference),
        cmocka_unit_test(test_mono_sums_are_equal),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
