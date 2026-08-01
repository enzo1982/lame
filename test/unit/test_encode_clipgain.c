/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the clipping figures reported after an encode
 *        (libmp3lame/lame.c).
 *
 * With the peak measurement enabled the encoder decodes its own output and
 * records the loudest sample it sees. Material whose samples are all zero
 * leaves that peak at zero, which is the one value the headroom figure cannot
 * be computed from - the logarithm of zero is not a number, and converting it
 * to an integer is undefined. Silence is ordinary input for this measurement:
 * a leading gap, a muted track, an empty capture.
 *
 * Both cases need a build that can decode: without one the request to measure
 * the peak is refused outright, so nothing computes a headroom figure and there
 * is no behaviour here to check. That is asked of the library rather than of a
 * configuration macro, since the refusal is what a caller actually meets.
 *
 * The loud case is the control for the silent one: it shows the figures do
 * move, without which a zero would equally be the signature of a measurement
 * that never ran.
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
 * @brief Encodes one buffer repeatedly with the peak measurement on, then
 *        flushes, which is what computes the figures under test.
 * @param left,right the block of samples to encode, repeated #CALLS times.
 * @return the encoder instance, still open, for the caller to interrogate, or
 *         NULL where this build refuses to measure the peak at all.
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
 * @brief Silence reports no clipping and no scaling, and nothing undefined.
 *
 * Before the guard this computed the logarithm of zero and converted the
 * infinity that comes back to an int, which the C standard leaves undefined;
 * what it produced in practice was the most negative int there is, reported to
 * the caller as a headroom of some 214 million decibels.
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
 * @brief Material that is not silent moves the figures.
 *
 * The control for the case above: it shows the measurement runs and writes,
 * so a zero there is the answer for silence rather than the signature of a
 * peak that was never taken.
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
