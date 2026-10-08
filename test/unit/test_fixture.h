/**
 * @file
 * @brief A new encoder instance for each cmocka test that needs one.
 *
 * Register a test with LAME_FIXTURE_TEST(), or pass lame_fixture_setup() and
 * lame_fixture_teardown() to cmocka_unit_test_setup_teardown(). The test then
 * finds its instance in @c *state. A test that needs more set up calls
 * lame_fixture_setup() from its own setup first.
 */
#ifndef LAME_TEST_FIXTURE_H
#define LAME_TEST_FIXTURE_H

#include "lame.h"

/**
 * @brief Per-test setup: stores a new encoder instance in @p state.
 * @param state  receives the instance.
 * @return 0, or -1 when lame_init() fails.
 */
static inline int
lame_fixture_setup(void **state)
{
    lame_t gfp = lame_init();
    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

/**
 * @brief Per-test teardown: closes the encoder instance in @p state.
 * @param state  the instance that lame_fixture_setup() stored.
 * @return 0.
 */
static inline int
lame_fixture_teardown(void **state)
{
    lame_close((lame_t) *state);
    return 0;
}

/** @brief Registers cmocka test @p f with an encoder instance of its own. */
#define LAME_FIXTURE_TEST(f) cmocka_unit_test_setup_teardown(f, lame_fixture_setup, lame_fixture_teardown)

#endif /* LAME_TEST_FIXTURE_H */
