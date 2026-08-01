/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the automatic rung choice in vector_impl_init().
 *
 * The function under test is the *decision*, not the report.  Since the
 * decision was split out, vector_implementation() only hands back what was
 * decided earlier, so asking it what a machine would choose answers nothing:
 * @c vector_impl_init() is where a capability combination turns into a rung.
 *
 * It reads @c gfc->CPU_features and nothing else - not cpuid, not the
 * processor it is running on.  That is what makes this testable anywhere: the
 * capability bits are set by hand, so one host covers the whole lattice,
 * including combinations it does not have and the all-scalar machine that has
 * none of them.  A host with AVX-512 tests the no-vector case here, and a host
 * with nothing tests the AVX-512 case; neither could be reached by encoding
 * something and looking at what came out.
 *
 * Four properties, deliberately checked separately rather than folded into one
 * loop.  The first states the answer; the other three state things that must
 * hold whatever the answer is, so a mistake in the first one's model of the
 * ladder does not silence them:
 *
 *   - AUTO picks the widest rung this build carries whose capability bit is
 *     set;
 *   - it never picks a rung whose bit is clear, which is the failure that
 *     ends in an illegal instruction on a user's machine;
 *   - it never picks a rung this build did not compile, which is the failure
 *     that ends in a link error or a call through nothing;
 *   - with no capabilities at all the answer is the scalar code.
 *
 * @c vector_impl_init() is internal and is withheld from the shared library by
 * @c include/libmp3lame.sym, so this test links the static archive - the same
 * arrangement @c test_set_get.c uses for the internal tuning setters.
 *
 * Not covered here, on purpose: that an explicit request is honoured.
 * @c test_vector_routines.c already proves that end to end through the public
 * API, which is the level a caller sees it at.  See @ref vector_dispatch.
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

/** @brief Rungs this build compiled; the sentinel is not one of them. */
static int
ladder_count(void)
{
    return (int) (sizeof ladder / sizeof ladder[0]) - 1;
}

/** @brief A zeroed encoder context - the only two fields in play are set here. */
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

/** @brief Present @p mask to the decision as if the processor reported it. */
static void
set_capabilities(lame_internal_flags * gfc, unsigned mask)
{
    gfc->CPU_features.SSE2 = OFFERS(mask, CAP_SSE2);
    gfc->CPU_features.AVX2 = OFFERS(mask, CAP_AVX2);
    gfc->CPU_features.AVX512 = OFFERS(mask, CAP_AVX512);
    gfc->CPU_features.NEON = OFFERS(mask, CAP_NEON);
}

/**
 * @brief What the ladder owes for @p mask, worked out independently.
 *
 * The rows ascend, so the last one whose bit is set is the widest offered.
 * test_ladder_is_ordered() is what entitles this to say "last" and mean
 * "widest".
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
 * @brief Run the decision on a context that carries exactly @p mask.
 *
 * Twice, from opposite starting values, because the interesting way for a
 * decision to be wrong is not to be made at all: a path that returns without
 * writing the field leaves whatever was there, and reading the field back
 * afterwards cannot tell that from an answer.  Starting once at the bottom of
 * the ladder and once at the top makes any such path disagree with itself.
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

/** @brief Whether @p impl is a rung this build actually carries. */
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
 * @brief The enum is capability-ordered, and so is the table above.
 *
 * Both the "widest wins" walk in util.c and the call sites that ask
 * ">= the rung my routine needs" are comparisons on this enum, so its order is
 * load-bearing rather than cosmetic.  Reordering the members - or adding a new
 * rung in the wrong place - would leave every one of those comparisons
 * compiling and answering differently.
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
 * @brief AUTO answers with the widest rung the machine offers.
 *
 * Every capability combination, not just the plausible ones: a processor that
 * reports AVX-512 without AVX2 does not exist, but the walk must not depend on
 * that, because the bits it reads have already been masked by the deprecated
 * asm_optimizations flags and can arrive with holes in them.
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
 * @brief It never picks a rung the processor did not report.
 *
 * The consequence of getting this wrong is an illegal instruction on a
 * machine that is otherwise fine, which is why it is asserted on its own
 * rather than left to the model above.
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
 * @brief It never picks a rung this build did not compile.
 *
 * The mirror failure: a machine that reports more than the binary carries -
 * an AVX-512 processor running a build configured without it, which is the
 * common case, not an exotic one.  The answer there must be the widest rung
 * that was compiled, never the one the hardware could have run.
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
 * @brief A processor with none of them runs the scalar code.
 *
 * Stated without reference to the model above, because it is the one answer
 * that is the same in every configuration - including a build carrying no
 * vector routines at all, where it is the only answer.
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
