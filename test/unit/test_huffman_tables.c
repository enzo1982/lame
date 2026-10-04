/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for mapping a chosen Huffman table to a table that can be
 *        written (libmp3lame/tables.c).
 *
 * The table search compares candidate tables by their code lengths. It can
 * return table 14. Table 14 has code lengths but no code words, because the
 * standard defines none for it. So any code that writes a chosen table as bits
 * must call resolve_huffman_table() first. This file checks that the result
 * is always a table with code words.
 *
 * The checks read the tables themselves. They do not use a list of the values
 * that the search can return. The search can cost an entry exactly when the
 * entry has code lengths, so it can choose only such an entry. So the test
 * needs no copy of the encoder's candidate tables, and it cannot get out of
 * date when those tables change.
 *
 * The test calls an internal symbol, so it links the static archive.
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
 * @brief Checks that every table the search can choose maps to a table with
 *        code words.
 *
 * The search cannot cost an entry without code lengths. So the search never
 * chooses such an entry, and the loop skips it. After the loop, the test
 * checks how many tables it examined. Without this count, a loop that skips
 * every table passes.
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
 * @brief Checks that table 14, the only table without code words, maps to
 *        table 16.
 *
 * The standard says that a decoder reads table 16 in place of table 14. So
 * the encoder writes 16 in the side information. It also writes the data
 * with the code words of table 16. One function returns the value for both
 * uses, so the two cannot disagree.
 */
static void
test_the_table_without_codewords_maps_to_sixteen(LAME_UNUSED void **state)
{
    assert_null(ht[14].table);
    assert_non_null(ht[14].hlen);

    assert_int_equal(resolve_huffman_table(14), 16);
}

/**
 * @brief Checks that every other table maps to itself.
 *
 * This includes the two count1 tables at the end of the array.
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
