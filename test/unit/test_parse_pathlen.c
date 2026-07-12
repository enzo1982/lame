/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c set_path_arg() in @c frontend/parse.c.
 *
 * Verifies that a positional input/output filename of @c PATH_MAX bytes or
 * longer is rejected, and that a shorter one is copied and null-terminated.
 *
 * @c set_path_arg() is static, so the whole translation unit is pulled in with
 * @c \#include; @c parse_test_stubs.c supplies the console/file helpers
 * @c parse.c references and libmp3lame provides the @c lame_* API.
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

#include "parse.c"

/** Non-NUL fill byte: a copy that fails to terminate leaves this in place of
    the expected '\0', making the fault detectable. */
#define SENTINEL 0x7f

/**
 * @brief A filename of @c PATH_MAX bytes or longer must be rejected.
 *
 * Were it copied instead, the destination would be left unterminated.
 * @param state cmocka fixture state (unused).
 */
static void
test_overlong_path_rejected(void **state)
{
    char    dst[PATH_MAX + 1];
    size_t  n = PATH_MAX + 64;
    char   *src = malloc(n + 1);
    (void) state;
    assert_non_null(src);
    memset(src, 'a', n);
    src[n] = '\0';
    memset(dst, SENTINEL, sizeof dst);

    assert_int_equal(set_path_arg(src, dst), -1);

    free(src);
}

/**
 * @brief A short filename must be copied verbatim and null-terminated.
 * @param state cmocka fixture state (unused).
 */
static void
test_fitting_path_terminated(void **state)
{
    char        dst[PATH_MAX + 1];
    const char *src = "path/to/input.wav";
    (void) state;
    memset(dst, SENTINEL, sizeof dst);

    assert_int_equal(set_path_arg(src, dst), 0);
    assert_string_equal(dst, "path/to/input.wav");
}

/**
 * @brief Boundary check around the limit.
 *
 * A name of exactly @c PATH_MAX bytes is rejected; @c PATH_MAX-1 is accepted
 * and the result is properly terminated (no walk-off possible).
 * @param state cmocka fixture state (unused).
 */
static void
test_boundary_length(void **state)
{
    char    dst[PATH_MAX + 1];
    char   *src = malloc(PATH_MAX + 1);
    (void) state;
    assert_non_null(src);

    memset(src, 'b', PATH_MAX);
    src[PATH_MAX] = '\0';                 /* length == PATH_MAX  -> reject */
    assert_int_equal(set_path_arg(src, dst), -1);

    src[PATH_MAX - 1] = '\0';             /* length == PATH_MAX-1 -> accept */
    memset(dst, SENTINEL, sizeof dst);
    assert_int_equal(set_path_arg(src, dst), 0);
    assert_int_equal((int) strlen(dst), PATH_MAX - 1);
    assert_int_equal(dst[PATH_MAX - 1], '\0');

    free(src);
}

/** @brief Registers and runs the set_path_arg() test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_overlong_path_rejected),
        cmocka_unit_test(test_fitting_path_terminated),
        cmocka_unit_test(test_boundary_length),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
