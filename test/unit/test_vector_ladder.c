/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the automatic tier choice in vector_impl_init().
 *
 * This file tests the *decision*, not the report. vector_implementation()
 * only returns the tier that was stored earlier. So it cannot tell which tier
 * a machine would choose. @c vector_impl_init() is the function that turns a
 * combination of capabilities into a tier.
 *
 * @c vector_impl_init() reads @c gfc->CPU_features and nothing else. It does
 * not run cpuid, and it does not look at the CPU it runs on. So this test can
 * run on any host. The test sets the capability bits by hand. So one host can
 * test every combination, including combinations that it does not have. One
 * of them is a machine with no vector capability at all. A host with AVX-512
 * tests the case without vector routines here. A host without vector routines
 * tests the AVX-512 case. An encode on either host cannot test these cases.
 *
 * The file checks four properties, each in its own test. The first test
 * states the expected tier. The other three properties must be true whatever
 * that tier is. So a mistake in the first test's model of the tiers does not
 * hide a failure in the other three:
 *
 *   - AUTO picks the widest tier that this build has and whose capability
 *     bit is set.
 *   - AUTO never picks a tier whose bit is clear. This failure ends in an
 *     illegal instruction on a user's machine.
 *   - AUTO never picks a tier that this build did not compile. This failure
 *     ends in a link error or in a call to a routine that does not exist.
 *   - With no capabilities at all, the result is the scalar code.
 *
 * @c vector_impl_init() is internal. @c include/libmp3lame.sym does not list
 * it, so the shared library does not export it. So this test links the
 * static archive. @c test_set_get.c does the same for the internal tuning
 * setters.
 *
 * This file does not test that an explicit request is applied.
 * @c test_vector_routines.c tests that end to end through the public API.
 * That is the level at which a caller sees it. See @ref vector_dispatch.
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

/*
 * The capability bits, as this test names them.  All four exist in
 * CPU_features on every architecture - the struct does not vary - so the
 * lattice below is the same 16 combinations everywhere, and on any one build
 * most of them describe a machine wider than the binary.  That is the
 * interesting half.
 */
#define CAP_SSE2    0x1u
#define CAP_AVX2    0x2u
#define CAP_AVX512  0x4u
#define CAP_NEON    0x8u
#define CAP_ALL     (CAP_SSE2 | CAP_AVX2 | CAP_AVX512 | CAP_NEON)

/*
 * Whether the processor described by a mask offers one capability.  The bit
 * test is written once per capability and once per ladder row, and a mistyped
 * one of those reads exactly like the rest of them.
 */
#define OFFERS(mask, cap)   (((mask) & (cap)) != 0u)

/*
 * The ladder as the test states it: one row per set this build compiled, in
 * increasing capability order, each with the bit the decision has to consult.
 * Written out here rather than read back out of util.c's table, so that the
 * two have to agree - a rung wired to the wrong capability bit would be
 * self-consistent inside util.c and wrong here.
 *
 * The #if guards are copied from that table verbatim: if the two ever
 * disagree about which rungs exist, this file stops compiling rather than
 * quietly testing a shorter ladder.  The trailing row is a sentinel, since
 * C89 forbids an empty initialiser and a build with no vector routines at all
 * is a configuration this has to hold for.
 */
static const struct {
    unsigned cap;
    vector_impl_t impl;
} ladder[] = {
#if defined( HAVE_SSE2_INTRINSICS )
    { CAP_SSE2, VECTOR_IMPL_SSE2 },
#endif
#if defined( HAVE_AVX2_INTRINSICS )
    { CAP_AVX2, VECTOR_IMPL_AVX2 },
#endif
#if defined( HAVE_AVX512_INTRINSICS )
    { CAP_AVX512, VECTOR_IMPL_AVX512 },
#endif
#if defined( HAVE_NEON_INTRINSICS )
    { CAP_NEON, VECTOR_IMPL_NEON },
#endif
    { 0, VECTOR_IMPL_NONE }
};

/** @brief Returns the number of tiers that this build compiled. The sentinel is not counted. */
static int
ladder_count(void)
{
    return (int) (sizeof ladder / sizeof ladder[0]) - 1;
}

/** @brief Allocates a zeroed encoder context. The tests set the only two fields that matter. */
static int
gfc_setup(void **state)
{
    lame_internal_flags *gfc = calloc(1, sizeof *gfc);

    if (gfc == NULL)
        return -1;
    *state = gfc;
    return 0;
}

static int
gfc_teardown(void **state)
{
    free(*state);
    return 0;
}

/** @brief Sets the capability bits from @p mask, as if the CPU had reported them. */
static void
set_capabilities(lame_internal_flags * gfc, unsigned mask)
{
    gfc->CPU_features.SSE2 = OFFERS(mask, CAP_SSE2);
    gfc->CPU_features.AVX2 = OFFERS(mask, CAP_AVX2);
    gfc->CPU_features.AVX512 = OFFERS(mask, CAP_AVX512);
    gfc->CPU_features.NEON = OFFERS(mask, CAP_NEON);
}

/**
 * @brief Returns the tier that AUTO must pick for @p mask, computed
 *        independently.
 *
 * The rows are in increasing order. So the last row whose bit is set is the
 * widest tier offered. test_ladder_is_ordered() checks this order. That check
 * is what makes "last" mean "widest" here.
 */
static vector_impl_t
widest_offered(unsigned mask)
{
    vector_impl_t best = VECTOR_IMPL_NONE;
    int     i;

    for (i = 0; i < ladder_count(); ++i) {
        if (OFFERS(mask, ladder[i].cap))
            best = ladder[i].impl;
    }
    return best;
}

/**
 * @brief Runs the decision on a context that has exactly the bits in @p mask.
 *
 * The decision runs twice, from opposite start values. One way for the
 * decision to be wrong is that it is not made at all. A path that returns
 * without writing the field keeps the old value. A read of the field cannot
 * tell that old value from a real result. So one run starts at the lowest
 * tier and one run starts at the highest. A path that does not write the
 * field then gives two different results.
 */
static vector_impl_t
decide(lame_internal_flags * gfc, unsigned mask)
{
    vector_impl_t from_bottom, from_top;

    set_capabilities(gfc, mask);

    gfc->vector_impl = VECTOR_IMPL_NONE;
    vector_impl_init(gfc, VECTOR_IMPL_AUTO);
    from_bottom = gfc->vector_impl;

    gfc->vector_impl = widest_offered(CAP_ALL);
    vector_impl_init(gfc, VECTOR_IMPL_AUTO);
    from_top = gfc->vector_impl;

    assert_int_equal((int) from_bottom, (int) from_top);
    return from_bottom;
}

/** @brief Checks whether @p impl is a tier that this build compiled. */
static int
is_compiled_rung(vector_impl_t impl)
{
    int     i;

    for (i = 0; i < ladder_count(); ++i) {
        if (ladder[i].impl == impl)
            return 1;
    }
    return 0;
}

/**
 * @brief Checks that the enum values in the table above increase in
 *        capability order.
 *
 * The dispatch sites test ">= the tier my routine needs". These tests compare
 * values of the enum, so the order of its members matters. The walk in util.c
 * goes through its table from the last row to the first. So the order of that
 * table matters too. A reordered enum, or a new tier in the wrong place,
 * still compiles. But the comparisons then give different results.
 */
static void
test_ladder_is_ordered(LAME_UNUSED void **state)
{
    int     i;

    for (i = 0; i < ladder_count(); ++i) {
        assert_true(ladder[i].impl > VECTOR_IMPL_NONE);
        if (i > 0)
            assert_true(ladder[i].impl > ladder[i - 1].impl);
    }
}

/**
 * @brief Checks that AUTO returns the widest tier that the machine offers.
 *
 * The test runs every combination of capabilities, not only the realistic
 * ones. No CPU reports AVX-512 without AVX2. But the walk must not depend on
 * that. Separate probes set the bits that it reads. The deprecated
 * asm_optimizations flags then mask them. The walk must return the right tier
 * for any combination.
 */
static void
test_auto_picks_the_widest_offered(void **state)
{
    lame_internal_flags *gfc = (lame_internal_flags *) * state;
    unsigned mask;

    for (mask = 0; mask <= CAP_ALL; ++mask)
        assert_int_equal((int) decide(gfc, mask), (int) widest_offered(mask));
}

/**
 * @brief Checks that AUTO never picks a tier that the CPU did not report.
 *
 * If this property fails, a machine that otherwise works stops with an
 * illegal instruction. So this test checks the property on its own. It does
 * not depend on the model above.
 */
static void
test_auto_never_exceeds_the_capabilities(void **state)
{
    lame_internal_flags *gfc = (lame_internal_flags *) * state;
    unsigned mask;

    for (mask = 0; mask <= CAP_ALL; ++mask) {
        vector_impl_t const got = decide(gfc, mask);
        int     i;

        if (got == VECTOR_IMPL_NONE)
            continue;   /* the scalar code needs no capability */
        for (i = 0; i < ladder_count(); ++i) {
            if (ladder[i].impl == got)
                assert_true(OFFERS(mask, ladder[i].cap));
        }
    }
}

/**
 * @brief Checks that AUTO never picks a tier that this build did not compile.
 *
 * This is the opposite failure. The machine reports more than the binary
 * has. An example is an AVX-512 CPU that runs a build configured without
 * AVX-512. This case is common. The result must be the widest tier that the
 * build compiled, never the tier that the hardware could run.
 */
static void
test_auto_never_exceeds_the_build(void **state)
{
    lame_internal_flags *gfc = (lame_internal_flags *) * state;
    unsigned mask;

    for (mask = 0; mask <= CAP_ALL; ++mask) {
        vector_impl_t const got = decide(gfc, mask);

        if (got == VECTOR_IMPL_NONE)
            continue;
        assert_int_equal(is_compiled_rung(got), 1);
    }
}

/**
 * @brief Checks that a CPU with none of the capabilities runs the scalar code.
 *
 * This test does not use the model above. Its result is the same in every
 * configuration. In a build with no vector routines at all, it is the only
 * possible result.
 */
static void
test_no_capabilities_is_scalar(void **state)
{
    lame_internal_flags *gfc = (lame_internal_flags *) * state;

    assert_int_equal((int) decide(gfc, 0u), (int) VECTOR_IMPL_NONE);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ladder_is_ordered),
        cmocka_unit_test_setup_teardown(test_auto_picks_the_widest_offered,
                                        gfc_setup, gfc_teardown),
        cmocka_unit_test_setup_teardown(test_auto_never_exceeds_the_capabilities,
                                        gfc_setup, gfc_teardown),
        cmocka_unit_test_setup_teardown(test_auto_never_exceeds_the_build,
                                        gfc_setup, gfc_teardown),
        cmocka_unit_test_setup_teardown(test_no_capabilities_is_scalar,
                                        gfc_setup, gfc_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
