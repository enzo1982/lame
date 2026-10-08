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

#include "test_unused.h"

#include "lame.h"

/** Samples in an MPEG-2 layer III frame. */
#define SAMPLES_PER_FRAME 576
/** Frames of digital silence before the audio starts. The value is above 256,
 *  the peak number of header slots that the low-bitrate setting needs. */
#define SILENT_FRAMES     260
/** Frames of ordinary audio after the silence. These frames spend the reservoir. */
#define SIGNAL_FRAMES     40
#define MP3BUF_SIZE       (5 * SAMPLES_PER_FRAME / 4 + 7200)
/** Buffer size for the whole encode, with a large margin. The 128 kbit/s control produces the largest stream. */
#define STREAM_SIZE       (256 * 1024)

/** The layer III bitrates in kbit/s, in the order of the bitrate index in the header. */
static int const bitrate_mpeg1[15] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
};
static int const bitrate_mpeg2[15] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
};
/** Sample rates by the header's version and rate fields. */
static long const samplerates[4][3] = {
    {11025, 12000, 8000},       /* MPEG-2.5 */
    {0, 0, 0},                  /* reserved */
    {22050, 24000, 16000},      /* MPEG-2 */
    {44100, 48000, 32000}       /* MPEG-1 */
};

/**
 * @brief Returns the length in bytes of the frame whose header starts at
 *        @p h. Returns 0 if that is not a layer III header.
 */
static int
frame_length(unsigned char const *h)
{
    int const version = (h[1] >> 3) & 3;
    int const layer = (h[1] >> 1) & 3;
    int const bitrate_index = (h[2] >> 4) & 15;
    int const rate_index = (h[2] >> 2) & 3;
    int const padding = (h[2] >> 1) & 1;
    int     kbps;
    long    rate;

    if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
        return 0;
    if (layer != 1 || version == 1) /* layer III, and not the reserved version */
        return 0;
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
        return 0;
    kbps = version == 3 ? bitrate_mpeg1[bitrate_index]
        : bitrate_mpeg2[bitrate_index];
    rate = samplerates[version][rate_index];
    if (kbps == 0 || rate == 0)
        return 0;
    /* 1152 samples a frame in MPEG-1, 576 in the others */
    return (int) ((version == 3 ? 144000L : 72000L) * kbps / rate) + padding;
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
    unsigned char frame[MP3BUF_SIZE];
    short   left[SAMPLES_PER_FRAME];
    short   right[SAMPLES_PER_FRAME];
    int     collected = 0;
    int     i, f, rc;

    memset(left, 0, sizeof left);
    memset(right, 0, sizeof right);
    for (f = 0; f < silent + loud; f++) {
        if (f == silent) {
            for (i = 0; i < SAMPLES_PER_FRAME; i++) {
                /* a loud sawtooth: it costs bits in every band, which is what
                   makes the frame spend from the reservoir the silence filled */
                short const v = (short) (12000 - 48 * (i % 500));

                left[i] = v;
                right[i] = (short) -v;
            }
        }
        rc = lame_encode_buffer(gfp, left, right, SAMPLES_PER_FRAME,
                                frame, (int) sizeof frame);
        assert_true(rc >= 0);
        assert_true(collected + rc <= mp3_size);
        memcpy(mp3 + collected, frame, (size_t) rc);
        collected += rc;
    }
    rc = lame_encode_flush(gfp, frame, (int) sizeof frame);
    assert_true(rc >= 0);
    assert_true(collected + rc <= mp3_size);
    memcpy(mp3 + collected, frame, (size_t) rc);
    return collected + rc;
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
    static unsigned char mp3[STREAM_SIZE];
    lame_t  gfp = new_encoder();
    int     len, walked, frames;

    assert_int_equal(lame_set_out_samplerate(gfp, 24000), 0);
    assert_int_equal(lame_set_brate(gfp, 8), 0);
    assert_int_equal(lame_set_mode(gfp, STEREO), 0);
    assert_int_equal(lame_set_error_protection(gfp, 1), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    len = encode(gfp, SILENT_FRAMES, SIGNAL_FRAMES, mp3, (int) sizeof mp3);
    walked = walk_frames(mp3, len, &frames);

    if (report_calls != 0)
        fail_msg("the encoder reported %d message(s): %s", report_calls, report_text);
    /* Every byte belongs to a frame, and there are as many frames as were
       encoded - the second half of that is what a stream of the right length
       carrying no headers would fail. */
    assert_int_equal(walked, len);
    assert_true(frames >= SILENT_FRAMES);
    assert_int_equal(lame_close(gfp), 0);
}

/**
 * @brief Checks that an everyday encode keeps its framing.
 *
 * This test is the control. It shows that the two checks above pass on a
 * stream that the encoder really produced. So the result of the first test
 * comes from its settings, not from a walk over an empty buffer.
 */
static void
test_ordinary_encode_keeps_the_framing(LAME_UNUSED void **state)
{
    static unsigned char mp3[STREAM_SIZE];
    lame_t  gfp = new_encoder();
    int     len, walked, frames;

    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    len = encode(gfp, SILENT_FRAMES, SIGNAL_FRAMES, mp3, (int) sizeof mp3);
    walked = walk_frames(mp3, len, &frames);

    if (report_calls != 0)
        fail_msg("the encoder reported %d message(s): %s", report_calls, report_text);
    assert_int_equal(walked, len);
    assert_true(frames >= SILENT_FRAMES);
    assert_int_equal(lame_close(gfp), 0);
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
