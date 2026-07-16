/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the rejection of non-finite PCM input (libmp3lame/lame.c).
 *
 * The floating point encoding entry points refuse a buffer containing a NaN or
 * an infinity with #LAME_BADINPUTDATA. These tests cover each of those entry
 * points, both channels, and a non-finite sample at the end of the buffer as
 * well as at the start, so a screen that stopped after the first sample would
 * be caught.
 *
 * The boundaries the entry points document must still be accepted: full scale
 * (+/- 1, or +/- 32768 for the short int scaled variant), the smallest denormal
 * and negative zero. The screen looks at the exponent bits, and a test which
 * only fed it NaN would not notice if it rejected ordinary audio as well. The
 * integer entry points cannot express a non-finite value and must be
 * unaffected.
 *
 * Input far outside the documented range is not covered here: a sample around
 * 1e30 is 1e30 times full scale, and overflows the psycho acoustic model's own
 * arithmetic long after this screen has passed it. That predates the screen and
 * is not what it is for.
 *
 * The bit patterns are assembled by hand rather than taken from the @c NAN and
 * @c INFINITY macros: the library is built with fast floating point maths, and
 * a test compiled the same way could otherwise have its constants folded away
 * before they ever reach the encoder.
 *
 * These are library-level tests: they link libmp3lame and call the exported
 * API directly, so no frontend translation unit is compiled in.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "lame.h"

/** @brief Samples per channel handed to the encoder in one call. */
#define NSAMPLES 4608
/** @brief Size of the output buffer, per the worst case in lame.h. */
#define MP3BUF_SIZE (NSAMPLES * 5 / 4 + 7200)

/** @brief Assembles a float from its IEEE-754 bit pattern, unfoldable by the compiler. */
static float
float_from_bits(uint32_t bits)
{
    float   f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/** @brief Assembles a double from its IEEE-754 bit pattern, unfoldable by the compiler. */
static double
double_from_bits(uint64_t bits)
{
    double  d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float
float_nan(void)
{
    return float_from_bits(0x7FC00000u); /* quiet NaN */
}

static float
float_inf(int negative)
{
    return float_from_bits(negative ? 0xFF800000u : 0x7F800000u);
}

static double
double_nan(void)
{
    return double_from_bits(0x7FF8000000000000ull);
}

/**
 * @brief Creates an encoder configured for the given channel count.
 * @param channels number of input channels.
 * @return an initialized handle; the test fails if setup does not succeed.
 */
static lame_t
encoder_new(int channels)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, channels), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_default), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    return gfp;
}

/** @brief Fills a buffer with a valid, finite ramp scaled to +/- 1 full scale. */
static void
fill_valid_float(float *buf, int n)
{
    int     i;

    for (i = 0; i < n; i++) {
        buf[i] = 0.25f * (float) ((i % 200) - 100) / 100.0f;
    }
}

/** @brief Fills a buffer with a valid, finite ramp scaled to +/- 1 full scale. */
static void
fill_valid_double(double *buf, int n)
{
    int     i;

    for (i = 0; i < n; i++) {
        buf[i] = 0.25 * (double) ((i % 200) - 100) / 100.0;
    }
}

/* --- the scaled 'float' entry point ------------------------------------- */

/**
 * @brief Valid input is encoded, so the screen does not reject everything.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_valid_is_encoded(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    assert_true(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief A NaN in the left channel is rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_left(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    l[0] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief A NaN in the right channel is rejected too, not just the left.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_right(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    r[0] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief A non-finite sample at the very end is found, so the whole buffer is screened.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_last_sample(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    l[NSAMPLES - 1] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Both infinities are rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_infinities(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);

    l[17] = float_inf(0);
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);

    l[17] = float_inf(1);
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief The documented +/- 1 full scale boundary is accepted, at and around it.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_boundary_accepted(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    l[0] = 1.0f;                         /* full scale, the documented boundary */
    l[1] = -1.0f;
    l[2] = float_from_bits(0x3F800001u); /* the next value above full scale */
    l[3] = float_from_bits(0x00000001u); /* smallest denormal */
    l[4] = float_from_bits(0x80000000u); /* negative zero */
    assert_true(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief The documented +/- 32768 boundary of the short int scaled variant is accepted.
 * @param state cmocka fixture state (unused).
 */
static void
test_buffer_float_boundary_accepted(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (i & 1) ? 32768.0f : -32768.0f;
        r[i] = -l[i];
    }
    assert_true(lame_encode_buffer_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief The documented +/- 1 full scale boundary of the double variant is accepted.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_double_boundary_accepted(void **state)
{
    static double l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (i & 1) ? 1.0 : -1.0;
        r[i] = -l[i];
    }
    assert_true(lame_encode_buffer_ieee_double(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief A refused buffer does not leave the encoder in a broken state.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_recovers_after_rejection(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    l[123] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);

    /* same handle, corrected data */
    fill_valid_float(l, NSAMPLES);
    assert_true(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief The mono path screens its single buffer.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_mono_nan(void **state)
{
    static float l[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);

    (void) state;
    fill_valid_float(l, NSAMPLES);
    l[5] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, NULL, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/* --- the other floating point entry points ------------------------------ */

/**
 * @brief lame_encode_buffer_float(), scaled to short int range, rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_buffer_float_nan(void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (float) ((i % 2000) - 1000);
        r[i] = -l[i];
    }
    l[3] = float_nan();
    assert_int_equal(lame_encode_buffer_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief The interleaved float entry point rejects a NaN in either channel slot.
 * @param state cmocka fixture state (unused).
 */
static void
test_interleaved_ieee_float_nan(void **state)
{
    static float pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_float(pcm, 2 * NSAMPLES);
    pcm[1] = float_nan(); /* right channel of the first frame */
    assert_int_equal(lame_encode_buffer_interleaved_ieee_float(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief The double entry point rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_double_nan(void **state)
{
    static double l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_double(l, NSAMPLES);
    fill_valid_double(r, NSAMPLES);
    r[9] = double_nan();
    assert_int_equal(lame_encode_buffer_ieee_double(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief The interleaved double entry point rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_interleaved_ieee_double_nan(void **state)
{
    static double pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    (void) state;
    fill_valid_double(pcm, 2 * NSAMPLES);
    pcm[2] = double_nan();
    assert_int_equal(lame_encode_buffer_interleaved_ieee_double(gfp, pcm, NSAMPLES, mp3,
                                                                sizeof mp3), LAME_BADINPUTDATA);
    lame_close(gfp);
}

/* --- the integer entry points are untouched ----------------------------- */

/**
 * @brief Integer input cannot be non-finite and is still encoded.
 * @param state cmocka fixture state (unused).
 */
static void
test_short_int_still_encodes(void **state)
{
    static short int l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (short int) ((i % 2000) - 1000);
        r[i] = (short int) -l[i];
    }
    assert_true(lame_encode_buffer(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/** @brief Registers and runs the non-finite input test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ieee_float_valid_is_encoded),
        cmocka_unit_test(test_ieee_float_nan_left),
        cmocka_unit_test(test_ieee_float_nan_right),
        cmocka_unit_test(test_ieee_float_nan_last_sample),
        cmocka_unit_test(test_ieee_float_infinities),
        cmocka_unit_test(test_ieee_float_boundary_accepted),
        cmocka_unit_test(test_buffer_float_boundary_accepted),
        cmocka_unit_test(test_ieee_double_boundary_accepted),
        cmocka_unit_test(test_ieee_float_recovers_after_rejection),
        cmocka_unit_test(test_ieee_float_mono_nan),
        cmocka_unit_test(test_buffer_float_nan),
        cmocka_unit_test(test_interleaved_ieee_float_nan),
        cmocka_unit_test(test_ieee_double_nan),
        cmocka_unit_test(test_interleaved_ieee_double_nan),
        cmocka_unit_test(test_short_int_still_encodes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
