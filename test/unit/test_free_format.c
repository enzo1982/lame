/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the bitrates that a caller can request for a free
 *        format stream (libmp3lame/lame.c).
 *
 * A free format bitrate is the caller's own number. It is not one of the
 * values in the tables of the standard. So it can describe a frame that is
 * too small for its own side information. An example is MPEG-1 at 48 kHz and
 * 8 kbit/s, with two channels and CRC. The frame is 24 bytes, but the side
 * information is 38 bytes. Nothing can be encoded in that case. So
 * \c lame_init_params() rejects these settings. The encoder does not fail
 * later, partway through the first frame.
 *
 * The tests call only \c lame_init_params(), because that function makes the
 * decision. So no audio is encoded, and each case takes a few microseconds.
 *
 * Some of the tests check that the rejection is narrow. The bound is eight
 * bits (one byte) of room per granule. The tightest configuration that the
 * bitrate tables allow is exactly on this bound. So a check that is even
 * slightly stricter rejects an ordinary MPEG-2 stream that encodes correctly.
 * One test checks this case on its own.
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

#include "test_unused.h"

#include "lame.h"

/** Number of messages that the library reported through its error callback. */
static int error_messages;

static void
count_error_message(LAME_UNUSED const char *format, LAME_UNUSED va_list ap)
{
    error_messages++;
}

/**
 * @brief Creates an encoder instance with the settings that the cases below
 *        share, and runs \c lame_init_params() on it.
 * @param samplerate the output sample rate, in Hz.
 * @param channels 1 or 2.
 * @param kbps the bitrate to ask for.
 * @param free_format 1 for a free format stream, 0 for a tabulated bitrate.
 * @param crc 1 to add the CRC, which is two more bytes of side information.
 * @return the result of \c lame_init_params() for those settings. The
 *         function closes the instance in both cases.
 */
static int
try_settings(int samplerate, int channels, int kbps, int free_format, int crc)
{
    lame_t  gfp = lame_init();
    int     rc;

    assert_non_null(gfp);
    error_messages = 0;
    assert_int_equal(lame_set_errorf(gfp, count_error_message), 0);
    assert_int_equal(lame_set_num_channels(gfp, channels), 0);
    assert_int_equal(lame_set_mode(gfp, channels == 1 ? MONO : STEREO), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, samplerate), 0);
    assert_int_equal(lame_set_out_samplerate(gfp, samplerate), 0);
    assert_int_equal(lame_set_brate(gfp, kbps), 0);
    assert_int_equal(lame_set_free_format(gfp, free_format), 0);
    assert_int_equal(lame_set_error_protection(gfp, crc), 0);
    rc = lame_init_params(gfp);
    /* The instance stays valid after a failed initialization and is still the
       caller's to close, which this exercises on both paths. */
    (void) lame_close(gfp);
    return rc;
}

/**
 * @brief Checks that a free format bitrate is rejected if its frame has no
 *        room for audio.
 *
 * MPEG-1 side information for two channels with CRC is 38 bytes. A frame at
 * 8 kbit/s and 48 kHz is 24 bytes. Without the check, a build with
 * assertions aborts partway through the first frame. A build without
 * assertions returns success and writes several megabytes of data that is
 * not an MP3.
 */
static void
test_bitrate_below_the_floor_is_refused(LAME_UNUSED void **state)
{
    assert_int_not_equal(try_settings(48000, 2, 8, 1, 1), 0);
    /* Refusing in silence would be its own defect - a caller that does not
       check the return value would be no better off than before. */
    assert_true(error_messages > 0);
}

/**
 * @brief Checks that the floor depends on the channel count. Mono has its own
 *        floor.
 *
 * Mono side information is 15 bytes shorter. So 8 kbit/s is 1 kbit/s below
 * the mono floor of 9 kbit/s. It is 6 kbit/s below the stereo floor of
 * 14 kbit/s. Without this case, the tests would also pass with a check that
 * only knows about stereo.
 */
static void
test_the_floor_follows_the_channel_count(LAME_UNUSED void **state)
{
    assert_int_not_equal(try_settings(48000, 1, 8, 1, 1), 0);
    assert_true(error_messages > 0);
    /* and one step up is enough for mono, where stereo would still be refused */
    assert_int_equal(try_settings(48000, 1, 9, 1, 1), 0);
}

/**
 * @brief Checks that a free format bitrate that fits is accepted.
 *
 * This is the control for the two tests above. It shows that the rejection
 * depends on the bitrate, not on free format itself.
 */
static void
test_a_workable_free_format_bitrate_is_accepted(LAME_UNUSED void **state)
{
    assert_int_equal(try_settings(48000, 2, 14, 1, 1), 0);
    assert_int_equal(error_messages, 0);
}

/**
 * @brief Checks that free format at 8 kbit/s is accepted where a frame has
 *        room for it.
 *
 * An MPEG-2 frame has one granule, not two, and its side information is
 * shorter. So the same bitrate that fails at 48 kHz is ordinary at 24 kHz. A
 * check that rejects free format below a fixed bitrate fails here.
 */
static void
test_the_floor_follows_the_sample_rate(LAME_UNUSED void **state)
{
    assert_int_equal(try_settings(24000, 2, 8, 1, 1), 0);
    assert_int_equal(error_messages, 0);
}

/**
 * @brief Checks that the tightest stream that the bitrate tables allow still
 *        initializes.
 *
 * The settings are MPEG-2 at 24 kHz and 8 kbit/s, two channels with CRC. The
 * frame is 24 bytes, and 23 bytes of it are side information. So it has
 * exactly the bound: one byte of room for its one granule. The rejection
 * comes closest to this case. The case is legal, it is in the tables, and it
 * encodes correctly.
 */
static void
test_the_tightest_tabulated_stream_is_accepted(LAME_UNUSED void **state)
{
    assert_int_equal(try_settings(24000, 2, 8, 0, 1), 0);
    assert_int_equal(error_messages, 0);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bitrate_below_the_floor_is_refused),
        cmocka_unit_test(test_the_floor_follows_the_channel_count),
        cmocka_unit_test(test_a_workable_free_format_bitrate_is_accepted),
        cmocka_unit_test(test_the_floor_follows_the_sample_rate),
        cmocka_unit_test(test_the_tightest_tabulated_stream_is_accepted),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
