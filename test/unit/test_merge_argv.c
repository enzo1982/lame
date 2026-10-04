/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c merge_argv() in @c frontend/parse.c.
 *
 * @c parse_args() allocates the merged argument vector with room for every
 * token: the tokens from LAMEOPT and the real @c argv. The tests check two
 * things:
 * - When the array is large enough, @c merge_argv() reports the full count
 *   and fills every slot.
 * - A defensive clamp stops @c merge_argv() from reporting more entries than
 *   the array has slots.
 *
 * @c merge_argv() is static, so the test includes the whole translation unit
 * with @c \#include. @c parse_test_stubs.c supplies the frontend externs.
 * libmp3lame supplies the @c lame_* API.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

#include <cmocka.h>

#include "test_unused.h"

#include "parse.c"

/**
 * @brief Checks that every argument is kept when the array is large enough.
 *
 * @c parse_args() makes this case certain, because it allocates @c str_argv
 * with room for all tokens. @c merge_argv() must return the full count and
 * fill every slot in range. The test sets all slots to NULL before the call,
 * so it finds a slot that the merge leaves empty.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_sized_to_fit(LAME_UNUSED void **state)
{
    enum { ARGC = 600 };
    static char *big[ARGC];
    char        *dst[ARGC + 1];        /* argc entries + the argv[0] slot */
    int          i, ret;

    for (i = 0; i < ARGC; ++i)
        big[i] = "x";
    for (i = 0; i <= ARGC; ++i)
        dst[i] = NULL;                 /* poison: a dropped arg stays NULL */

    ret = merge_argv(ARGC, big, 0, dst, ARGC + 1);

    assert_int_equal(ret, ARGC);       /* nothing dropped */
    for (i = 0; i < ret; ++i)
        assert_non_null(dst[i]);
}

/**
 * @brief Checks that a small array never gives a count that is too large.
 *
 * If the destination has fewer slots than there are tokens, @c merge_argv()
 * must report exactly the array size, not the total number of tokens. Every
 * slot within that count must be set.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_clamped_to_bound(LAME_UNUSED void **state)
{
    enum { N = 512 };
    static char *big[600];
    char        *dst[N];
    int          i, ret;

    for (i = 0; i < 600; ++i)
        big[i] = "x";
    for (i = 0; i < N; ++i)
        dst[i] = NULL;

    ret = merge_argv(600, big, 0, dst, N);

    assert_int_equal(ret, N);          /* clamped, not 600 */
    for (i = 0; i < ret; ++i)
        assert_non_null(dst[i]);
}

/**
 * @brief Checks that the clamp does not change the ordinary case, where all
 *        tokens fit.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_no_overflow_unchanged(LAME_UNUSED void **state)
{
    char *av[3] = { "lame", "a", "b" };
    char *dst[512];
    int   ret;

    ret = merge_argv(3, av, 0, dst, 512);
    assert_int_equal(ret, 3);
}

/**
 * @brief Checks the limit. A total of exactly N stays N. A total of N+1 is
 *        clamped to N.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_boundary(LAME_UNUSED void **state)
{
    enum { N = 512 };
    static char *big[513];
    char        *dst[N];
    int          i, ret;

    for (i = 0; i < 513; ++i)
        big[i] = "x";

    /* argc 512, str_argc 0 -> str_argc becomes 1, total = 512 == N */
    ret = merge_argv(512, big, 0, dst, N);
    assert_int_equal(ret, N);

    /* argc 513 -> total = 513 > N -> clamped to N */
    ret = merge_argv(513, big, 0, dst, N);
    assert_int_equal(ret, N);
}

/** @brief Registers and runs the merge_argv() test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_merge_sized_to_fit),
        cmocka_unit_test(test_merge_clamped_to_bound),
        cmocka_unit_test(test_merge_no_overflow_unchanged),
        cmocka_unit_test(test_merge_boundary),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
