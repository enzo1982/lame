/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the room that the LAME tag leaves in its frame
 *        (libmp3lame/VbrTag.c).
 *
 * \c InitVbrTag() chooses the size of the tag frame from a fixed count of
 * the tag's bytes, and drops the tag if the frame is smaller. The function
 * that writes the tag counts the bytes it writes on its own. In the
 * configurations below, the tag fills the frame to its last byte. So if the
 * writer writes one byte more than the count, it writes past the frame here.
 *
 * Each case encodes a short signal, then asks \c lame_get_lametag_frame()
 * for the frame in a buffer of exactly its size, followed by guard bytes.
 * The case checks that the tag ends at the last byte of the frame, that the
 * tag CRC is right, and that the guard bytes are unchanged.
 *
 * One more test sets the encoder's count of the stream's bytes to 5 GiB and
 * checks the two 32-bit fields that carry it: the Xing "bytes" field and the
 * LAME tag's music length hold their largest value, not the count modulo 2^32.
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API; the last one also sets the internal byte count directly.
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
#include <math.h>

#include <cmocka.h>

#include "test_unused.h"
#include "test_report.h"
#include "test_encode.h"

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_global_flags.h"

/** @brief The samples per channel of each encode call. */
#define SAMPLES_PER_CALL 4608
/** @brief The number of encode calls of each case. */
#define CALLS 4
/** @brief The size of the stream buffer, in bytes. */
#define STREAM_BYTES (1 << 20)
/** @brief The bytes after the frame that the library must not write. */
#define GUARD_BYTES 64
/** @brief The value of each guard byte. */
#define GUARD_VALUE 0xA5
/**
 * @brief The bytes of the Xing or Info header and the LAME tag, from the
 *        marker to the tag CRC: marker, flags, frames, bytes and quality 4
 *        each, the seek table 100, the LAME tag 36.
 */
#define TAG_BYTES (4 + 4 + 4 + 4 + 100 + 4 + 36)
/** @brief The bytes of the tag CRC, at the end of the LAME tag. */
#define TAG_CRC_BYTES 2

/** @brief One encoder configuration and the room its tag frame leaves. */
struct room_case {
    int     samplerate;  /**< the output sample rate, in Hz */
    int     channels;    /**< 1 or 2 */
    int     kbps;        /**< the bitrate */
    int     free_format; /**< 1 for a free format stream */
    int     room;        /**< the bytes of the frame after the tag */
};

/** @brief The configurations whose tag fills its frame, and one with room. */
static const struct room_case room_cases[] = {
    {11025, 1, 26, 1, 0},
    {22050, 1, 52, 1, 0},
    {24000, 2, 59, 1, 0},
    {44100, 2, 59, 1, 0},
    {48000, 1, 59, 1, 0},
    {48000, 2, 64, 0, 0},
    {48000, 2, 64, 1, 0},
    {44100, 2, 128, 0, 225}
};

/** @brief The left channel of the test signal. */
static short left[SAMPLES_PER_CALL * CALLS];
/** @brief The right channel of the test signal. */
static short right[SAMPLES_PER_CALL * CALLS];
/** @brief The encoded stream. */
static unsigned char stream[STREAM_BYTES];

/**
 * @brief Computes the CRC-16 that the LAME tag stores.
 * @param p  the bytes.
 * @param n  the number of bytes.
 * @return the CRC of the @p n bytes at @p p.
 */
static unsigned int
tag_crc(const unsigned char *p, size_t n)
{
    unsigned int crc = 0;
    size_t  i;
    int     b;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

/**
 * @brief Returns the side information size of a frame without CRC, header
 *        included. The Xing or Info marker starts there.
 * @param version   1 for MPEG-1, 0 for MPEG-2 and MPEG-2.5.
 * @param channels  1 or 2.
 * @return the size in bytes.
 */
static int
sideinfo_bytes(int version, int channels)
{
    if (version == 1)
        return channels == 1 ? 4 + 17 : 4 + 32;
    return channels == 1 ? 4 + 9 : 4 + 17;
}

/**
 * @brief Encodes one configuration and checks its tag frame.
 * @param c  the configuration.
 */
static void
check_room(const struct room_case *c)
{
    lame_t  gfp = lame_init();
    unsigned char *frame;
    size_t  need, i;
    int     n, start, frame_bytes;

    assert_non_null(gfp);
    report_reset();
    assert_int_equal(lame_set_errorf(gfp, report_capture), 0);
    assert_int_equal(lame_set_num_channels(gfp, c->channels), 0);
    assert_int_equal(lame_set_mode(gfp, c->channels == 1 ? MONO : JOINT_STEREO), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, c->samplerate), 0);
    assert_int_equal(lame_set_out_samplerate(gfp, c->samplerate), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_free_format(gfp, c->free_format), 0);
    assert_int_equal(lame_set_brate(gfp, c->kbps), 0);
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 1), 0);
    assert_int_equal(lame_set_quality(gfp, 7), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    assert_int_equal(lame_get_brate(gfp), c->kbps);
    assert_int_equal(lame_get_out_samplerate(gfp), c->samplerate);

    n = encode_collect(gfp, left, right, SAMPLES_PER_CALL, CALLS, SAMPLES_PER_CALL, stream,
                       (int) sizeof stream);
    assert_true(n > 0);

    /* The frame size from the bitrate, and where the tag starts in it. */
    frame_bytes = (lame_get_version(gfp) + 1) * 72000 * c->kbps / c->samplerate;
    start = sideinfo_bytes(lame_get_version(gfp), c->channels);
    assert_int_equal(frame_bytes - start - TAG_BYTES, c->room);

    need = lame_get_lametag_frame(gfp, NULL, 0);
    assert_int_equal(need, (size_t) frame_bytes);
    frame = malloc(need + GUARD_BYTES);
    assert_non_null(frame);
    memset(frame, GUARD_VALUE, need + GUARD_BYTES);
    assert_int_equal(lame_get_lametag_frame(gfp, frame, need), need);

    assert_memory_equal(frame + start, "Info", 4);
    for (i = 0; i < GUARD_BYTES; i++)
        assert_int_equal(frame[need + i], GUARD_VALUE);
    i = (size_t) start + TAG_BYTES - TAG_CRC_BYTES;
    assert_int_equal(tag_crc(frame, i), (unsigned int) ((frame[i] << 8) | frame[i + 1]));
    for (i = (size_t) start + TAG_BYTES; i < need; i++)
        assert_int_equal(frame[i], 0);

    free(frame);
    (void) lame_close(gfp);
}

/**
 * @brief Checks the tag frame of each configuration in #room_cases.
 * @param state  not used.
 */
static void
test_the_tag_fits_its_frame(LAME_UNUSED void **state)
{
    size_t  k;

    for (k = 0; k < sizeof room_cases / sizeof room_cases[0]; k++)
        check_room(&room_cases[k]);
}

/** @brief Offset of the Xing "bytes" field from the Xing or Info marker:
 *         after the marker, the flags and the frame count, 4 bytes each. */
#define XING_BYTES_OFFSET 12
/** @brief Offset of the LAME tag's music length from the Xing or Info marker. */
#define MUSIC_LENGTH_OFFSET (120 + 9 + 1 + 1 + 4 + 2 + 2 + 1 + 1 + 3 + 1 + 1 + 2)

/**
 * @brief Encodes a short CBR stream, sets the stream's byte count to
 *        @p bytes, and reads the two size fields of the tag frame.
 * @param bytes   the byte count to set.
 * @param xing    receives the Xing "bytes" field.
 * @param music   receives the LAME tag's music length.
 */
static void
tag_sizes(uint64_t bytes, uint32_t *xing, uint32_t *music)
{
    unsigned char frame[2880];
    lame_t  gfp = lame_init();
    size_t  n;
    int     start;

    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_out_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 1), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    assert_true(encode_collect(gfp, left, right, SAMPLES_PER_CALL, CALLS, SAMPLES_PER_CALL, stream,
                               (int) sizeof stream) > 0);
    gfp->internal_flags->VBR_seek_table.nBytesWritten = bytes;
    n = lame_get_lametag_frame(gfp, frame, sizeof frame);
    assert_true(n > 0 && n <= sizeof frame);
    start = sideinfo_bytes(1, 2);
    assert_memory_equal(frame + start, "Info", 4);
    *xing = ((uint32_t) frame[start + XING_BYTES_OFFSET] << 24) | ((uint32_t) frame[start + XING_BYTES_OFFSET + 1] << 16)
        | ((uint32_t) frame[start + XING_BYTES_OFFSET + 2] << 8) | frame[start + XING_BYTES_OFFSET + 3];
    *music = ((uint32_t) frame[start + MUSIC_LENGTH_OFFSET] << 24) | ((uint32_t) frame[start + MUSIC_LENGTH_OFFSET + 1] << 16)
        | ((uint32_t) frame[start + MUSIC_LENGTH_OFFSET + 2] << 8) | frame[start + MUSIC_LENGTH_OFFSET + 3];
    assert_int_equal(tag_crc(frame, (size_t) start + TAG_BYTES - TAG_CRC_BYTES),
                     (unsigned int) ((frame[start + TAG_BYTES - 2] << 8) | frame[start + TAG_BYTES - 1]));
    (void) lame_close(gfp);
}

/**
 * @brief Checks the size fields of a stream of 5 GiB, and of a small one as
 *        the control.
 * @param state  not used.
 */
static void
test_a_stream_of_4gib_or_more(LAME_UNUSED void **state)
{
    uint32_t xing, music;
    uint32_t const frame_bytes = 417;   /* the tag frame at 44.1 kHz, 128 kbit/s */

    tag_sizes(1000, &xing, &music);
    assert_int_equal(xing, 1000 + frame_bytes);
    assert_int_equal(music, 1000 + frame_bytes);
    tag_sizes((uint64_t) 5 << 30, &xing, &music);
    assert_int_equal(xing, 0xFFFFFFFFu);
    assert_int_equal(music, 0xFFFFFFFFu);
}

/**
 * @brief Runs the tests.
 * @return the number of failed tests.
 */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_the_tag_fits_its_frame),
        cmocka_unit_test(test_a_stream_of_4gib_or_more),
    };
    int     i;

    for (i = 0; i < SAMPLES_PER_CALL * CALLS; i++) {
        left[i] = (short) (8000 * sin(i * 0.05));
        right[i] = (short) (8000 * sin(i * 0.07));
    }
    return cmocka_run_group_tests(tests, NULL, NULL);
}
