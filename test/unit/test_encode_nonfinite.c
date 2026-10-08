/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the rejection of non-finite PCM input (libmp3lame/lame.c).
 *
 * The floating point encode functions reject a buffer that contains a NaN or
 * an infinity. They return #LAME_BADINPUTDATA. The tests cover:
 * - each of these functions,
 * - both channels,
 * - a non-finite sample at the start and at the end of the buffer.
 *
 * The sample at the end catches a check that stops after the first sample.
 *
 * The functions must still accept the values at the documented limits: full
 * scale (+/- 1, or +/- 32768 for the variant scaled like short int), the
 * smallest denormal, and negative zero. The check reads the exponent bits. A
 * test that only passes NaN does not notice if the check also rejects normal
 * audio. The integer encode functions cannot express a non-finite value. They
 * must still encode normally.
 *
 * This file does not test input far outside the documented range. A separate
 * check in the library rejects it: a sample louder than 4096 times full scale
 * also returns #LAME_BADINPUTDATA (see lame_encode_buffer()).
 *
 * The tests build the bit patterns by hand and do not use the @c NAN and
 * @c INFINITY macros. Each value also passes through a volatile object on its
 * way to the buffer. The tests use the same fast floating point math as the
 * library. With it, the compiler can remove a constant that it knows is NaN or
 * infinite, before the encoder sees it. A test that cannot pass a non-finite
 * sample to the encoder proves nothing about the check.
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API directly. No frontend translation unit is compiled in.
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

#include "test_unused.h"

#include "lame.h"

/** @brief Samples per channel passed to the encoder in one call. */
#define NSAMPLES 4608
/** @brief Size of the output buffer, from the worst case in lame.h. */
#define MP3BUF_SIZE (NSAMPLES * 5 / 4 + 7200)

/** @brief Returns the float with bit pattern @p bits. The compiler cannot fold it. */
static float
float_from_bits(uint32_t bits)
{
    uint32_t volatile opaque = bits;
    uint32_t pattern;
    float   f;

    pattern = opaque;
    memcpy(&f, &pattern, sizeof f);
    return f;
}

/** @brief Returns the double with bit pattern @p bits. The compiler cannot fold it. */
static double
double_from_bits(uint64_t bits)
{
    uint64_t volatile opaque = bits;
    uint64_t pattern;
    double  d;

    pattern = opaque;
    memcpy(&d, &pattern, sizeof d);
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
 * @brief Creates an encoder instance for the given channel count.
 * @param channels number of input channels.
 * @return an initialized encoder instance. The test fails if the setup fails.
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

/** @brief Fills a float buffer with a finite ramp from -0.25 to +0.25 full scale. */
static void
fill_valid_float(float *buf, int n)
{
    int     i;

    for (i = 0; i < n; i++) {
        buf[i] = 0.25f * (float) ((i % 200) - 100) / 100.0f;
    }
}

/** @brief Does the same as fill_valid_float(), for a double buffer. */
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
 * @brief Checks that valid input is encoded. So the check does not reject
 *        everything.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_valid_is_encoded(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    assert_true(lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief Encodes valid stereo input with one sample replaced, through
 *        lame_encode_buffer_ieee_float().
 * @param channel  the channel of the sample: 0 for the left, 1 for the right.
 * @param index    the index of the sample.
 * @param bad      the value that replaces it.
 * @return the result of the encode call.
 */
static int
ieee_float_encode_with(int channel, int index, float bad)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     rc;

    fill_valid_float(l, NSAMPLES);
    fill_valid_float(r, NSAMPLES);
    if (channel == 0)
        l[index] = bad;
    else
        r[index] = bad;
    rc = lame_encode_buffer_ieee_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3);
    lame_close(gfp);
    return rc;
}

/**
 * @brief Checks that a NaN in the left channel is rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_left(LAME_UNUSED void **state)
{
    assert_int_equal(ieee_float_encode_with(0, 0, float_nan()), LAME_BADINPUTDATA);
}

/**
 * @brief Checks that a NaN in the right channel is also rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_right(LAME_UNUSED void **state)
{
    assert_int_equal(ieee_float_encode_with(1, 0, float_nan()), LAME_BADINPUTDATA);
}

/**
 * @brief Checks that a NaN in the last sample is found. So the check reads the
 *        whole buffer.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_nan_last_sample(LAME_UNUSED void **state)
{
    assert_int_equal(ieee_float_encode_with(0, NSAMPLES - 1, float_nan()), LAME_BADINPUTDATA);
}

/**
 * @brief Checks that both infinities are rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_infinities(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

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
 * @brief Checks that values at and near the documented full scale limit of
 *        +/- 1 are accepted.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_boundary_accepted(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

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
 * @brief Checks that the documented limit of +/- 32768 is accepted by the
 *        variant scaled like short int.
 * @param state cmocka fixture state (unused).
 */
static void
test_buffer_float_boundary_accepted(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (i & 1) ? 32768.0f : -32768.0f;
        r[i] = -l[i];
    }
    assert_true(lame_encode_buffer_float(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief Checks that the documented full scale limit of +/- 1 is accepted by
 *        the double variant.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_double_boundary_accepted(LAME_UNUSED void **state)
{
    static double l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    for (i = 0; i < NSAMPLES; i++) {
        l[i] = (i & 1) ? 1.0 : -1.0;
        r[i] = -l[i];
    }
    assert_true(lame_encode_buffer_ieee_double(gfp, l, r, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a rejected buffer does not leave the encoder instance in
 *        a broken state.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_recovers_after_rejection(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

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
 * @brief Checks that a NaN in the single buffer of a mono encode is rejected.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_float_mono_nan(LAME_UNUSED void **state)
{
    static float l[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);

    fill_valid_float(l, NSAMPLES);
    l[5] = float_nan();
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, l, NULL, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/* --- the other floating point entry points ------------------------------ */

/**
 * @brief Checks that lame_encode_buffer_float(), which uses the short int
 *        range, rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_buffer_float_nan(LAME_UNUSED void **state)
{
    static float l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

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
 * @brief Checks that the interleaved float function rejects a NaN in the
 *        right channel slot.
 * @param state cmocka fixture state (unused).
 */
static void
test_interleaved_ieee_float_nan(LAME_UNUSED void **state)
{
    static float pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    fill_valid_float(pcm, 2 * NSAMPLES);
    pcm[1] = float_nan(); /* right channel of the first frame */
    assert_int_equal(lame_encode_buffer_interleaved_ieee_float(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Checks that the double function rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_ieee_double_nan(LAME_UNUSED void **state)
{
    static double l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    fill_valid_double(l, NSAMPLES);
    fill_valid_double(r, NSAMPLES);
    r[9] = double_nan();
    assert_int_equal(lame_encode_buffer_ieee_double(gfp, l, r, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Checks that the interleaved double function rejects a NaN.
 * @param state cmocka fixture state (unused).
 */
static void
test_interleaved_ieee_double_nan(LAME_UNUSED void **state)
{
    static double pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);

    fill_valid_double(pcm, 2 * NSAMPLES);
    pcm[2] = double_nan();
    assert_int_equal(lame_encode_buffer_interleaved_ieee_double(gfp, pcm, NSAMPLES, mp3,
                                                                sizeof mp3), LAME_BADINPUTDATA);
    lame_close(gfp);
}

/* --- the integer entry points are untouched ----------------------------- */

/**
 * @brief Checks that integer input, which cannot be non-finite, is still
 *        encoded.
 * @param state cmocka fixture state (unused).
 */
static void
test_short_int_still_encodes(LAME_UNUSED void **state)
{
    static short int l[NSAMPLES], r[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

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
