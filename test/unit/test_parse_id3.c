/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for the ID3 dispatch of the frontend in frontend/parse.c
 *        (SF #524).
 *
 * The tests call the static id3_tag() entry point. It converts the argument
 * and passes it to the handler for its encoding: set_id3v2tag_utf8() or
 * set_id3v2tag_utf16(). The UTF-8 handler must call the UTF-8 setters, not the
 * UTF-16 or UCS-2 setters (SF #524). The tests check that UTF-8 text, comments
 * and field values give the right frame. They also check that the UTF-16 path
 * works.
 *
 * id3_tag() is static, so the test includes the translation unit with
 * @c \#include. parse_test_stubs.c supplies the frontend externs. libmp3lame
 * supplies the id3tag_* and lame_get_id3v2_tag API. id3_tag() converts the
 * text with iconv. So the inputs are ASCII, which converts the same way in
 * every locale.
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

/** @brief Returns 1 if the byte string @p needle occurs unchanged in @p hay, 0 otherwise. */
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

/** @brief Checks that a UTF-8 text tag goes to the correct frame (the #524 path). */
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

/** @brief Checks that a UTF-8 comment goes to a COMM frame. */
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

/** @brief Checks that a UTF-8 field value goes through id3tag_set_fieldvalue_utf8 (#524). */
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

/** @brief Checks that the UTF-16 path works (regression guard). */
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

/** @brief Per-test setup: creates a new encoder instance and stores it in @p state. */
static int
setup_lame(void **state)
{
    lame_t gfp = lame_init();
    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

/** @brief Per-test teardown: closes the encoder instance in @p state. */
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
