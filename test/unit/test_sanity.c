/*
 * Minimal CMocka smoke test.
 *
 * Its only purpose is to prove the unit-test harness is wired correctly:
 * that --enable-unit-tests locates CMocka, that a test binary compiles and
 * links against it, and that "make check" runs the result and reports its
 * pass/fail status.
 *
 * Only the pre-2.0 CMocka macro set is used, a temporary restriction until
 * cmocka 2.0 is more widely available across Linux distributions.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

static int
add(int a, int b)
{
    return a + b;
}

static void
test_harness_runs(void **state)
{
    (void) state;
    assert_int_equal(add(2, 2), 4);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_harness_runs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
