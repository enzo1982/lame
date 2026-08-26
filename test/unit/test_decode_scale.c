/**
 * @file
 * @ingroup unit_tests
 * @brief The scale the unclipped decoder hands back.
 *
 * @c hip_decode1_unclipped() exists so that the encoder can measure the peak of
 * what a decoder will produce, and clipping is exactly the case where that peak
 * goes past full scale. Both of those depend on one thing the type does not
 * carry: the samples are in LAME's own scale, where full scale is 32768.
 *
 * The decoder underneath was replaced by libmpg123, whose floating point output
 * is normalised to +-1, and nothing noticed for a release: the encoder still
 * produced correct audio, and the only symptom was that @c --clipdetect stopped
 * being able to report clipping and @c --replaygain-accurate returned the same
 * figure for every input. There was no test of the scale, because the scale is
 * not visible in any signature.
 *
 * So these tests ask the question directly, and the strong one asks it without
 * naming 32768 at all: decode the same stream through the clipped path, whose
 * scale is fixed by its @c short type, and through the unclipped one, and
 * require them to agree. That comparison would have failed by a factor of 32768
 * before the fix, and it cannot be satisfied by a wrong constant.
 *
 * The tests skip rather than fail where the library was built without a
 * decoder: @c hip_decode_init() hands back nothing there, and "no decoder" and
 * "a decoder with the wrong scale" must not look the same.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "test_unused.h"

/** @brief Sample rate every test in this file encodes at. */
#define SAMPLE_RATE  44100
/** @brief Samples per channel in the test signal - half a second. */
#define FRAMES       (SAMPLE_RATE / 2)
/** @brief Samples per channel handed to the encoder in one call. */
#define CHUNK        1152
/** @brief Room for the encoded stream, ample at these settings. */
#define MP3_ROOM     (FRAMES * 2)

/**
 * @brief Full scale for a @c short sample, the scale the unclipped decoder
 *        is documented to share.
 */
#define FULL_SCALE   32767.0


/**
 * @brief Encodes a sine at a fraction of full scale and hands back the stream.
 *
 * @param amplitude_fraction  1.0 is full scale; more than that asks for a
 *                            stream that decodes past it.
 * @param out_len             receives the length in bytes.
 * @return the bitstream, which the caller frees, or NULL.
 */
static unsigned char *
encode_sine(double amplitude_fraction, size_t * out_len)
{
    lame_t  gf = lame_init();
    short  *l = malloc(sizeof(short) * FRAMES);
    short  *r = malloc(sizeof(short) * FRAMES);
    unsigned char *mp3 = malloc(MP3_ROOM);
    size_t  used = 0;
    int     i, n;

    assert_non_null(gf);
    assert_non_null(l);
    assert_non_null(r);
    assert_non_null(mp3);

    for (i = 0; i < FRAMES; i++) {
        double  v = FULL_SCALE * amplitude_fraction
            * sin(2.0 * 3.14159265358979323846 * 997.0 * i / SAMPLE_RATE);
        l[i] = r[i] = (short) (v < 0 ? v - 0.5 : v + 0.5);
    }
    lame_set_in_samplerate(gf, SAMPLE_RATE);
    lame_set_num_channels(gf, 2);
    lame_set_brate(gf, 192);
    lame_set_VBR(gf, vbr_off);
    assert_true(lame_init_params(gf) >= 0);

    for (i = 0; i + CHUNK <= FRAMES; i += CHUNK) {
        n = lame_encode_buffer(gf, l + i, r + i, CHUNK, mp3 + used,
                               (int) (MP3_ROOM - used));
        assert_true(n >= 0);
        used += (size_t) n;
    }
    n = lame_encode_flush(gf, mp3 + used, (int) (MP3_ROOM - used));
    assert_true(n >= 0);
    used += (size_t) n;

    lame_close(gf);
    free(l);
    free(r);
    *out_len = used;
    return mp3;
}


/**
 * @brief Decodes a stream and reports the largest magnitude it saw.
 *
 * @param mp3        the bitstream to decode.
 * @param len        its length in bytes.
 * @param unclipped  take the float path, which is not limited to full scale,
 *                   rather than the one handing back a @c short.
 * @return the peak, or -1 where the build has no decoder at all.
 */
static double
peak_of(unsigned char *mp3, size_t len, int unclipped)
{
    hip_t   hip = hip_decode_init();
    short   spcm_l[1152], spcm_r[1152];
    sample_t fpcm_l[1152], fpcm_r[1152];
    double  peak = 0;
    size_t  fed = 0;
    int     got, i;

    if (hip == 0) {
        return -1;      /* built without a decoder */
    }
    /* Feed in blocks, then drain: both entry points take "no more input" as a
     * zero length, and a frame may need more than one call to come out. */
    while (fed <= len) {
        size_t  chunk = len - fed > 1024 ? 1024 : len - fed;

        do {
            if (unclipped) {
                got = hip_decode1_unclipped(hip, mp3 + fed, chunk, fpcm_l, fpcm_r);
            }
            else {
                got = hip_decode1(hip, mp3 + fed, chunk, spcm_l, spcm_r);
            }
            chunk = 0;  /* only the first call of the pair carries input */
            if (got < 0) {
                break;
            }
            for (i = 0; i < got; i++) {
                double  a = unclipped ? fabs((double) fpcm_l[i]) : fabs((double) spcm_l[i]);
                double  b = unclipped ? fabs((double) fpcm_r[i]) : fabs((double) spcm_r[i]);
                if (a > peak)
                    peak = a;
                if (b > peak)
                    peak = b;
            }
        } while (got > 0);
        if (fed == len) {
            break;
        }
        fed += len - fed > 1024 ? 1024 : len - fed;
    }
    hip_decode_exit(hip);
    return peak;
}


/**
 * @brief The two decode paths return the same audio in the same scale.
 * @param state cmocka fixture state (unused).
 *
 * The clipped path hands back a @c short, whose type fixes its scale; the
 * unclipped one hands back a @c float and nothing in its signature says what
 * full scale is. Comparing the two is a check on the scale that does not
 * have to name the factor between them.
 */
static void
test_both_paths_agree(LAME_UNUSED void **state)
{
    size_t  len;
    unsigned char *mp3 = encode_sine(0.5, &len);
    double  clipped = peak_of(mp3, len, 0);
    double  unclipped = peak_of(mp3, len, 1);

    free(mp3);
    if (clipped < 0 || unclipped < 0) {
        skip();         /* no decoder in this build */
    }
    assert_true(clipped > 1000.0);      /* the control: something was decoded */
    assert_true(unclipped > 1000.0);
    /* Within 1% of each other. They are the same audio through two output
     * formats, so the only differences are rounding and the clipped path's
     * limit, which a half-scale signal does not reach. */
    assert_true(fabs(clipped - unclipped) < 0.01 * clipped);
}


/**
 * @brief The peak the library reports is in the encoder's scale.
 * @param state cmocka fixture state (unused).
 *
 * That is the scale every reader of it assumes: the clipping count, and the
 * gain figure written into the tag.
 */
static void
test_peak_is_in_lame_scale(LAME_UNUSED void **state)
{
    size_t  len;
    unsigned char *mp3 = encode_sine(0.5, &len);
    double  peak = peak_of(mp3, len, 1);

    free(mp3);
    if (peak < 0) {
        skip();
    }
    assert_true(peak > 0.4 * FULL_SCALE);
    assert_true(peak < 0.6 * FULL_SCALE);
}


/**
 * @brief Clipping takes samples away from the peak and never adds to it.
 * @param state cmocka fixture state (unused).
 *
 * Stated as an inequality: a correct decoder's stream need not exceed full
 * scale, and a particular figure would pin an encoder property in a decoder
 * test.
 */
static void
test_clipping_only_reduces(LAME_UNUSED void **state)
{
    size_t  len;
    unsigned char *mp3 = encode_sine(1.0, &len);
    double  clipped = peak_of(mp3, len, 0);
    double  unclipped = peak_of(mp3, len, 1);

    free(mp3);
    if (clipped < 0 || unclipped < 0) {
        skip();
    }
    assert_true(clipped > 0.9 * FULL_SCALE);        /* the control: it is loud */
    assert_true(clipped <= FULL_SCALE + 1);         /* a short cannot say more */
    assert_true(unclipped >= clipped * 0.999);      /* and the float is not below it */
}


/** @brief Runs the scale tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_both_paths_agree),
        cmocka_unit_test(test_peak_is_in_lame_scale),
        cmocka_unit_test(test_clipping_only_reduces),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
