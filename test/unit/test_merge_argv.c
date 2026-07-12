/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c merge_argv() in @c frontend/parse.c.
 *
 * @c parse_args() allocates the merged argument vector large enough to hold
 * every token (LAMEOPT plus the real @c argv). These tests cover both that
 * property - when the array is sized to fit, @c merge_argv() reports the full
 * count and fills every slot - and the defensive clamp that keeps
 * @c merge_argv() from ever reporting more entries than the array it was
 * handed can hold.
 *
 * @c merge_argv() is static, so the whole translation unit is pulled in with
 * @c \#include; @c parse_test_stubs.c supplies the frontend externs and
 * libmp3lame provides the @c lame_* API.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

#include <cmocka.h>

#include "parse.c"

/**
 * @brief Sized to fit: every argument survives the merge.
 *
 * The scenario @c parse_args() guarantees by allocating @c str_argv to hold
 * all tokens - @c merge_argv() must return the full count and fill every slot
 * in range (poisoned to NULL beforehand, so a gap would be detected).
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_sized_to_fit(void **state)
{
    enum { ARGC = 600 };
    static char *big[ARGC];
    char        *dst[ARGC + 1];        /* argc entries + the argv[0] slot */
    int          i, ret;
    (void) state;

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
 * @brief Defensive clamp: an undersized array never yields an over-count.
 *
 * If the destination is smaller than the token total, @c merge_argv() must
 * report exactly the array size (never the inflated raw total), and every slot
 * within that count must be initialised.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_clamped_to_bound(void **state)
{
    enum { N = 512 };
    static char *big[600];
    char        *dst[N];
    int          i, ret;
    (void) state;

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
 * @brief The ordinary (non-overflowing) case is untouched by the clamp.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_no_overflow_unchanged(void **state)
{
    char *av[3] = { "lame", "a", "b" };
    char *dst[512];
    int   ret;
    (void) state;

    ret = merge_argv(3, av, 0, dst, 512);
    assert_int_equal(ret, 3);
}

/**
 * @brief Boundary: a total of exactly N stays N; one over is clamped to N.
 * @param state cmocka fixture state (unused).
 */
static void
test_merge_boundary(void **state)
{
    enum { N = 512 };
    static char *big[513];
    char        *dst[N];
    int          i, ret;
    (void) state;

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
