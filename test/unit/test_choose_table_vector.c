/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vectorised Huffman table search primitives.
 *
 * The bitstream-identity check covers these routines from a long way off: it
 * encodes whole files and compares the result. That answers "does a real
 * encode still produce the same bits", but it exercises whatever region
 * lengths and value ranges the music happens to produce, and it says nothing
 * about the ones it never reaches. These tests go at the routines directly,
 * over the cases that are awkward by construction rather than by luck: the
 * lengths either side of the block size and the vector threshold, the value
 * where the code lengths switch to escape coding, and the range where
 * narrowing to sixteen bits saturates.
 *
 * Each routine is checked against an independent scalar reference written
 * here rather than against LAME's own - two spellings of the same loop would
 * agree about a shared misunderstanding.
 *
 * The tables are synthetic for the same reason. The routines take their
 * tables as arguments, so nothing here depends on the contents of LAME's, and
 * an index computed one place off shows up as a wrong sum instead of being
 * masked by neighbouring entries that happen to be equal.
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

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "quantize_pvt.h"
#include "vector/lame_intrin.h"

/** Longest region the encoder ever asks about. */
#define MAX_LEN 576

/** @brief Does the running CPU offer AVX2? */
static int
have_avx2(void)
{
#if defined( HAVE_AVX2_INTRINSICS )
# if defined( __AVX2__ )
    return 1;
# elif defined( __GNUC__ ) || defined( __clang__ )
    return __builtin_cpu_supports("avx2") != 0;
# else
    return 0;               /* no way to ask; skip rather than crash */
# endif
#else
    return 0;
#endif
}

/**
 * @brief The AVX2 maximum, or a stand-in where it was not compiled.
 *
 * The stand-in is never called: have_avx2() answers no wherever the routine
 * does not exist, so the guard is one test rather than one per call site.
 */
static int
avx2_max(const int *ix, const int *end)
{
#if defined( HAVE_AVX2_INTRINSICS )
    return ix_max_avx2(ix, end);
#else
    (void) ix;
    (void) end;
    return 0;
#endif
}

/* ------------------------------------------------------------------ */
/* synthetic tables                                                    */
/* ------------------------------------------------------------------ */

static uint32_t largetbl_t[16 * 16];
static uint8_t hlen_a[256], hlen_b[256], hlen_c[256];

static void
tables_init(void)
{
    int     i;
    for (i = 0; i < 16 * 16; ++i) {
        /* the shape the real one has: two code lengths packed into one word,
           distinct per index so a misplaced index cannot pass */
        largetbl_t[i] = ((uint32_t) (i % 19 + 1) << 16) | (uint32_t) (i % 13 + 1);
    }
    for (i = 0; i < 256; ++i) {
        hlen_a[i] = (uint8_t) (i % 17 + 1);
        hlen_b[i] = (uint8_t) (i % 11 + 2);
        hlen_c[i] = (uint8_t) (i % 7 + 3);
    }
}

/* ------------------------------------------------------------------ */
/* independent scalar references                                       */
/* ------------------------------------------------------------------ */

static int
ref_max(const int *ix, int n)
{
    int     m = 0, i;
    for (i = 0; i < n; ++i)
        if (m < ix[i])
            m = ix[i];
    return m;
}

static unsigned int
ref_esc(const int *ix, int n, unsigned int *nclamped)
{
    unsigned int sum = 0, nc = 0;
    int     i;
    for (i = 0; i < n; i += 2) {
        unsigned int x = (unsigned int) ix[i];
        unsigned int y = (unsigned int) ix[i + 1];
        if (x >= 15u) { x = 15u; ++nc; }
        if (y >= 15u) { y = 15u; ++nc; }
        sum += largetbl_t[(x << 4u) + y];
    }
    *nclamped = nc;
    return sum;
}

static void
ref_from3(const int *ix, int n, int xlen, unsigned int sums[3])
{
    int     i;
    sums[0] = sums[1] = sums[2] = 0;
    for (i = 0; i < n; i += 2) {
        unsigned int const k = (unsigned int) ix[i] * (unsigned int) xlen
                             + (unsigned int) ix[i + 1];
        sums[0] += hlen_a[k];
        sums[1] += hlen_b[k];
        sums[2] += hlen_c[k];
    }
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/** @brief Fill @p ix with a repeatable pseudo-random pattern in [0, hi]. */
static void
fill(int *ix, int n, int hi, unsigned int seed)
{
    int     i;
    for (i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        ix[i] = (int) ((seed >> 16) % (unsigned int) (hi + 1));
    }
}

/* ------------------------------------------------------------------ */
/* ix_max                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief The vector maximum agrees with a scalar one at every length.
 *
 * Lengths run from the shortest a region can be up to past two full vector
 * blocks, so the block loop, its remainder, and the case where the loop never
 * runs at all are each covered.
 */
static void
test_ix_max_lengths(LAME_UNUSED void **state)
{
    int     ix[MAX_LEN];
    int     n;

    for (n = 2; n <= 80; n += 2) {
        fill(ix, n, 8000, (unsigned int) n + 1u);
        assert_int_equal(ix_max_sse2(ix, ix + n), ref_max(ix, n));
        if (have_avx2())
            assert_int_equal(avx2_max(ix, ix + n), ref_max(ix, n));
    }
}

/**
 * @brief The values the caller makes decisions on are reported exactly.
 *
 * choose_table asks three questions of this number - is it at most 15, is it
 * above IXMAX_VAL, and which linbits bucket does it fall in - so the answers
 * either side of both boundaries have to be exact.
 */
static void
test_ix_max_boundaries(LAME_UNUSED void **state)
{
    static const int interesting[] = { 0, 1, 14, 15, 16, 17, 8190, 8191,
                                       IXMAX_VAL - 1, IXMAX_VAL };
    int     ix[MAX_LEN];
    size_t  k;

    for (k = 0; k < sizeof interesting / sizeof interesting[0]; ++k) {
        int const v = interesting[k];
        int     pos;
        /* put the peak at each position in turn: a horizontal reduction that
           drops a lane only fails for some of them */
        for (pos = 0; pos < 64; ++pos) {
            memset(ix, 0, sizeof ix);
            ix[pos] = v;
            assert_int_equal(ix_max_sse2(ix, ix + 64), v);
            if (have_avx2())
                assert_int_equal(avx2_max(ix, ix + 64), v);
        }
    }
}

/**
 * @brief Above the narrowing's saturation point the answer stays usable.
 *
 * Sixteen bits cannot hold 40000, and the routine does not pretend otherwise.
 * What it promises is weaker and is all the caller needs: a value that is
 * still above IXMAX_VAL, so the region is still rejected. This is the test
 * that fails if IXMAX_VAL is ever raised past the saturation point - together
 * with the compile-time assertion in takehiro.c.
 */
static void
test_ix_max_saturation(LAME_UNUSED void **state)
{
    static const int huge[] = { 32766, 32767, 32768, 40000, 1 << 24, 0x7ffffffe };
    int     ix[64];
    size_t  k;

    assert_true(IXMAX_VAL < 32767);

    for (k = 0; k < sizeof huge / sizeof huge[0]; ++k) {
        memset(ix, 0, sizeof ix);
        ix[13] = huge[k];
        assert_true(ix_max_sse2(ix, ix + 64) > IXMAX_VAL);
        if (have_avx2())
            assert_true(avx2_max(ix, ix + 64) > IXMAX_VAL);
    }
}

/* ------------------------------------------------------------------ */
/* count_bit_esc_sse2                                                  */
/* ------------------------------------------------------------------ */

/** @brief Sum and clamp count agree with a scalar reference at every length. */
static void
test_esc_lengths(LAME_UNUSED void **state)
{
    int     ix[MAX_LEN];
    int     n;

    for (n = 2; n <= 80; n += 2) {
        unsigned int nc_v = 12345, nc_r = 0;
        unsigned int sv, sr;
        fill(ix, n, 200, (unsigned int) n + 7u);
        sv = count_bit_esc_sse2(ix, ix + n, largetbl_t, &nc_v);
        sr = ref_esc(ix, n, &nc_r);
        assert_int_equal(sv, sr);
        assert_int_equal(nc_v, nc_r);
    }
}

/**
 * @brief 15 is clamped and 14 is not - the boundary the escape coding turns on.
 *
 * Checked as whole regions of one value so a count that is off by one per
 * block, per lane, or per remainder cannot hide in a mixed sample.
 */
static void
test_esc_clamp_boundary(LAME_UNUSED void **state)
{
    int     ix[64];
    int     v;

    for (v = 13; v <= 17; ++v) {
        unsigned int nc_v = 0, nc_r = 0;
        unsigned int sv, sr;
        int     i;
        for (i = 0; i < 64; ++i)
            ix[i] = v;
        sv = count_bit_esc_sse2(ix, ix + 64, largetbl_t, &nc_v);
        sr = ref_esc(ix, 64, &nc_r);
        assert_int_equal(sv, sr);
        assert_int_equal(nc_v, nc_r);
        assert_int_equal(nc_v, v >= 15 ? 64u : 0u);
    }
}

/** @brief Values far above the clamp still count once each, not more. */
static void
test_esc_large_values(LAME_UNUSED void **state)
{
    int     ix[64];
    unsigned int nc_v = 0, nc_r = 0;
    unsigned int sv, sr;
    int     i;

    for (i = 0; i < 64; ++i)
        ix[i] = (i % 3 == 0) ? 40000 : 3;
    sv = count_bit_esc_sse2(ix, ix + 64, largetbl_t, &nc_v);
    sr = ref_esc(ix, 64, &nc_r);
    assert_int_equal(sv, sr);
    assert_int_equal(nc_v, nc_r);
}

/* ------------------------------------------------------------------ */
/* count_bit_noESC_from3_sse2                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief The three sums agree with a scalar reference, at each table width.
 *
 * The widths are the ones the table selection can produce, and each caps the
 * values that can occur with it - a wider index than that would run off the
 * end of the code-length table, so the ranges here are the real ones.
 */
static void
test_from3_widths(LAME_UNUSED void **state)
{
    struct { int xlen; int maxv; } const cases[] = { { 6, 5 }, { 8, 7 }, { 16, 15 } };
    int     ix[MAX_LEN];
    size_t  c;

    for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        int     n;
        for (n = 2; n <= 80; n += 2) {
            unsigned int sv[3], sr[3];
            fill(ix, n, cases[c].maxv, (unsigned int) (n + cases[c].xlen));
            count_bit_noESC_from3_sse2(ix, ix + n, cases[c].xlen,
                                       hlen_a, hlen_b, hlen_c, sv);
            ref_from3(ix, n, cases[c].xlen, sr);
            assert_int_equal(sv[0], sr[0]);
            assert_int_equal(sv[1], sr[1]);
            assert_int_equal(sv[2], sr[2]);
        }
    }
}

/** @brief The extreme index of each width is computed, not wrapped or clipped. */
static void
test_from3_index_extremes(LAME_UNUSED void **state)
{
    struct { int xlen; int maxv; } const cases[] = { { 6, 5 }, { 8, 7 }, { 16, 15 } };
    int     ix[64];
    size_t  c;

    for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        unsigned int sv[3], sr[3];
        int     i;
        for (i = 0; i < 64; ++i)
            ix[i] = cases[c].maxv;
        count_bit_noESC_from3_sse2(ix, ix + 64, cases[c].xlen,
                                   hlen_a, hlen_b, hlen_c, sv);
        ref_from3(ix, 64, cases[c].xlen, sr);
        assert_int_equal(sv[0], sr[0]);
        assert_int_equal(sv[1], sr[1]);
        assert_int_equal(sv[2], sr[2]);

        memset(ix, 0, sizeof ix);
        count_bit_noESC_from3_sse2(ix, ix + 64, cases[c].xlen,
                                   hlen_a, hlen_b, hlen_c, sv);
        ref_from3(ix, 64, cases[c].xlen, sr);
        assert_int_equal(sv[0], sr[0]);
    }
}

/* ------------------------------------------------------------------ */

/** @brief Guard: a reference that agreed with everything would prove nothing. */
static void
test_reference_can_disagree(LAME_UNUSED void **state)
{
    int     ix[64];
    unsigned int sv[3], sr[3];
    int     i;

    for (i = 0; i < 64; ++i)
        ix[i] = i % 16;
    count_bit_noESC_from3_sse2(ix, ix + 64, 16, hlen_a, hlen_b, hlen_c, sv);
    ref_from3(ix, 64, 16, sr);
    assert_int_equal(sv[0], sr[0]);

    /* same data, wrong width: the reference must NOT match now */
    ref_from3(ix, 64, 8, sr);
    assert_int_not_equal(sv[0], sr[0]);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ix_max_lengths),
        cmocka_unit_test(test_ix_max_boundaries),
        cmocka_unit_test(test_ix_max_saturation),
        cmocka_unit_test(test_esc_lengths),
        cmocka_unit_test(test_esc_clamp_boundary),
        cmocka_unit_test(test_esc_large_values),
        cmocka_unit_test(test_from3_widths),
        cmocka_unit_test(test_from3_index_extremes),
        cmocka_unit_test(test_reference_can_disagree),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
