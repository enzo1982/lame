/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the NEON Huffman escape-counting primitive.
 *
 * The ARM tier has one routine, and this file tests it. The x86 file
 * test_choose_table_vector.c tests four routines, because x86 has four. The
 * difference comes from measurements. They showed which routines a compiler
 * does not already vectorize, and which of those give a gain. See
 * @ref vector_dispatch.
 *
 * This is a separate program, not a part of test_choose_table_vector.c. That
 * program is built only `if WITH_XMM`, and it calls the SSE2 and AVX2
 * routines by name throughout. So the two programs share no code. They share
 * the method on purpose. They test the cases that are difficult by design,
 * not by chance:
 *
 *   - every even length on both sides of the vector threshold and of the
 *     block size,
 *   - both sides of the clamp boundary,
 *   - values far above the clamp boundary.
 *
 * The scalar reference is written in this file. It is not LAME's own loop,
 * for the reason that the x86 file gives. Two versions of the same loop can
 * share a mistake and still agree. The table is synthetic for the same
 * reason. The routine takes the table as an argument. So nothing here depends
 * on the contents of LAME's table. An index that is off by one gives a wrong
 * sum. Neighbor entries with the same value cannot hide the mistake.
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

/** Longest region that the encoder passes to the routine. */
#define MAX_LEN 576

/** @brief A 256-entry table in which every entry is different. */
static uint32_t largetbl_t[16 * 16];

static void
tables_init(void)
{
    unsigned int i;

    for (i = 0; i < 16u * 16u; ++i)
        largetbl_t[i] = i * 7u + 1u;
}

/** @brief Fills @p ix with pseudo-random values in [0, hi). The values fall on both sides of the clamp. */
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
 * @brief Computes the scalar result, independently of LAME's code.
 *
 * Reads pairs and clamps each value at 15. Counts the clamped values. Sums
 * the table entries at x * 16 + y.
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
 * @brief Checks that the sum and the clamp count agree with the reference at
 *        every length.
 *
 * The test runs every even length from 2 to 80. So it crosses the vector
 * block (eight values), the threshold that the caller applies, and every
 * possible remainder. It does not depend on one convenient size.
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
 * @brief Checks that 15 is clamped and 14 is not. Escape coding starts at
 *        this boundary.
 *
 * Each region has the same value in every position. So a count that is off
 * by one per block, per lane or per remainder cannot hide in a mixed sample.
 * The test also checks the expected count as an exact number, not only as
 * agreement with the reference. Both could be wrong in the same way. For a
 * value of 15 or more, 64 is the only correct count.
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
 * @brief Checks that values far above the clamp still count once each, not
 *        more.
 *
 * The x86 kernel narrows to sixteen bits here. Its narrowing saturates, and
 * that makes it harmless. The ARM kernel works in 32-bit lanes and does not
 * narrow. So there is no saturation to consider. The two implementations
 * differ most in this case, so the test is worth keeping. A rewrite that
 * narrows without saturation fails here, before an encode writes wrong
 * output.
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
 * @brief Checks that the reference can disagree. Without this check, the
 *        tests above prove nothing.
 *
 * Every test above compares the routine with ref_esc(). If the two could
 * never differ, that comparison would test nothing. So this test gives the
 * reference wrong data on purpose. It requires the two results to differ.
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
