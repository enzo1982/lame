/**
 * @file
 * @ingroup unit_tests
 * @brief What a floating point sample becomes as an integer sample
 *        (@c float_sample_to_int(), @c frontend/get_audio.c).
 *
 * The frontend reads floating point audio through two readers, its own and
 * libsndfile's, and both convert through @c float_sample_to_int(). These tests
 * cover the full-scale mapping, that two levels stay two levels, all 65536
 * 16 bit values against what the integer path produces, the clamp at and
 * beyond +/-1.0, and the sample just below full scale that must not be
 * clamped.
 *
 * They do not cover a reader that normalises a whole file, which happens
 * before this function is called; the end-to-end comparison of the two builds
 * in the release harness covers that.
 *
 * @c float_sample_to_int() is static, so the reader is compiled directly into
 * the test, the same arrangement @c test_get_audio_wav.c uses. The assertions
 * hold in either @c --with-fileio build.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* The conversion is independent of the bundled MP3 decoder, so compile
   get_audio.c's core reader without those code paths - see the note in
   test_get_audio_aiff.c, which does the same for the same reason. */
#undef HAVE_MPG123

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

/* the code under test (pulls in the static float_sample_to_int) */
#include "get_audio.c"

/** @brief The integer a sample of 1.0 would reach if it were not clamped. */
#define FULL_SCALE 2147483648.0

/** @brief Half scale, the value 0.5 must convert to. */
#define HALF_SCALE 1073741824

/**
 * @brief The largest 32-bit float below 1.0, which must not be clamped.
 *
 * Written as the arithmetic that produces it rather than as a hexadecimal
 * constant, so the test says what it means on any host whose floats are IEEE.
 */
#define JUST_BELOW_ONE (1.0f - 1.0f / 16777216.0f)

/* --- the mapping ------------------------------------------------------- */

/**
 * @brief Silence converts to silence.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_zero_is_zero(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_int(0.0f), 0);
}

/**
 * @brief Half scale in, half scale out, both signs.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_half_scale(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_int(0.5f), HALF_SCALE);
    assert_int_equal(float_sample_to_int(-0.5f), -HALF_SCALE);
}

/**
 * @brief Two levels 12 dB apart stay 12 dB apart.
 *
 * A conversion that divided by a file-wide factor would answer the same for
 * both and still pass every single-value assertion here.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_levels_are_preserved(void **state)
{
    int const loud = float_sample_to_int(0.5f);
    int const quiet = float_sample_to_int(0.125f);
    (void) state;
    assert_int_equal(loud, HALF_SCALE);
    assert_int_equal(quiet, HALF_SCALE / 4);
    assert_int_equal(loud, quiet * 4);
}

/**
 * @brief A 16 bit sample and its floating point spelling become one integer.
 *
 * The frontend's integer path puts a 16 bit sample in the top half of an
 * @c int, which is @c s * 65536. The float path is handed @c s/32768 and
 * multiplies by 2^31, the same number when the arithmetic is exact. It is:
 * @c s/32768 is exact in a 32 bit float, and @c s = -32768 arrives as exactly
 * -1.0, where the clamp produces @c INT_MIN, which is @c -32768 << 16. The
 * loop checks all 65536 values.
 *
 * A file's bit depth therefore cannot change what the encoder is given, only
 * its content can: one recording written both as 16 bit and as 32 bit float
 * encodes to byte-identical MP3s.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_16bit_ladder_matches_the_integer_path(void **state)
{
    int     s, checked = 0, bad = 0, first_bad = 0;
    (void) state;
    for (s = -32768; s <= 32767; ++s) {
        int const want = (int) ((unsigned int) (s & 0xffff) << 16);
        int const got = float_sample_to_int((ieee754_float32_t) s / 32768.0f);
        ++checked;
        if (got != want && bad++ == 0)
            first_bad = s;
    }
    /* The count is asserted as well as the comparison: a loop that never ran
       would agree with itself about nothing. */
    assert_int_equal(checked, 65536);
    if (bad != 0)
        fail_msg("%d of 65536 samples disagree, the first at s=%d", bad, first_bad);
}

/* --- the clamp --------------------------------------------------------- */

/**
 * @brief Full scale itself is the first clamped value, both signs.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_full_scale_clamps(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_int(1.0f), INT_MAX);
    assert_int_equal(float_sample_to_int(-1.0f), INT_MIN);
}

/**
 * @brief Beyond full scale is flattened onto it, not wrapped.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_over_range_clamps(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_int(1.5f), INT_MAX);
    assert_int_equal(float_sample_to_int(-1.5f), INT_MIN);
    assert_int_equal(float_sample_to_int(1000.0f), INT_MAX);
    assert_int_equal(float_sample_to_int(-1000.0f), INT_MIN);
}

/**
 * @brief The sample just below full scale is converted, not clamped.
 *
 * The clamp has to start at 1.0 and not one representable value earlier, or
 * every peak-normalised file would lose its loudest sample to it.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_just_below_full_scale_is_not_clamped(void **state)
{
    int const v = float_sample_to_int(JUST_BELOW_ONE);
    (void) state;
    assert_true(v < INT_MAX);
    assert_true(v > INT_MAX - 1024);
    assert_int_equal(float_sample_to_int(-JUST_BELOW_ONE), -v);
}

/**
 * @brief A sample far below the integer resolution still converts.
 *
 * Not a rounding assertion: small inputs must stay small and keep their sign
 * rather than saturate.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_small_samples(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_int(1.0f / (float) FULL_SCALE), 1);
    assert_int_equal(float_sample_to_int(-1.0f / (float) FULL_SCALE), -1);
}

/**
 * @brief The count rises for clipped samples and for nothing else.
 *
 * Both directions are asserted from a known starting point: a count that rose
 * on every sample would pass a test that only checked it rises.
 *
 * At the boundary, a sample of exactly full scale is clamped but loses nothing
 * - -1.0 reaches @c INT_MIN either way. @c -32768/32768 is exactly -1.0, so
 * every 16 bit recording that touches full scale becomes a floating point file
 * full of them, and counting those would report an intact file as clipped.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_clipped_samples_are_counted(void **state)
{
    (void) state;
    global.num_samples_clipped = 0;
    float_sample_to_int(0.0f);
    float_sample_to_int(0.5f);
    float_sample_to_int(-JUST_BELOW_ONE);
    assert_int_equal(samples_clipped_on_input(), 0);

    /* Exactly full scale, both signs: clamped, not counted. */
    assert_int_equal(float_sample_to_int(1.0f), INT_MAX);
    assert_int_equal(float_sample_to_int(-1.0f), INT_MIN);
    assert_int_equal(samples_clipped_on_input(), 0);

    /* Strictly beyond, both signs: clamped and counted. */
    float_sample_to_int(1.5f);
    float_sample_to_int(-1.5f);
    assert_int_equal(samples_clipped_on_input(), 2);
}

/** @brief Registers and runs the float-conversion test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_zero_is_zero),
        cmocka_unit_test(test_half_scale),
        cmocka_unit_test(test_levels_are_preserved),
        cmocka_unit_test(test_16bit_ladder_matches_the_integer_path),
        cmocka_unit_test(test_full_scale_clamps),
        cmocka_unit_test(test_over_range_clamps),
        cmocka_unit_test(test_just_below_full_scale_is_not_clamped),
        cmocka_unit_test(test_small_samples),
        cmocka_unit_test(test_clipped_samples_are_counted),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
