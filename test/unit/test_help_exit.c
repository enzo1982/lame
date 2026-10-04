/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for the exit status that @c parse_args() reports for help.
 *
 * @c parse_args() has two ways to stop before an encode. -2 means that it
 * printed what the user asked for and has nothing left to do. -1 means that
 * the command line is wrong. Both frontends that read the value act on this
 * difference. A caller sees it as the exit status of the process.
 *
 * These tests check that every argument form that asks for help returns the
 * first code. They also check that wrong command lines return the second
 * code. Without these checks, a parser that always reports success would
 * pass.
 *
 * @c presets_set() is static, so the test includes the whole translation unit
 * with @c \#include. @c parse_test_stubs.c supplies the console and file
 * helpers that @c parse.c uses. libmp3lame supplies the @c lame_* API.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"

#include "parse.c"

/**
 * @brief Names for the codes that @c parse_args() returns, so that the
 *        assertions are easy to read.
 */
#define PARSE_PRINTED_AND_DONE  (-2)
#define PARSE_REJECTED          (-1)
#define PARSE_PROCEED           0

/** @brief Creates an encoder instance, and sets the streams that the parser
 *         writes to.
 *
 * The startup code of the frontend opens the console streams. This test does
 * not link that code, so the stub leaves the streams null. A rejection path
 * passes one of them directly to the version banner, lame_version_print() in
 * parse.c. The banner does not check its argument. Without this setup, a test
 * crashes on the case that it checks and does not report a result. The stub
 * cannot initialize the streams where it declares them, because @c stderr
 * need not be a constant expression.
 */
static int
gfp_setup(void **state)
{
    lame_t gfp = lame_init();

    if (gfp == NULL)
        return -1;
    Console_IO.Console_fp = stdout;
    Console_IO.Error_fp = stderr;
    Console_IO.Report_fp = stdout;
    *state = gfp;
    return 0;
}

static int
gfp_teardown(void **state)
{
    (void) lame_close((lame_t) *state);
    return 0;
}

/**
 * @brief Runs @p argv through the option parser and returns its result.
 *
 * The output goes to standard output. These command lines are meant to write
 * there. The test harness captures the output of each program.
 */
static int
parse(lame_t gfp, int argc, char **argv)
{
    char    inPath[PATH_MAX + 1];
    char    outPath[PATH_MAX + 1];
    char    outDir[PATH_MAX + 1];

    inPath[0] = '\0';
    outPath[0] = '\0';
    return parse_args(gfp, argc, argv, inPath, outPath, outDir, NULL, NULL);
}

/**
 * @brief Checks that every way to ask for help reports success.
 *
 * The test checks all forms together, because they must agree. A test of only
 * some forms does not notice when another form changes its result.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_help_requests_report_success(void **state)
{
    lame_t  gfp = (lame_t) *state;
    static char *const forms[][3] = {
        { "lame", "--help", NULL },
        { "lame", "--longhelp", NULL },
        { "lame", "--version", NULL },
        { "lame", "--license", NULL },
        { "lame", "-?", NULL },
        { "lame", "--preset", "help" }
    };
    size_t  i;

    for (i = 0; i < sizeof forms / sizeof forms[0]; ++i) {
        int const argc = forms[i][2] != NULL ? 3 : 2;
        int const ret = parse(gfp, argc, (char **) forms[i]);

        if (ret != PARSE_PRINTED_AND_DONE)
            fail_msg("\"%s%s%s\" reported %d, not %d",
                     forms[i][1],
                     forms[i][2] != NULL ? " " : "",
                     forms[i][2] != NULL ? forms[i][2] : "",
                     ret, PARSE_PRINTED_AND_DONE);
    }
}

/**
 * @brief Checks that a preset that does not exist is an error.
 *
 * This is the control for the test above. The help request and the unknown
 * preset go through the same function, presets_set(). A change that reports
 * success for both passes the test above. This test catches such a change.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_unknown_preset_is_rejected(void **state)
{
    lame_t  gfp = (lame_t) *state;
    char   *argv[3];

    argv[0] = "lame";
    argv[1] = "--preset";
    argv[2] = "nosuchpreset";
    assert_int_equal(parse(gfp, 3, argv), PARSE_REJECTED);
}

/**
 * @brief Checks that an unrecognized option is an error.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_unknown_option_is_rejected(void **state)
{
    lame_t  gfp = (lame_t) *state;
    char   *argv[2];

    argv[0] = "lame";
    argv[1] = "--no-such-option";
    assert_int_equal(parse(gfp, 2, argv), PARSE_REJECTED);
}

/**
 * @brief Checks that an empty argument list, without a program name, is an
 *        error.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_empty_argument_list_is_rejected(void **state)
{
    lame_t  gfp = (lame_t) *state;
    char   *argv[1];

    argv[0] = NULL;
    assert_int_equal(parse(gfp, 0, argv), PARSE_REJECTED);
}

/**
 * @brief Checks that an ordinary command line returns the code to proceed.
 *
 * The parser only stores the file names and opens no file. So this test shows
 * that neither of the codes above appears on the encoding path.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_ordinary_invocation_proceeds(void **state)
{
    lame_t  gfp = (lame_t) *state;
    char   *argv[4];

    argv[0] = "lame";
    argv[1] = "-V5";
    argv[2] = "in.wav";
    argv[3] = "out.mp3";
    assert_int_equal(parse(gfp, 4, argv), PARSE_PROCEED);
}

/** @brief Registers and runs the help-exit-status group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_help_requests_report_success,
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_unknown_preset_is_rejected,
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_unknown_option_is_rejected,
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_empty_argument_list_is_rejected,
                                        gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_ordinary_invocation_proceeds,
                                        gfp_setup, gfp_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
