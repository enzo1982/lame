/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for lame_init_params() when the decoder that measures the
 *        encoded output cannot start (libmp3lame/lame.c).
 *
 * The test compiles lame.c in, with a hip_decode_init() that fails when the
 * test asks it to. The rest of the library comes from libmp3lame as usual.
 *
 * A failed lame_init_params() must leave the caller's settings as they were,
 * and a second call must give the encoder that a fresh instance gets: the same
 * MP3 bytes, at the same sample rate and at another one.
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

#ifdef HAVE_MPG123
/** The length of the test signal, in samples per channel. */
#define SIGNAL_SAMPLES 44100
/** Room for the MP3 bytes of the test signal at the highest bitrate. */
#define MP3_BYTES (SIGNAL_SAMPLES * 2 + 7200)

/**
 * @brief Prepares an encoder that decodes its own output: joint stereo, CBR
 *        128 kbit/s or VBR quality 2.
 * @param rate  the input sample rate.
 * @param vbr   1 for VBR, 0 for CBR.
 * @return the instance.
 */
static lame_t
prepare(int rate, int vbr)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, rate), 0);
    assert_int_equal(lame_set_decode_on_the_fly(gfp, 1), 0);
    assert_int_equal(lame_set_mode(gfp, JOINT_STEREO), 0);
    if (vbr) {
        assert_int_equal(lame_set_VBR(gfp, vbr_mtrh), 0);
        assert_int_equal(lame_set_VBR_q(gfp, 2), 0);
    }
    else
        assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 0), 0);
    return gfp;
}

/**
 * @brief Encodes one second of a fixed stereo signal with an initialized
 *        instance, and closes it.
 * @param gfp  the instance.
 * @param mp3  receives the MP3 bytes, at least @c MP3_BYTES.
 * @return the number of MP3 bytes.
 */
static int
encode_and_close(lame_t gfp, unsigned char *mp3)
{
    static short l[SIGNAL_SAMPLES], r[SIGNAL_SAMPLES];
    int     i, n, k;

    for (i = 0; i < SIGNAL_SAMPLES; i++) {
        l[i] = (short) (8000 * sin(2 * PI * 440.0 * i / 44100) + 2000 * sin(2 * PI * 3100.0 * i / 44100));
        r[i] = (short) (7000 * sin(2 * PI * 440.0 * i / 44100 + 0.3) - 2000 * sin(2 * PI * 3100.0 * i / 44100));
    }
    n = lame_encode_buffer(gfp, l, r, SIGNAL_SAMPLES, mp3, MP3_BYTES);
    assert_true(n >= 0);
    k = lame_encode_flush(gfp, mp3 + n, MP3_BYTES - n);
    assert_true(k >= 0);
    (void) lame_close(gfp);
    return n + k;
}

/**
 * @brief Encodes the test signal with a fresh instance at @p rate, then with
 *        an instance whose first lame_init_params() at @p first_rate failed
 *        and whose second one at @p rate succeeded, and compares the bytes.
 * @param first_rate  the input sample rate of the failed call.
 * @param rate        the input sample rate of the encodes.
 * @param vbr         1 for VBR, 0 for CBR.
 */
static void
check_retry_encodes_as_fresh(int first_rate, int rate, int vbr)
{
    static unsigned char fresh[MP3_BYTES], retried[MP3_BYTES];
    lame_t  gfp;
    int     n_fresh, n_retried;

    gfp = prepare(rate, vbr);
    assert_int_equal(lame_init_params(gfp), 0);
    n_fresh = encode_and_close(gfp, fresh);

    gfp = prepare(first_rate, vbr);
    fail_next_decoder = 1;
    assert_int_equal(lame_init_params(gfp), -1);
    assert_int_equal(lame_set_in_samplerate(gfp, rate), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    n_retried = encode_and_close(gfp, retried);

    assert_int_equal(n_retried, n_fresh);
    assert_memory_equal(retried, fresh, (size_t) n_fresh);
}
#endif

/**
 * @brief Checks that a failed lame_init_params() leaves the caller's
 *        settings as they were.
 * @param state cmocka fixture state (unused).
 */
static void
test_a_failed_init_keeps_the_settings(LAME_UNUSED void **state)
{
#ifdef HAVE_MPG123
    lame_t  gfp = prepare(44100, 0);
    lame_global_flags before;

    report_reset();
    assert_int_equal(lame_set_errorf(gfp, report_capture), 0);
    before = *gfp;
    fail_next_decoder = 1;
    assert_int_equal(lame_init_params(gfp), -1);
    assert_memory_equal(gfp, &before, sizeof before);
    (void) lame_close(gfp);
#else
    skip();
#endif
}

/**
 * @brief Checks that a retried lame_init_params() at the same sample rate
 *        gives the encoder of a fresh instance (CBR).
 * @param state cmocka fixture state (unused).
 */
static void
test_a_retry_encodes_as_a_fresh_instance(LAME_UNUSED void **state)
{
#ifdef HAVE_MPG123
    check_retry_encodes_as_fresh(44100, 44100, 0);
#else
    skip();
#endif
}

/**
 * @brief Checks that a retried lame_init_params() at another sample rate
 *        builds the tables for that rate (VBR).
 * @param state cmocka fixture state (unused).
 */
static void
test_a_retry_at_another_rate_encodes_as_fresh(LAME_UNUSED void **state)
{
#ifdef HAVE_MPG123
    check_retry_encodes_as_fresh(32000, 44100, 1);
#else
    skip();
#endif
}

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
        cmocka_unit_test(test_a_failed_init_keeps_the_settings),
        cmocka_unit_test(test_a_retry_encodes_as_a_fresh_instance),
        cmocka_unit_test(test_a_retry_at_another_rate_encodes_as_fresh),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
