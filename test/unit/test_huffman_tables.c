/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for resolving a chosen Huffman table onto a writable one
 *        (libmp3lame/tables.c).
 *
 * The table search compares candidates by their code lengths, and one of the
 * candidates it can return - table 14 - has code lengths but no code words,
 * because the standard defines none for it. Anything that turns a chosen table
 * into bits therefore has to resolve it first, and the property this file
 * checks is that resolving always lands somewhere writable.
 *
 * The check is expressed against the tables themselves rather than against a
 * list of the values the search can produce: an entry with code lengths is
 * exactly an entry the search can cost and therefore choose, so "every table
 * that can be chosen can be written" needs no copy of the encoder's own
 * candidate tables here, and cannot go stale against them.
 *
 * These reach an internal symbol, so the test links the static archive.
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

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "tables.h"

/**
 * @brief Every table the search can choose resolves to one that has code words.
 *
 * An entry without code lengths cannot be costed, so it is not a possible
 * outcome and is skipped - the count is asserted afterwards so that a version
 * of this loop which skipped everything could not report success.
 */
static void
test_every_choosable_table_can_be_written(LAME_UNUSED void **state)
{
    unsigned int i;
    int     examined = 0;

    for (i = 0; i < HTN; i++) {
        if (ht[i].hlen == NULL) {
            continue;
        }
        assert_non_null(ht[resolve_huffman_table(i)].table);
        examined++;
    }
    assert_true(examined >= 30);
}

/**
 * @brief The one table without code words of its own is encoded as table 16.
 *
 * 16 is what the standard says a decoder reads in its place, so this is the
 * value the side information carries and the codebook the data is written
 * with - one answer for both, which is the point of asking in one place.
 */
static void
test_the_table_without_codewords_maps_to_sixteen(LAME_UNUSED void **state)
{
    assert_null(ht[14].table);
    assert_non_null(ht[14].hlen);

    assert_int_equal(resolve_huffman_table(14), 16);
}

/**
 * @brief Nothing else is moved.
 *
 * Resolving is not a place to be clever: every other table, including the two
 * count1 tables at the end of the array, is its own answer.
 */
static void
test_every_other_table_resolves_to_itself(LAME_UNUSED void **state)
{
    unsigned int i;

    for (i = 0; i < HTN; i++) {
        if (i == 14) {
            continue;
        }
        assert_int_equal(resolve_huffman_table(i), i);
    }
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_every_choosable_table_can_be_written),
        cmocka_unit_test(test_the_table_without_codewords_maps_to_sixteen),
        cmocka_unit_test(test_every_other_table_resolves_to_itself),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
