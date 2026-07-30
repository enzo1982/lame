/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the NEON Huffman escape-counting primitive.
 *
 * The ARM tier carries one routine, and this is it. The x86 file next door
 * tests four, because x86 has four; the difference is not an omission but the
 * result of measuring which of them a compiler does not already write, and
 * then which of those actually pay; see @ref vector_dispatch.
 *
 * A separate program rather than an arm of test_choose_table_vector.c: that
 * one is built only `if WITH_XMM` and calls the SSE2 and AVX2 routines by
 * name throughout, so the two have no overlapping body. What they do share is
 * the method, deliberately - the cases that are awkward by construction
 * rather than by luck: every length either side of the vector threshold and
 * the block size, both sides of the clamp boundary, and values far above it.
 *
 * The scalar reference is written here rather than taken from LAME's own, for
 * the reason the x86 file gives: two spellings of the same loop would agree
 * about a shared misunderstanding. The table is synthetic for the same reason
 * - the routine takes it as an argument, so nothing here depends on the
 * contents of LAME's, and an index computed one place off shows up as a wrong
 * sum instead of being masked by neighbouring entries that happen to be equal.
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
#include "test_unused.h"

/** Longest region the encoder ever asks about. */
#define MAX_LEN 576

/** @brief A 256-entry table whose every entry is distinct. */
static uint32_t largetbl_t[16 * 16];

static void
tables_init(void)
{
    unsigned int i;

    for (i = 0; i < 16u * 16u; ++i)
        largetbl_t[i] = i * 7u + 1u;
}

/** @brief Fill with a spread that crosses the clamp in both directions. */
static void
fill(int *ix, int n, int hi, unsigned int seed)
{
    int     i;
    unsigned int s = seed;

    for (i = 0; i < n; ++i) {
        s = s * 1103515245u + 12345u;
        ix[i] = (int) ((s >> 16) % (unsigned int) hi);
    }
}

/**
 * @brief The scalar answer, written independently of LAME's.
 *
 * Reads pairs, clamps each value at 15, counts how many were clamped, and
 * sums the table at x * 16 + y.
 */
static unsigned int
ref_esc(const int *ix, int n, unsigned int *nclamped)
{
    unsigned int sum = 0;
    unsigned int nc = 0;
    int     i;

    for (i = 0; i < n; i += 2) {
        unsigned int x = (unsigned int) ix[i];
        unsigned int y = (unsigned int) ix[i + 1];

        if (x >= 15u) {
            x = 15u;
            ++nc;
        }
        if (y >= 15u) {
            y = 15u;
            ++nc;
        }
        sum += largetbl_t[(x << 4u) + y];
    }
    *nclamped = nc;
    return sum;
}

/**
 * @brief Sum and clamp count agree with the reference at every length.
 *
 * Every even length from 2 to 80 - so the vector block (eight values), the
 * threshold its caller applies, and every possible remainder are all crossed,
 * rather than trusting one convenient size.
 */
static void
test_esc_lengths(LAME_UNUSED void **state)
{
    int     ix[MAX_LEN];
    int     n;

    for (n = 2; n <= 80; n += 2) {
        unsigned int nc_v = 12345, nc_r = 0;
        unsigned int sv, sr;

        fill(ix, n, 200, (unsigned int) n + 7u);
        sv = count_bit_esc_neon(ix, ix + n, largetbl_t, &nc_v);
        sr = ref_esc(ix, n, &nc_r);
        assert_int_equal(sv, sr);
        assert_int_equal(nc_v, nc_r);
    }
}

/**
 * @brief 15 is clamped and 14 is not - the boundary escape coding turns on.
 *
 * Whole regions of one value, so a count that is off by one per block, per
 * lane or per remainder cannot hide in a mixed sample. The expected count is
 * asserted as an exact number, not merely as agreement with the reference:
 * both could be wrong the same way, and 64 is the only right answer here.
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
        sv = count_bit_esc_neon(ix, ix + 64, largetbl_t, &nc_v);
        sr = ref_esc(ix, 64, &nc_r);
        assert_int_equal(sv, sr);
        assert_int_equal(nc_v, nc_r);
        assert_int_equal(nc_v, v >= 15 ? 64u : 0u);
    }
}

/**
 * @brief Values far above the clamp still count once each, not more.
 *
 * The x86 kernel narrows to sixteen bits here and has to argue that the
 * saturation is harmless. The ARM one works in 32-bit lanes and never
 * narrows, so there is no saturation to reason about - which is exactly why
 * this case is worth keeping: it is the one where the two implementations
 * differ most, and a future rewrite that reintroduces narrowing would fail
 * here rather than silently in an encode.
 */
static void
test_esc_large_values(LAME_UNUSED void **state)
{
    int     ix[64];
    unsigned int nc_v = 0, nc_r = 0;
    unsigned int sv, sr;
    int     i;

    for (i = 0; i < 64; ++i)
        ix[i] = (i % 3 == 0) ? 40000 : 3;
    sv = count_bit_esc_neon(ix, ix + 64, largetbl_t, &nc_v);
    sr = ref_esc(ix, 64, &nc_r);
    assert_int_equal(sv, sr);
    assert_int_equal(nc_v, nc_r);
    assert_int_equal(nc_v, 22u);
}

/**
 * @brief The reference can disagree - otherwise the tests above prove nothing.
 *
 * Every assertion here compares the routine against ref_esc(). If the two
 * could not differ, that comparison would be untestable by construction. So
 * one case feeds the reference deliberately wrong data and requires the
 * answers to part company.
 */
static void
test_reference_can_disagree(LAME_UNUSED void **state)
{
    int     ix[64];
    int     bad[64];
    unsigned int nc_v = 0, nc_r = 0;
    unsigned int sv, sr;
    int     i;

    for (i = 0; i < 64; ++i) {
        ix[i] = (i * 5) % 17;
        bad[i] = ix[i];
    }
    bad[9] += 1;
    sv = count_bit_esc_neon(ix, ix + 64, largetbl_t, &nc_v);
    sr = ref_esc(bad, 64, &nc_r);
    assert_int_not_equal(sv, sr);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_esc_lengths),
        cmocka_unit_test(test_esc_clamp_boundary),
        cmocka_unit_test(test_esc_large_values),
        cmocka_unit_test(test_reference_can_disagree),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
