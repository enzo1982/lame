/**
 * @file
 * @ingroup unit_tests
 * @brief CMocka harness smoke test.
 *
 * Checks that the unit-test harness is set up correctly:
 * - @c --enable-unit-tests finds CMocka.
 * - A test binary compiles and links against CMocka.
 * - @c "make check" runs the binary and reports whether it passes or fails.
 *
 * The test uses only the CMocka macros from before version 2.0. This is a
 * temporary limit. Many Linux distributions do not ship cmocka 2.0 yet.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "test_unused.h"

/** @brief Returns the sum of @p a and @p b. test_harness_runs() calls it. */
static int
add(int a, int b)
{
    return a + b;
}

/**
 * @brief Checks a result that is always true.
 *
 * This gives the harness one test to run.
 * @param state cmocka fixture state (unused).
 */
static void
test_harness_runs(LAME_UNUSED void **state)
{
    assert_int_equal(add(2, 2), 4);
}

/** @brief Registers the smoke test and runs it. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_harness_runs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
