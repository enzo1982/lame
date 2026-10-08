/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for what the ID3 tag setters return when memory runs out
 *        (libmp3lame/id3tag.c).
 *
 * The test compiles id3tag.c in, with a calloc() that fails when the test
 * asks it to. The rest of the library comes from libmp3lame as usual.
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

/** Set to 1 to make the next calloc() of id3tag.c fail. */
static int fail_next_calloc = 0;

/**
 * @brief The calloc() that id3tag.c calls in this test.
 * @param count the number of elements.
 * @param size the size of one element.
 * @return NULL once after the test sets @c fail_next_calloc, else what
 *         calloc() returns.
 */
static void *
failing_calloc(size_t count, size_t size)
{
    if (fail_next_calloc) {
        fail_next_calloc = 0;
        return NULL;
    }
    return calloc(count, size);
}

#define calloc(count, size) failing_calloc(count, size)
#include "id3tag.c"
#undef calloc

/**
 * @brief Checks that a UTF-16 genre fails with -254 when its Latin-1 copy
 *        cannot be allocated.
 *
 * The genre "Rock" in UTF-16 with a byte order mark is Latin-1 text, so the
 * setter copies it to Latin-1 to look it up in the genre list.
 * @param state cmocka fixture state (unused).
 */
static void
test_utf16_genre_reports_a_failed_allocation(LAME_UNUSED void **state)
{
    static const unsigned short rock[] = { 0xfeff, 'R', 'o', 'c', 'k', 0 };
    lame_t  gfp = lame_init();

    assert_non_null(gfp);
    id3tag_init(gfp);
    fail_next_calloc = 1;
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "TCON", rock), -254);
    assert_int_equal(fail_next_calloc, 0);
    /* with memory, the same call succeeds */
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "TCON", rock), 0);
    (void) lame_close(gfp);
}

/** @brief Registers the allocation test and runs it. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_utf16_genre_reports_a_failed_allocation),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
