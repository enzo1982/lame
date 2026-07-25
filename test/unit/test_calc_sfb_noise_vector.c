/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vectorised VBR scalefactor-band noise.
 *
 * calc_sfb_noise_x34 returns one number - a sum of squared quantization errors -
 * so unlike the quantize_lines test there is no output array to diff, and the
 * sum is a floating-point reduction whose exact last bit depends on association.
 * There is a single vector tier here (SSE2): an AVX2 version was measured and
 * added nothing on the variable-bitrate workload, so it is not carried.  The
 * test checks:
 *
 *   - The SSE2 tier must match a reference to within float rounding.  The
 *     reference accumulates in double, so it is association-independent and
 *     near-exact; comparing the float result to it catches a wrong index, a
 *     wrong formula or a mishandled tail without depending on which association
 *     the compiler gave the reference.  A bit-exact scalar reference would be
 *     reassociated by -ffast-math and could not be trusted here.
 *
 * The tail is the off-by-one a vector rewrite invites: the loop consumes fours
 * then an optional 1-3, and the scalar routine squares zeros for the padding, so
 * an odd bw is exercised at every length.  The table is synthetic and varies per
 * index so a subscript computed one off changes the answer.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
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

#define MAX_BW 576

static FLOAT adj_t[PRECALC_SIZE];
static FLOAT pw43_t[PRECALC_SIZE];

static void
tables_init(void)
{
    int i;
    for (i = 0; i < PRECALC_SIZE; ++i) {
        /* adj43[] sits a little under one half; vary per index so a wrong
           subscript lands on a different value. */
        adj_t[i] = (FLOAT) (0.4054 + 0.001 * (double) (i % 97));
        pw43_t[i] = (FLOAT) pow((double) i, 4.0 / 3.0);   /* like pow43[] */
    }
}

/** Association-independent reference: accumulate the squares in double. */
static double
ref_noise(const FLOAT * xr, const FLOAT * xr34, unsigned int bw, FLOAT sfpow, FLOAT sfpow34)
{
    unsigned int const full = bw >> 2u;
    unsigned int const rem = bw & 3u;
    double sum = 0;
    unsigned int i, k;

    for (i = 0; i < full; ++i) {
        for (k = 0; k < 4; ++k) {
            FLOAT x = sfpow34 * xr34[4 * i + k];
            int l3 = (int) x;
            double d;
            x += adj_t[l3];
            l3 = (int) x;
            d = (double) fabsf(xr[4 * i + k]) - (double) sfpow * (double) pw43_t[l3];
            sum += d * d;
        }
    }
    for (k = 0; k < rem; ++k) {
        FLOAT x = sfpow34 * xr34[4 * full + k];
        int l3 = (int) x;
        double d;
        x += adj_t[l3];
        l3 = (int) x;
        d = (double) fabsf(xr[4 * full + k]) - (double) sfpow * (double) pw43_t[l3];
        sum += d * d;
    }
    return sum;
}

/* The SSE2 tier vs the double reference (within float rounding).  rel tolerance
   is generous: a float sum over up to 576 terms. */
static void
check_one(unsigned int bw, FLOAT sfpow, FLOAT sfpow34, const FLOAT * xr, const FLOAT * xr34)
{
    double const r = ref_noise(xr, xr34, bw, sfpow, sfpow34);
    FLOAT const s = calc_sfb_noise_x34_sse2(xr, xr34, bw, sfpow, sfpow34, adj_t, pw43_t);
    double const tol = 1e-4 * (r > 0 ? r : 1.0);

    assert_true(fabs((double) s - r) <= tol);
}

static void
fill(FLOAT * xr, FLOAT * xr34)
{
    int i;
    for (i = 0; i < MAX_BW + 8; ++i) {
        xr[i] = (FLOAT) (((i * 13) % 197) + 0.6);
        xr34[i] = (FLOAT) (((i * 7) % 211) + 0.3);   /* * sfpow34 stays in table range */
    }
}

/** Every length to a full band, both tiers, every odd length (the tail). */
static void
test_lengths(LAME_UNUSED void **state)
{
    FLOAT xr[MAX_BW + 8], xr34[MAX_BW + 8];
    unsigned int bw;

    fill(xr, xr34);
    for (bw = 0; bw <= 80; ++bw)
        check_one(bw, 0.85f, 1.9f, xr, xr34);
    for (bw = MAX_BW - 3; bw <= MAX_BW; ++bw)
        check_one(bw, 0.85f, 1.9f, xr, xr34);
}

/** The ends of the index range the caller guarantees (0 .. IXMAX_VAL). */
static void
test_index_boundaries(LAME_UNUSED void **state)
{
    static int const idx[] = { 0, 1, 2, 14, 15, 16, IXMAX_VAL - 1, IXMAX_VAL };
    FLOAT xr[MAX_BW + 8], xr34[MAX_BW + 8];
    int i;

    /* xr34 * sfpow34(=1) truncates straight to these indices */
    for (i = 0; i < MAX_BW + 8; ++i) {
        xr34[i] = (FLOAT) idx[i % (int) (sizeof idx / sizeof idx[0])];
        xr[i] = (FLOAT) (idx[i % (int) (sizeof idx / sizeof idx[0])] % 40);
    }
    check_one(64, 1.0f, 1.0f, xr, xr34);
    check_one(66, 1.0f, 1.0f, xr, xr34);   /* odd tail over the boundaries */
    check_one(67, 1.0f, 1.0f, xr, xr34);
}

/** All zero - a silent band. */
static void
test_all_zero(LAME_UNUSED void **state)
{
    FLOAT xr[MAX_BW + 8], xr34[MAX_BW + 8];
    unsigned int bw;

    memset(xr, 0, sizeof xr);
    memset(xr34, 0, sizeof xr34);
    for (bw = 0; bw <= 40; ++bw)
        check_one(bw, 0.85f, 1.9f, xr, xr34);
}

/** Guard: the reference must be able to disagree, or the checks prove nothing. */
static void
test_reference_can_disagree(LAME_UNUSED void **state)
{
    FLOAT xr[64], xr34[64];
    double r0, r1;
    int i;

    for (i = 0; i < 64; ++i) { xr[i] = (FLOAT) (i + 0.6); xr34[i] = (FLOAT) (i + 0.3); }
    r0 = ref_noise(xr, xr34, 64, 0.85f, 1.9f);
    r1 = ref_noise(xr, xr34, 64, 0.85f, 2.9f);   /* different sfpow34 -> different */
    assert_true(fabs(r0 - r1) > 1.0);
    /* and the routine tracks the reference across that change, not a constant */
    {
        FLOAT s0 = calc_sfb_noise_x34_sse2(xr, xr34, 64, 0.85f, 1.9f, adj_t, pw43_t);
        FLOAT s1 = calc_sfb_noise_x34_sse2(xr, xr34, 64, 0.85f, 2.9f, adj_t, pw43_t);
        assert_true(fabs((double) s0 - r0) <= 1e-4 * r0);
        assert_true(fabs((double) s1 - r1) <= 1e-4 * r1);
    }
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_lengths),
        cmocka_unit_test(test_index_boundaries),
        cmocka_unit_test(test_all_zero),
        cmocka_unit_test(test_reference_can_disagree),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
