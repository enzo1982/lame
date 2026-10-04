/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the rejection of an interleaved call on a mono session
 *        (libmp3lame/lame.c).
 *
 * The interleaved entry points read the left and right channels from one
 * buffer with a stride of two. A session with one input channel has only one
 * channel in that buffer. So the second read runs one channel's worth of
 * samples past the end of the buffer. The entry points must reject this
 * combination with #LAME_BADINPUTDATA. They must not read outside the buffer
 * (SourceForge bug #522).
 *
 * Each input buffer has exactly the mono sample count. When the suite is built
 * with the address sanitizer, the sanitizer catches a read past the end. The
 * check of the return value also works without the sanitizer.
 *
 * The non-interleaved entry points on the same mono session must keep working.
 * So the tests also catch a rejection of every mono encode, when only the
 * interleaved one must fail.
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

/** @brief Samples per channel that the test passes to the encoder in one call. */
#define NSAMPLES 4608
/** @brief Size of the output buffer, per the worst case in lame.h. */
#define MP3BUF_SIZE (NSAMPLES * 5 / 4 + 7200)

/**
 * @brief Creates an encoder instance with the given number of input channels.
 * @param channels Number of input channels.
 * @return An initialized encoder instance. The caller closes it.
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

/**
 * @brief Checks that a mono session rejects the short interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_short_rejected(LAME_UNUSED void **state)
{
    static short int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (short int) ((i % 2000) - 1000);
    assert_int_equal(lame_encode_buffer_interleaved(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Checks that a mono session rejects the int interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_int_rejected(LAME_UNUSED void **state)
{
    static int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (i % 2000) - 1000;
    assert_int_equal(lame_encode_buffer_interleaved_int(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Checks that a mono session rejects the float interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_float_rejected(LAME_UNUSED void **state)
{
    static float pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (float) ((i % 2000) - 1000) / 1000.0f;
    assert_int_equal(lame_encode_buffer_interleaved_ieee_float(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Checks that the non-interleaved entry point on a mono session
 *        encodes.
 * @param state cmocka fixture state (unused).
 *
 * The rejection must apply only to the interleaved call. A mono session that
 * gets its samples through the ordinary entry point must keep working.
 */
static void
test_mono_noninterleaved_still_encodes(LAME_UNUSED void **state)
{
    static short int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (short int) ((i % 2000) - 1000);
    assert_true(lame_encode_buffer(gfp, pcm, pcm, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a stereo session on the same interleaved entry point
 *        encodes.
 * @param state cmocka fixture state (unused).
 *
 * The interleaved call is wrong only for a mono session. The documented
 * stereo use must keep working.
 */
static void
test_stereo_interleaved_still_encodes(LAME_UNUSED void **state)
{
    static short int pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    for (i = 0; i < 2 * NSAMPLES; i++)
        pcm[i] = (short int) ((i % 2000) - 1000);
    assert_true(lame_encode_buffer_interleaved(gfp, pcm, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/** @brief Registers and runs the mono-interleaved rejection test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mono_interleaved_short_rejected),
        cmocka_unit_test(test_mono_interleaved_int_rejected),
        cmocka_unit_test(test_mono_interleaved_float_rejected),
        cmocka_unit_test(test_mono_noninterleaved_still_encodes),
        cmocka_unit_test(test_stereo_interleaved_still_encodes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
