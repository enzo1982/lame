/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the decoder handle's public entry points.
 *
 * Three groups, with different prerequisites:
 *
 * The analysis hooks and the handle's lifecycle are the part of this API a
 * frontend wires up without necessarily having a decoder to wire it to: a
 * library built without libmpg123 hands back no handle at all, and a frontend
 * that plots what the decoder saw may install no block. Both calls therefore
 * have to survive being handed nothing, which is what the header undertakes
 * and what is checked here. Reaching the end of a case is the assertion in the
 * two hook tests: the failure they guard against is a dereference, so a
 * regressed build leaves the case on a signal rather than on a failed
 * comparison.
 *
 * The decoding calls need a decoder, so they encode a short stream with this
 * same library and read it back. They skip where hip_decode_init() returns
 * NULL rather than assert a decode that could not be attempted.
 *
 * The obsolete lame_decode* entry points need nothing at all: they are inert
 * in every build, and what is pinned is that they stay inert - fixed answers,
 * and output buffers they never touch.
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
#define NSAMPLES    4608        /**< four granules per encode call */
#define NCALLS      6
#define MP3CAP      (NCALLS * (NSAMPLES * 5 / 4 + 7200) + 7200)
#define SAMPLES_IN  (NSAMPLES * NCALLS)
#define FRAME       1152        /**< samples per channel in one MPEG frame */

/** Room for every frame the input can yield, which is what hip_decode() needs
    the caller to provide - it has no bound of its own to enforce. */
#define PCMCAP      (SAMPLES_IN + 16 * FRAME)

/** A value no decoded sample of this signal can take, so "was not written" is
    distinguishable from "was written with a plausible number". */
#define SENTINEL    0x5A5A

/** Spelled out rather than taken from math.h: M_PI is not standard C, and
    whether it is visible depends on which feature-test macros the compiler
    happens to have set. */
#define PI          3.14159265358979323846

/**
 * @brief Encode a short stereo stream with this library.
 *
 * @param mp3       receives the encoded stream.
 * @param cap       how much room @a mp3 has.
 * @param with_tag  write the real LAME tag over the frame the encoder reserved
 *                  for it. The delay and padding figures live there, so it is
 *                  what separates a stream those can be recovered from.
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
 * @brief Decode a whole stream one frame at a time, reporting delay and padding.
 *
 * The loop the header describes: feed the input on the first call, then keep
 * calling with a length of 0 to drain what the decoder still holds.
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
 * @brief Neither analysis hook dereferences a handle it was not given.
 *
 * A caller that never obtained a decoder - which is every caller on a library
 * built without libmpg123, since hip_decode_init() then returns NULL - still
 * reaches the frontend's shutdown path, and the header promises both calls do
 * nothing rather than that they are unreachable.
 */
static void
test_analysis_hooks_tolerate_a_null_handle(LAME_UNUSED void **state)
{
    hip_set_pinfo(NULL, NULL);
    hip_finish_pinfo(NULL);
}

/**
 * @brief hip_finish_pinfo() does nothing when no block was installed.
 *
 * The other half of the same promise, on a handle that really exists. Skipped
 * where the library cannot hand one out, rather than passing on the strength of
 * a call that was never made.
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
 * @brief Releasing a handle that was never obtained reports success.
 *
 * hip_decode_exit() accepts NULL, so a frontend can call it unconditionally on
 * a path where the handle may never have been created - including the one where
 * the library has no decoder to hand out.
 */
static void
test_decode_exit_accepts_a_null_handle(LAME_UNUSED void **state)
{
    assert_int_equal(hip_decode_exit(NULL), 0);
}

/**
 * @brief The three reporting setters accept anything and report nothing.
 *
 * They are documented as accepting the callback and discarding it, on a handle
 * or on NULL, so what is checked is that all six calls return - the failure
 * they could have is a dereference, not a wrong answer. A caller that installs
 * all six reporting callbacks, encoder and decoder, is the reason they exist.
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
 * @brief A stream this library encoded decodes back through hip_decode1_headers().
 *
 * The round trip is the only way to check that the frame description reaches
 * the caller: the rate, the channel count and the bitrate come out of the
 * frame header, so they can be compared against what the encode was told to
 * do. The sample count is checked as a range rather than a figure - a decoder
 * hands back whole frames and the encoder pads to a frame boundary, so the
 * exact total is a property of both and not a contract of either.
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
 * @brief hip_decode() returns in one call what hip_decode1() returns in pieces.
 *
 * That is the whole of what hip_decode() undertakes - it repeats hip_decode1()
 * until there is nothing left - so the two totals agreeing is the contract
 * itself rather than an incidental property. hip_decode_headers() is the same
 * loop with the frame description, and is checked alongside so that all three
 * are known to agree.
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
 * @brief hip_decode1_headersB() recovers the delay and padding from the tag.
 *
 * Those two figures are the only reason to call it rather than
 * hip_decode1_headers(), and they are carried by the LAME tag - so the same
 * audio is encoded twice, once with that tag written and once without, and the
 * assertion is on the difference. Without both arms, "-1 and -1" from an
 * untagged stream would satisfy a test that only ever saw one.
 *
 * The figures themselves are checked for being present and sane rather than
 * for a particular value: the delay is the encoder's, so pinning it here would
 * make an encoder change break a decoder test.
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
 * @brief hip_decode1_headersB() refuses a handle it was not given.
 *
 * The header states its return is -1 on an error including a NULL handle, and
 * that a caller who did not check hip_decode_init() meets the failure at the
 * first decode instead. This is the entry point that keeps that promise, and
 * the one an unchecked caller is most likely to be holding, since it is the
 * one with the delay and padding.
 */
static void
test_headersB_refuses_a_null_handle(LAME_UNUSED void **state)
{
    short   pcm_l[FRAME], pcm_r[FRAME];
    unsigned char buf[64];
    mp3data_struct mp3data;
    int     enc_delay = -999, enc_padding = -999;

    memset(buf, 0, sizeof(buf));
    memset(&mp3data, 0, sizeof(mp3data));
    assert_int_equal(hip_decode1_headersB(NULL, buf, sizeof(buf), pcm_l, pcm_r,
                                          &mp3data, &enc_delay, &enc_padding), -1);
}

/**
 * @brief The gapless handle is a decoder like any other.
 *
 * What it does differently is libmpg123's - it applies the tag's trimming
 * inside the decoder instead of handing the figures out - so what is pinned
 * here is the part that is this library's: it either yields a working handle
 * or, in a build with no decoder, the same NULL its plain counterpart yields.
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
 * @brief The obsolete decoder entry points are inert, and stay that way.
 *
 * They are kept so that programs linked against an older release still
 * resolve; the decoder they drove was global and no longer exists. Two things
 * are pinned. The fixed answers, so that "inert" does not quietly become
 * "returns something a caller might act on"; and that they do not write to the
 * buffers they are handed, which the header makes explicit - a caller that
 * ignores the -1 reads whatever was in its output buffer already, and that is
 * only safe to say if it really is untouched.
 *
 * They need no decoder, so this runs in every build.
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
        cmocka_unit_test(test_headersB_refuses_a_null_handle),
        cmocka_unit_test(test_gapless_handle_decodes),
        cmocka_unit_test(test_obsolete_decoders_are_inert),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
