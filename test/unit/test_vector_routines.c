/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the vector-routines API.
 *
 * Unlike the tests for the routines themselves, nothing here is gated on an
 * architecture: that is the point of the API. It reports what this build
 * carries, whatever that is, so the same assertions hold on a build with two
 * sets, one set, or none - and a build with none is a real configuration, not
 * a degenerate one.
 *
 * The assertions are therefore written against the *contract* rather than
 * against a known list of names. A test that expected "sse2" at index 0 would
 * fail on ARM for no reason, and would say nothing about whether the contract
 * holds.
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

#include "lame.h"
#include "test_unused.h"

/*
 * The test's own bound on a name, deliberately not taken from the library:
 * these are library-level tests, so they see only the public header. Every
 * name the API reports is asserted to fit, which is what makes the bounded
 * comparisons below exact rather than merely safe.
 */
#define VECTOR_NAME_MAX 32

/** @brief A fresh encoder context per test. */
static int
gfp_setup(void **state)
{
    lame_t  gfp = lame_init();

    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

static int
gfp_teardown(void **state)
{
    lame_close((lame_t) *state);
    return 0;
}

/** @brief An instance taken all the way through lame_init_params(). */
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
 * @brief The count is never negative, and the names it promises are all there.
 *
 * The function takes no arguments and no instance, so it has nothing to fail
 * on; the contract says so explicitly and this pins it. Zero is a legitimate
 * answer, so the test must not require any sets to exist.
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

/** @brief Out of range is the only reason the name lookup returns NULL. */
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

/** @brief No two sets share a name, or an index would be ambiguous. */
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
 * @brief Every enumerated name is one the setter knows.
 *
 * This is the guard on the two lists in util.c drifting apart: the table of
 * sets this build compiled, and the list of names the project knows at all.
 * If a set were added to the first and not the second, its own name would come
 * back "unknown" (-2) - the enumeration would be advertising something the
 * setter rejects. -4 is allowed, because a processor that cannot run a set is
 * a fact about the machine, not about the name.
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

/** @brief The two reserved names are always accepted, even with no sets. */
static void
test_reserved_names(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_int_equal(lame_set_vector_routines(gfp, "auto"), 0);
    assert_int_equal(lame_set_vector_routines(gfp, "none"), 0);
}

/**
 * @brief The rejections, each of which tells the caller a different thing.
 *
 * -2 is a typo, -3 asks for a rebuild, -4 asks for another machine, -1 is a
 * broken call. Collapsing any two of them would leave a user guessing.
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

/** @brief Before lame_init_params() there is no outcome to report. */
static void
test_outcome_unavailable_before_init(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_null(lame_get_vector_routines(gfp));
    assert_null(lame_get_vector_routines(NULL));
}

/**
 * @brief After init the outcome is a real name, and never the request word.
 *
 * "auto" is a request, not an answer; reporting it back would tell a caller
 * nothing about what is running.
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
 * @brief A selection is honoured, which is the whole purpose of the API.
 *
 * "none" is the case every build can run, so it is asserted unconditionally;
 * a named set is asserted only where the processor can execute it, since -4 is
 * a legitimate answer on a machine that cannot.
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
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_reserved_names, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_rejections, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_outcome_unavailable_before_init,
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_outcome_after_init,
                                        inited_setup, gfp_teardown),
        cmocka_unit_test(test_selection_is_honoured),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
