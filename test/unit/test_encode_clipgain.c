/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the clipping figures that the encoder reports after an
 *        encode (libmp3lame/lame.c).
 *
 * With the peak measurement on, the encoder decodes its own output. It stores
 * the loudest sample that it sees. If all samples are zero, the peak stays at
 * zero. The headroom figure cannot be computed from a peak of zero. The
 * logarithm of zero is minus infinity, and its conversion to an integer is
 * undefined. Silence is ordinary input for this measurement, for example a
 * leading gap, a muted track or an empty capture.
 *
 * Both tests need a build that can decode. Without a decoder, the library
 * rejects the request to measure the peak. Then nothing computes a headroom
 * figure, and there is no behavior to check. The test asks the library, not a
 * configuration macro, because the rejection is what a caller sees.
 *
 * The loud case is the control for the silent case. It shows that the figures
 * change. Without it, a zero can also mean that the measurement never ran.
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
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"

#include "lame.h"

#define SAMPLES_PER_CALL 1152
#define CALLS            8
#define MP3BUF_SIZE      (5 * SAMPLES_PER_CALL / 4 + 7200)

/**
 * @brief Encodes one buffer several times with the peak measurement on, and
 *        then flushes.
 *
 * The flush computes the figures that the tests check.
 * @param left,right the block of samples to encode, repeated #CALLS times.
 * @return the encoder instance, still open, for the caller to check. NULL if
 *         this build rejects the peak measurement.
 */
static lame_t
encode_with_peak_measurement(short const *left, short const *right)
{
    unsigned char mp3buf[MP3BUF_SIZE];
    lame_t  gfp = lame_init();
    int     i, rc;

    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    /* The documented way to ask for the peak: it is what decoding on the fly
       is for, and the setting the frontend's --clipdetect turns on. A build
       without the decoder refuses it here rather than accepting it and
       measuring nothing, so the caller has an answer and so has this test. */
    if (lame_set_decode_on_the_fly(gfp, 1) != 0) {
        /* The result is not asserted: this instance never reached
           lame_init_params(), and closing one that did not is reported as a
           failure although it frees everything. */
        (void) lame_close(gfp);
        return NULL;
    }
    assert_int_equal(lame_init_params(gfp), 0);

    for (i = 0; i < CALLS; i++) {
        rc = lame_encode_buffer(gfp, left, right, SAMPLES_PER_CALL,
                                mp3buf, (int) sizeof mp3buf);
        assert_true(rc >= 0);
    }
    rc = lame_encode_flush(gfp, mp3buf, (int) sizeof mp3buf);
    assert_true(rc >= 0);
    return gfp;
}

/**
 * @brief Checks that silence reports no clipping, no scaling and no undefined
 *        value.
 *
 * Without the check for a zero peak, the code takes the logarithm of zero. It
 * then converts the resulting infinity to an int. The C standard leaves this
 * conversion undefined. A typical result is the most negative int. The caller
 * then sees a headroom of about 214 million decibels.
 */
static void
test_silence_reports_no_headroom(LAME_UNUSED void **state)
{
    static short const zeros[SAMPLES_PER_CALL];
    lame_t  gfp = encode_with_peak_measurement(zeros, zeros);

    if (gfp == NULL) {
        skip();         /* no decoder, so no peak measurement to ask for */
    }
    assert_true(lame_get_PeakSample(gfp) == 0.0f);
    assert_int_equal(lame_get_noclipGainChange(gfp), 0);
    assert_true(lame_get_noclipScale(gfp) == -1.0f);
    assert_int_equal(lame_close(gfp), 0);
}

/**
 * @brief Checks that a signal that is not silent changes the figures.
 *
 * This is the control for the test above. It shows that the measurement runs
 * and writes its result. So a zero in the test above is the result for
 * silence. It does not mean that the peak was never measured.
 */
static void
test_signal_reports_a_peak(LAME_UNUSED void **state)
{
    short   left[SAMPLES_PER_CALL];
    short   right[SAMPLES_PER_CALL];
    lame_t  gfp;
    int     i;

    for (i = 0; i < SAMPLES_PER_CALL; i++) {
        /* a loud triangle, well away from both zero and full scale */
        short const v = (short) (16000 - 32 * (i % 1000));

        left[i] = v;
        right[i] = (short) -v;
    }
    gfp = encode_with_peak_measurement(left, right);

    if (gfp == NULL) {
        skip();         /* no decoder, so no peak measurement to ask for */
    }
    assert_true(lame_get_PeakSample(gfp) > 0.0f);
    assert_int_equal(lame_close(gfp), 0);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_silence_reports_no_headroom),
        cmocka_unit_test(test_signal_reports_a_peak),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
