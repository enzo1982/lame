/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for an encoder whose bit reservoir disagrees with the bits
 *        it wrote (libmp3lame/bitstream.c).
 *
 * After each frame, the frame writer compares the bits it wrote with the bit
 * reservoir. The test makes them disagree by changing the reservoir count of
 * an encoder through its internal state, and checks that the encode call and
 * the flush fail with #LAME_INTERNALERROR.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"
#include "test_report.h"

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_global_flags.h"

/** Samples per channel and encode call: four MPEG-1 frames. */
#define CALL_SAMPLES 4608

/** One channel of input: a square wave, so that every frame has bits to spend. */
static short pcm[CALL_SAMPLES];
/** The encoded output. */
static unsigned char mp3[LAME_MAXMP3BUFFER];

/**
 * @brief Checks that a reservoir count that disagrees with the written bits
 *        fails the encoder.
 * @param state cmocka fixture state (unused).
 */
static void
test_a_reservoir_mismatch_fails_the_encoder(LAME_UNUSED void **state)
{
    lame_t  gfp = lame_init();
    int     i;

    for (i = 0; i < CALL_SAMPLES; ++i) {
        pcm[i] = (i / 50) % 2 ? 8000 : -8000;
    }
    assert_non_null(gfp);
    assert_int_equal(lame_set_errorf(gfp, report_capture), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    /* the control: the same encoder works before the change */
    report_reset();
    assert_true(lame_encode_buffer(gfp, pcm, pcm, CALL_SAMPLES, mp3, sizeof(mp3)) >= 0);
    assert_int_equal(report_calls, 0);

    gfp->internal_flags->sv_enc.ResvSize += 8;
    assert_int_equal(lame_encode_buffer(gfp, pcm, pcm, CALL_SAMPLES, mp3, sizeof(mp3)),
                     LAME_INTERNALERROR);
    assert_true(report_calls > 0);
    /* and the error is kept */
    assert_int_equal(lame_encode_flush(gfp, mp3, sizeof(mp3)), LAME_INTERNALERROR);
    (void) lame_close(gfp);
}

/** @brief Registers the reservoir test and runs it. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_a_reservoir_mismatch_fails_the_encoder),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
