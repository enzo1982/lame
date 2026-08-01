/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for the exit status @c parse_args() reports for help.
 *
 * @c parse_args() distinguishes two ways of not proceeding to an encode: -2
 * means it printed what was asked for and there is nothing left to do, and -1
 * means the command line was wrong. Both frontends that read the value act on
 * that difference, and it is what a caller sees as the process exit status.
 *
 * These cases pin every argument form that asks for help to the first of the
 * two, and one wrong command line to the second - without which "always
 * report success" would pass.
 *
 * @c presets_set() is static, so the whole translation unit is pulled in with
 * @c \#include; @c parse_test_stubs.c supplies the console/file helpers
 * @c parse.c references and libmp3lame provides the @c lame_* API.
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

/** @brief Codes @c parse_args() returns, named so the assertions read. */
#define PARSE_PRINTED_AND_DONE  (-2)
#define PARSE_REJECTED          (-1)
#define PARSE_PROCEED           0

/** @brief An encoder instance, and somewhere for the parser to write.
 *
 * The console streams are opened by the frontend's own startup, which is not
 * linked here, so the stub leaves them null. The rejection paths hand one of
 * them straight to the library's version banner, which does not test its
 * argument - so a test that skips this crashes on the case it is checking
 * rather than reporting it. They cannot be initialised where the stub declares
 * them: @c stderr need not be a constant expression.
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
 * @brief Run @p argv through the option parser and report what it decided.
 *
 * The output goes to stdout, which is where these invocations are supposed to
 * write it; the test harness captures it per program.
 */
static int
parse(lame_t gfp, int argc, char **argv)
{
    char    inPath[PATH_MAX + 1];
    char    outPath[PATH_MAX + 1];

    inPath[0] = '\0';
    outPath[0] = '\0';
    return parse_args(gfp, argc, argv, inPath, outPath, NULL, NULL);
}

/**
 * @brief Every way of asking for help reports success.
 *
 * The three that already did are here with the two that did not, because the
 * property being pinned is that they agree - checking only the two that
 * changed would not notice the others drifting the other way.
 * @param state fixture state holding an initialised @c lame_t.
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
 * @brief A preset that does not exist is still an error.
 *
 * The control for the case above: the help request and the unusable argument
 * come out of the same call, so a change that reported success for both would
 * satisfy it.
 * @param state fixture state holding an initialised @c lame_t.
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
 * @brief An unrecognised option is still an error.
 * @param state fixture state holding an initialised @c lame_t.
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
 * @brief An ordinary command line still asks to proceed.
 *
 * Nothing is opened here - the parser only records the names - so this says
 * that neither of the codes above has leaked onto the encoding path.
 * @param state fixture state holding an initialised @c lame_t.
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
        cmocka_unit_test_setup_teardown(test_ordinary_invocation_proceeds,
                                        gfp_setup, gfp_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
