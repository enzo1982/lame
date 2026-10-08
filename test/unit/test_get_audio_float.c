/**
 * @file
 * @ingroup unit_tests
 * @brief Tests what the frontend passes on from a floating point input file
 *        (@c get_audio_float(), @c frontend/get_audio.c).
 *
 * The frontend reads a floating point file as floats. It passes them to the
 * library unchanged. @c lame_encode_buffer_ieee_float() rejects the samples
 * that it cannot encode. These tests check the following:
 * - The samples arrive bit for bit, whatever their value. This is checked for
 *   a little-endian WAVE file, a big-endian AIFF-C file, and a WAVE file read
 *   with @c --swap-bytes.
 * - The integer reader rejects such a file.
 * - The count of samples above full scale uses the same scaling as the
 *   encoder.
 * - The 16 bit conversion that @c --decode writes matches the integer path
 *   for every 16 bit value.
 *
 * The helpers of the reader are static. So the test compiles @c get_audio.c
 * directly, as @c test_get_audio_wav.c does. The file tests use the reader of
 * the frontend itself.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* The reader is independent of the bundled MP3 decoder, so compile
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

#include "test_bytes.h"

/* the code under test (pulls in the static helpers) */
#include "get_audio.c"

/**
 * @brief The largest 32-bit float below 1.0.
 *
 * It is written as the arithmetic that gives the value, not as a hexadecimal
 * constant. So the meaning is clear, and the value is the same on any host
 * with IEEE floats.
 */
#define JUST_BELOW_ONE (1.0f - 1.0f / 16777216.0f)

/* --- the 16 bit conversion --decode writes ------------------------------ */

/**
 * @brief Checks the conversion of silence and of half scale, with both signs.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_16bit_zero_and_half_scale(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_16bit(0.0f), 0);
    assert_int_equal(float_sample_to_16bit(0.5f), 16384);
    assert_int_equal(float_sample_to_16bit(-0.5f), -16384);
}

/**
 * @brief Checks that a 16 bit sample written as a float converts back to the
 *        same value.
 *
 * @c s/32768 is exact in a 32 bit float. So for a floating point file made
 * from a 16 bit file, @c --decode writes the original 16 bit samples. The
 * loop checks all 65536 values. @c s = -32768 gives exactly -1.0.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_16bit_ladder_comes_back(void **state)
{
    int     s, checked = 0, bad = 0, first_bad = 0;
    (void) state;
    for (s = -32768; s <= 32767; ++s) {
        int const got = float_sample_to_16bit((float) s / 32768.0f);
        ++checked;
        if (got != s && bad++ == 0)
            first_bad = s;
    }
    /* The count is asserted as well as the comparison: a loop that never ran
       would agree with itself about nothing. */
    assert_int_equal(checked, 65536);
    if (bad != 0)
        fail_msg("%d of 65536 samples disagree, the first at s=%d", bad, first_bad);
}

/**
 * @brief Checks that full scale and above give the largest or smallest
 *        16 bit value.
 *
 * The value just below 1.0 does not wrap around.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_16bit_full_scale_and_beyond(void **state)
{
    (void) state;
    assert_int_equal(float_sample_to_16bit(1.0f), SHRT_MAX);
    assert_int_equal(float_sample_to_16bit(-1.0f), SHRT_MIN);
    assert_int_equal(float_sample_to_16bit(1.5f), SHRT_MAX);
    assert_int_equal(float_sample_to_16bit(-1000.0f), SHRT_MIN);
    assert_int_equal(float_sample_to_16bit(JUST_BELOW_ONE), SHRT_MAX);
}

/* --- the count of samples beyond full scale ----------------------------- */

/**
 * @brief Checks that a sample above full scale is counted, in either channel.
 *
 * A sample at full scale or within full scale is not counted.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_count_unscaled(void **state)
{
    float const l[6] = { 0.5f, 1.0f, -1.0f, 1.5f, -1.5f, JUST_BELOW_ONE };
    float const r[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f };
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 6), 3);
    lame_close(gfp);
}

/**
 * @brief Checks that the count uses the overall scaling factor.
 *
 * A sample that the scaling brings within full scale is not counted. A sample
 * that stays above full scale is counted.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_count_follows_scale(void **state)
{
    float const l[3] = { 1.5f, 2.5f, -2.5f };
    float const r[3] = { 0.0f, 0.0f, 0.0f };
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 3), 3);
    assert_int_equal(lame_set_scale(gfp, 0.5f), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 3), 2);
    lame_close(gfp);
}

/**
 * @brief Checks that a per-channel scaling factor applies to its own channel
 *        only.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_count_follows_channel_scale(void **state)
{
    float const l[1] = { 1.5f };
    float const r[1] = { 1.5f };
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_scale_left(gfp, 0.5f), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 1), 1);
    lame_close(gfp);
}

/**
 * @brief Checks that two channels mixed into one count as one sample.
 *
 * The count uses the mixed sample, not the two input samples.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_count_follows_downmix(void **state)
{
    float const l[2] = { 1.5f, 1.5f };
    float const r[2] = { -1.5f, 1.5f };
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_mode(gfp, MONO), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 2), 1);
    lame_close(gfp);
}

/**
 * @brief Checks that mono input counts its one channel only.
 *
 * The function does not read the second buffer.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_count_mono_input(void **state)
{
    float const l[2] = { 1.5f, -1.5f };
    float const r[2] = { 5.0f, 5.0f };
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 1), 0);
    assert_int_equal(count_above_full_scale(gfp, l, r, 2), 2);
    lame_close(gfp);
}

/* --- a whole file through the reader ------------------------------------ */

/**
 * @brief Writes a 32 bit float WAVE file to a temporary file.
 *
 * @param channels  1 or 2.
 * @param samples   the IEEE-754 bit patterns of the samples, interleaved.
 * @param n         the total number of samples.
 * @param reversed  nonzero to store the bytes of each sample most significant
 *                  first. @c --swap-bytes reads them in this order.
 * @return the file, positioned at its start.
 */
static FILE *
float_wave(int channels, const uint32_t *samples, int n, int reversed)
{
    unsigned char h[44];
    int     i;
    FILE   *f = tmpfile();

    assert_non_null(f);
    memcpy(h, "RIFF", 4);
    put_le(h + 4, (uint32_t) (36 + 4 * n), 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, 3, 2);                   /* IEEE float */
    put_le(h + 22, (uint32_t) channels, 2);
    put_le(h + 24, 44100, 4);
    put_le(h + 28, (uint32_t) (44100 * 4 * channels), 4);
    put_le(h + 32, (uint32_t) (4 * channels), 2);
    put_le(h + 34, 32, 2);
    memcpy(h + 36, "data", 4);
    put_le(h + 40, (uint32_t) (4 * n), 4);
    assert_int_equal(fwrite(h, 1, sizeof h, f), sizeof h);
    for (i = 0; i < n; ++i) {
        unsigned char s[4];
        if (reversed)
            put_be(s, samples[i], 4);
        else
            put_le(s, samples[i], 4);
        assert_int_equal(fwrite(s, 1, 4, f), 4);
    }
    rewind(f);
    return f;
}

/**
 * @brief Writes a stereo 32 bit float AIFF-C file to a temporary file.
 *
 * The samples are stored most significant byte first, as the format requires.
 *
 * @param samples  the IEEE-754 bit patterns of the samples, interleaved.
 * @param n        the total number of samples. It must be even.
 * @return the file, positioned at its start.
 */
static FILE *
float_aiff(const uint32_t *samples, int n)
{
    static const unsigned char rate_44100[10] = {
        0x40, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char h[54];
    int     i;
    FILE   *f = tmpfile();

    assert_non_null(f);
    memcpy(h, "FORM", 4);
    put_be(h + 4, (uint32_t) (50 + 4 * n), 4);   /* all that follows the size */
    memcpy(h + 8, "AIFCCOMM", 8);
    put_be(h + 16, 22, 4);                  /* COMM, with the compression type */
    put_be(h + 20, 2, 2);                   /* stereo */
    put_be(h + 22, (uint32_t) (n / 2), 4);  /* sample frames */
    put_be(h + 26, 32, 2);
    memcpy(h + 28, rate_44100, sizeof rate_44100);
    memcpy(h + 38, "fl32", 4);
    memcpy(h + 42, "SSND", 4);
    put_be(h + 46, (uint32_t) (8 + 4 * n), 4);
    put_be(h + 50, 0, 4);                   /* offset; blockSize follows */
    assert_int_equal(fwrite(h, 1, sizeof h, f), sizeof h);
    {
        unsigned char block_size[4] = { 0, 0, 0, 0 };
        assert_int_equal(fwrite(block_size, 1, 4, f), 4);
    }
    for (i = 0; i < n; ++i) {
        unsigned char s[4];
        put_be(s, samples[i], 4);
        assert_int_equal(fwrite(s, 1, 4, f), 4);
    }
    rewind(f);
    return f;
}

/**
 * @brief Sets @p f as the input of the reader, and initializes the encoder
 *        instance for it.
 *
 * The reader is left in the same state as @c init_infile() leaves it for each
 * input file.
 *
 * @param gfp        the encoder instance.
 * @param f          the input file.
 * @param swapbytes  the @c --swap-bytes setting.
 */
static void
open_reader(lame_t gfp, FILE *f, int swapbytes)
{
    memset(&global, 0, sizeof global);
    global_reader.swapbytes = swapbytes;
    global.pcmswapbytes = swapbytes;
    global_reader.input_format = parse_file_header(gfp, f);
    assert_true(global_reader.input_format == sf_wave
                || global_reader.input_format == sf_aiff);
    global.music_in = f;
    initPcmBuffer(&global.pcm32, sizeof(int));
    initPcmBuffer(&global.pcm16, sizeof(short));
    initPcmBuffer(&global.pcmf, sizeof(float));
    assert_int_equal(lame_init_params(gfp), 0);
}

/**
 * @brief Closes what ::open_reader() opened, and closes the encoder instance.
 *
 * @param gfp  the encoder instance.
 */
static void
close_reader(lame_t gfp)
{
    freePcmBuffer(&global.pcm32);
    freePcmBuffer(&global.pcm16);
    freePcmBuffer(&global.pcmf);
    fclose(global.music_in);
    global.music_in = NULL;
    global_reader.swapbytes = 0;
    lame_close(gfp);
}

/** @brief Samples that the library rejects, or that must arrive unchanged.
 *
 * They are: half scale, above full scale with both signs, a quiet NaN, an
 * infinity, 5000.0 and the smallest denormal. */
static const uint32_t odd_samples[7] = {
    0x3F000000u, 0x3FC00000u, 0xBFC00000u, 0x7FC00000u, 0x7F800000u, 0x459C4000u, 0x00000001u
};

/**
 * @brief Compares the samples from the reader with the bit patterns that were
 *        written.
 *
 * @param got   the samples of one channel, as the reader returned them.
 * @param want  the bit patterns.
 * @param n     the number of samples.
 * @param step  the distance between two samples of one channel in @p want.
 */
static void
assert_bits(const float *got, const uint32_t *want, int n, int step)
{
    int     i;
    for (i = 0; i < n; ++i) {
        uint32_t bits;
        memcpy(&bits, got + i, sizeof bits);
        if (bits != want[i * step])
            fail_msg("sample %d is %08x, written %08x", i, (unsigned) bits, (unsigned) want[i * step]);
    }
}

/**
 * @brief Checks that the samples of a little-endian WAVE file arrive bit for
 *        bit, whatever their value.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_wave_samples_arrive_unchanged(void **state)
{
    float   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, odd_samples, 7, 0), 0);
    assert_true(input_is_float());
    assert_int_equal(get_audio_float(gfp, buffer), 7);
    assert_bits(buffer[0], odd_samples, 7, 1);
    close_reader(gfp);
}

/**
 * @brief Checks that the samples of a big-endian AIFF-C file arrive bit for
 *        bit, each in its own channel.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_aiff_samples_arrive_unchanged(void **state)
{
    static const uint32_t samples[8] = {
        0x3F000000u, 0xBF000000u, 0x3FC00000u, 0x3E800000u,
        0x7FC00000u, 0x00000001u, 0xBE800000u, 0x459C4000u
    };
    float   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_aiff(samples, 8), 0);
    assert_true(input_is_float());
    assert_int_equal(get_audio_float(gfp, buffer), 4);
    assert_bits(buffer[0], samples, 4, 2);
    assert_bits(buffer[1], samples + 1, 4, 2);
    close_reader(gfp);
}

/**
 * @brief Checks that with @c --swap-bytes, the samples arrive bit for bit
 *        from a WAVE file that stores them most significant byte first.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_swapped_wave_arrives_unchanged(void **state)
{
    float   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, odd_samples, 7, 1), 1);
    assert_int_equal(get_audio_float(gfp, buffer), 7);
    assert_bits(buffer[0], odd_samples, 7, 1);
    close_reader(gfp);
}

/**
 * @brief Checks that the same file without @c --swap-bytes gives different
 *        samples.
 *
 * This shows that the test above depends on the byte order.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_swapped_wave_differs_unswapped(void **state)
{
    float   buffer[2][1152];
    uint32_t bits;
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, odd_samples, 7, 1), 0);
    assert_int_equal(get_audio_float(gfp, buffer), 7);
    memcpy(&bits, &buffer[0][0], sizeof bits);
    assert_int_equal(bits, 0x0000003Fu);
    close_reader(gfp);
}

/**
 * @brief Checks that @c samples_above_full_scale() returns the count of the
 *        reader.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_reader_counts_above_full_scale(void **state)
{
    static const uint32_t samples[4] = { 0x3F000000u, 0x3FC00000u, 0xBFC00000u, 0x3F800000u };
    float   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, samples, 4, 0), 0);
    assert_int_equal(get_audio_float(gfp, buffer), 4);
    assert_int_equal(samples_above_full_scale(), 2);
    close_reader(gfp);
}

/**
 * @brief Checks that the integer reader rejects a floating point file.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_integer_reader_refuses_float_file(void **state)
{
    static const uint32_t samples[2] = { 0x3F000000u, 0xBF000000u };
    int     buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, samples, 2, 0), 0);
    assert_true(get_audio(gfp, buffer) < 0);
    close_reader(gfp);
}

/**
 * @brief Checks that the 16 bit reader that @c --decode uses converts finite
 *        samples.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_decode_reader_converts(void **state)
{
    static const uint32_t samples[3] = { 0x3F000000u, 0xBF000000u, 0x3FC00000u };
    short   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, samples, 3, 0), 0);
    assert_int_equal(get_audio16(gfp, buffer), 3);
    assert_int_equal(buffer[0][0], 16384);
    assert_int_equal(buffer[0][1], -16384);
    assert_int_equal(buffer[0][2], SHRT_MAX);
    close_reader(gfp);
}

/**
 * @brief Checks that the 16 bit reader that @c --decode uses rejects a sample
 *        that is not a number.
 *
 * A NaN has no 16 bit value.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_decode_reader_refuses_nan(void **state)
{
    static const uint32_t samples[3] = { 0x3F000000u, 0x7FC00000u, 0xBF000000u };
    short   buffer[2][1152];
    lame_t  gfp = lame_init();
    (void) state;
    assert_non_null(gfp);
    open_reader(gfp, float_wave(1, samples, 3, 0), 0);
    assert_true(get_audio16(gfp, buffer) < 0);
    close_reader(gfp);
}

/** @brief Registers the tests of the floating point reader and runs them. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_16bit_zero_and_half_scale),
        cmocka_unit_test(test_16bit_ladder_comes_back),
        cmocka_unit_test(test_16bit_full_scale_and_beyond),
        cmocka_unit_test(test_count_unscaled),
        cmocka_unit_test(test_count_follows_scale),
        cmocka_unit_test(test_count_follows_channel_scale),
        cmocka_unit_test(test_count_follows_downmix),
        cmocka_unit_test(test_count_mono_input),
        cmocka_unit_test(test_wave_samples_arrive_unchanged),
        cmocka_unit_test(test_aiff_samples_arrive_unchanged),
        cmocka_unit_test(test_swapped_wave_arrives_unchanged),
        cmocka_unit_test(test_swapped_wave_differs_unswapped),
        cmocka_unit_test(test_reader_counts_above_full_scale),
        cmocka_unit_test(test_integer_reader_refuses_float_file),
        cmocka_unit_test(test_decode_reader_converts),
        cmocka_unit_test(test_decode_reader_refuses_nan),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
