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
 * program is built only `if WITH_XMM`. The two run the same escape cases,
 * from test_esc_cases.h.
 *
 * The table is synthetic. The routine takes the table as an argument. So
 * nothing here depends on the contents of LAME's table. An index that is off
 * by one gives a wrong sum. Neighbor entries with the same value cannot hide
 * the mistake.
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
#include "test_esc_cases.h"

/** @brief A 256-entry table in which every entry is different. */
static uint32_t largetbl_t[16 * 16];

static void
tables_init(void)
{
    unsigned int i;

    for (i = 0; i < 16u * 16u; ++i)
        largetbl_t[i] = i * 7u + 1u;
}

/**
 * @brief Runs esc_check_lengths() on the NEON routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_lengths(LAME_UNUSED void **state)
{
    esc_check_lengths(count_bit_esc_neon, largetbl_t);
}

/**
 * @brief Runs esc_check_clamp_boundary() on the NEON routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_clamp_boundary(LAME_UNUSED void **state)
{
    esc_check_clamp_boundary(count_bit_esc_neon, largetbl_t);
}

/**
 * @brief Runs esc_check_large_values() on the NEON routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_large_values(LAME_UNUSED void **state)
{
    esc_check_large_values(count_bit_esc_neon, largetbl_t);
}

/**
 * @brief Runs esc_check_reference_can_disagree() on the NEON routine.
 * @param state cmocka fixture state (unused).
 */
static void
test_esc_reference_can_disagree(LAME_UNUSED void **state)
{
    esc_check_reference_can_disagree(count_bit_esc_neon, largetbl_t);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_esc_lengths),
        cmocka_unit_test(test_esc_clamp_boundary),
        cmocka_unit_test(test_esc_large_values),
        cmocka_unit_test(test_esc_reference_can_disagree),
    };
    tables_init();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
