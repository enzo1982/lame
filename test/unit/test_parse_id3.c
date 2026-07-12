/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for the frontend ID3 dispatch in frontend/parse.c (SF #524).
 *
 * Drives the static id3_tag() entry point, which converts the argument and
 * routes it to the encoding-specific set_id3v2tag_utf8() / set_id3v2tag_utf16()
 * handlers. The UTF-8 path is where SF #524 lived (it used to feed UTF-8 data
 * to the UTF-16/UCS-2 setters); these tests assert the UTF-8 text, comment and
 * field-value cases produce the right frame, and that the UTF-16 path still
 * works after the split.
 *
 * id3_tag() is static, so the translation unit is pulled in with @c \#include;
 * parse_test_stubs.c supplies the frontend externs and libmp3lame provides the
 * id3tag_* / lame_get_id3v2_tag API. id3_tag() converts via iconv, so inputs
 * are kept ASCII (encoding-transparent) to stay locale-independent.
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

#include "parse.c"

static unsigned char tagbuf[8192];

/** @brief True if the byte string @p needle occurs verbatim in @p hay. */
static int
mem_contains(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t nn = strlen(needle);
    size_t i;
    if (nn == 0 || nn > hn)
        return 0;
    for (i = 0; i + nn <= hn; ++i) {
        if (memcmp(hay + i, needle, nn) == 0)
            return 1;
    }
    return 0;
}

/** @brief A UTF-8 text tag routes to the correct frame (the #524 path). */
static void
test_utf8_text_frame(void **state)
{
    lame_t gfp = (lame_t) *state;
    char   arg[] = "DispArtist";
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3_tag(gfp, 'a', TENC_UTF8, arg), 0);
    sz = lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
    assert_true(mem_contains(tagbuf, sz, "TPE1"));
    assert_true(mem_contains(tagbuf, sz, "DispArtist"));
}

/** @brief A UTF-8 comment routes to a COMM frame. */
static void
test_utf8_comment_frame(void **state)
{
    lame_t gfp = (lame_t) *state;
    char   arg[] = "DispComment";
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3_tag(gfp, 'c', TENC_UTF8, arg), 0);
    sz = lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
    assert_true(mem_contains(tagbuf, sz, "COMM"));
    assert_true(mem_contains(tagbuf, sz, "DispComment"));
}

/** @brief A UTF-8 field value routes through id3tag_set_fieldvalue_utf8 (#524). */
static void
test_utf8_fieldvalue(void **state)
{
    lame_t gfp = (lame_t) *state;
    char   arg[] = "TIT2=DispField";
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3_tag(gfp, 'v', TENC_UTF8, arg), 0);
    sz = lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
    assert_true(mem_contains(tagbuf, sz, "DispField"));
}

/** @brief The UTF-16 path still works after the split (regression guard). */
static void
test_utf16_still_works(void **state)
{
    lame_t gfp = (lame_t) *state;
    char   arg[] = "DispTitle";
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3_tag(gfp, 't', TENC_UTF16, arg), 0);
    sz = lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
}

/** @brief Per-test fixture: fresh lame_t into @p state. */
static int
setup_lame(void **state)
{
    lame_t gfp = lame_init();
    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

/** @brief Per-test fixture teardown: closes the lame_t. */
static int
teardown_lame(void **state)
{
    lame_close((lame_t) *state);
    return 0;
}

#define ID3_TEST(f) cmocka_unit_test_setup_teardown(f, setup_lame, teardown_lame)

/** @brief Registers and runs the frontend ID3-dispatch test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        ID3_TEST(test_utf8_text_frame),
        ID3_TEST(test_utf8_comment_frame),
        ID3_TEST(test_utf8_fieldvalue),
        ID3_TEST(test_utf16_still_works),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
