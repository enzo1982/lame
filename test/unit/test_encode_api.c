/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the encode entry points, the post-encode statistics,
 *        the LAME tag and the reporting calls (libmp3lame/lame.c,
 *        libmp3lame/VbrTag.c).
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API directly. The test compiles no source file of the frontend.
 *
 * The tests check three contracts. No other test in the suite checks them.
 *
 * <b>The integer entry points agree.</b> #lame_encode_buffer and its @c long,
 * @c long2 and @c int forms each expect a different input scale. Each gets
 * the same audio at its own scale. They must then produce the same bitstream.
 * This is not obvious for the shifted forms. lame.h says that the other
 * functions use a different scale, which loses precision for the @c int form.
 * The streams are still identical byte for byte. Each form multiplies its
 * input by the inverse of the power of two that the caller shifted by. A
 * @c float stores this factor without rounding. A test of agreement is useful
 * only if it can also fail. So the test also uses a wrong scale, and that
 * stream must differ.
 *
 * <b>The statistics describe the encode that just ran.</b> The tests check the
 * histograms through their invariants: what they sum to, and whether the two
 * dimensional tables agree with the one dimensional tables. The tests do not
 * check exact counts. Any change to the encoder can change the counts for
 * good reasons.
 *
 * <b>The reporting calls use the report callbacks.</b> #lame_print_config and
 * #lame_print_internals write through the callback that the caller set. A
 * test that only checks that they do not crash does not notice when they
 * write to @c stderr.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include <cmocka.h>

#include "test_mem.h"
#include "test_report.h"

#include "test_unused.h"

#include "lame.h"

/*
 * lame_encode_finish() is obsolete: it is still built and exported so that
 * programs linked against an older release keep working, but its prototype is
 * guarded out of lame.h by DEPRECATED_OR_OBSOLETE_CODE_REMOVED. Declaring it
 * here is what lets that slice of the ABI be tested at all - the same
 * arrangement test_set_get.c uses for the deprecated setters. New code calls
 * lame_encode_flush() and then lame_close().
 */
extern int lame_encode_finish(lame_global_flags *, unsigned char *, int);

/** @brief Samples per channel that one call passes to the encoder. */
#define NSAMPLES 4608
/** @brief Number of encode calls before the flush. */
#define NCALLS   6
/** @brief Output buffer size: the worst case from lame.h, plus the flush. */
#define MP3CAP   (NCALLS * (NSAMPLES * 5 / 4 + 7200) + 7200)

/** @brief Sample rate of every encode in this file. */
#define RATE     44100
/** @brief CBR bitrate, for the tests that need a known bitrate. */
#define CBR_KBPS 128
/** @brief Index of #CBR_KBPS in the 14 slots of lame_bitrate_kbps() and
 *         lame_bitrate_hist(). */
#define CBR_INDEX 8

/** @brief Number of granule and channel slots that each frame adds to a
 *         block type count. */
#define BLOCKS_PER_FRAME 4

/** @brief Left channel of the shared test signal. */
static short pcm_l[NSAMPLES * NCALLS];
/** @brief Right channel of the shared test signal. */
static short pcm_r[NSAMPLES * NCALLS];

/**
 * @brief Fills #pcm_l and #pcm_r with a deterministic stereo signal.
 *
 * Each channel has two tones, at levels like those of a real recording. Four
 * positions contain the smallest and largest value of the type. An off-by-one
 * error in a scale conversion shows at these values. Every scale that the
 * integer entry points use represents them exactly.
 */
static void
make_signal(void)
{
    int     i;

    for (i = 0; i < NSAMPLES * NCALLS; i++) {
        double  t = (double) i / (double) RATE;

        pcm_l[i] = (short) (30000.0 * (0.31 * sin(2.0 * M_PI * 441.0 * t)
                                       + 0.17 * sin(2.0 * M_PI * 1337.0 * t)));
        pcm_r[i] = (short) (30000.0 * (0.29 * sin(2.0 * M_PI * 440.0 * t)
                                       + 0.19 * sin(2.0 * M_PI * 2200.0 * t)));
    }
    pcm_l[100] = SHRT_MIN;
    pcm_l[101] = SHRT_MAX;
    pcm_r[200] = SHRT_MIN;
    pcm_r[201] = SHRT_MAX;
}

/**
 * @brief Creates an initialized encoder instance.
 * @param vbr Nonzero for VBR, zero for CBR at #CBR_KBPS.
 * @param tag Nonzero to reserve and write the LAME tag.
 * @return An initialized lame_t. The caller closes it.
 */
static lame_t
encoder_new(int vbr, int tag)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, RATE), 0);
    assert_int_equal(lame_set_quality(gfp, 5), 0);
    assert_int_equal(lame_set_bWriteVbrTag(gfp, tag), 0);
    if (vbr) {
        assert_int_equal(lame_set_VBR(gfp, vbr_default), 0);
        assert_int_equal(lame_set_VBR_q(gfp, 4), 0);
    }
    else {
        assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
        assert_int_equal(lame_set_brate(gfp, CBR_KBPS), 0);
    }
    assert_int_equal(lame_init_params(gfp), 0);
    return gfp;
}

/** @brief Selects the integer entry point that encode_variant() calls. */
enum variant {
    VAR_SHORT,                  /**< lame_encode_buffer(), +/- 32768.        */
    VAR_LONG,                   /**< lame_encode_buffer_long(), +/- 32768.   */
    VAR_LONG2,                  /**< lame_encode_buffer_long2(), full range. */
    VAR_INT,                    /**< lame_encode_buffer_int(), full range.   */
    VAR_INT_MISSCALED           /**< the int form at the short form's range. */
};

/**
 * @brief Encodes the shared signal through one integer entry point, then
 *        flushes.
 * @param variant the entry point to use.
 * @param out     receives the whole stream.
 * @param cap     size of @p out in bytes.
 * @return Total bytes written, or a negative value from the library.
 *
 * Each buffer is filled at the scale that its own entry point expects. So when
 * two runs differ, the audio differs, not only the units of the samples.
 */
static int
encode_variant(enum variant variant, unsigned char *out, int cap)
{
    static long bl[NSAMPLES], br[NSAMPLES];
    static int il[NSAMPLES], ir[NSAMPLES];
    long const  lscale = (long) 1 << (8 * (int) sizeof(long) - 16);
    int const   iscale = 1 << (8 * (int) sizeof(int) - 16);
    lame_t  gfp = encoder_new(0, 0);
    int     used = 0, call, i, n;

    for (call = 0; call < NCALLS; call++) {
        short const *sl = pcm_l + call * NSAMPLES;
        short const *sr = pcm_r + call * NSAMPLES;

        switch (variant) {
        case VAR_SHORT:
            n = lame_encode_buffer(gfp, sl, sr, NSAMPLES, out + used, cap - used);
            break;
        case VAR_LONG:
            for (i = 0; i < NSAMPLES; i++) {
                bl[i] = (long) sl[i];
                br[i] = (long) sr[i];
            }
            n = lame_encode_buffer_long(gfp, bl, br, NSAMPLES, out + used, cap - used);
            break;
        case VAR_LONG2:
            for (i = 0; i < NSAMPLES; i++) {
                bl[i] = (long) sl[i] * lscale;
                br[i] = (long) sr[i] * lscale;
            }
            n = lame_encode_buffer_long2(gfp, bl, br, NSAMPLES, out + used, cap - used);
            break;
        case VAR_INT:
            for (i = 0; i < NSAMPLES; i++) {
                il[i] = (int) sl[i] * iscale;
                ir[i] = (int) sr[i] * iscale;
            }
            n = lame_encode_buffer_int(gfp, il, ir, NSAMPLES, out + used, cap - used);
            break;
        case VAR_INT_MISSCALED:
            for (i = 0; i < NSAMPLES; i++) {
                il[i] = (int) sl[i];
                ir[i] = (int) sr[i];
            }
            n = lame_encode_buffer_int(gfp, il, ir, NSAMPLES, out + used, cap - used);
            break;
        default:
            fail_msg("unknown variant %d", (int) variant);
            n = -1;
            break;
        }
        assert_true(n >= 0);
        used += n;
    }
    n = lame_encode_flush(gfp, out + used, cap - used);
    assert_true(n >= 0);
    used += n;
    lame_close(gfp);
    return used;
}

/**
 * @brief Encodes the shared signal through the short entry point, then
 *        flushes.
 * @param gfp an initialized encoder instance. It stays open for the
 *            statistics calls.
 * @param out receives the whole stream.
 * @param cap size of @p out in bytes.
 * @return Total bytes written.
 */
static int
encode_and_flush(lame_t gfp, unsigned char *out, int cap)
{
    int     used = 0, call, n;

    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, out + used, cap - used);
        assert_true(n >= 0);
        used += n;
    }
    n = lame_encode_flush(gfp, out + used, cap - used);
    assert_true(n >= 0);
    return used + n;
}

/**
 * @brief Sums an integer array.
 * @param a the array.
 * @param n the number of elements.
 * @return The sum of the elements.
 */
static int
sum_of(const int *a, int n)
{
    int     i, s = 0;

    for (i = 0; i < n; i++)
        s += a[i];
    return s;
}

/**
 * @brief Checks that the four integer entry points produce the same
 *        bitstream.
 * @param state cmocka fixture state (unused).
 *
 * Each one expects a different scale, but the audio is the same. The contract
 * is identical bytes. The result is exact, not approximate, because every
 * scale here is a power of two.
 */
static void
test_integer_variants_agree(LAME_UNUSED void **state)
{
    static unsigned char a[MP3CAP], b[MP3CAP];
    int     na, nb;

    na = encode_variant(VAR_SHORT, a, MP3CAP);
    assert_true(na > 1000);

    nb = encode_variant(VAR_LONG, b, MP3CAP);
    assert_int_equal(nb, na);
    assert_int_equal(memcmp(a, b, (size_t) na), 0);

    nb = encode_variant(VAR_LONG2, b, MP3CAP);
    assert_int_equal(nb, na);
    assert_int_equal(memcmp(a, b, (size_t) na), 0);

    nb = encode_variant(VAR_INT, b, MP3CAP);
    assert_int_equal(nb, na);
    assert_int_equal(memcmp(a, b, (size_t) na), 0);
}

/**
 * @brief Checks that the int entry point at the wrong scale produces a
 *        different stream.
 * @param state cmocka fixture state (unused).
 *
 * This is the control for the test above. It stays in the suite, so it runs
 * every time. It fails if the entry points stop reading their samples. In that
 * case, the test above passes for no real reason.
 */
static void
test_int_wrong_scaling_differs(LAME_UNUSED void **state)
{
    static unsigned char a[MP3CAP], b[MP3CAP];
    int     na, nb;

    na = encode_variant(VAR_SHORT, a, MP3CAP);
    nb = encode_variant(VAR_INT_MISSCALED, b, MP3CAP);
    assert_true(na > 1000);
    assert_true(nb > 0);
    if (na == nb)
        assert_int_not_equal(memcmp(a, b, (size_t) na), 0);
}

/**
 * @brief Checks that lame_bitrate_kbps() returns the MPEG-1 Layer III bitrate
 *        table.
 * @param state cmocka fixture state (unused).
 *
 * The table belongs to the format, not to the encoder. So the test can check
 * exact values. The table gives the meaning of the 14 slots of the bitrate
 * histogram.
 */
static void
test_bitrate_kbps_is_the_mpeg1_table(LAME_UNUSED void **state)
{
    static const int expect[14] = {
        32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
    };
    int     kbps[14], i;
    lame_t  gfp = encoder_new(0, 0);

    lame_bitrate_kbps(gfp, kbps);
    for (i = 0; i < 14; i++)
        assert_int_equal(kbps[i], expect[i]);
    assert_int_equal(kbps[CBR_INDEX], CBR_KBPS);
    lame_close(gfp);
}

/**
 * @brief Checks that a CBR encode counts every frame in the slot for its
 *        bitrate.
 * @param state cmocka fixture state (unused).
 *
 * The frame count comes from lame_get_frameNum(), not from the histogram. So
 * two independent sources must agree.
 */
static void
test_bitrate_hist_counts_cbr_frames(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(0, 0);
    int     hist[14], frames, i;

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    frames = lame_get_frameNum(gfp);
    assert_true(frames > 0);

    lame_bitrate_hist(gfp, hist);
    assert_int_equal(hist[CBR_INDEX], frames);
    assert_int_equal(sum_of(hist, 14), frames);
    for (i = 0; i < 14; i++)
        if (i != CBR_INDEX)
            assert_int_equal(hist[i], 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a VBR encode spreads its frames over more than one
 *        bitrate.
 * @param state cmocka fixture state (unused).
 *
 * The histogram exists to show the distribution. This test sees a real
 * distribution. So the result of the CBR test above is not a coincidence. The
 * encoder decides which bitrates it uses, so the test does not check them.
 */
static void
test_bitrate_hist_counts_vbr_frames(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(1, 0);
    int     hist[14], frames, i, used = 0;

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    frames = lame_get_frameNum(gfp);
    assert_true(frames > 0);

    lame_bitrate_hist(gfp, hist);
    assert_int_equal(sum_of(hist, 14), frames);
    for (i = 0; i < 14; i++)
        if (hist[i] > 0)
            used++;
    assert_true(used > 1);
    lame_close(gfp);
}

/**
 * @brief Checks that the stereo mode histogram counts every frame exactly
 *        once.
 * @param state cmocka fixture state (unused).
 */
static void
test_stereo_mode_hist_counts_frames(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(0, 0);
    int     stmode[4], frames;

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    frames = lame_get_frameNum(gfp);
    assert_true(frames > 0);

    lame_stereo_mode_hist(gfp, stmode);
    assert_int_equal(sum_of(stmode, 4), frames);
    lame_close(gfp);
}

/**
 * @brief Checks that the block type histogram stores its own total in the
 *        last slot.
 * @param state cmocka fixture state (unused).
 *
 * Five slots count block types, and the sixth slot is their sum. So the whole
 * array sums to twice the number of blocks. Each frame adds #BLOCKS_PER_FRAME
 * blocks: two granules for each of two channels. So the total also depends
 * on the frame count.
 */
static void
test_block_type_hist_totals(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(0, 0);
    int     btype[6], frames;

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    frames = lame_get_frameNum(gfp);
    assert_true(frames > 0);

    lame_block_type_hist(gfp, btype);
    assert_int_equal(btype[5], sum_of(btype, 5));
    assert_int_equal(btype[5], frames * BLOCKS_PER_FRAME);
    lame_close(gfp);
}

/**
 * @brief Checks that the two dimensional histograms agree with the one
 *        dimensional histograms.
 * @param state cmocka fixture state (unused).
 *
 * Each table counts the same frames, split in a second way. So every row must
 * sum to the frame count of its bitrate. In the block type table, every row
 * must sum to the number of blocks in those frames. The last column again
 * stores the total of the row.
 */
static void
test_two_dimensional_hists_agree(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(1, 0);
    int     hist[14], brst[14][4], brbt[14][6];
    int     i, j, row;

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    lame_bitrate_hist(gfp, hist);
    lame_bitrate_stereo_mode_hist(gfp, brst);
    lame_bitrate_block_type_hist(gfp, brbt);

    for (i = 0; i < 14; i++) {
        row = 0;
        for (j = 0; j < 4; j++)
            row += brst[i][j];
        assert_int_equal(row, hist[i]);

        row = 0;
        for (j = 0; j < 5; j++)
            row += brbt[i][j];
        assert_int_equal(row, brbt[i][5]);
        assert_int_equal(brbt[i][5], hist[i] * BLOCKS_PER_FRAME);
    }
    lame_close(gfp);
}

/**
 * @brief Checks that lame_init_bitstream() clears the statistics that its
 *        documentation names.
 * @param state cmocka fixture state (unused).
 *
 * The counters must be nonzero first. Without this, the test passes against a
 * library that never counts anything.
 */
static void
test_init_bitstream_clears_statistics(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(0, 0);
    int     hist[14], btype[6], stmode[4];

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    lame_bitrate_hist(gfp, hist);
    lame_block_type_hist(gfp, btype);
    lame_stereo_mode_hist(gfp, stmode);
    assert_true(sum_of(hist, 14) > 0);
    assert_true(sum_of(btype, 6) > 0);
    assert_true(sum_of(stmode, 4) > 0);

    assert_int_equal(lame_init_bitstream(gfp), 0);

    lame_bitrate_hist(gfp, hist);
    lame_block_type_hist(gfp, btype);
    lame_stereo_mode_hist(gfp, stmode);
    assert_int_equal(sum_of(hist, 14), 0);
    assert_int_equal(sum_of(btype, 6), 0);
    assert_int_equal(sum_of(stmode, 4), 0);
    assert_int_equal(lame_get_frameNum(gfp), 0);
    lame_close(gfp);
}

/**
 * @brief Checks that the same encoder instance continues to encode after
 *        lame_encode_flush_nogap().
 * @param state cmocka fixture state (unused).
 *
 * This is the purpose of the call. It completes the MP3 data so far, and it
 * does not write an ID3v1 tag. The encoder instance stays usable for the next
 * stream.
 */
static void
test_flush_nogap_allows_continuing(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(0, 0);
    int     used = 0, call, n, first;

    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, mp3 + used, MP3CAP - used);
        assert_true(n >= 0);
        used += n;
    }
    n = lame_encode_flush_nogap(gfp, mp3 + used, MP3CAP - used);
    assert_true(n > 0);
    used += n;
    first = used;

    assert_int_equal(lame_init_bitstream(gfp), 0);
    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, mp3 + used, MP3CAP - used);
        assert_true(n >= 0);
        used += n;
    }
    assert_true(used > first);
    n = lame_encode_flush(gfp, mp3 + used, MP3CAP - used);
    assert_true(n >= 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a call whose output does not fit can be repeated, and
 *        that the encoder continues when there is room.
 *
 * Each call passes twenty frames of noise and room for about four frames. So
 * the call returns -1 after it encodes a few frames. The frame that did not
 * fit stays in the encoder. At least one call must return -1. Otherwise the
 * test did not cover the case.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_small_buffer_calls_repeat(LAME_UNUSED void **state)
{
    enum { FRAMES = 20 * 1152, CALLS = 20, SMALL = 2000 };
    static short pcm[2 * FRAMES];
    static unsigned char mp3[LAME_MAXMP3BUFFER];
    unsigned int s = 12345u;
    int     i, call, n, refused = 0;
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_true(lame_init_params(gfp) >= 0);
    for (i = 0; i < 2 * FRAMES; i++) {
        s = s * 1103515245u + 12345u;
        pcm[i] = (short) (s >> 16);
    }
    for (call = 0; call < CALLS; call++) {
        n = lame_encode_buffer_interleaved(gfp, pcm, FRAMES, mp3, SMALL);
        if (n < 0)
            refused++;
    }
    assert_true(refused > 0);
    n = lame_encode_buffer_interleaved(gfp, pcm, 1152, mp3, sizeof mp3);
    assert_true(n >= 0);
    n = lame_encode_flush(gfp, mp3, sizeof mp3);
    assert_true(n >= 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a negative sample count fails with LAME_BADINPUTDATA.
 *
 * After this, the encoder accepts samples as usual.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_negative_count_refused(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    static short interleaved[2 * NSAMPLES];
    lame_t  gfp = encoder_new(0, 0);

    assert_int_equal(lame_encode_buffer(gfp, pcm_l, pcm_r, -1, mp3, MP3CAP), LAME_BADINPUTDATA);
    assert_int_equal(lame_encode_buffer(gfp, pcm_l, pcm_r, INT_MIN, mp3, MP3CAP), LAME_BADINPUTDATA);
    assert_int_equal(lame_encode_buffer_interleaved(gfp, interleaved, -1, mp3, MP3CAP),
                     LAME_BADINPUTDATA);
    assert_true(lame_encode_buffer(gfp, pcm_l, pcm_r, NSAMPLES, mp3, MP3CAP) >= 0);
    assert_true(lame_encode_flush(gfp, mp3, MP3CAP) > 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a NULL output buffer fails like a buffer that is too
 *        small, once there are bytes to write.
 * @param state cmocka fixture state (unused).
 *
 * A call that produces no bytes yet may pass NULL. A call that produces bytes
 * returns -1. This is true with a size of 0 ("do not check the size") and
 * with a given size.
 */
static void
test_null_output_buffer_refused(LAME_UNUSED void **state)
{
    lame_t  gfp = encoder_new(0, 0);
    int     i, n = 0;

    assert_int_equal(lame_encode_buffer(gfp, pcm_l, pcm_r, 64, NULL, 0), 0);
    for (i = 0; i < NCALLS && n == 0; i++)
        n = lame_encode_buffer(gfp, pcm_l + i * NSAMPLES, pcm_r + i * NSAMPLES, NSAMPLES, NULL, 0);
    assert_int_equal(n, -1);
    assert_int_equal(lame_encode_buffer(gfp, pcm_l, pcm_r, NSAMPLES, NULL, MP3CAP), -1);
    assert_int_equal(lame_encode_flush(gfp, NULL, MP3CAP), -1);
    lame_close(gfp);
}

/**
 * @brief Checks that a call without an output buffer returns 0 while the
 *        frames that it encodes produce no bytes.
 * @param state cmocka fixture state (unused).
 *
 * With silent input, the first frame produces no bytes.
 */
static void
test_null_output_buffer_empty_frame(LAME_UNUSED void **state)
{
    static short silence[NSAMPLES];
    lame_t  gfp = encoder_new(0, 0);
    int const n = lame_get_framesize(gfp);

    assert_true(n <= NSAMPLES);
    assert_int_equal(lame_encode_buffer(gfp, silence, silence, n, NULL, 0), 0);
    assert_int_equal(lame_encode_buffer(gfp, silence, silence, n, NULL, 0), 0);
    assert_true(lame_get_frameNum(gfp) > 0);
    lame_close(gfp);
}

/**
 * @brief Checks that a sample louder than 4096 times full scale, after all
 *        scaling factors, returns LAME_BADINPUTDATA.
 *
 * Input up to about 4000 times full scale is encoded. Floating point input
 * can exceed the limit by itself. 16-bit input can exceed it only through the
 * scaling factors. The setters cannot limit the product of the factors. The
 * bitrate presets scale the input by 0.95 to 1. So the rejected sample is
 * well above 4096, not just above it.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_input_beyond_bound_refused(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    static float fl[NSAMPLES], fr[NSAMPLES];
    lame_t  gfp = encoder_new(0, 0);
    int     i;

    for (i = 0; i < NSAMPLES; i++)
        fl[i] = fr[i] = 4000.0f * (float) pcm_l[i] / 32768.0f;
    assert_true(lame_encode_buffer_ieee_float(gfp, fl, fr, NSAMPLES, mp3, MP3CAP) >= 0);
    fl[NSAMPLES / 2] = 5000.0f;
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, fl, fr, NSAMPLES, mp3, MP3CAP),
                     LAME_BADINPUTDATA);
    fl[NSAMPLES / 2] = -5000.0f;
    assert_int_equal(lame_encode_buffer_ieee_float(gfp, fl, fr, NSAMPLES, mp3, MP3CAP),
                     LAME_BADINPUTDATA);
    assert_true(lame_encode_flush(gfp, mp3, MP3CAP) > 0);
    lame_close(gfp);

    gfp = lame_init();
    assert_non_null(gfp);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, RATE), 0);
    assert_int_equal(lame_set_scale(gfp, 4096.0f), 0);
    assert_int_equal(lame_set_scale_left(gfp, 2.0f), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    assert_int_equal(lame_encode_buffer(gfp, pcm_l, pcm_r, NSAMPLES, mp3, MP3CAP),
                     LAME_BADINPUTDATA);
    lame_close(gfp);
}

/**
 * @brief Reads the encoder delay and padding from a LAME tag frame.
 * @param frame   the frame that lame_get_lametag_frame() filled.
 * @param n       the length of the frame.
 * @param delay   receives the delay field.
 * @param padding receives the padding field.
 *
 * The two 12-bit fields start 21 bytes after the Xing fields. The flag word
 * sets the length of the Xing fields. This function reads the frame in the
 * same way as the tag reader of the library. The function must find the
 * marker. Otherwise a test of the fields reads two zero bytes as a delay
 * of 0.
 */
static void
lametag_delay_padding(const unsigned char *frame, size_t n, int *delay, int *padding)
{
    size_t  at = 0, i;
    unsigned long flags;

    for (i = 0; i + 4 <= n; i++) {
        if (memcmp(frame + i, "Xing", 4) == 0 || memcmp(frame + i, "Info", 4) == 0) {
            at = i;
            break;
        }
    }
    assert_true(at > 0);
    assert_true(at + 8 <= n);
    flags = ((unsigned long) frame[at + 4] << 24) | ((unsigned long) frame[at + 5] << 16)
        | ((unsigned long) frame[at + 6] << 8) | frame[at + 7];
    at += 8;
    if (flags & 1)
        at += 4;        /* frames */
    if (flags & 2)
        at += 4;        /* bytes */
    if (flags & 4)
        at += 100;      /* toc */
    if (flags & 8)
        at += 4;        /* vbr scale */
    at += 21;
    assert_true(at + 3 <= n);
    *delay = (frame[at] << 4) | (frame[at + 1] >> 4);
    *padding = ((frame[at + 1] & 0x0f) << 8) | frame[at + 2];
}

/**
 * @brief Checks that the LAME tag of a later file in a nogap set reports no
 *        encoder delay.
 * @param state cmocka fixture state (unused).
 *
 * The encoder writes its lead-in once, at the start of the first file of a
 * set. The next file starts with audio. Its LAME tag must say so. Otherwise a
 * reader that trims by the delay field drops real samples from every file
 * after the first. The LAME tag of the first file is the control. It reports
 * the delay. The LAME tag of the last file reports the padding that the final
 * flush computed.
 */
static void
test_lametag_delay_zero_after_nogap_flush(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    unsigned char frame[2048];
    lame_t  gfp = encoder_new(0, 1);
    int     used = 0, call, n;
    int     delay, padding;
    size_t  got;

    assert_int_equal(lame_set_nogap_total(gfp, 2), 0);
    assert_int_equal(lame_set_nogap_currentindex(gfp, 0), 0);
    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, mp3 + used, MP3CAP - used);
        assert_true(n >= 0);
        used += n;
    }
    n = lame_encode_flush_nogap(gfp, mp3 + used, MP3CAP - used);
    assert_true(n > 0);
    got = lame_get_lametag_frame(gfp, frame, sizeof frame);
    assert_true(got > 0);
    lametag_delay_padding(frame, got, &delay, &padding);
    assert_int_equal(delay, lame_get_encoder_delay(gfp));
    assert_true(delay > 0);
    assert_int_equal(padding, 0);

    assert_int_equal(lame_init_bitstream(gfp), 0);
    assert_int_equal(lame_set_nogap_currentindex(gfp, 1), 0);
    used = 0;
    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, mp3 + used, MP3CAP - used);
        assert_true(n >= 0);
        used += n;
    }
    n = lame_encode_flush(gfp, mp3 + used, MP3CAP - used);
    assert_true(n >= 0);
    got = lame_get_lametag_frame(gfp, frame, sizeof frame);
    assert_true(got > 0);
    lametag_delay_padding(frame, got, &delay, &padding);
    assert_int_equal(delay, 0);
    assert_int_equal(padding, lame_get_encoder_padding(gfp));
    assert_true(padding > 0);
    lame_close(gfp);
}

/**
 * @brief Checks that lame_encode_finish() works as lame_encode_flush() plus
 *        lame_close().
 * @param state cmocka fixture state (unused).
 *
 * The function is obsolete, but the library still exports it. So it needs a
 * test. It promises the combination of the two calls. So the same audio must
 * give the same stream both ways. The test must not close the encoder
 * instance again, because lame_encode_finish() already closed it.
 */
static void
test_encode_finish_matches_flush_then_close(LAME_UNUSED void **state)
{
    static unsigned char viaflush[MP3CAP], viafinish[MP3CAP];
    lame_t  gfp;
    int     nflush = 0, nfinish = 0, call, n;

    gfp = encoder_new(0, 0);
    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, viaflush + nflush, MP3CAP - nflush);
        assert_true(n >= 0);
        nflush += n;
    }
    n = lame_encode_flush(gfp, viaflush + nflush, MP3CAP - nflush);
    assert_true(n > 0);
    nflush += n;
    lame_close(gfp);

    gfp = encoder_new(0, 0);
    for (call = 0; call < NCALLS; call++) {
        n = lame_encode_buffer(gfp, pcm_l + call * NSAMPLES, pcm_r + call * NSAMPLES,
                               NSAMPLES, viafinish + nfinish, MP3CAP - nfinish);
        assert_true(n >= 0);
        nfinish += n;
    }
    n = lame_encode_finish(gfp, viafinish + nfinish, MP3CAP - nfinish);
    assert_true(n > 0);
    nfinish += n;

    assert_true(nflush > 1000);
    assert_int_equal(nfinish, nflush);
    assert_int_equal(memcmp(viaflush, viafinish, (size_t) nflush), 0);
}

/**
 * @brief Checks that lame_get_lametag_frame() returns the size it needs, and
 *        then fills the buffer.
 * @param state cmocka fixture state (unused).
 *
 * The call does not write to a buffer that is too small. It returns the size
 * that the frame needs, which is larger than the given size. A caller uses
 * this to ask for the size. The sentinel shows that nothing was written. The
 * return value alone does not show this.
 */
static void
test_lametag_frame_reports_required_size(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    unsigned char frame[2048];
    lame_t  gfp = encoder_new(1, 1);
    size_t  need, got;

    (void) encode_and_flush(gfp, mp3, MP3CAP);

    need = lame_get_lametag_frame(gfp, NULL, 0);
    assert_true(need > 0);
    assert_true(need <= sizeof frame);

    memset(frame, 0xa5, sizeof frame);
    got = lame_get_lametag_frame(gfp, frame, 4);
    assert_int_equal((int) got, (int) need);
    assert_true(got > 4);
    assert_int_equal(frame[0], 0xa5);

    memset(frame, 0xa5, sizeof frame);
    got = lame_get_lametag_frame(gfp, frame, sizeof frame);
    assert_int_equal((int) got, (int) need);
    assert_int_equal(frame[0], 0xff);
    assert_int_equal(frame[1] & 0xe0, 0xe0);
    assert_true(mem_contains(frame, got, "Xing"));
    assert_true(mem_contains(frame, got, "LAME"));
    lame_close(gfp);
}

/**
 * @brief Checks that there is no frame to return when the LAME tag is off.
 * @param state cmocka fixture state (unused).
 *
 * This is the control for the test above. The same call on the same audio
 * must be able to return nothing. Otherwise the test above cannot tell a
 * real size from a call that always returns a size.
 */
static void
test_lametag_frame_absent_without_tag(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    unsigned char frame[2048];
    lame_t  gfp = encoder_new(1, 0);

    (void) encode_and_flush(gfp, mp3, MP3CAP);
    assert_int_equal((int) lame_get_lametag_frame(gfp, NULL, 0), 0);
    assert_int_equal((int) lame_get_lametag_frame(gfp, frame, sizeof frame), 0);
    lame_close(gfp);
}

/** @brief The bytes at the start of a written stream that the tag tests compare. */
#define TAG_PROBE_BYTES 512

/**
 * @brief Writes an encoded stream to a file and calls lame_mp3_tags_fid() on
 *        it. Reads the start of the file before and after the call.
 * @param with_tag  1 for an encoder that writes the LAME tag, 0 for one that
 *                  does not.
 * @param before    receives the first #TAG_PROBE_BYTES bytes before the call.
 * @param after     receives the first #TAG_PROBE_BYTES bytes after the call.
 *
 * tmpfile() gives the stream that the documentation asks for: seekable, and
 * open for reading and writing. It also leaves no file behind.
 */
static void
tags_fid_round(int with_tag, unsigned char *before, unsigned char *after)
{
    static unsigned char mp3[MP3CAP];
    lame_t  gfp = encoder_new(1, with_tag);
    FILE   *f;
    int     used;

    used = encode_and_flush(gfp, mp3, MP3CAP);
    assert_true(used > TAG_PROBE_BYTES);

    f = tmpfile();
    assert_non_null(f);
    assert_int_equal((int) fwrite(mp3, 1, (size_t) used, f), used);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    assert_int_equal((int) fread(before, 1, TAG_PROBE_BYTES, f), TAG_PROBE_BYTES);

    lame_mp3_tags_fid(gfp, f);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    assert_int_equal((int) fread(after, 1, TAG_PROBE_BYTES, f), TAG_PROBE_BYTES);

    fclose(f);
    lame_close(gfp);
}

/**
 * @brief Checks that lame_mp3_tags_fid() replaces the reserved frame in a
 *        written stream.
 * @param state cmocka fixture state (unused).
 *
 * LAME reserves a frame at the start of the audio. This frame has no tag
 * until this call writes one into it. So the file must change, and the marker
 * must appear where it was missing before.
 */
static void
test_mp3_tags_fid_writes_the_tag(LAME_UNUSED void **state)
{
    unsigned char before[TAG_PROBE_BYTES], after[TAG_PROBE_BYTES];

    tags_fid_round(1, before, after);
    assert_false(mem_contains(before, TAG_PROBE_BYTES, "Xing"));
    assert_int_not_equal(memcmp(before, after, TAG_PROBE_BYTES), 0);
    assert_true(mem_contains(after, TAG_PROBE_BYTES, "Xing"));
}

/**
 * @brief Checks that lame_mp3_tags_fid() does not change the file when the
 *        LAME tag is off.
 * @param state cmocka fixture state (unused).
 *
 * This is the control for the test above. Without it, a call that rewrites
 * the start of every file also passes.
 */
static void
test_mp3_tags_fid_noop_without_tag(LAME_UNUSED void **state)
{
    unsigned char before[TAG_PROBE_BYTES], after[TAG_PROBE_BYTES];

    tags_fid_round(0, before, after);
    assert_false(mem_contains(before, TAG_PROBE_BYTES, "Xing"));
    assert_int_equal(memcmp(before, after, TAG_PROBE_BYTES), 0);
}

/**
 * @brief Creates a CBR stereo encoder instance that reports through
 *        report_capture(), and empties the collector.
 * @return the instance, after lame_init_params().
 */
static lame_t
capture_encoder_new(void)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_msgf(gfp, report_capture), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, RATE), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_brate(gfp, CBR_KBPS), 0);
    assert_int_equal(lame_init_params(gfp), 0);
    report_reset();
    return gfp;
}

/**
 * @brief Checks that lame_print_config() writes through the report callback
 *        that the caller set.
 * @param state cmocka fixture state (unused).
 *
 * The text belongs to the encoder and changes with the build. The contract is
 * that the text goes to the callback of the caller, not to stderr.
 */
static void
test_print_config_routes_through_callback(LAME_UNUSED void **state)
{
    lame_t  gfp = capture_encoder_new();

    assert_int_equal(report_calls, 0);
    lame_print_config(gfp);
    assert_true(report_calls > 0);
    assert_true(report_len > 0);
    assert_true(mem_contains((const unsigned char *) report_text, report_len, "LAME "));
    lame_close(gfp);
}

/**
 * @brief Checks that lame_print_internals() writes through the report
 *        callback that the caller set.
 * @param state cmocka fixture state (unused).
 *
 * This is a separate test, not a second half of the test above. Each test
 * checks a different exported function. cmocka stops a test at its first
 * failed check. So in a combined test, a failure in the first function hides
 * the result for the second.
 */
static void
test_print_internals_routes_through_callback(LAME_UNUSED void **state)
{
    lame_t  gfp = capture_encoder_new();

    assert_int_equal(report_calls, 0);
    lame_print_internals(gfp);
    assert_true(report_calls > 0);
    assert_true(report_len > 0);
    assert_true(mem_contains((const unsigned char *) report_text, report_len, "stream format:"));
    lame_close(gfp);
}

/**
 * @brief Builds the shared signal once for the whole group.
 * @param state cmocka group state (unused).
 * @return 0.
 */
static int
group_setup(LAME_UNUSED void **state)
{
    make_signal();
    return 0;
}

/** @brief Registers the tests of the encode API and runs them. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_integer_variants_agree),
        cmocka_unit_test(test_int_wrong_scaling_differs),
        cmocka_unit_test(test_bitrate_kbps_is_the_mpeg1_table),
        cmocka_unit_test(test_bitrate_hist_counts_cbr_frames),
        cmocka_unit_test(test_bitrate_hist_counts_vbr_frames),
        cmocka_unit_test(test_stereo_mode_hist_counts_frames),
        cmocka_unit_test(test_block_type_hist_totals),
        cmocka_unit_test(test_two_dimensional_hists_agree),
        cmocka_unit_test(test_init_bitstream_clears_statistics),
        cmocka_unit_test(test_flush_nogap_allows_continuing),
        cmocka_unit_test(test_small_buffer_calls_repeat),
        cmocka_unit_test(test_negative_count_refused),
        cmocka_unit_test(test_null_output_buffer_refused),
        cmocka_unit_test(test_null_output_buffer_empty_frame),
        cmocka_unit_test(test_input_beyond_bound_refused),
        cmocka_unit_test(test_lametag_delay_zero_after_nogap_flush),
        cmocka_unit_test(test_encode_finish_matches_flush_then_close),
        cmocka_unit_test(test_lametag_frame_reports_required_size),
        cmocka_unit_test(test_lametag_frame_absent_without_tag),
        cmocka_unit_test(test_mp3_tags_fid_writes_the_tag),
        cmocka_unit_test(test_mp3_tags_fid_noop_without_tag),
        cmocka_unit_test(test_print_config_routes_through_callback),
        cmocka_unit_test(test_print_internals_routes_through_callback),
    };
    return cmocka_run_group_tests(tests, group_setup, NULL);
}
