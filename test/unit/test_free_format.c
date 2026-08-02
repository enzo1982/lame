/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the bitrates a free format stream can be asked for
 *        (libmp3lame/lame.c).
 *
 * A free format bitrate is the caller's own number rather than one of the
 * values the standard tabulates, so it can name a frame too small to hold the
 * side information the frame itself has to carry - MPEG-1 at 48 kHz and
 * 8 kbit/s is a 24 byte frame carrying 38 bytes of it for two channels with
 * CRC. There is no encoding to be done in that case and the settings are
 * refused when they are prepared, rather than partway through the first frame.
 *
 * The tests are on \c lame_init_params() alone, which is where the answer is,
 * so no audio is encoded and each case is a handful of microseconds.
 *
 * Half of them exist to check the refusal is narrow. The bound is one granule
 * of audio per granule of frame, and the tightest configuration the bitrate
 * tables allow sits exactly on it - so a check that were even slightly stricter
 * would refuse an ordinary MPEG-2 stream that encodes correctly. That case is
 * here as its own test.
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

/** Messages the library reported through its error callback. */
static int error_messages;

static void
count_error_message(LAME_UNUSED const char *format, LAME_UNUSED va_list ap)
{
    error_messages++;
}

/**
 * @brief Prepares an instance with everything the cases below share.
 * @param samplerate the output sample rate, in Hz.
 * @param channels 1 or 2.
 * @param kbps the bitrate to ask for.
 * @param free_format 1 for a free format stream, 0 for a tabulated bitrate.
 * @param crc 1 to add the CRC, which is two more bytes of side information.
 * @return the result of \c lame_init_params() for those settings; the instance
 *         is closed before returning either way.
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
 * @brief A free format bitrate whose frame cannot carry a frame is refused.
 *
 * Two channels of MPEG-1 side information with CRC is 38 bytes; 8 kbit/s at
 * 48 kHz is a 24 byte frame. What this used to do depended on the build: with
 * assertions live it aborted the process partway through the first frame, and
 * without them it returned success and wrote several megabytes of data that
 * was not an MP3.
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
 * @brief The floor depends on the channel count, so mono has its own.
 *
 * Mono side information is 15 bytes shorter, which is why 8 kbit/s is refused
 * here at one bitrate below the mono floor rather than at the six below the
 * stereo one. Without this case the test would pass against a check that only
 * knew about stereo.
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
 * @brief A free format bitrate that does fit is accepted.
 *
 * The control for the two above: it shows the refusal is about the bitrate
 * rather than about free format itself.
 */
static void
test_a_workable_free_format_bitrate_is_accepted(LAME_UNUSED void **state)
{
    assert_int_equal(try_settings(48000, 2, 14, 1, 1), 0);
    assert_int_equal(error_messages, 0);
}

/**
 * @brief Free format at 8 kbit/s is accepted where a frame can carry it.
 *
 * MPEG-2 frames hold one granule instead of two and their side information is
 * shorter, so the same bitrate that cannot work at 48 kHz is ordinary at
 * 24 kHz. A check that refused free format below some fixed bitrate would fail
 * here.
 */
static void
test_the_floor_follows_the_sample_rate(LAME_UNUSED void **state)
{
    assert_int_equal(try_settings(24000, 2, 8, 1, 1), 0);
    assert_int_equal(error_messages, 0);
}

/**
 * @brief The tightest stream the bitrate tables allow still initializes.
 *
 * MPEG-2 at 24 kHz and 8 kbit/s, two channels with CRC: a 24 byte frame
 * carrying 23 bytes of side information, so it has exactly the one granule of
 * room that is the bound. This is the case the refusal comes closest to
 * catching, and it is legal, tabulated, and encodes correctly.
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
