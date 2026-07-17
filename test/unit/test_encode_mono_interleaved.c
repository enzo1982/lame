/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for rejecting an interleaved call on a mono session
 *        (libmp3lame/lame.c).
 *
 * The interleaved entry points read the left and right channels from one
 * buffer with a stride of two. A session opened with a single input channel
 * has only one channel in that buffer, so the second, strided read runs one
 * channel's worth of samples past its end. The entry points must refuse this
 * combination with #LAME_BADINPUTDATA instead of reading outside the buffer
 * (SourceForge bug #522).
 *
 * The tests size each input buffer to exactly the mono sample count and rely on
 * the address sanitizer, when the suite is built with it, to catch a read past
 * the end; the return-value check stands on its own without it.
 *
 * The non-interleaved entry points on the same mono session must keep working,
 * so a rejection that fired on every mono encode rather than only the
 * interleaved one would be caught here too.
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

/**
 * @brief Opens an encoder with the given input-channel count.
 * @param channels Number of input channels.
 * @return An initialised lame_t; the caller closes it.
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
 * @brief A mono session rejects the short interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_short_rejected(void **state)
{
    static short int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (short int) ((i % 2000) - 1000);
    assert_int_equal(lame_encode_buffer_interleaved(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief A mono session rejects the int interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_int_rejected(void **state)
{
    static int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (i % 2000) - 1000;
    assert_int_equal(lame_encode_buffer_interleaved_int(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief A mono session rejects the float interleaved entry point.
 * @param state cmocka fixture state (unused).
 */
static void
test_mono_interleaved_float_rejected(void **state)
{
    static float pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (float) ((i % 2000) - 1000) / 1000.0f;
    assert_int_equal(lame_encode_buffer_interleaved_ieee_float(gfp, pcm, NSAMPLES, mp3, sizeof mp3),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief The non-interleaved entry point on a mono session still encodes.
 * @param state cmocka fixture state (unused).
 *
 * The rejection must be specific to the interleaved call; a mono session fed
 * through the ordinary entry point continues to work.
 */
static void
test_mono_noninterleaved_still_encodes(void **state)
{
    static short int pcm[NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(1);
    int     i;

    (void) state;
    for (i = 0; i < NSAMPLES; i++)
        pcm[i] = (short int) ((i % 2000) - 1000);
    assert_true(lame_encode_buffer(gfp, pcm, pcm, NSAMPLES, mp3, sizeof mp3) >= 0);
    lame_close(gfp);
}

/**
 * @brief A stereo session on the same interleaved entry point still encodes.
 * @param state cmocka fixture state (unused).
 *
 * The interleaved call is only wrong for a mono session; the documented stereo
 * use must be unaffected.
 */
static void
test_stereo_interleaved_still_encodes(void **state)
{
    static short int pcm[2 * NSAMPLES];
    unsigned char mp3[MP3BUF_SIZE];
    lame_t  gfp = encoder_new(2);
    int     i;

    (void) state;
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
