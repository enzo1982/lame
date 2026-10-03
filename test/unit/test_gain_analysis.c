/**
 * @file
 * @ingroup unit_tests
 * @brief ReplayGain analysis (libmp3lame/gain_analysis.c).
 *
 * Mono and stereo input give the window levels and the title gain of a double
 * precision reference, which the test computes with its own filter code.
 * Checked over 3 s of signal and a part window, fed in blocks of several sizes,
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
/** @brief Samples in one 50 ms analysis window at RATE. */
#define WINDOW (RATE / 20)
/** @brief Full windows in the signal. */
#define NWINDOWS (NSAMPLES / WINDOW)
/** @brief The Yule filter's order. */
#define ORDER 10

static Float_t left[NSAMPLES], right[NSAMPLES];

/** @brief The Yule filter coefficients at 44.1 kHz, copied from
 *         gain_analysis.c: 11 for the input, then 10 for the output, the
 *         oldest sample first. */
static const float yule44[2 * ORDER + 1] = {
    -0.00187763777362f, 0.00674613682247f, -0.00240879051584f, 0.01624864962975f,
    -0.02596338512915f, 0.02245293253339f, -0.00834990904936f, -0.00851165645469f,
    -0.00848709379851f, -0.02911007808948f, 0.05418656406430f,
    0.13149317958808f, -0.75104302451432f, 2.19611684890774f, -4.39470996079559f,
    6.85401540936998f, -8.81498681370155f, 9.47693607801280f, -8.54751527471874f,
    6.36317777566148f, -3.47845948550071f
};

/** @brief The Butterworth filter coefficients for the same rate: input two
 *         back, output two back, input one back, output one back, the input
 *         sample. */
static const float butter44[5] = {
    0.98500175787242f, 0.97022847566350f, -1.97000351574484f, -1.96977855582618f,
    0.98500175787242f
};

/** @brief The reference's output of the first filter, with ORDER zeros before
 *         it. */
static double step[NSAMPLES + ORDER];
/** @brief The reference's output of both filters for each channel, with ORDER
 *         zeros before it. */
static double filtered[2][NSAMPLES + ORDER];

/** @brief Block sizes: shorter than the filter order, a granule, a frame,
 *         and one that crosses the 2205-sample window. */
static const int blocks[] = { 5, 576, 1152, 2300 };

/**
 * @brief Filters one channel in double precision, the way the ReplayGain
 *        specification defines it.
 *
 * @param in       the samples.
 * @param channel  0 or 1: which of #filtered gets the result.
 */
static void
reference_filter(const Float_t *in, int channel)
{
    double *const y = step + ORDER;
    double *const z = filtered[channel] + ORDER;
    int     i, k;
    for (i = 0; i < NSAMPLES; ++i) {
        double  s = 0;
        for (k = 0; k <= ORDER; ++k)
            if (i - ORDER + k >= 0)
                s += (double) in[i - ORDER + k] * yule44[k];
        for (k = 0; k < ORDER; ++k)
            s -= y[i - ORDER + k] * yule44[ORDER + 1 + k];
        y[i] = s;
    }
    for (i = 0; i < NSAMPLES; ++i)
        z[i] = y[i - 2] * butter44[0] + y[i - 1] * butter44[2] + y[i] * butter44[4]
            - z[i - 2] * butter44[1] - z[i - 1] * butter44[3];
}

/**
 * @brief Fills the left channel with a tone and noise, and the right channel
 *        with a different tone and noise, each under a slow envelope, so the
 *        window levels spread over about 45 dB. Then filters them with the
 *        reference.
 *
 * @param channels  1 for mono: the reference filters the left channel twice.
 *                  2 for stereo.
 */
static void
make_signal(int channels)
{
    unsigned int seed = 12345u;
    int     i;
    for (i = 0; i < NSAMPLES; ++i) {
        seed = seed * 1103515245u + 12345u;
        left[i] = (Float_t) ((8000.0 * sin(i * 0.031) + (int) ((seed >> 16) % 2000) - 1000)
                             * (0.005 + fabs(sin(i * 0.00005))));
        seed = seed * 1103515245u + 12345u;
        right[i] = (Float_t) ((6000.0 * sin(i * 0.017 + 1.0) + (int) ((seed >> 16) % 3000) - 1500)
                              * (0.005 + fabs(cos(i * 0.00007))));
    }
    reference_filter(left, 0);
    reference_filter(channels == 2 ? right : left, 1);
}

/**
 * @brief The level of the reference's filtered samples, as the analysis
 *        computes it: 10 log10 of half the summed mean square of both channels.
 *
 * @param first  the first sample.
 * @param n      the number of samples.
 * @return the level in dB.
 */
static double
reference_level(int first, int n)
{
    double  sum = 0;
    int     i;
    for (i = first; i < first + n; ++i)
        sum += filtered[0][ORDER + i] * filtered[0][ORDER + i]
            + filtered[1][ORDER + i] * filtered[1][ORDER + i];
    return 10. * log10(sum / n * 0.5 + 1.e-37);
}

/**
 * @brief The title gain of the reference: the histogram and the percentile of
 *        the analysis, over the reference's levels of the full windows.
 *
 * @return the gain in dB.
 */
static Float_t
reference_title_gain(void)
{
    static uint32_t hist[STEPS_per_dB * MAX_dB];
    uint32_t upper, sum = 0;
    size_t  i;
    int     w;
    memset(hist, 0, sizeof hist);
    for (w = 0; w < NWINDOWS; ++w) {
        double const val = STEPS_per_dB * reference_level(w * WINDOW, WINDOW);
        size_t  ival = val <= 0 ? 0 : (size_t) val;
        if (ival >= sizeof hist / sizeof *hist)
            ival = sizeof hist / sizeof *hist - 1;
        hist[ival]++;
    }
    upper = (uint32_t) ceil(NWINDOWS * (1. - RMS_PERCENTILE));
    for (i = sizeof hist / sizeof *hist; i-- > 0;) {
        sum += hist[i];
        if (sum >= upper)
            break;
    }
    return (Float_t) ((Float_t) PINK_REF - (Float_t) i / (Float_t) STEPS_per_dB);
}

/**
 * @brief Analyses the signal in blocks of one size.
 *
 * @param channels  1 for mono (the left channel), 2 for stereo.
 * @param block     samples per call.
 * @return the analysis state after the last block, before the title gain is
 *         taken. The caller frees it.
 */
static replaygain_t *
analyse(int channels, int block)
{
    replaygain_t *rg = calloc(1, sizeof *rg);
    int     done;
    assert_non_null(rg);
    assert_int_equal(InitGainAnalysis(rg, RATE), INIT_GAIN_ANALYSIS_OK);
    for (done = 0; done < NSAMPLES; done += block) {
        int const n = NSAMPLES - done < block ? NSAMPLES - done : block;
        assert_int_equal(AnalyzeSamples(rg, left + done, right + done, (size_t) n, channels),
                         GAIN_ANALYSIS_OK);
    }
    return rg;
}

/**
 * @brief Checks that the input gives the reference's level in every window
 *        above 30 dB, within 0.001 dB.
 *
 * Each window is fed in two blocks: all but its last sample, then that sample.
 * Between the two, the sums of the window in progress give its level.
 *
 * @param channels  1 for mono, 2 for stereo.
 */
static void
check_levels(int channels)
{
    replaygain_t *rg = calloc(1, sizeof *rg);
    int     w, checked = 0;
    make_signal(channels);
    assert_non_null(rg);
    assert_int_equal(InitGainAnalysis(rg, RATE), INIT_GAIN_ANALYSIS_OK);
    for (w = 0; w < NWINDOWS; ++w) {
        int const first = w * WINDOW;
        double  level, ref;
        assert_int_equal(AnalyzeSamples(rg, left + first, right + first, WINDOW - 1, channels),
                         GAIN_ANALYSIS_OK);
        assert_int_equal(rg->totsamp, WINDOW - 1);
        level = 10. * log10((rg->lsum + rg->rsum) / (WINDOW - 1) * 0.5 + 1.e-37);
        ref = reference_level(first, WINDOW - 1);
        if (ref > 30.) {
            if (!double_is_finite(level) || fabs(level - ref) > 0.001)
                fail_msg("window %d: %.6f dB, the reference %.6f dB", w, level, ref);
            ++checked;
        }
        assert_int_equal(AnalyzeSamples(rg, left + first + WINDOW - 1, right + first + WINDOW - 1,
                                        1, channels), GAIN_ANALYSIS_OK);
        assert_int_equal(rg->totsamp, 0);
    }
    assert_true(checked >= NWINDOWS * 3 / 4);
    free(rg);
}

/**
 * @brief Checks that the input gives the reference's title gain in every block
 *        size.
 *
 * @param channels  1 for mono, 2 for stereo.
 */
static void
check_title_gain(int channels)
{
    size_t  k;
    Float_t ref;
    make_signal(channels);
    ref = reference_title_gain();
    for (k = 0; k < sizeof blocks / sizeof *blocks; ++k) {
        replaygain_t *rg = analyse(channels, blocks[k]);
        Float_t const gain = GetTitleGain(rg);
        if (gain != ref)
            fail_msg("block %d: title gain %.2f dB, the reference %.2f dB", blocks[k], gain, ref);
        free(rg);
    }
}

/**
 * @brief Runs check_levels() on mono input.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_levels(void **state)
{
    (void) state;
    check_levels(1);
}

/**
 * @brief Runs check_levels() on stereo input.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_stereo_levels(void **state)
{
    (void) state;
    check_levels(2);
}

/**
 * @brief Runs check_title_gain() on mono input.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_title_gain(void **state)
{
    (void) state;
    check_title_gain(1);
}

/**
 * @brief Runs check_title_gain() on stereo input.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_stereo_title_gain(void **state)
{
    (void) state;
    check_title_gain(2);
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
    make_signal(1);
    mono = analyse(1, 1000);
    assert_true(mono->totsamp > 0);
    assert_memory_equal(&mono->lsum, &mono->rsum, sizeof mono->lsum);
    free(mono);
}

/** @brief Registers and runs the ReplayGain analysis tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mono_levels),
        cmocka_unit_test(test_stereo_levels),
        cmocka_unit_test(test_mono_title_gain),
        cmocka_unit_test(test_stereo_title_gain),
        cmocka_unit_test(test_mono_sums_are_equal),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
