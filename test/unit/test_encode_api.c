/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the encode entry points, the post-encode statistics,
 *        the LAME tag and the reporting calls (libmp3lame/lame.c,
 *        libmp3lame/VbrTag.c).
 *
 * These are library-level tests: they link libmp3lame and call the exported
 * API directly, so no frontend translation unit is compiled in.
 *
 * The group covers three contracts that nothing else in the suite states.
 *
 * **The integer entry points agree.** #lame_encode_buffer, and its @c long,
 * @c long2 and @c int forms, each declare a different input scaling; fed the
 * same audio at the scaling each one asks for, they must produce the same
 * bitstream. That the shifted forms come out byte for byte identical is not
 * self-evident - lame.h says the @c int form "cannot, without loosing
 * precision, use the same scaling" - so it was measured before it was asserted
 * here. It holds exactly because each form's internal normalisation is the
 * reciprocal power of two of the shift the caller applies, which a @c float
 * carries without rounding. A test asserting agreement is only worth something
 * if it can also say no, so the wrong scaling is exercised alongside it and
 * must disagree.
 *
 * **The statistics describe the encode that just happened.** The histograms
 * are asserted through their invariants - what they sum to, and whether the
 * two dimensional tables agree with the one dimensional ones - rather than
 * through any particular set of counts, which any future encoder change would
 * move for legitimate reasons.
 *
 * **The reporting calls go through the report callbacks.** #lame_print_config
 * and #lame_print_internals write through the callback the caller installed;
 * a test that only checked they did not crash would not notice them going to
 * @c stderr instead.
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

/** @brief Samples per channel handed to the encoder in one call. */
#define NSAMPLES 4608
/** @brief Number of encode calls before the flush. */
#define NCALLS   6
/** @brief Output capacity, per the worst case in lame.h plus the flush. */
#define MP3CAP   (NCALLS * (NSAMPLES * 5 / 4 + 7200) + 7200)

/** @brief Sample rate every test in this file encodes at. */
#define RATE     44100
/** @brief Constant bit rate used where the test needs a known one. */
#define CBR_KBPS 128
/** @brief Index of #CBR_KBPS in the MPEG-1 Layer III bit rate table. */
#define CBR_INDEX 8

/** @brief Granule/channel slots each frame contributes to a block-type count. */
#define BLOCKS_PER_FRAME 4

/** @brief Left channel of the shared test signal. */
static short pcm_l[NSAMPLES * NCALLS];
/** @brief Right channel of the shared test signal. */
static short pcm_r[NSAMPLES * NCALLS];

/** @brief Text collected by capture_report(). */
static char capture[65536];
/** @brief Bytes currently held in #capture. */
static size_t caplen;
/** @brief Number of times capture_report() has been called. */
static int  capcalls;

/**
 * @brief Report callback that collects what the library writes.
 * @param format printf format string.
 * @param ap     the arguments for @p format.
 */
static void
capture_report(const char *format, va_list ap)
{
    char    line[1024];
    int     n = vsnprintf(line, sizeof line, format, ap);

    capcalls++;
    if (n > 0 && caplen + (size_t) n + 1 < sizeof capture) {
        memcpy(capture + caplen, line, (size_t) n);
        caplen += (size_t) n;
        capture[caplen] = '\0';
    }
}

/** @brief Empties the capture buffer before a call that should fill it. */
static void
capture_reset(void)
{
    caplen = 0;
    capcalls = 0;
    capture[0] = '\0';
}

/**
 * @brief Searches a byte range for a NUL-terminated needle.
 * @param hay  start of the range to search.
 * @param n    length of that range.
 * @param what the string to look for.
 * @return Non-zero when @p what occurs in the range.
 *
 * memmem() is a GNU extension, and this suite builds on three platforms.
 */
static int
mem_contains(const unsigned char *hay, size_t n, const char *what)
{
    size_t  len = strlen(what), i;

    if (len > n)
        return 0;
    for (i = 0; i + len <= n; i++)
        if (memcmp(hay + i, what, len) == 0)
            return 1;
    return 0;
}

/**
 * @brief Fills #pcm_l and #pcm_r with a deterministic stereo signal.
 *
 * Two tones per channel, in the range a real recording occupies, plus the
 * extremes of the type at four positions: those are where an off-by-one in a
 * scaling conversion would show, and they are exactly representable at every
 * scaling the integer entry points use.
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
 * @brief Opens an initialised encoder.
 * @param vbr Non-zero for VBR, zero for CBR at #CBR_KBPS.
 * @param tag Non-zero to reserve and write the LAME tag.
 * @return An initialised lame_t; the caller closes it.
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

/** @brief Which integer entry point encode_variant() drives. */
enum variant {
    VAR_SHORT,                  /**< lame_encode_buffer(), +/- 32768.        */
    VAR_LONG,                   /**< lame_encode_buffer_long(), +/- 32768.   */
    VAR_LONG2,                  /**< lame_encode_buffer_long2(), full range. */
    VAR_INT,                    /**< lame_encode_buffer_int(), full range.   */
    VAR_INT_MISSCALED           /**< the int form at the short form's range. */
};

/**
 * @brief Encodes the shared signal through one integer entry point and flushes.
 * @param variant which entry point to use.
 * @param out     receives the whole stream.
 * @param cap     capacity of @p out.
 * @return Total bytes written, or a negative value from the library.
 *
 * Each buffer is filled at the scaling its own entry point declares, so a
 * disagreement between two runs is a disagreement about the audio and not
 * about the units it arrived in.
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
 * @brief Encodes the shared signal through the short entry point and flushes.
 * @param gfp an initialised encoder, left open for the statistics calls.
 * @param out receives the whole stream.
 * @param cap capacity of @p out.
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
 * @param n its length.
 * @return The sum of its elements.
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
 * @brief The four integer entry points produce the same bitstream.
 * @param state cmocka fixture state (unused).
 *
 * The scaling each one declares differs; the audio does not. Byte identity is
 * the contract, and it is exact rather than approximate because every scaling
 * involved is a power of two.
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
 * @brief The int entry point at the wrong scaling produces a different stream.
 * @param state cmocka fixture state (unused).
 *
 * This is the control for the test above, kept in the suite rather than run
 * once by hand: it fails if the entry points ever stop reading the samples
 * they are given, which is the way the agreement assertion could come to hold
 * for no reason at all.
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
 * @brief lame_bitrate_kbps() reports the MPEG-1 Layer III bit rate table.
 * @param state cmocka fixture state (unused).
 *
 * The table is the format's, not the encoder's, so it is safe to pin exactly:
 * it is what the bit rate histogram's fourteen slots mean.
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
 * @brief A CBR encode puts every frame in the slot for its bit rate.
 * @param state cmocka fixture state (unused).
 *
 * The frame count comes from lame_get_frameNum() rather than from the
 * histogram, so the two have to agree about something neither one defines.
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
 * @brief A VBR encode spreads its frames over more than one bit rate.
 * @param state cmocka fixture state (unused).
 *
 * The point of the histogram is the distribution, so a run that produced one
 * is what makes the CBR case above a statement rather than a coincidence.
 * Which rates get used is the encoder's business and is not asserted.
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
 * @brief The stereo mode histogram accounts for every frame exactly once.
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
 * @brief The block type histogram carries its own total in the last slot.
 * @param state cmocka fixture state (unused).
 *
 * Five slots count block types and the sixth is their sum, which is why the
 * array sums to twice the number of blocks. Each frame contributes
 * #BLOCKS_PER_FRAME of them - two granules for each of two channels - so the
 * total is tied to the frame count as well.
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
 * @brief The two dimensional histograms agree with the one dimensional ones.
 * @param state cmocka fixture state (unused).
 *
 * Each is the same population split a second way, so every row has to add up
 * to that bit rate's frame count - and for the block-type table, to that
 * count's worth of blocks, the last column again being the row's own total.
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
 * @brief lame_init_bitstream() clears the statistics it documents clearing.
 * @param state cmocka fixture state (unused).
 *
 * The counters have to be non-zero first, or the test would pass against a
 * library that never counted anything.
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
 * @brief After lame_encode_flush_nogap() the same instance keeps encoding.
 * @param state cmocka fixture state (unused).
 *
 * That is what the call is for: it completes the mp3 data so far without
 * writing an ID3v1 tag, leaving the instance usable for the next stream.
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
 * @brief lame_encode_finish() is lame_encode_flush() plus lame_close().
 * @param state cmocka fixture state (unused).
 *
 * Obsolete, still exported, and therefore still owed a test. What it promises
 * is the combination, so the same audio taken both ways has to end in the same
 * stream; the instance it was given must not be closed again afterwards,
 * because this call already did it.
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
 * @brief lame_get_lametag_frame() reports the size it needs, then fills it.
 * @param state cmocka fixture state (unused).
 *
 * A buffer that is too small is not written to: the call reports the size the
 * frame needs, which is larger than the size offered, and that is how the
 * caller is meant to ask. The sentinel proves nothing was written, which the
 * return value alone does not.
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
 * @brief With the LAME tag turned off there is no frame to hand over.
 * @param state cmocka fixture state (unused).
 *
 * The control for the test above: the same call on the same audio has to be
 * able to answer nothing, or a return value of "the size I need" would be
 * unconditional.
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

/**
 * @brief lame_mp3_tags_fid() replaces the reserved frame in a written stream.
 * @param state cmocka fixture state (unused).
 *
 * The reserved frame LAME puts at the front of the audio carries no tag until
 * this call goes back and writes one, so the file has to change and the marker
 * has to appear where it was not before. tmpfile() supplies the seekable
 * read/write stream the call documents needing, and leaves no path behind.
 */
static void
test_mp3_tags_fid_writes_the_tag(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    unsigned char before[512], after[512];
    lame_t  gfp = encoder_new(1, 1);
    FILE   *f;
    size_t  nb, na;
    int     used;

    used = encode_and_flush(gfp, mp3, MP3CAP);
    assert_true(used > (int) sizeof before);

    f = tmpfile();
    assert_non_null(f);
    assert_int_equal((int) fwrite(mp3, 1, (size_t) used, f), used);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    nb = fread(before, 1, sizeof before, f);
    assert_int_equal((int) nb, (int) sizeof before);
    assert_false(mem_contains(before, nb, "Xing"));

    lame_mp3_tags_fid(gfp, f);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    na = fread(after, 1, sizeof after, f);
    assert_int_equal((int) na, (int) nb);
    assert_int_not_equal(memcmp(before, after, nb), 0);
    assert_true(mem_contains(after, na, "Xing"));

    fclose(f);
    lame_close(gfp);
}

/**
 * @brief With the LAME tag turned off, lame_mp3_tags_fid() leaves the file alone.
 * @param state cmocka fixture state (unused).
 *
 * The control for the test above. Without it, a call that rewrote the front of
 * every file it was handed would pass just as well.
 */
static void
test_mp3_tags_fid_noop_without_tag(LAME_UNUSED void **state)
{
    static unsigned char mp3[MP3CAP];
    unsigned char before[512], after[512];
    lame_t  gfp = encoder_new(1, 0);
    FILE   *f;
    size_t  nb, na;
    int     used;

    used = encode_and_flush(gfp, mp3, MP3CAP);
    assert_true(used > (int) sizeof before);

    f = tmpfile();
    assert_non_null(f);
    assert_int_equal((int) fwrite(mp3, 1, (size_t) used, f), used);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    nb = fread(before, 1, sizeof before, f);

    lame_mp3_tags_fid(gfp, f);
    assert_int_equal(fflush(f), 0);
    assert_int_equal(fseek(f, 0, SEEK_SET), 0);
    na = fread(after, 1, sizeof after, f);
    assert_int_equal((int) na, (int) nb);
    assert_int_equal(memcmp(before, after, nb), 0);

    fclose(f);
    lame_close(gfp);
}

/**
 * @brief lame_print_config() writes through the installed report callback.
 * @param state cmocka fixture state (unused).
 *
 * What it says is the encoder's business and changes with the build; that it
 * arrives at the caller's callback rather than at stderr is the contract.
 */
static void
test_print_config_routes_through_callback(LAME_UNUSED void **state)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_msgf(gfp, capture_report), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, RATE), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_brate(gfp, CBR_KBPS), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    capture_reset();
    assert_int_equal(capcalls, 0);
    lame_print_config(gfp);
    assert_true(capcalls > 0);
    assert_true(caplen > 0);
    assert_true(mem_contains((const unsigned char *) capture, caplen, "LAME "));
    lame_close(gfp);
}

/**
 * @brief lame_print_internals() writes through the installed report callback.
 * @param state cmocka fixture state (unused).
 *
 * A separate test from the one above rather than a second half of it: each is
 * a claim about a different exported function, and cmocka stops a test at its
 * first failed assertion, so a combined one would only ever demonstrate the
 * first.
 */
static void
test_print_internals_routes_through_callback(LAME_UNUSED void **state)
{
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    assert_int_equal(lame_set_msgf(gfp, capture_report), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, RATE), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_brate(gfp, CBR_KBPS), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    capture_reset();
    assert_int_equal(capcalls, 0);
    lame_print_internals(gfp);
    assert_true(capcalls > 0);
    assert_true(caplen > 0);
    assert_true(mem_contains((const unsigned char *) capture, caplen, "stream format:"));
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

/** @brief Registers and runs the encode-API test group. */
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
