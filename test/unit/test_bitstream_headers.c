/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the frame headers the encoder buffers before writing
 *        them (libmp3lame/bitstream.c).
 *
 * A frame's header is produced when the frame is encoded but written when the
 * stream reaches the header's own position, so headers wait in between. How
 * many wait at once is the bit reservoir divided by what a frame has room for
 * beyond its own side info - a quantity that stays at one or two for ordinary
 * settings and reaches the hundreds at the format's low end, where an MPEG-2
 * frame at 8 kbit/s and 24 kHz is 24 bytes carrying 23 bytes of side info for
 * two channels with CRC. Silence spends none of the remaining byte, so a
 * silent lead-in fills the reservoir and leaves a header waiting for every
 * eight bits of it.
 *
 * That is ordinary input - a recording that starts with a gap - and the first
 * test encodes it. The second is an everyday encode, which must be unaffected
 * and which is what says the first one's result is about the settings rather
 * than about the test having encoded nothing.
 *
 * Both ask the same two things: that the encoder reports no error, and that
 * every byte it produced belongs to a frame. The second matters because the
 * failure this guards against is silent - the encoder reports once and carries
 * on writing a stream whose framing it has lost, so a test that only watched
 * for a return code would see a clean encode.
 *
 * Library-level tests: they link libmp3lame and call the exported API directly.
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

#include "test_unused.h"

#include "lame.h"

/** Samples in an MPEG-2 layer III frame. */
#define SAMPLES_PER_FRAME 576
/** Frames of digital silence before the audio starts. One more than the
 *  headers that can be waiting at once, which is where the demand peaks. */
#define SILENT_FRAMES     260
/** Frames of ordinary audio after it, which is what spends the reservoir. */
#define SIGNAL_FRAMES     40
#define MP3BUF_SIZE       (5 * SAMPLES_PER_FRAME / 4 + 7200)
/** Room for the whole encode, generously: the loud control is the large one. */
#define STREAM_SIZE       (256 * 1024)

/** Messages the encoder reported through its error callback. */
static int error_messages;
/** The first of them, for a failure that says what happened. */
static char first_message[200];

/**
 * @brief Records that the encoder reported something.
 *
 * The format string carries the whole text in this library - the messages
 * under test take no arguments - so it is kept as it stands rather than
 * formatted, which keeps the callback free of varargs handling.
 */
static void
count_error_message(const char *format, LAME_UNUSED va_list ap)
{
    if (error_messages == 0 && format != NULL) {
        strncpy(first_message, format, sizeof first_message - 1);
        first_message[sizeof first_message - 1] = '\0';
    }
    error_messages++;
}

/** The layer III bitrates, in kbit/s, indexed as the header holds them. */
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
 * @brief Length in bytes of the frame whose header starts at @p h, or 0 if
 *        that is not a layer III header.
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
 * @brief Follows the frame chain from the first byte, the way a stream parser
 *        does: each frame's length says where the next header must be.
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
 * @brief Encodes @p silent frames of digital silence followed by @p loud
 *        frames of a tone, with the settings the caller has already made.
 * @return the number of bytes collected in @p mp3.
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
 * @brief Prepares an encoder that reports through #count_error_message.
 */
static lame_t
new_encoder(void)
{
    lame_t  gfp = lame_init();

    error_messages = 0;
    first_message[0] = '\0';
    assert_non_null(gfp);
    assert_int_equal(lame_set_errorf(gfp, count_error_message), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 24000), 0);
    /* The tag frame is written by the caller, not by the encoder, so asking
       for one would leave a gap in the stream this test walks. */
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 0), 0);
    return gfp;
}

/**
 * @brief A silent lead-in does not cost the file its framing.
 *
 * MPEG-2 at 8 kbit/s and 24 kHz, two channels, CRC on: one byte of room per
 * frame beyond the side info, so 260 silent frames leave more headers waiting
 * than a ring of 256 slots can hold while also taking the next one. What that
 * used to produce was an encoder that reported once, returned success, and
 * wrote a further 255 frames' worth of audio with no frame headers in it.
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

    if (error_messages != 0)
        fail_msg("the encoder reported %d message(s), the first being: %s",
                 error_messages, first_message);
    /* Every byte belongs to a frame, and there are as many frames as were
       encoded - the second half of that is what a stream of the right length
       carrying no headers would fail. */
    assert_int_equal(walked, len);
    assert_true(frames >= SILENT_FRAMES);
    assert_int_equal(lame_close(gfp), 0);
}

/**
 * @brief An everyday encode is unaffected.
 *
 * The control: it shows the two checks above pass on a stream that was really
 * produced, so the first test's result belongs to its settings rather than to
 * a walk over an empty buffer.
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

    if (error_messages != 0)
        fail_msg("the encoder reported %d message(s), the first being: %s",
                 error_messages, first_message);
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
