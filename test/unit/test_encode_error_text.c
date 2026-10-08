/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c frontend_encode_error_text() in
 *        @c frontend/parse.c.
 *
 * The three frontend programs print this text when an encode or flush call
 * fails. The tests check that every code that the encode calls document has
 * a text of its own, and that an unknown code still gets one.
 *
 * The test includes the whole translation unit with @c \#include, as the
 * other tests of @c parse.c do. @c parse_test_stubs.c supplies the console and
 * file helpers that @c parse.c uses. libmp3lame supplies the @c lame_* API.
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

/** @brief The negative results that lame_encode_buffer() documents. */
static const int encode_errors[] = { -1, -2, -3, -4, -6, LAME_BADINPUTDATA, LAME_INTERNALERROR };

/**
 * @brief Checks that each documented code has a text of its own, without a
 *        line break, and different from the text of an unknown code.
 * @param state cmocka fixture state (unused).
 */
static void
test_each_code_has_its_text(LAME_UNUSED void **state)
{
    const char *const unknown = frontend_encode_error_text(-99);
    size_t  i, j;

    assert_non_null(unknown);
    assert_true(unknown[0] != '\0');
    for (i = 0; i < sizeof encode_errors / sizeof encode_errors[0]; ++i) {
        const char *const text = frontend_encode_error_text(encode_errors[i]);

        assert_non_null(text);
        assert_true(text[0] != '\0');
        assert_null(strchr(text, '\n'));
        if (strcmp(text, unknown) == 0)
            fail_msg("code %d has the text of an unknown code", encode_errors[i]);
        for (j = 0; j < i; ++j) {
            if (strcmp(text, frontend_encode_error_text(encode_errors[j])) == 0)
                fail_msg("codes %d and %d have the same text", encode_errors[i], encode_errors[j]);
        }
    }
}

/**
 * @brief Checks that the text for #LAME_BADINPUTDATA says what is wrong with
 *        the input.
 * @param state cmocka fixture state (unused).
 */
static void
test_bad_input_text(LAME_UNUSED void **state)
{
    assert_non_null(strstr(frontend_encode_error_text(LAME_BADINPUTDATA), "not a number"));
}

/** @brief Registers and runs the encode error text tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_each_code_has_its_text),
        cmocka_unit_test(test_bad_input_text),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
