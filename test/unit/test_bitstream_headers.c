/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the frame headers that the encoder buffers before it
 *        writes them (libmp3lame/bitstream.c).
 *
 * The encoder produces the header of a frame when it encodes the frame. It
 * writes the header when the output gets to the position of that header. In
 * between, the header waits in a buffer. The number of waiting headers is the
 * bit reservoir divided by the room that a frame has beyond its own side
 * info. For ordinary settings this number is one or two. At the low end of
 * the format it is in the hundreds. An MPEG-2 frame at 8 kbit/s and 24 kHz is
 * 24 bytes. With two channels and CRC, 23 of these bytes are side info.
 * Silence uses none of the remaining byte. So a silent lead-in fills the
 * reservoir, and one header waits for every eight bits of it.
 *
 * This is ordinary input: a recording that starts with a gap. The first test
 * encodes it. The second test is an everyday encode, which must not be
 * affected. The second test also shows that the result of the first test
 * comes from its settings. It does not come from a test that encoded nothing.
 *
 * Both tests check the same two things. The encoder reports no error. And
 * every byte that it produced belongs to a frame. The second check matters
 * because the failure is silent. The encoder reports once and then continues
 * to write a stream that has lost its framing. A test that only checks a
 * return code sees a clean encode.
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API directly.
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

#include "test_report.h"
#include "test_encode.h"
#include "mp3frame.h"

#include "test_unused.h"

#include "lame.h"

/** Samples in an MPEG-2 layer III frame. */
#define SAMPLES_PER_FRAME 576
/** Frames of digital silence before the audio starts. The value is above 256,
 *  the peak number of header slots that the low-bitrate setting needs. */
#define SILENT_FRAMES     260
/** Frames of ordinary audio after the silence. These frames spend the reservoir. */
#define SIGNAL_FRAMES     40
/** Buffer size for the whole encode, with a large margin. The 128 kbit/s control produces the largest stream. */
#define STREAM_SIZE       (256 * 1024)

/**
 * @brief Returns the length in bytes of the frame whose header starts at
 *        @p h. Returns 0 if that is not a layer III header.
 */
static int
frame_length(unsigned char const *h)
{
    if (!mp3_is_frame_sync(h) || !mp3_is_layer3(h))
        return 0;
    return mp3_frame_length(h);
}

/**
 * @brief Follows the frame chain from the first byte, as a stream parser
 *        does. The length of each frame says where the next header must be.
 * @param mp3,len the encoded stream.
 * @param frames set to the number of frames the chain covers.
 * @return the offset of the first byte that is not where a frame header was
 *         expected, or @p len if the chain covers the whole stream.
 */
static int
walk_frames(unsigned char const *mp3, int len, int *frames)
{
    int     at = 0;

    *frames = 0;
    while (at + 4 <= len) {
        int const n = frame_length(mp3 + at);

        if (n < 4)
            break;
        at += n;
        ++*frames;
    }
    return at;
}

/**
 * @brief Encodes @p silent frames of digital silence and then @p loud frames
 *        of a sawtooth tone. Uses the settings that the caller has made.
 * @return the number of bytes stored in @p mp3.
 */
static int
encode(lame_t gfp, int silent, int loud, unsigned char *mp3, int mp3_size)
{
    static short left[(SILENT_FRAMES + SIGNAL_FRAMES) * SAMPLES_PER_FRAME];
    static short right[(SILENT_FRAMES + SIGNAL_FRAMES) * SAMPLES_PER_FRAME];
    int     i, f, rc;

    assert_true(silent + loud <= SILENT_FRAMES + SIGNAL_FRAMES);
    memset(left, 0, sizeof left);
    memset(right, 0, sizeof right);
    for (f = silent; f < silent + loud; f++) {
        for (i = 0; i < SAMPLES_PER_FRAME; i++) {
            /* a loud sawtooth: it costs bits in every band, which is what
               makes the frame spend from the reservoir the silence filled */
            short const v = (short) (12000 - 48 * (i % 500));

            left[f * SAMPLES_PER_FRAME + i] = v;
            right[f * SAMPLES_PER_FRAME + i] = (short) -v;
        }
    }
    rc = encode_collect(gfp, left, right, SAMPLES_PER_FRAME, silent + loud, SAMPLES_PER_FRAME,
                        mp3, mp3_size);
    assert_true(rc >= 0);
    return rc;
}

/**
 * @brief Creates an encoder instance that reports errors through
 *        report_capture().
 */
static lame_t
new_encoder(void)
{
    lame_t  gfp = lame_init();

    report_reset();
    assert_non_null(gfp);
    assert_int_equal(lame_set_errorf(gfp, report_capture), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 24000), 0);
    /* The tag frame is written by the caller, not by the encoder, so asking
       for one would leave a gap in the stream this test walks. */
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 0), 0);
    return gfp;
}

/**
 * @brief Encodes a silent lead-in and a signal, and checks that the stream is
 *        whole MPEG frames. Closes the encoder.
 * @param gfp  the encoder instance, after lame_init_params().
 *
 * Every byte belongs to a frame, and there are as many frames as were
 * encoded. The second half is what a stream of the right length that carries
 * no headers would fail.
 */
static void
assert_framed(lame_t gfp)
{
    static unsigned char mp3[STREAM_SIZE];
    int     len, walked, frames;

    len = encode(gfp, SILENT_FRAMES, SIGNAL_FRAMES, mp3, (int) sizeof mp3);
    walked = walk_frames(mp3, len, &frames);

    if (report_calls != 0)
        fail_msg("the encoder reported %d message(s): %s", report_calls, report_text);
    assert_int_equal(walked, len);
    assert_true(frames >= SILENT_FRAMES);
    assert_int_equal(lame_close(gfp), 0);
}

/**
 * @brief Checks that a silent lead-in keeps the framing of the file.
 *
 * The settings are MPEG-2 at 8 kbit/s and 24 kHz, two channels, CRC on. A
 * frame then has one byte of room beyond the side info. So up to 255 headers
 * wait at once. The frame that is being encoded needs one more slot. So the
 * 260 silent frames need 256 slots at the peak. A ring of 256 slots is one
 * slot short. With a ring that is too small, the encoder reports an error
 * once and returns success. It then writes about 255 frames of audio with no
 * frame headers.
 */
static void
test_silent_lead_in_keeps_the_framing(LAME_UNUSED void **state)
{
    lame_t  gfp = new_encoder();

    assert_int_equal(lame_set_out_samplerate(gfp, 24000), 0);
    assert_int_equal(lame_set_brate(gfp, 8), 0);
    assert_int_equal(lame_set_mode(gfp, STEREO), 0);
    assert_int_equal(lame_set_error_protection(gfp, 1), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    assert_framed(gfp);
}

/**
 * @brief Checks that an everyday encode keeps its framing.
 *
 * This test is the control. It shows that the checks of assert_framed() pass
 * on a stream that the encoder really produced. So the result of the first test
 * comes from its settings, not from a walk over an empty buffer.
 */
static void
test_ordinary_encode_keeps_the_framing(LAME_UNUSED void **state)
{
    lame_t  gfp = new_encoder();

    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    assert_framed(gfp);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_silent_lead_in_keeps_the_framing),
        cmocka_unit_test(test_ordinary_encode_keeps_the_framing),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
