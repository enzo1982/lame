/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c set_path_arg() and the option value readers in
 *        @c frontend/parse.c.
 *
 * The tests check four things:
 * - A positional input or output file name of @c PATH_MAX bytes or longer is
 *   rejected.
 * - A shorter name is copied and ends with a NUL.
 * - An option value that no option can use makes @c parse_args() fail.
 * - The four filter options take a small value as kHz and a large one as Hz.
 *
 * @c set_path_arg() is static, so the test includes the whole translation unit
 * with @c \#include. @c parse_test_stubs.c supplies the console and file
 * helpers that @c parse.c uses. libmp3lame supplies the @c lame_* API.
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

/** A fill byte that is not NUL. If a copy does not write the NUL, this byte
    stays in its place, and the test finds the fault. */
#define SENTINEL 0x7f

/**
 * @brief Checks that a file name of @c PATH_MAX bytes or longer is rejected.
 *
 * A copy of such a name with @c strncpy() and a limit of @c PATH_MAX writes
 * no NUL.
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
 * @brief Checks that a short file name is copied without change and ends with
 *        a NUL.
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
 * @brief Checks the lengths at the limit.
 *
 * A name of exactly @c PATH_MAX bytes is rejected. A name of @c PATH_MAX-1
 * bytes is accepted, and the copy ends with a NUL. So a read of the copy
 * cannot run past its end.
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
 * @brief Checks that parse_args() rejects an option value that is not a
 *        finite number or is out of range for every option. It also checks
 *        that parse_args() accepts an ordinary value.
 *
 * The values cover both readers. The double reader gets NaN, infinity and
 * huge values. Several options convert such a double to an int. The integer
 * reader gets a value outside the range of an int.
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

/**
 * @brief Checks that the four filter options take a small value as kHz and a
 *        large one as Hz.
 *
 * Each option is given once below its kHz limit and once above it, and the
 * frequency is read back from the encoder settings.
 * @param state cmocka fixture state (unused).
 */
static void
test_filter_options_take_khz(LAME_UNUSED void **state)
{
    static const struct {
        const char *opt, *val;
        int     (*get)(const lame_global_flags *);
        int     hz;
    } cases[] = {
        { "--lowpass", "0.5", lame_get_lowpassfreq, 500 },
        { "--lowpass", "60", lame_get_lowpassfreq, 60 },
        { "--lowpass-width", "0.5", lame_get_lowpasswidth, 500 },
        { "--lowpass-width", "500", lame_get_lowpasswidth, 500 },
        { "--highpass", "0.5", lame_get_highpassfreq, 500 },
        { "--highpass", "500", lame_get_highpassfreq, 500 },
        { "--highpass-width", "0.5", lame_get_highpasswidth, 500 },
        { "--highpass-width", "500", lame_get_highpasswidth, 500 },
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
        assert_int_equal(r, 0);
        if (cases[c].get(gf) != cases[c].hz)
            fail_msg("%s %s: %d Hz, wanted %d", cases[c].opt, cases[c].val, cases[c].get(gf),
                     cases[c].hz);
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
        cmocka_unit_test(test_filter_options_take_khz),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
