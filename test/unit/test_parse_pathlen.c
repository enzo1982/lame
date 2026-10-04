/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c set_path_arg() and the option value readers in
 *        @c frontend/parse.c.
 *
 * Verifies that a positional input/output filename of @c PATH_MAX bytes or
 * longer is rejected, and that a shorter one is copied and null-terminated;
 * and that an option value no option can use makes @c parse_args() fail.
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

#include "test_unused.h"

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
test_overlong_path_rejected(LAME_UNUSED void **state)
{
    char    dst[PATH_MAX + 1];
    size_t  n = PATH_MAX + 64;
    char   *src = malloc(n + 1);
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
test_fitting_path_terminated(LAME_UNUSED void **state)
{
    char        dst[PATH_MAX + 1];
    const char *src = "path/to/input.wav";
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
test_boundary_length(LAME_UNUSED void **state)
{
    char    dst[PATH_MAX + 1];
    char   *src = malloc(PATH_MAX + 1);
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

/**
 * @brief parse_args() refuses an option value that is not a finite number or
 *        lies beyond what any option can mean, and accepts an ordinary one.
 *
 * The values cover both readers: a double that is NaN, infinite or huge, which
 * several options convert to an int, and an integer beyond the range of one.
 * @param state cmocka fixture state (unused).
 */
static void
test_unusable_numbers_refused(LAME_UNUSED void **state)
{
    static const struct {
        const char *opt, *val;
        int     ret;
    } cases[] = {
        { "-s", "nan", -1 },
        { "-s", "1e10", -1 },
        { "-s", "-44444444", -1 },      /* scaled by 1000 before the conversion */
        { "--scale", "inf", -1 },
        { "--resample", "-1e300", -1 },
        { "--lowpass", "nan", -1 },
        { "-b", "4294967424", -1 },
        { "-s", "44.1", 0 },
    };
    static char in_path[PATH_MAX + 1], out_path[PATH_MAX + 1], out_dir[PATH_MAX + 1];
    size_t  c;
    for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        char    prog[] = "lame", in[] = "in.wav", out[] = "out.mp3";
        char    opt[32], val[32];
        char   *argv[6];
        lame_t  gf = lame_init();
        int     r;
        assert_non_null(gf);
        snprintf(opt, sizeof opt, "%s", cases[c].opt);
        snprintf(val, sizeof val, "%s", cases[c].val);
        argv[0] = prog; argv[1] = opt; argv[2] = val; argv[3] = in; argv[4] = out; argv[5] = NULL;
        r = parse_args(gf, 5, argv, in_path, out_path, out_dir, NULL, NULL);
        if (r != cases[c].ret)
            fail_msg("%s %s: parse_args() answered %d", cases[c].opt, cases[c].val, r);
        lame_close(gf);
    }
}

/** @brief Registers and runs the set_path_arg() and option value test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_overlong_path_rejected),
        cmocka_unit_test(test_fitting_path_terminated),
        cmocka_unit_test(test_boundary_length),
        cmocka_unit_test(test_unusable_numbers_refused),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
