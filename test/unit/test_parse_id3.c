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
 * text with iconv. Most inputs are ASCII, which converts the same way in
 * every locale. The tests of a character outside ASCII give it as UTF-8, as
 * the command line has it on Windows and in a UTF-8 locale. They check the
 * title frame in each tag encoding.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <cmocka.h>

#include "test_mem.h"

#include "test_fixture.h"

#include "parse.c"

static unsigned char tagbuf[8192];

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

/** @brief The size of an ID3v2 frame header, in bytes. */
#define FRAME_HEADER_BYTES 10

/**
 * @brief Prepares the conditions under which the frontend's text is UTF-8,
 *        or skips the calling test.
 *
 * On Windows the text is UTF-8 in every case. @c LANG is set to @c C, which
 * names no character set, so a result that depends on @c LANG shows. On other
 * systems the character set is the locale's: a UTF-8 locale is selected. The
 * test skips where none is installed, or where this build converts no text.
 */
static void
utf8_text_or_skip(void)
{
#ifndef ID3TAGS_EXTENDED
    print_message("this build stores the text as given: no conversion to test\n");
    skip();
#elif defined(_WIN32)
    static char lang_c[] = "LANG=C";
    assert_int_equal(putenv(lang_c), 0);
#else
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL && setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
        print_message("no UTF-8 locale is installed\n");
        skip();
    }
#endif
}

/**
 * @brief Checks that the tag holds the frame @p id with exactly @p body.
 *
 * The frames here are short, so the size field reads the same in ID3v2.3 and
 * in the synchsafe form of ID3v2.4.
 *
 * @param tag   the ID3v2 tag.
 * @param sz    the size of @p tag, in bytes.
 * @param id    the frame ID, four characters.
 * @param body  the expected frame body: the encoding byte, then the text.
 * @param n     the size of @p body, in bytes.
 */
static void
assert_frame_body(const unsigned char *tag, size_t sz, const char *id,
                  const unsigned char *body, size_t n)
{
    size_t  i;

    for (i = 0; i + FRAME_HEADER_BYTES <= sz; ++i) {
        if (memcmp(tag + i, id, 4) == 0)
            break;
    }
    assert_true(i + FRAME_HEADER_BYTES <= sz);
    assert_int_equal(tag[i + 4], 0);
    assert_int_equal(tag[i + 5], 0);
    assert_int_equal(tag[i + 6], 0);
    assert_int_equal(tag[i + 7], n);
    assert_true(i + FRAME_HEADER_BYTES + n <= sz);
    assert_memory_equal(tag + i + FRAME_HEADER_BYTES, body, n);
}

/** @brief A title of one character outside ASCII, U+00E9, in UTF-8. */
#define E_ACUTE_UTF8 "\xc3\xa9"

/**
 * @brief Gives id3_tag() the title #E_ACUTE_UTF8 in one tag encoding, and
 *        checks that the title frame holds that one character.
 * @param gfp   the encoder instance.
 * @param enc   the tag encoding.
 * @param body  the expected frame body: the encoding byte, then the text.
 * @param n     the size of @p body, in bytes.
 */
static void
check_e_acute_title(lame_t gfp, TextEncoding enc, const unsigned char *body, size_t n)
{
    char    arg[] = E_ACUTE_UTF8;
    size_t  sz;

    utf8_text_or_skip();
    id3tag_add_v2(gfp);
    assert_int_equal(id3_tag(gfp, 't', enc, arg), 0);
    sz = lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
    assert_frame_body(tagbuf, sz, "TIT2", body, n);
}

/**
 * @brief Latin-1: the byte E9.
 * @param state  the encoder instance of the fixture.
 */
static void
test_latin1_non_ascii(void **state)
{
    static const unsigned char body[] = { 0x00, 0xe9 };

    check_e_acute_title((lame_t) *state, TENC_LATIN1, body, sizeof body);
}

/**
 * @brief UTF-16: the byte-order mark, then the code unit 00E9.
 * @param state  the encoder instance of the fixture.
 */
static void
test_utf16_non_ascii(void **state)
{
    static const unsigned char body[] = { 0x01, 0xff, 0xfe, 0xe9, 0x00 };

    check_e_acute_title((lame_t) *state, TENC_UTF16, body, sizeof body);
}

/**
 * @brief UTF-8: the two bytes as given.
 * @param state  the encoder instance of the fixture.
 */
static void
test_utf8_non_ascii(void **state)
{
    static const unsigned char body[] = { 0x03, 0xc3, 0xa9 };

    check_e_acute_title((lame_t) *state, TENC_UTF8, body, sizeof body);
}

#define ID3_TEST(f) cmocka_unit_test_setup_teardown(f, lame_fixture_setup, lame_fixture_teardown)

/** @brief Registers and runs the frontend ID3-dispatch test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        ID3_TEST(test_utf8_text_frame),
        ID3_TEST(test_utf8_comment_frame),
        ID3_TEST(test_utf8_fieldvalue),
        ID3_TEST(test_utf16_still_works),
        ID3_TEST(test_latin1_non_ascii),
        ID3_TEST(test_utf16_non_ascii),
        ID3_TEST(test_utf8_non_ascii),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
