/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the public functions of the decoder instance.
 *
 * The tests form three groups. Each group needs different things.
 *
 * The analysis hooks and the create and destroy calls: a frontend calls
 * these even when it may have no decoder. A library built without libmpg123
 * returns no decoder instance at all. A frontend that plots what the decoder
 * saw may set no block. So these calls must accept NULL. The header promises
 * this, and these tests check it. In the two hook tests, the check is that
 * the test gets to its end. A broken build dereferences NULL. The test then
 * stops on a signal, and CMocka reports it as failed.
 *
 * The decoding calls: these need a decoder. The tests encode a short stream
 * with this library and decode it again. Where hip_decode_init() returns
 * NULL, the tests skip. They do not check a decode that cannot run.
 *
 * The obsolete lame_decode* functions: these need nothing. They do nothing in
 * every build. The tests check that they keep doing nothing. They return
 * fixed values, and they never write to the output buffers.
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
#include <math.h>

#include <cmocka.h>

#include "test_unused.h"

#include "lame.h"

/* The obsolete decoder entry points are still built and exported, but
   DEPRECATED_OR_OBSOLETE_CODE_REMOVED compiles their declarations out of the
   installed header, so they are declared here to be called at all. Same
   arrangement test_set_get.c uses for the deprecated setters, and the only way
   this slice of the ABI is exercised. */
extern int lame_decode_init(void);
extern int lame_decode_exit(void);
extern int lame_decode(unsigned char *, int, short[], short[]);
extern int lame_decode1(unsigned char *, int, short[], short[]);
extern int lame_decode_headers(unsigned char *, int, short[], short[],
                               mp3data_struct *);
extern int lame_decode1_headers(unsigned char *, int, short[], short[],
                                mp3data_struct *);
extern int lame_decode1_headersB(unsigned char *, int, short[], short[],
                                 mp3data_struct *, int *, int *);

#define RATE        44100
#define KBPS        128
#define NSAMPLES    4608        /**< four MPEG-1 frames per encode call */
#define NCALLS      6
#define MP3CAP      (NCALLS * (NSAMPLES * 5 / 4 + 7200) + 7200)
#define SAMPLES_IN  (NSAMPLES * NCALLS)
#define FRAME       1152        /**< samples per channel in one MPEG-1 frame */

/** Room for every frame that the input can produce. hip_decode() needs this
    much room from the caller. It does not know the buffer size, so it cannot
    check it. */
#define PCMCAP      (SAMPLES_IN + 16 * FRAME)

/** A value that no decoded sample of this signal can have. So the test can
    tell "was not written" from "was written with a plausible number". */
#define SENTINEL    0x5A5A

/** Written out here, not taken from math.h. M_PI is not standard C. Whether
    math.h defines it depends on the feature-test macros that are set. */
#define PI          3.14159265358979323846

/**
 * @brief Encodes a short stereo stream with this library.
 *
 * @param mp3       receives the encoded stream.
 * @param cap       the size of @a mp3 in bytes.
 * @param with_tag  nonzero to write the real LAME tag into the frame that the
 *                  encoder reserved for it. The LAME tag stores the delay and
 *                  padding. So a decoder can read these values only from a
 *                  stream with the tag.
 * @return the number of bytes written, or -1.
 */
static int
encode_a_stream(unsigned char *mp3, int cap, int with_tag)
{
    lame_global_flags *gf = lame_init();
    short  *pcm_l = malloc(NSAMPLES * sizeof(short));
    short  *pcm_r = malloc(NSAMPLES * sizeof(short));
    int     i, c, n, total = -1;

    if (gf == NULL || pcm_l == NULL || pcm_r == NULL)
        goto done;

    lame_set_in_samplerate(gf, RATE);
    lame_set_num_channels(gf, 2);
    lame_set_brate(gf, KBPS);
    lame_set_VBR(gf, vbr_off);
    lame_set_quality(gf, 5);
    if (lame_init_params(gf) < 0)
        goto done;

    for (i = 0; i < NSAMPLES; i++) {
        double  t = (double) i / RATE;
        pcm_l[i] = (short) (20000.0 * sin(2.0 * PI * 440.0 * t));
        pcm_r[i] = (short) (16000.0 * sin(2.0 * PI * 660.0 * t));
    }

    total = 0;
    for (c = 0; c < NCALLS; c++) {
        n = lame_encode_buffer(gf, pcm_l, pcm_r, NSAMPLES, mp3 + total, cap - total);
        if (n < 0) {
            total = -1;
            goto done;
        }
        total += n;
    }
    n = lame_encode_flush(gf, mp3 + total, cap - total);
    if (n < 0) {
        total = -1;
        goto done;
    }
    total += n;

    if (with_tag) {
        size_t  want = lame_get_lametag_frame(gf, NULL, 0);
        unsigned char *tag = want ? malloc(want) : NULL;

        if (tag != NULL && lame_get_lametag_frame(gf, tag, want) == want
            && (int) want <= total)
            memcpy(mp3, tag, want);
        else
            total = -1;         /* asked for the tag and did not get it */
        free(tag);
    }

  done:
    if (gf != NULL)
        lame_close(gf);
    free(pcm_l);
    free(pcm_r);
    return total;
}

/**
 * @brief Decodes a whole stream one frame at a time, and reports the delay and
 *        padding.
 *
 * This is the loop that the documentation describes. The first call passes
 * the input. Each later call passes a length of 0, to get the samples that
 * the decoder still has.
 *
 * @return the total samples per channel, or -1 if the decode failed.
 */
static int
drain_headersB(hip_t hip, unsigned char *mp3, int mp3len, short *pcm_l,
               short *pcm_r, mp3data_struct * mp3data, int *enc_delay,
               int *enc_padding)
{
    int     i, n, total = 0;

    /* Bounded rather than while(1): a decoder that returned a positive count
       forever would otherwise hang the suite instead of failing it. The bound
       is well above the frames this input can hold. */
    for (i = 0; i < 64; i++) {
        n = hip_decode1_headersB(hip, i == 0 ? mp3 : NULL,
                                 i == 0 ? (size_t) mp3len : 0,
                                 pcm_l, pcm_r, mp3data, enc_delay, enc_padding);
        if (n < 0)
            return -1;
        if (n == 0)
            return total;
        total += n;
    }
    return total;
}

/**
 * @brief Checks that neither analysis hook dereferences a NULL decoder
 *        instance.
 *
 * In a library built without libmpg123, hip_decode_init() returns NULL. So no
 * caller gets a decoder instance there. The frontend can still call these
 * functions with NULL. The header promises that both calls accept NULL and
 * do nothing.
 */
static void
test_analysis_hooks_tolerate_a_null_handle(LAME_UNUSED void **state)
{
    hip_set_pinfo(NULL, NULL);
    hip_finish_pinfo(NULL);
}

/**
 * @brief Checks that hip_finish_pinfo() does nothing when no block was set.
 *
 * This is the other half of the same promise, on a real decoder instance.
 * Where the library cannot create one, the test skips. A test that passes
 * without making the call checks nothing.
 */
static void
test_finish_pinfo_without_a_block(LAME_UNUSED void **state)
{
    hip_t   hip = hip_decode_init();

    if (hip == NULL) {
        skip();         /* no decoder in this build */
    }
    hip_finish_pinfo(hip);
    assert_int_equal(hip_decode_exit(hip), 0);
}

/**
 * @brief Checks that hip_decode_exit() returns success for a NULL decoder
 *        instance.
 *
 * hip_decode_exit() accepts NULL. So a frontend can call it on every path,
 * also where no decoder instance was created. One such path is a library
 * that has no decoder.
 */
static void
test_decode_exit_accepts_a_null_handle(LAME_UNUSED void **state)
{
    assert_int_equal(hip_decode_exit(NULL), 0);
}

/**
 * @brief Checks that the three reporting setters accept any argument and
 *        report nothing.
 *
 * Their documentation says that they accept the callback and discard it. This
 * is true for a decoder instance and for NULL. So the test checks that every
 * call returns. The possible failure is a dereference, not a wrong value. The
 * setters exist for callers that set all six reporting callbacks,
 * three for the encoder and three for the decoder.
 */
static void
test_reporting_setters_accept_a_handle_or_null(LAME_UNUSED void **state)
{
    hip_t   hip = hip_decode_init();

    hip_set_errorf(NULL, NULL);
    hip_set_msgf(NULL, NULL);
    hip_set_debugf(NULL, NULL);

    if (hip != NULL) {
        hip_set_errorf(hip, NULL);
        hip_set_msgf(hip, NULL);
        hip_set_debugf(hip, NULL);
        assert_int_equal(hip_decode_exit(hip), 0);
    }
}

/**
 * @brief Checks that hip_decode1_headers() decodes a stream that this library
 *        encoded.
 *
 * Only a round trip checks that the caller gets the frame description. The
 * sample rate, the channel count and the bitrate come from the frame header.
 * So the test compares them with the settings of the encode.
 *
 * The test checks the sample count against a range, not an exact number. A
 * decoder returns whole frames, and the encoder pads to a frame boundary. So
 * the exact total depends on both. Neither of them promises it.
 */
static void
test_decode1_headers_round_trip(LAME_UNUSED void **state)
{
    unsigned char *mp3 = malloc(MP3CAP);
    short  *pcm_l = malloc(PCMCAP * sizeof(short));
    short  *pcm_r = malloc(PCMCAP * sizeof(short));
    mp3data_struct mp3data;
    hip_t   hip;
    int     mp3len, i, n, total = 0;

    assert_non_null(mp3);
    assert_non_null(pcm_l);
    assert_non_null(pcm_r);

    mp3len = encode_a_stream(mp3, MP3CAP, 0);
    assert_true(mp3len > 0);

    hip = hip_decode_init();
    if (hip == NULL) {
        free(mp3);
        free(pcm_l);
        free(pcm_r);
        skip();         /* no decoder in this build */
    }

    memset(&mp3data, 0, sizeof(mp3data));
    for (i = 0; i < 64; i++) {
        n = hip_decode1_headers(hip, i == 0 ? mp3 : NULL,
                                i == 0 ? (size_t) mp3len : 0,
                                pcm_l + total, pcm_r + total, &mp3data);
        assert_true(n >= 0);
        if (n == 0)
            break;
        total += n;
    }

    assert_int_equal(mp3data.header_parsed, 1);
    assert_int_equal(mp3data.samplerate, RATE);
    assert_int_equal(mp3data.stereo, 2);
    assert_int_equal(mp3data.bitrate, KBPS);

    /* Everything that went in comes back, and not unboundedly more. */
    assert_true(total >= SAMPLES_IN);
    assert_true(total <= SAMPLES_IN + 4 * FRAME);

    assert_int_equal(hip_decode_exit(hip), 0);
    free(mp3);
    free(pcm_l);
    free(pcm_r);
}

/**
 * @brief Checks that hip_decode() returns in one call what hip_decode1()
 *        returns in pieces.
 *
 * hip_decode() promises only this. It decodes one frame at a time until the
 * decoder has nothing left. So equal totals are the contract itself.
 * hip_decode_headers() is the same loop, and it also fills in the frame
 * description. The test checks it too, so that all three agree.
 */
static void
test_decode_matches_the_piecewise_total(LAME_UNUSED void **state)
{
    unsigned char *mp3 = malloc(MP3CAP);
    short  *pcm_l = malloc(PCMCAP * sizeof(short));
    short  *pcm_r = malloc(PCMCAP * sizeof(short));
    mp3data_struct mp3data;
    hip_t   hip;
    int     mp3len, i, n, piecewise = 0, at_once, with_headers;

    assert_non_null(mp3);
    assert_non_null(pcm_l);
    assert_non_null(pcm_r);

    mp3len = encode_a_stream(mp3, MP3CAP, 0);
    assert_true(mp3len > 0);

    hip = hip_decode_init();
    if (hip == NULL) {
        free(mp3);
        free(pcm_l);
        free(pcm_r);
        skip();         /* no decoder in this build */
    }

    for (i = 0; i < 64; i++) {
        n = hip_decode1(hip, i == 0 ? mp3 : NULL, i == 0 ? (size_t) mp3len : 0,
                        pcm_l + piecewise, pcm_r + piecewise);
        assert_true(n >= 0);
        if (n == 0)
            break;
        piecewise += n;
    }
    assert_true(piecewise > 0);
    assert_int_equal(hip_decode_exit(hip), 0);

    /* A fresh handle for each arm: a decoder that has already consumed the
       stream would report 0 for the second one and the comparison would pass
       by having measured nothing. */
    hip = hip_decode_init();
    assert_non_null(hip);
    at_once = hip_decode(hip, mp3, (size_t) mp3len, pcm_l, pcm_r);
    assert_int_equal(hip_decode_exit(hip), 0);

    hip = hip_decode_init();
    assert_non_null(hip);
    memset(&mp3data, 0, sizeof(mp3data));
    with_headers = hip_decode_headers(hip, mp3, (size_t) mp3len, pcm_l, pcm_r,
                                      &mp3data);
    assert_int_equal(hip_decode_exit(hip), 0);

    assert_int_equal(at_once, piecewise);
    assert_int_equal(with_headers, piecewise);
    assert_int_equal(mp3data.header_parsed, 1);
    assert_int_equal(mp3data.samplerate, RATE);

    free(mp3);
    free(pcm_l);
    free(pcm_r);
}

/**
 * @brief Checks that hip_decode1_headersB() reads the delay and padding from
 *        the LAME tag.
 *
 * These two values are the only reason to call this function and not
 * hip_decode1_headers(). The LAME tag stores them. So the test encodes the
 * same audio twice, once with the tag and once without. It checks the
 * difference between the two results. With only one stream, a result of -1
 * and -1 from a stream without the tag passes the test.
 *
 * The test checks that the values are present and plausible. It does not
 * check exact values. The delay comes from the encoder. With an exact value
 * here, a change to the encoder breaks a decoder test.
 */
static void
test_headersB_reports_the_tags_delay_and_padding(LAME_UNUSED void **state)
{
    unsigned char *plain = malloc(MP3CAP);
    unsigned char *tagged = malloc(MP3CAP);
    short  *pcm_l = malloc(PCMCAP * sizeof(short));
    short  *pcm_r = malloc(PCMCAP * sizeof(short));
    mp3data_struct mp3data;
    hip_t   hip;
    int     plainlen, taggedlen, total;
    int     plain_delay = -999, plain_padding = -999;
    int     tag_delay = -999, tag_padding = -999;

    assert_non_null(plain);
    assert_non_null(tagged);
    assert_non_null(pcm_l);
    assert_non_null(pcm_r);

    plainlen = encode_a_stream(plain, MP3CAP, 0);
    taggedlen = encode_a_stream(tagged, MP3CAP, 1);
    assert_true(plainlen > 0);
    assert_true(taggedlen > 0);

    hip = hip_decode_init();
    if (hip == NULL) {
        free(plain);
        free(tagged);
        free(pcm_l);
        free(pcm_r);
        skip();         /* no decoder in this build */
    }

    memset(&mp3data, 0, sizeof(mp3data));
    total = drain_headersB(hip, plain, plainlen, pcm_l, pcm_r, &mp3data,
                           &plain_delay, &plain_padding);
    assert_true(total > 0);
    assert_int_equal(hip_decode_exit(hip), 0);

    hip = hip_decode_init();
    assert_non_null(hip);
    memset(&mp3data, 0, sizeof(mp3data));
    total = drain_headersB(hip, tagged, taggedlen, pcm_l, pcm_r, &mp3data,
                           &tag_delay, &tag_padding);
    assert_true(total > 0);
    assert_int_equal(hip_decode_exit(hip), 0);

    /* No tag, no figures - the documented "-1 if the figure is not available". */
    assert_int_equal(plain_delay, -1);
    assert_int_equal(plain_padding, -1);

    /* With the tag they arrive, and describe a real encode. */
    assert_true(tag_delay > 0);
    assert_true(tag_padding >= 0);
    assert_true(tag_delay < FRAME);
    assert_true(tag_padding < 4 * FRAME);

    free(plain);
    free(tagged);
    free(pcm_l);
    free(pcm_r);
}

/**
 * @brief Checks that hip_decode1_headersB() also returns the frame
 *        description, delay and padding on a call that needs more input.
 *
 * The test makes two such calls:
 * - The first call of a stream, with too few bytes for a header. It has
 *   nothing to report and must say so. header_parsed is 0, and the delay and
 *   padding are -1.
 * - A call after a stream with a LAME tag was decoded to its end. It must
 *   report the delay and padding from the tag.
 *
 * The outputs start with values that no result can have: -2, and a byte
 * pattern. So the test sees a call that writes nothing.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_headersB_answers_while_it_needs_more_input(LAME_UNUSED void **state)
{
    unsigned char *tagged = malloc(MP3CAP);
    short  *pcm_l = malloc(PCMCAP * sizeof(short));
    short  *pcm_r = malloc(PCMCAP * sizeof(short));
    mp3data_struct mp3data;
    hip_t   hip;
    int     taggedlen, total, n;
    int     tag_delay = -999, tag_padding = -999;
    int     delay = -2, padding = -2;

    assert_non_null(tagged);
    assert_non_null(pcm_l);
    assert_non_null(pcm_r);
    taggedlen = encode_a_stream(tagged, MP3CAP, 1);
    assert_true(taggedlen > 4);

    hip = hip_decode_init();
    if (hip == NULL) {
        free(tagged);
        free(pcm_l);
        free(pcm_r);
        skip();         /* no decoder in this build */
    }
    memset(&mp3data, 0x55, sizeof(mp3data));
    n = hip_decode1_headersB(hip, tagged, 4, pcm_l, pcm_r, &mp3data, &delay, &padding);
    assert_int_equal(n, 0);
    assert_int_equal(mp3data.header_parsed, 0);
    assert_int_equal(delay, -1);
    assert_int_equal(padding, -1);
    assert_int_equal(hip_decode_exit(hip), 0);

    hip = hip_decode_init();
    assert_non_null(hip);
    total = drain_headersB(hip, tagged, taggedlen, pcm_l, pcm_r, &mp3data,
                           &tag_delay, &tag_padding);
    assert_true(total > 0);
    assert_true(tag_delay > 0);
    delay = padding = -2;
    n = hip_decode1_headersB(hip, NULL, 0, pcm_l, pcm_r, &mp3data, &delay, &padding);
    assert_int_equal(n, 0);
    assert_int_equal(delay, tag_delay);
    assert_int_equal(padding, tag_padding);
    assert_int_equal(hip_decode_exit(hip), 0);

    free(tagged);
    free(pcm_l);
    free(pcm_r);
}

/**
 * @brief Checks that every decoding function rejects a NULL decoder instance.
 *
 * hip_decode_init() returns NULL where the library has no decoder. Its
 * documentation says that a caller who does not check the result gets an
 * error from every decode call. For this to be true, all five functions must
 * return -1. It must be true in both builds, because the library exports
 * these functions with or without a decoder.
 *
 * A dereference here stops the test on a signal. CMocka reports the signal as
 * a failure of this test, and runs the next test.
 */
static void
test_decode_calls_refuse_a_null_handle(LAME_UNUSED void **state)
{
    short   pcm_l[FRAME], pcm_r[FRAME];
    unsigned char buf[64];
    mp3data_struct mp3data;
    int     enc_delay = -999, enc_padding = -999;

    memset(buf, 0, sizeof(buf));
    memset(&mp3data, 0, sizeof(mp3data));

    assert_int_equal(hip_decode1_headersB(NULL, buf, sizeof(buf), pcm_l, pcm_r,
                                          &mp3data, &enc_delay, &enc_padding), -1);
    assert_int_equal(hip_decode1_headers(NULL, buf, sizeof(buf), pcm_l, pcm_r,
                                         &mp3data), -1);
    assert_int_equal(hip_decode1(NULL, buf, sizeof(buf), pcm_l, pcm_r), -1);
    assert_int_equal(hip_decode_headers(NULL, buf, sizeof(buf), pcm_l, pcm_r,
                                        &mp3data), -1);
    assert_int_equal(hip_decode(NULL, buf, sizeof(buf), pcm_l, pcm_r), -1);
}

/**
 * @brief Checks that the gapless decoder instance decodes like any other.
 *
 * libmpg123 does what is different about it. It removes the delay and
 * padding from the LAME tag inside the decoder, so the caller does not have
 * to. So the test checks only the part that belongs to this library. Either
 * the function returns a working decoder instance, or it returns NULL in a
 * build with no decoder, as hip_decode_init() does.
 */
static void
test_gapless_handle_decodes(LAME_UNUSED void **state)
{
    unsigned char *mp3 = malloc(MP3CAP);
    short  *pcm_l = malloc(PCMCAP * sizeof(short));
    short  *pcm_r = malloc(PCMCAP * sizeof(short));
    hip_t   hip;
    int     mp3len, n;

    assert_non_null(mp3);
    assert_non_null(pcm_l);
    assert_non_null(pcm_r);

    mp3len = encode_a_stream(mp3, MP3CAP, 1);
    assert_true(mp3len > 0);

    hip = hip_decode_init_gapless();
    if (hip == NULL) {
        /* The two constructors answer the same question about this build, so
           disagreeing is itself a defect - one of them handing back a handle
           the other refuses would leave a caller with no reliable way to ask
           whether decoding is available. */
        hip_t   plain = hip_decode_init();

        assert_null(plain);
        free(mp3);
        free(pcm_l);
        free(pcm_r);
        skip();         /* no decoder in this build */
    }

    n = hip_decode(hip, mp3, (size_t) mp3len, pcm_l, pcm_r);
    assert_true(n > 0);
    assert_true(n <= SAMPLES_IN + 4 * FRAME);
    assert_int_equal(hip_decode_exit(hip), 0);

    free(mp3);
    free(pcm_l);
    free(pcm_r);
}

/**
 * @brief Checks that the obsolete decoder functions do nothing.
 *
 * The library keeps them so that programs built against an older release
 * still link. The global decoder that they used does not exist in this
 * library. The test checks two things:
 * - They return fixed values. So they cannot start to return a value that a
 *   caller acts on.
 * - They do not write to the buffers that the caller passes. Their
 *   documentation in mpglib_interface.c says this. A caller that ignores the
 *   -1 reads what was in its output buffer before the call. The
 *   documentation is correct only if the buffer stays unchanged.
 *
 * They need no decoder, so this test runs in every build.
 */
static void
test_obsolete_decoders_are_inert(LAME_UNUSED void **state)
{
    short   pcm_l[FRAME], pcm_r[FRAME];
    unsigned char buf[64];
    mp3data_struct mp3data;
    int     enc_delay = -999, enc_padding = -999;
    int     i, disturbed = 0;

    memset(buf, 0, sizeof(buf));
    memset(&mp3data, 0, sizeof(mp3data));
    for (i = 0; i < FRAME; i++)
        pcm_l[i] = pcm_r[i] = SENTINEL;

    /* The two that report success, having created and destroyed nothing. */
    assert_int_equal(lame_decode_init(), 0);
    assert_int_equal(lame_decode_exit(), 0);

    /* The five that report failure, having decoded nothing. */
    assert_int_equal(lame_decode(buf, (int) sizeof(buf), pcm_l, pcm_r), -1);
    assert_int_equal(lame_decode1(buf, (int) sizeof(buf), pcm_l, pcm_r), -1);
    assert_int_equal(lame_decode_headers(buf, (int) sizeof(buf), pcm_l, pcm_r,
                                         &mp3data), -1);
    assert_int_equal(lame_decode1_headers(buf, (int) sizeof(buf), pcm_l, pcm_r,
                                          &mp3data), -1);
    assert_int_equal(lame_decode1_headersB(buf, (int) sizeof(buf), pcm_l, pcm_r,
                                           &mp3data, &enc_delay, &enc_padding), -1);

    for (i = 0; i < FRAME; i++)
        if (pcm_l[i] != SENTINEL || pcm_r[i] != SENTINEL)
            disturbed++;
    assert_int_equal(disturbed, 0);

    /* The out parameters are untouched on the same terms. */
    assert_int_equal(enc_delay, -999);
    assert_int_equal(enc_padding, -999);
    assert_int_equal(mp3data.header_parsed, 0);
    assert_int_equal(mp3data.samplerate, 0);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_analysis_hooks_tolerate_a_null_handle),
        cmocka_unit_test(test_finish_pinfo_without_a_block),
        cmocka_unit_test(test_decode_exit_accepts_a_null_handle),
        cmocka_unit_test(test_reporting_setters_accept_a_handle_or_null),
        cmocka_unit_test(test_decode1_headers_round_trip),
        cmocka_unit_test(test_decode_matches_the_piecewise_total),
        cmocka_unit_test(test_headersB_reports_the_tags_delay_and_padding),
        cmocka_unit_test(test_headersB_answers_while_it_needs_more_input),
        cmocka_unit_test(test_decode_calls_refuse_a_null_handle),
        cmocka_unit_test(test_gapless_handle_decodes),
        cmocka_unit_test(test_obsolete_decoders_are_inert),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
