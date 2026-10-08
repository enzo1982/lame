/**
 * @file
 * @ingroup unit_tests
 * @brief The escape-counting cases of the x86 and the ARM vector tests.
 *
 * count_bit_esc_sse2() and count_bit_esc_neon() have the same contract.
 * test_choose_table_vector.c and test_choose_table_neon.c run the cases here
 * on their own routine and their own table. The cases are the ones that are
 * difficult by design, not by chance:
 *
 *   - every even length on both sides of the vector threshold and of the
 *     block size,
 *   - both sides of the clamp boundary,
 *   - values far above the clamp boundary.
 *
 * The scalar reference is not LAME's own loop. Two versions of the same loop
 * can share a mistake and still agree.
 */
#ifndef LAME_TEST_ESC_CASES_H
#define LAME_TEST_ESC_CASES_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "machine.h"

/** @brief An escape-counting routine: count_bit_esc_sse2() or count_bit_esc_neon(). */
typedef unsigned int (*esc_count_fn)(const int *ix, const int *end,
                                     const uint32_t *largetbl, unsigned int *nclamped);

/** @brief The longest region that esc_check_lengths() passes, in values. */
#define ESC_LONGEST_REGION 80
/** @brief The region length of the other cases, in values. */
#define ESC_REGION 64
/** @brief The largest value that escape coding does not clamp. */
#define ESC_LAST_UNCLAMPED 14

/**
 * @brief Fills @p ix with a repeatable pseudo-random pattern in [0, hi].
 * @param ix    the region to fill.
 * @param n     its length.
 * @param hi    the largest value.
 * @param seed  the start of the pattern.
 */
static inline void
fill_pattern(int *ix, int n, int hi, unsigned int seed)
{
    int     i;

    for (i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        ix[i] = (int) ((seed >> 16) % (unsigned int) (hi + 1));
    }
}

/**
 * @brief Computes the scalar result, independently of LAME's code.
 *
 * Reads pairs and clamps each value at 15. Counts the clamped values. Sums
 * the entries of @p tbl at x * 16 + y.
 *
 * @param ix        the region.
 * @param n         its length, an even number.
 * @param tbl       the 256-entry table.
 * @param nclamped  receives the number of clamped values.
 * @return the sum.
 */
static inline unsigned int
ref_esc(const int *ix, int n, const uint32_t *tbl, unsigned int *nclamped)
{
    unsigned int sum = 0;
    unsigned int nc = 0;
    int     i;

    for (i = 0; i < n; i += 2) {
        unsigned int x = (unsigned int) ix[i];
        unsigned int y = (unsigned int) ix[i + 1];

        if (x > ESC_LAST_UNCLAMPED) {
            x = ESC_LAST_UNCLAMPED + 1;
            ++nc;
        }
        if (y > ESC_LAST_UNCLAMPED) {
            y = ESC_LAST_UNCLAMPED + 1;
            ++nc;
        }
        sum += tbl[(x << 4u) + y];
    }
    *nclamped = nc;
    return sum;
}

/**
 * @brief Checks that the sum and the clamp count agree with the reference at
 *        every length.
 *
 * The test runs every even length from 2 to 80. So it crosses the vector
 * block, the threshold that the caller applies, and every possible remainder.
 * It does not depend on one convenient size.
 *
 * @param count  the routine.
 * @param tbl    its table.
 */
static inline void
esc_check_lengths(esc_count_fn count, const uint32_t *tbl)
{
    int     ix[ESC_LONGEST_REGION];
    int     n;

    for (n = 2; n <= ESC_LONGEST_REGION; n += 2) {
        unsigned int nc_v = 12345, nc_r = 0;
        unsigned int sv, sr;

        fill_pattern(ix, n, 200, (unsigned int) n + 7u);
        sv = count(ix, ix + n, tbl, &nc_v);
        sr = ref_esc(ix, n, tbl, &nc_r);
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
 *
 * @param count  the routine.
 * @param tbl    its table.
 */
static inline void
esc_check_clamp_boundary(esc_count_fn count, const uint32_t *tbl)
{
    int     ix[ESC_REGION];
    int     v;

    for (v = ESC_LAST_UNCLAMPED - 1; v <= ESC_LAST_UNCLAMPED + 3; ++v) {
        unsigned int nc_v = 0, nc_r = 0;
        unsigned int sv, sr;
        int     i;

        for (i = 0; i < ESC_REGION; ++i)
            ix[i] = v;
        sv = count(ix, ix + ESC_REGION, tbl, &nc_v);
        sr = ref_esc(ix, ESC_REGION, tbl, &nc_r);
        assert_int_equal(sv, sr);
        assert_int_equal(nc_v, nc_r);
        assert_int_equal(nc_v, v > ESC_LAST_UNCLAMPED ? (unsigned int) ESC_REGION : 0u);
    }
}

/**
 * @brief Checks that values far above the clamp still count once each, not
 *        more.
 *
 * The x86 routine narrows to sixteen bits here. Its narrowing saturates, and
 * that makes it harmless. The ARM routine works in 32-bit lanes and does not
 * narrow. The two differ most in this case. A rewrite that narrows without
 * saturation fails here, before an encode writes wrong output. Every third
 * value of the 64 is large, so 22 is the only correct count.
 *
 * @param count  the routine.
 * @param tbl    its table.
 */
static inline void
esc_check_large_values(esc_count_fn count, const uint32_t *tbl)
{
    int     ix[ESC_REGION];
    unsigned int nc_v = 0, nc_r = 0;
    unsigned int sv, sr;
    int     i;

    for (i = 0; i < ESC_REGION; ++i)
        ix[i] = (i % 3 == 0) ? 40000 : 3;
    sv = count(ix, ix + ESC_REGION, tbl, &nc_v);
    sr = ref_esc(ix, ESC_REGION, tbl, &nc_r);
    assert_int_equal(sv, sr);
    assert_int_equal(nc_v, nc_r);
    assert_int_equal(nc_v, 22u);
}

/**
 * @brief Checks that the reference can disagree with the routine. Without
 *        this check, the cases above prove nothing.
 *
 * Every case above compares the routine with ref_esc(). If the two could
 * never differ, that comparison would test nothing. So this case gives the
 * reference wrong data on purpose. It requires the two results to differ.
 *
 * @param count  the routine.
 * @param tbl    its table.
 */
static inline void
esc_check_reference_can_disagree(esc_count_fn count, const uint32_t *tbl)
{
    int     ix[ESC_REGION];
    int     bad[ESC_REGION];
    unsigned int nc_v = 0, nc_r = 0;
    unsigned int sv, sr;
    int     i;

    for (i = 0; i < ESC_REGION; ++i) {
        ix[i] = (i * 5) % 17;
        bad[i] = ix[i];
    }
    bad[9] += 1;
    sv = count(ix, ix + ESC_REGION, tbl, &nc_v);
    sr = ref_esc(bad, ESC_REGION, tbl, &nc_r);
    assert_int_not_equal(sv, sr);
}

#endif /* LAME_TEST_ESC_CASES_H */
