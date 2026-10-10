/**
 * @file
 * @ingroup unit_tests
 * @brief Tests an input file that ends inside a sample frame
 *        (@c get_audio_common(), @c frontend/get_audio.c).
 *
 * A stereo file whose last bytes hold only the left sample of a frame ends
 * inside that frame. The reader passes on the whole frames before it, with
 * each channel in its place, and reports the cut once. These tests check this
 * for the integer reader (16 bit WAVE) and for the floating point reader
 * (32 bit float WAVE), each against the same file without the cut. The header
 * of each file claims more frames than it holds, as in a file that was cut
 * off while it was written.
 *
 * In every file the left samples are above zero and the right samples below,
 * so a frame whose channels are swapped or shifted shows up as a wrong sign.
 *
 * The helpers of the reader are static. So the test compiles @c get_audio.c
 * directly, as @c test_get_audio_float.c does.
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

#include <stdlib.h>

#include "test_bytes.h"

/* the code under test (pulls in the static helpers) */
#include "get_audio.c"

/** @brief The frames that each test file holds: two whole reads and part of a
 *         third, so that the cut falls into a read of fewer frames. */
#define WHOLE_FRAMES (2 * 1152 + 500)
/** @brief The frames that the header of each test file claims. */
#define CLAIMED_FRAMES (WHOLE_FRAMES + 1000)

/**
 * @brief Writes a stereo WAVE file to a temporary file: @c WHOLE_FRAMES frames
 *        of a positive left and a negative right sample, then, if asked, the
 *        left sample of one more frame.
 *
 * @param bytes_per_sample  2 for 16 bit integer samples, 4 for 32 bit float.
 * @param cut               nonzero to end the file inside a frame.
 * @return the file, positioned at its start.
 */
static FILE *
stereo_wave(int bytes_per_sample, int cut)
{
    unsigned char h[44];
    int const is_float = bytes_per_sample == 4;
    uint32_t const left = is_float ? 0x3E800000u : 0x2000u;  /* 0.25 or 8192 */
    uint32_t const right = is_float ? 0xBE800000u : 0xE000u; /* -0.25 or -8192 */
    int     i;
    FILE   *f = tmpfile();

    assert_non_null(f);
    memcpy(h, "RIFF", 4);
    put_le(h + 4, (uint32_t) (36 + 2 * bytes_per_sample * CLAIMED_FRAMES), 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, is_float ? 3 : 1, 2);    /* IEEE float or PCM */
    put_le(h + 22, 2, 2);
    put_le(h + 24, 44100, 4);
    put_le(h + 28, (uint32_t) (44100 * 2 * bytes_per_sample), 4);
    put_le(h + 32, (uint32_t) (2 * bytes_per_sample), 2);
    put_le(h + 34, (uint32_t) (8 * bytes_per_sample), 2);
    memcpy(h + 36, "data", 4);
    put_le(h + 40, (uint32_t) (2 * bytes_per_sample * CLAIMED_FRAMES), 4);
    assert_int_equal(fwrite(h, 1, sizeof h, f), sizeof h);
    for (i = 0; i < WHOLE_FRAMES; ++i) {
        unsigned char s[8];
        put_le(s, left, bytes_per_sample);
        put_le(s + bytes_per_sample, right, bytes_per_sample);
        assert_int_equal(fwrite(s, 1, 2 * (size_t) bytes_per_sample, f), 2 * bytes_per_sample);
    }
    if (cut) {
        unsigned char s[4];
        put_le(s, left, bytes_per_sample);
        assert_int_equal(fwrite(s, 1, (size_t) bytes_per_sample, f), bytes_per_sample);
    }
    rewind(f);
    return f;
}

/**
 * @brief Sets @p f as the input of the reader, and initializes the encoder
 *        instance for it, as @c init_infile() leaves the reader for each
 *        input file.
 *
 * @param gfp  the encoder instance.
 * @param f    the input file.
 */
static void
open_reader(lame_t gfp, FILE *f)
{
    memset(&global, 0, sizeof global);
    frontend_config.reader.input_format = parse_file_header(gfp, f);
    assert_int_equal(frontend_config.reader.input_format, sf_wave);
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
    lame_close(gfp);
}

/**
 * @brief Reads a 16 bit file to its end through @c get_audio() and checks
 *        every frame and the report of the cut.
 *
 * @param cut  nonzero for the file that ends inside a frame.
 */
static void
check_integer_file(int cut)
{
    int     buffer[2][1152];
    long    frames = 0;
    int     n, i;
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    open_reader(gfp, stereo_wave(2, cut));
    while ((n = get_audio(gfp, buffer)) > 0) {
        for (i = 0; i < n; ++i) {
            if (buffer[0][i] <= 0 || buffer[1][i] >= 0)
                fail_msg("frame %ld: left %d, right %d", frames + i, buffer[0][i], buffer[1][i]);
        }
        frames += n;
    }
    assert_int_equal(n, 0);
    assert_int_equal(frames, WHOLE_FRAMES);
    assert_int_equal(input_ends_inside_a_frame(), cut ? 1 : 0);
    close_reader(gfp);
}

/**
 * @brief The same as ::check_integer_file(), for a 32 bit float file read
 *        through @c get_audio_float().
 *
 * @param cut  nonzero for the file that ends inside a frame.
 */
static void
check_float_file(int cut)
{
    float   buffer[2][1152];
    long    frames = 0;
    int     n, i;
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    open_reader(gfp, stereo_wave(4, cut));
    assert_true(input_is_float());
    while ((n = get_audio_float(gfp, buffer)) > 0) {
        for (i = 0; i < n; ++i) {
            if (!(buffer[0][i] > 0.0f) || !(buffer[1][i] < 0.0f))
                fail_msg("frame %ld: left %g, right %g", frames + i, buffer[0][i], buffer[1][i]);
        }
        frames += n;
    }
    assert_int_equal(n, 0);
    assert_int_equal(frames, WHOLE_FRAMES);
    assert_int_equal(input_ends_inside_a_frame(), cut ? 1 : 0);
    close_reader(gfp);
}

/**
 * @brief The 16 bit file that ends inside a frame.
 * @param state cmocka fixture state (unused).
 */
static void
test_integer_cut_file(void **state)
{
    (void) state;
    check_integer_file(1);
}

/**
 * @brief The 16 bit file without the cut.
 * @param state cmocka fixture state (unused).
 */
static void
test_integer_whole_file(void **state)
{
    (void) state;
    check_integer_file(0);
}

/**
 * @brief The 32 bit float file that ends inside a frame.
 * @param state cmocka fixture state (unused).
 */
static void
test_float_cut_file(void **state)
{
    (void) state;
    check_float_file(1);
}

/**
 * @brief The 32 bit float file without the cut.
 * @param state cmocka fixture state (unused).
 */
static void
test_float_whole_file(void **state)
{
    (void) state;
    check_float_file(0);
}

/** @brief Registers the tests of an input that ends inside a frame and runs them. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_integer_cut_file),
        cmocka_unit_test(test_integer_whole_file),
        cmocka_unit_test(test_float_cut_file),
        cmocka_unit_test(test_float_whole_file),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
