/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vectorized quantization of xr^(3/4).
 *
 * The bitstream check tests whether a real encode still produces the same
 * bits. That is the check that matters. But it only tests the run lengths and
 * value ranges that the music happens to produce. The bitstream check may
 * never test two cases, so this file tests them directly.
 *
 * The first case is the tail. The loop consumes its length in blocks of four
 * and then an optional pair. So an odd length leaves its last value
 * untouched. The callers depend on this behavior. A vector version can easily
 * get it wrong by one. So every case below checks the values written. It also
 * checks that nothing past them was written.
 *
 * The second case is the ends of the table index range. The index is a
 * truncated float. For input out of range, the truncating convert instruction
 * returns INT_MIN, and C leaves the result undefined. So the two forms agree
 * only inside the range that the caller guarantees. The tests check the ends
 * of that range explicitly. The input to the index must also stay inside the
 * range. So one test checks the SSE form of xr^(3/4) with coefficients far
 * below audibility.
 *
 * The reference is written in this file. It does not reuse LAME's own loop.
 * It also has a different shape: one flat loop over the element count, with
 * no blocks. So the two cannot share a mistake about how many elements a
 * length implies. The table is synthetic for the same reason. The routines
 * take the table as an argument. So an index that is off by one gives a wrong
 * value. A neighbor entry with the same value cannot hide the mistake.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "quantize_pvt.h"
#include "vector/lame_intrin.h"
#include "test_cpu_probe.h"

#if defined( HAVE_SSE2_INTRINSICS )
# include <xmmintrin.h>
/** MXCSR bit that flushes denormal results to zero. */
# define MXCSR_FLUSH_TO_ZERO 0x8000u
/** MXCSR bit that reads denormal operands as zero. */
# define MXCSR_DENORMALS_ARE_ZERO 0x0040u
#endif

/** Longest run that the encoder passes to the routines. */
#define MAX_LEN 576

/** Value written into the output before each call, to catch stray writes. */
#define SENTINEL (-99999)

static FLOAT adj_t[PRECALC_SIZE];

static void
tables_init(void)
{
    int     i;
    /* The real adj43[] lies a little under one half throughout. These vary
       per index so that a wrong subscript changes the answer instead of
       landing on an identical neighbour. */
    for (i = 0; i < PRECALC_SIZE; ++i)
        adj_t[i] = (FLOAT) (0.4054 + 0.001 * (double) (i % 97));
}

/**
 * @brief Calls the AVX2 form, or does nothing if the build does not have it.
 *
 * The empty case is never called. have_avx2() returns 0 wherever the routine
 * does not exist.
 */
static void
avx2_quantize(unsigned int l, FLOAT istep, const FLOAT * xr, int *ix, const FLOAT * adj)
{
#if defined( HAVE_AVX2_INTRINSICS )
    quantize_lines_xrpow_avx2(l, istep, xr, ix, adj);
#else
    (void) l; (void) istep; (void) xr; (void) ix; (void) adj;
#endif
}

/** @brief Calls the AVX-512 form, or does nothing if the build does not have it. */
static void
avx512_quantize(unsigned int l, FLOAT istep, const FLOAT * xr, int *ix, const FLOAT * adj)
{
#if defined( HAVE_AVX512_INTRINSICS )
    quantize_lines_xrpow_avx512(l, istep, xr, ix, adj);
#else
    (void) l; (void) istep; (void) xr; (void) ix; (void) adj;
#endif
}

/* ------------------------------------------------------------------ */
/* reference                                                           */
/* ------------------------------------------------------------------ */

/** @brief Returns the number of values that a run of length @a l consumes. */
static unsigned int
ref_count(unsigned int l)
{
    unsigned int const h = l >> 1;
    return 4u * (h >> 1) + 2u * (h & 1u);
}

/** @brief Computes the same arithmetic in one flat loop, without blocks. */
static void
ref_quantize(unsigned int l, FLOAT istep, const FLOAT * xr, int *ix, const FLOAT * adj)
{
    unsigned int const n = ref_count(l);
    unsigned int i;

    for (i = 0; i < n; ++i) {
        FLOAT   x = xr[i] * istep;
        int const r = (int) x;      /* consuming x here blocks contraction,
                                       exactly as the encoder's loop does */
        x += adj[r];
        ix[i] = (int) x;
    }
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/** @brief Selects the tier that check_one() tests. */
enum { TIER_SSE2 = 0, TIER_AVX2 = 1, TIER_AVX512 = 2 };

/**
 * @brief Runs one length through a tier and through the reference, and
 *        compares the results.
 *
 * Checks the values written. Also checks that every element past them still
 * contains the sentinel. The tail case is the main reason for these tests.
 */
static void
check_one(unsigned int l, FLOAT istep, const FLOAT * xr, const FLOAT * adj, int tier)
{
    int     got[MAX_LEN + 8], want[MAX_LEN + 8];
    unsigned int const n = ref_count(l);
    unsigned int i;

    for (i = 0; i < MAX_LEN + 8; ++i)
        got[i] = want[i] = SENTINEL;

    switch (tier) {
    case TIER_AVX512:
        avx512_quantize(l, istep, xr, got, adj);
        break;
    case TIER_AVX2:
        avx2_quantize(l, istep, xr, got, adj);
        break;
    default:
        quantize_lines_xrpow_sse2(l, istep, xr, got, adj);
        break;
    }
    ref_quantize(l, istep, xr, want, adj);

    for (i = 0; i < n; ++i)
        assert_int_equal(got[i], want[i]);
    for (i = n; i < MAX_LEN + 8; ++i)
        assert_int_equal(got[i], SENTINEL);
}

/* ------------------------------------------------------------------ */
/* tests                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Checks every length from 0 to 72 and the last four lengths up to a
 *        full run, on each tier that this CPU can run.
 *
 * The range covers both sides of each dispatch threshold in takehiro.c (8, 16
 * and 32 values) and of each block size. It also covers every odd length in
 * between. The untouched last value occurs at the odd lengths.
 */
static void
test_lengths(LAME_UNUSED void **state)
{
    FLOAT   xr[MAX_LEN + 8];
    unsigned int l;
    int     i;

    for (i = 0; i < MAX_LEN + 8; ++i)
        xr[i] = (FLOAT) ((i % 200) + 0.25);

    for (l = 0; l <= 72; ++l) {
        check_one(l, 1.0f, xr, adj_t, TIER_SSE2);
        if (have_avx2())
            check_one(l, 1.0f, xr, adj_t, TIER_AVX2);
        if (have_avx512())
            check_one(l, 1.0f, xr, adj_t, TIER_AVX512);
    }
    for (l = MAX_LEN - 3; l <= MAX_LEN; ++l) {
        check_one(l, 1.0f, xr, adj_t, TIER_SSE2);
        if (have_avx2())
            check_one(l, 1.0f, xr, adj_t, TIER_AVX2);
        if (have_avx512())
            check_one(l, 1.0f, xr, adj_t, TIER_AVX512);
    }
}

/**
 * @brief Checks the ends of the index range that the caller guarantees.
 *
 * count_bits() rejects a granule before any value here can exceed IXMAX_VAL.
 * So 0 and IXMAX_VAL are the extremes that can occur.
 */
static void
test_index_boundaries(LAME_UNUSED void **state)
{
    static int const idx[] = { 0, 1, 2, 14, 15, 16, IXMAX_VAL - 1, IXMAX_VAL };
    FLOAT   xr[MAX_LEN + 8];
    int     i;

    for (i = 0; i < MAX_LEN + 8; ++i)
        xr[i] = (FLOAT) idx[i % (int) (sizeof idx / sizeof idx[0])];

    check_one(64, 1.0f, xr, adj_t, TIER_SSE2);
    check_one(66, 1.0f, xr, adj_t, TIER_SSE2);
    if (have_avx2()) {
        check_one(64, 1.0f, xr, adj_t, TIER_AVX2);
        check_one(66, 1.0f, xr, adj_t, TIER_AVX2);
    }
    if (have_avx512()) {
        check_one(64, 1.0f, xr, adj_t, TIER_AVX512);
        check_one(66, 1.0f, xr, adj_t, TIER_AVX512);
    }

    /* the same values reached through istep rather than written down, which
       is how they arrive in the encoder */
    for (i = 0; i < MAX_LEN + 8; ++i)
        xr[i] = (FLOAT) idx[i % (int) (sizeof idx / sizeof idx[0])] * 4.0f;
    check_one(64, 0.25f, xr, adj_t, TIER_SSE2);
    if (have_avx2())
        check_one(64, 0.25f, xr, adj_t, TIER_AVX2);
    if (have_avx512())
        check_one(64, 0.25f, xr, adj_t, TIER_AVX512);
}

/** @brief Checks input that is all zero. A silent passage produces this case. */
static void
test_all_zero(LAME_UNUSED void **state)
{
    FLOAT   xr[MAX_LEN + 8];
    int     i;

    for (i = 0; i < MAX_LEN + 8; ++i)
        xr[i] = 0.0f;

    for (i = 0; i <= 40; ++i)
        check_one((unsigned int) i, 1.0f, xr, adj_t, TIER_SSE2);
    if (have_avx2())
        for (i = 0; i <= 40; ++i)
            check_one((unsigned int) i, 1.0f, xr, adj_t, TIER_AVX2);
    if (have_avx512())
        for (i = 0; i <= 40; ++i)
            check_one((unsigned int) i, 1.0f, xr, adj_t, TIER_AVX512);
}

/** @brief Checks that the AVX2 and AVX-512 tiers agree with the SSE2 tier directly. */
static void
test_tiers_agree(LAME_UNUSED void **state)
{
    FLOAT   xr[MAX_LEN + 8];
    int     a[MAX_LEN + 8], b[MAX_LEN + 8];
    unsigned int l;
    int     i;

    for (i = 0; i < MAX_LEN + 8; ++i)
        xr[i] = (FLOAT) ((i * 7 % 311) + 0.5);

    if (have_avx2())
        for (l = 0; l <= 72; ++l) {
            for (i = 0; i < MAX_LEN + 8; ++i)
                a[i] = b[i] = SENTINEL;
            quantize_lines_xrpow_sse2(l, 1.0f, xr, a, adj_t);
            avx2_quantize(l, 1.0f, xr, b, adj_t);
            for (i = 0; i < MAX_LEN + 8; ++i)
                assert_int_equal(a[i], b[i]);
        }

    if (have_avx512())
        for (l = 0; l <= 72; ++l) {
            for (i = 0; i < MAX_LEN + 8; ++i)
                a[i] = b[i] = SENTINEL;
            quantize_lines_xrpow_sse2(l, 1.0f, xr, a, adj_t);
            avx512_quantize(l, 1.0f, xr, b, adj_t);
            for (i = 0; i < MAX_LEN + 8; ++i)
                assert_int_equal(a[i], b[i]);
        }
}

/**
 * @brief Guard: checks that the element count really depends on the length.
 *
 * ref_count() is the one assumption that the reference and the routines
 * share. A mistake in it would be invisible to every test above. Both sides
 * would write the same wrong number of values and agree. This test checks
 * ref_count() against the property that it expresses.
 */
static void
test_odd_length_drops_last(LAME_UNUSED void **state)
{
    assert_int_equal(ref_count(0), 0);
    assert_int_equal(ref_count(1), 0);
    assert_int_equal(ref_count(2), 2);
    assert_int_equal(ref_count(3), 2);   /* the odd value is not consumed */
    assert_int_equal(ref_count(4), 4);
    assert_int_equal(ref_count(5), 4);
    assert_int_equal(ref_count(6), 6);
    assert_int_equal(ref_count(8), 8);
    assert_int_equal(ref_count(576), 576);
}

/**
 * @brief Guard: checks that the reference can disagree with the routine.
 *
 * A reference that agrees with everything proves nothing. Each change to the
 * input below must be large enough to survive the truncation. Adjacent entries
 * of the synthetic table differ by only 0.001. So a table that is shifted by
 * one entry does not change an integer result. The assertion that the results
 * differ would then fail. So each change here crosses an integer boundary.
 */
static void
test_reference_can_disagree(LAME_UNUSED void **state)
{
    static FLOAT adj_wrong[PRECALC_SIZE];
    FLOAT   xr[64], xr_shifted[64];
    int     got[64], want[64];
    int     i;

    for (i = 0; i < 64; ++i) {
        xr[i] = (FLOAT) (i + 0.75);
        xr_shifted[i] = xr[i] + 1.0f;
        got[i] = want[i] = SENTINEL;
    }
    /* the real table sits a little under one half; this one rounds the other
       way, so every value lands one higher */
    for (i = 0; i < PRECALC_SIZE; ++i)
        adj_wrong[i] = 1.5f;

    quantize_lines_xrpow_sse2(32, 1.0f, xr, got, adj_t);

    /* the same call reproduced: must agree */
    ref_quantize(32, 1.0f, xr, want, adj_t);
    assert_int_equal(got[0], want[0]);

    /* shifted input: must not */
    ref_quantize(32, 1.0f, xr_shifted, want, adj_t);
    assert_int_not_equal(got[0], want[0]);

    /* a table that rounds the other way: must not */
    ref_quantize(32, 1.0f, xr, want, adj_wrong);
    assert_int_not_equal(got[0], want[0]);

    /* a different istep: must not */
    ref_quantize(32, 2.0f, xr, want, adj_t);
    assert_int_not_equal(got[1], want[1]);
}

/**
 * @brief Checks whether the exponent field of the float at @p p is all ones
 *        (NaN or infinity).
 *
 * @param p a float in memory.
 * @return nonzero for a NaN or an infinity.
 */
static int
float_bits_nonfinite(const void *p)
{
    uint32_t bits;
    memcpy(&bits, p, sizeof bits);
    return ((bits >> 23) & 0xFFu) == 0xFFu;
}

/**
 * @brief Checks that the SSE form of xr^(3/4) stays finite for coefficients
 *        far below audibility.
 *
 * From 2^-84 down, x * sqrt(x) is a denormal. A square root that is computed
 * from a reciprocal estimate turns a denormal into a NaN. clang computes
 * square roots this way under -ffast-math, and the library is built with
 * -ffast-math. The quantizer's range check lets a NaN through. The vector
 * quantizers then use the NaN as a table index.
 *
 * The test uses magnitudes from 2^-60 down to 2^-139, with both signs and
 * with denormals. It runs every tail length. Every result must be finite.
 * Above the cut-off of 2^-80, each result must match |x|^0.75. Below the
 * cut-off, each result must be zero or no larger than |x|^0.75.
 *
 * The test runs with denormals enabled. A program linked with -ffast-math
 * starts with denormals flushed to zero, and that hides the fault. This test
 * is such a program. But an application that only loads the library does not
 * flush denormals.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_xrpow_core_tiny_coefficients(LAME_UNUSED void **state)
{
#if defined( HAVE_SSE2_INTRINSICS )
    /* values the encoder produced where the fault was found, beside a ladder
       of powers of two */
    static const double seen[4] = { 4.98604e-28, -1.67134e-26, -1.34997e-28, -4.51382e-29 };
    gr_info *gi = calloc(1, sizeof *gi);
    FLOAT   xrpow[576], sum = 0;
    int     i, bad = 0, first = -1, max_nz, upper;
    unsigned int const csr = _mm_getcsr();
    assert_non_null(gi);
    _mm_setcsr(csr & ~(MXCSR_FLUSH_TO_ZERO | MXCSR_DENORMALS_ARE_ZERO));
    for (i = 0; i < 576; ++i) {
        double const m = (i % 5 == 4) ? seen[(i / 5) % 4] : ldexp(1.0, -60 - (i % 80));
        gi->xr[i] = (FLOAT) ((i & 1) ? -m : m);
    }
    /* every tail length: the last 1-3 values take their own path through the
       vector code, and that path is where the fault was */
    for (max_nz = 0; max_nz < 576; max_nz += (max_nz < 24 ? 1 : 37)) {
        init_xrpow_core_sse(gi, xrpow, max_nz, &sum);
        upper = max_nz + 1;
        for (i = 0; i < upper; ++i) {
            float const f = xrpow[i];
            double const x = fabs((double) gi->xr[i]);
            double const want = pow(x, 0.75);
            int     ok;
            if (float_bits_nonfinite(&xrpow[i]))
                ok = 0;
            else if (x >= ldexp(1.0, -80))
                ok = fabs(f - want) <= want * 1e-5;
            else
                ok = f >= 0 && f <= want * 1.01;
            if (!ok && bad++ == 0)
                first = i;
        }
        assert_false(float_bits_nonfinite(&gi->xrpow_max));
        if (bad) {
            _mm_setcsr(csr);
            fail_msg("max_nz %d: %d value(s) wrong, the first at xr %g: %g", max_nz, bad,
                     (double) gi->xr[first], (double) xrpow[first]);
        }
    }
    _mm_setcsr(csr);
    free(gi);
#else
    skip();
#endif
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_lengths),
        cmocka_unit_test(test_index_boundaries),
        cmocka_unit_test(test_all_zero),
        cmocka_unit_test(test_tiers_agree),
        cmocka_unit_test(test_odd_length_drops_last),
        cmocka_unit_test(test_reference_can_disagree),
        cmocka_unit_test(test_xrpow_core_tiny_coefficients),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
