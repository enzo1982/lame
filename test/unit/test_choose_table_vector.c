/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vectorized Huffman table search primitives.
 *
 * The bitstream-identity check tests these routines only from far away. It
 * encodes whole files and compares the result. So it tests whether a real
 * encode still produces the same bits. But it only tests the region lengths
 * and value ranges that the music happens to produce. It says nothing about
 * the cases that it never gets to. These tests call the routines directly.
 * They test the cases that are difficult by design, not by chance:
 *
 *   - the lengths on both sides of the block size and of the vector
 *     threshold,
 *   - the value where the code lengths switch to escape coding,
 *   - the range where the narrowing to sixteen bits saturates.
 *
 * Each routine is checked against an independent scalar reference. The
 * reference is not LAME's own scalar loop. Two versions of the same loop can
 * share a mistake and still agree. The escape cases and their reference are
 * in test_esc_cases.h, because the NEON test runs them as well.
 *
 * The tables are synthetic for the same reason. The routines take their
 * tables as arguments. So nothing here depends on the contents of LAME's
 * tables. An index that is off by one gives a wrong sum. Neighbor entries
 * with the same value cannot hide the mistake.
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
#include "test_esc_cases.h"
#include "test_cpu_probe.h"

/** Longest region that the encoder passes to the routines. */
#define MAX_LEN 576

/**
 * @brief Calls the AVX2 maximum, or returns 0 if the build does not have it.
 *
 * The empty case is never called. have_avx2() returns 0 wherever the routine
 * does not exist. So one check guards every call site.
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

/* ------------------------------------------------------------------ */
/* ix_max                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Checks that the vector maximum agrees with a scalar maximum at every
 *        length.
 *
 * The lengths run from the shortest possible region to more than two full
 * vector blocks. So the test covers the block loop, its remainder, and the
 * case where the block loop does not run at all.
 */
static void
test_ix_max_lengths(LAME_UNUSED void **state)
{
    int     ix[MAX_LEN];
    int     n;

    for (n = 2; n <= 80; n += 2) {
        fill_pattern(ix, n, 8000, (unsigned int) n + 1u);
        assert_int_equal(ix_max_sse2(ix, ix + n), ref_max(ix, n));
        if (have_avx2())
            assert_int_equal(avx2_max(ix, ix + n), ref_max(ix, n));
    }
}

/**
 * @brief Checks that the values on which the caller decides are returned
 *        exactly.
 *
 * choose_table asks three questions about this number. Is it at most 15? Is
 * it above IXMAX_VAL? Which linbits range does it fall in? So the results on
 * both sides of both boundaries must be exact.
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
 * @brief Checks that the result stays usable above the saturation point of
 *        the narrowing.
 *
 * Sixteen bits cannot store 40000, and the routine does not claim otherwise.
 * It promises less, and that is all the caller needs. The result is still
 * above IXMAX_VAL, so the caller still rejects the region. This test fails if
 * IXMAX_VAL is ever raised past the saturation point. The compile-time
 * assertion in takehiro.c then fails too.
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

/**
 * @brief Runs esc_check_lengths() on the SSE2 routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_lengths(LAME_UNUSED void **state)
{
    esc_check_lengths(count_bit_esc_sse2, largetbl_t);
}

/**
 * @brief Runs esc_check_clamp_boundary() on the SSE2 routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_clamp_boundary(LAME_UNUSED void **state)
{
    esc_check_clamp_boundary(count_bit_esc_sse2, largetbl_t);
}

/**
 * @brief Runs esc_check_large_values() on the SSE2 routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_large_values(LAME_UNUSED void **state)
{
    esc_check_large_values(count_bit_esc_sse2, largetbl_t);
}

/**
 * @brief Runs esc_check_reference_can_disagree() on the SSE2 routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_reference_can_disagree(LAME_UNUSED void **state)
{
    esc_check_reference_can_disagree(count_bit_esc_sse2, largetbl_t);
}

/* ------------------------------------------------------------------ */
/* count_bit_noESC_from3_sse2                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Checks that the three sums agree with a scalar reference at each
 *        table width.
 *
 * The widths are the ones that the table selection can produce. Each width
 * limits the values that can occur with it. A larger index would run past the
 * end of the code-length table. So the value ranges here are the real ones.
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
            fill_pattern(ix, n, cases[c].maxv, (unsigned int) (n + cases[c].xlen));
            count_bit_noESC_from3_sse2(ix, ix + n, cases[c].xlen,
                                       hlen_a, hlen_b, hlen_c, sv);
            ref_from3(ix, n, cases[c].xlen, sr);
            assert_int_equal(sv[0], sr[0]);
            assert_int_equal(sv[1], sr[1]);
            assert_int_equal(sv[2], sr[2]);
        }
    }
}

/** @brief Checks that the extreme indices of each width are computed exactly, without wrap or clip. */
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

/** @brief Guard: checks that the reference can disagree. A reference that agrees with everything proves nothing. */
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
        cmocka_unit_test(test_esc_reference_can_disagree),
        cmocka_unit_test(test_from3_widths),
        cmocka_unit_test(test_from3_index_extremes),
        cmocka_unit_test(test_reference_can_disagree),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
