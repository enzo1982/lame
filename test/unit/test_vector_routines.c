/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vector-routines API.
 *
 * The tests for the routines themselves depend on the architecture. Nothing
 * here does, and that is the purpose of the API. It reports what this build
 * has, whatever that is. So the same assertions are true on a build with two
 * sets, one set or none. A build with no set is a real configuration, not a
 * broken one.
 *
 * So the assertions test the *contract*, not a known list of names. A test
 * that expects "sse2" at index 0 fails on ARM for no reason. It also says
 * nothing about whether the contract is met.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <cmocka.h>

#include "test_fixture.h"

#include "lame.h"
#include "test_unused.h"

/*
 * The test's own bound on a name, deliberately not taken from the library:
 * these are library-level tests, so they see only the public header. Every
 * name the API reports is asserted to fit, which is what makes the bounded
 * comparisons below exact rather than merely safe.
 */
#define VECTOR_NAME_MAX 32

/** @brief Creates an encoder instance and runs lame_init_params() on it. */
static int
inited_setup(void **state)
{
    lame_t  gfp = lame_init();

    if (gfp == NULL)
        return -1;
    lame_set_num_channels(gfp, 2);
    lame_set_in_samplerate(gfp, 44100);
    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        return -1;
    }
    *state = gfp;
    return 0;
}

/**
 * @brief Checks that the count is never negative and that every promised
 *        name exists.
 *
 * The function takes no arguments and no encoder instance. So it has nothing
 * to fail on. The contract says so explicitly, and this test checks it. Zero
 * is a valid count, so the test must not require any set to exist.
 */
static void
test_count_and_names(LAME_UNUSED void **state)
{
    int const n = lame_get_num_vector_routines();
    int     i;

    assert_true(n >= 0);

    for (i = 0; i < n; ++i) {
        const char *const name = lame_get_vector_routines_name(i);
        size_t  k;

        assert_non_null(name);
        assert_true(name[0] != '\0');
        /* lowercase identifiers - the setter is strict about it - and short
           enough that a fixed buffer holds one whole */
        for (k = 0; k < VECTOR_NAME_MAX && name[k] != '\0'; ++k)
            assert_false(isupper((unsigned char) name[k]));
        assert_true(k < VECTOR_NAME_MAX);
    }
}

/** @brief Checks that the name lookup returns NULL only for an index out of range. */
static void
test_name_bounds(LAME_UNUSED void **state)
{
    int const n = lame_get_num_vector_routines();

    assert_null(lame_get_vector_routines_name(-1));
    assert_null(lame_get_vector_routines_name(n));
    assert_null(lame_get_vector_routines_name(n + 1));
    if (n > 0)
        assert_non_null(lame_get_vector_routines_name(n - 1));
}

/** @brief Checks that no two sets have the same name. Otherwise one name could refer to two sets. */
static void
test_names_are_distinct(LAME_UNUSED void **state)
{
    int const n = lame_get_num_vector_routines();
    int     i, j;

    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j)
            assert_string_not_equal(lame_get_vector_routines_name(i),
                                    lame_get_vector_routines_name(j));
}

/**
 * @brief Checks that the setter accepts every enumerated name.
 *
 * lame_get_vector_routines_name() promises this. For a name that the
 * enumeration returns, the setter must never return "unknown" (-2) or "not in
 * this build" (-3). The test accepts -4, because a CPU that cannot run a set
 * is a fact about the machine, not about the name.
 */
static void
test_enumerated_names_round_trip(void **state)
{
    lame_t  gfp = (lame_t) *state;
    int const n = lame_get_num_vector_routines();
    int     i;

    for (i = 0; i < n; ++i) {
        int const r = lame_set_vector_routines(gfp, lame_get_vector_routines_name(i));

        assert_true(r == 0 || r == -4);
        assert_int_not_equal(r, -2);
        assert_int_not_equal(r, -3);
    }
}

/** @brief Checks that the setter always accepts the two reserved names, even in a build with no sets. */
static void
test_reserved_names(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_int_equal(lame_set_vector_routines(gfp, "auto"), 0);
    assert_int_equal(lame_set_vector_routines(gfp, "none"), 0);
}

/**
 * @brief Checks that an unknown name returns -2 and a NULL instance returns
 *        -1.
 *
 * The function also returns -3 for a set that this build does not include.
 * It returns -4 for a set that this CPU cannot run. This test does not check
 * those two codes.
 */
static void
test_rejections(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_int_equal(lame_set_vector_routines(gfp, "nosuchthing"), -2);
    assert_int_equal(lame_set_vector_routines(gfp, ""), -2);
    assert_int_equal(lame_set_vector_routines(gfp, NULL), -2);

    /* Strict lowercase: the name is an identifier. If this build has a set,
       its name upper-cased must not be accepted. */
    if (lame_get_num_vector_routines() > 0) {
        char    upper[VECTOR_NAME_MAX];
        const char *const name = lame_get_vector_routines_name(0);
        size_t  k;

        for (k = 0; k + 1 < sizeof upper && name[k] != '\0'; ++k)
            upper[k] = (char) toupper((unsigned char) name[k]);
        upper[k] = '\0';
        assert_int_equal(lame_set_vector_routines(gfp, upper), -2);
    }

    assert_int_equal(lame_set_vector_routines(NULL, "auto"), -1);
    assert_int_equal(lame_set_vector_routines(NULL, "nosuchthing"), -1);
}

/** @brief Checks that lame_get_vector_routines() returns NULL before lame_init_params(). */
static void
test_outcome_unavailable_before_init(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_null(lame_get_vector_routines(gfp));
    assert_null(lame_get_vector_routines(NULL));
}

/**
 * @brief Checks that after lame_init_params() the result is a real name,
 *        never the request word.
 *
 * "auto" is a request, not a result. If the function returned it, a caller
 * would learn nothing about the routines that run.
 */
static void
test_outcome_after_init(void **state)
{
    lame_t  gfp = (lame_t) *state;
    const char *const got = lame_get_vector_routines(gfp);
    int const n = lame_get_num_vector_routines();
    int     i, found = 0;

    assert_non_null(got);
    assert_string_not_equal(got, "auto");

    if (strncmp(got, "none", sizeof("none")) == 0)
        found = 1;
    for (i = 0; i < n; ++i) {
        if (strncmp(got, lame_get_vector_routines_name(i), VECTOR_NAME_MAX) == 0)
            found = 1;
    }
    assert_int_equal(found, 1);
}

/**
 * @brief Checks that the encoder uses the selected set. That is the purpose of
 *        the API.
 *
 * Every build can run "none", so the test always checks it. The test checks a
 * named set only where the CPU can run it. On a machine that cannot, -4 is a
 * valid result.
 */
static void
test_selection_is_honoured(LAME_UNUSED void **state)
{
    int const n = lame_get_num_vector_routines();
    int     i;

    {
        lame_t  gfp = lame_init();

        assert_non_null(gfp);
        assert_int_equal(lame_set_vector_routines(gfp, "none"), 0);
        lame_set_num_channels(gfp, 2);
        lame_set_in_samplerate(gfp, 44100);
        assert_true(lame_init_params(gfp) >= 0);
        assert_string_equal(lame_get_vector_routines(gfp), "none");
        lame_close(gfp);
    }

    for (i = 0; i < n; ++i) {
        const char *const name = lame_get_vector_routines_name(i);
        lame_t  gfp = lame_init();
        int     r;

        assert_non_null(gfp);
        r = lame_set_vector_routines(gfp, name);
        if (r == -4) {   /* this processor cannot run it; nothing to assert */
            lame_close(gfp);
            continue;
        }
        assert_int_equal(r, 0);
        lame_set_num_channels(gfp, 2);
        lame_set_in_samplerate(gfp, 44100);
        assert_true(lame_init_params(gfp) >= 0);
        assert_string_equal(lame_get_vector_routines(gfp), name);
        lame_close(gfp);
    }
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_count_and_names),
        cmocka_unit_test(test_name_bounds),
        cmocka_unit_test(test_names_are_distinct),
        cmocka_unit_test_setup_teardown(test_enumerated_names_round_trip,
                                        lame_fixture_setup, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_reserved_names, lame_fixture_setup, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_rejections, lame_fixture_setup, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_outcome_unavailable_before_init,
                                        lame_fixture_setup, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_outcome_after_init,
                                        inited_setup, lame_fixture_teardown),
        cmocka_unit_test(test_selection_is_honoured),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
