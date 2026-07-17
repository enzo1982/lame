/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the public id3tag_* tagging API (libmp3lame/id3tag.c).
 *
 * Exercises the ID3v1 and ID3v2 tag setters across the Latin-1, UTF-16 and
 * UTF-8 encodings and reads the result back with lame_get_id3v1_tag() /
 * lame_get_id3v2_tag(). Each case asserts the expected frame id is present,
 * and the stored text where it can be recovered by a byte search: Latin-1 and
 * UTF-8 verbatim, UTF-16 as its 2-byte code units. Genre is stored as a numeric
 * reference rather than the literal string, so only its frame is checked. The
 * id3tag_set_fieldvalue_utf8() setter and its malformed-input handling are
 * covered too.
 *
 * These are library-level tests: they link libmp3lame and call the exported
 * API directly, so no frontend translation unit is compiled in.
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

#include "lame.h"

/** Scratch buffer for an assembled tag; larger than any tag these tests make. */
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

/**
 * @brief True if the ASCII @p needle occurs in @p hay as UTF-16 code units.
 *
 * UTF-16 text is stored two bytes per character, so an ASCII needle never
 * appears as contiguous bytes; search for its little- or big-endian wide form
 * (each character paired with a zero byte) instead.
 */
static int
mem_contains_wide(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t nn = strlen(needle), wn = nn * 2, i, j;
    if (nn == 0 || wn > hn)
        return 0;
    for (i = 0; i + wn <= hn; ++i) {
        int le = 1, be = 1;
        for (j = 0; j < nn; ++j) {
            unsigned char c = (unsigned char) needle[j];
            if (hay[i + 2 * j] != c || hay[i + 2 * j + 1] != 0)
                le = 0;
            if (hay[i + 2 * j] != 0 || hay[i + 2 * j + 1] != c)
                be = 0;
        }
        if (le || be)
            return 1;
    }
    return 0;
}

/** @brief Assemble the ID3v2 tag into ::tagbuf, returning its size. */
static size_t
get_v2(lame_t gfp)
{
    return lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
}

/* --- ID3v2 text frames ------------------------------------------------- */

/** @brief Latin-1 title -> a TIT2 frame containing the text. */
static void
test_v2_title_latin1(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    id3tag_set_title(gfp, "MyTitle");
    sz = get_v2(gfp);
    assert_true(sz > 10);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
    assert_true(mem_contains(tagbuf, sz, "MyTitle"));
}

/** @brief UTF-8 textinfo -> the named frame with the (ASCII-transparent) text. */
static void
test_v2_textinfo_utf8(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_textinfo_utf8(gfp, "TPE1", "MyArtist"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TPE1"));
    assert_true(mem_contains(tagbuf, sz, "MyArtist"));
}

/** @brief UTF-16 textinfo -> the named frame (text stored as UTF-16). */
static void
test_v2_textinfo_utf16(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const unsigned short u_album[] = { 0xFEFF, 'M','y','A','l','b','u','m', 0 };
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "TALB", u_album), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TALB"));            /* frame present */
    assert_true(mem_contains_wide(tagbuf, sz, "MyAlbum"));    /* UTF-16 text */
}

/* --- ID3v2 comment frames ---------------------------------------------- */

/** @brief UTF-8 comment -> a COMM frame containing the text. */
static void
test_v2_comment_utf8(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_comment_utf8(gfp, 0, 0, "HelloComment"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "COMM"));
    assert_true(mem_contains(tagbuf, sz, "HelloComment"));
}

/** @brief UTF-16 comment -> a COMM frame. */
static void
test_v2_comment_utf16(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const unsigned short u_c[] = { 0xFEFF, 'H','i', 0 };
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_comment_utf16(gfp, 0, 0, u_c), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "COMM"));
    assert_true(mem_contains_wide(tagbuf, sz, "Hi"));   /* UTF-16 text */
}

/* --- ID3v2 field-value (arbitrary frame) ------------------------------- */

/** @brief Latin-1 field value "ID=text" -> that frame. */
static void
test_v2_fieldvalue_latin1(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue(gfp, "TIT2=FieldTitle"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
    assert_true(mem_contains(tagbuf, sz, "FieldTitle"));
}

/** @brief UTF-16 field value "ID=text" -> that frame. */
static void
test_v2_fieldvalue_utf16(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const unsigned short u_fv[] = {
        0xFEFF, 'T','I','T','2','=','F','V','1','6', 0
    };
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue_utf16(gfp, u_fv), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
    assert_true(mem_contains_wide(tagbuf, sz, "FV16"));   /* UTF-16 text */
}

/** @brief UTF-8 field value "ID=text" -> that frame (SF #524's new setter). */
static void
test_v2_fieldvalue_utf8(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, "TIT2=FieldUtf8"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TIT2"));
    assert_true(mem_contains(tagbuf, sz, "FieldUtf8"));
}

/** @brief id3tag_set_fieldvalue_utf8() rejects malformed "ID=..." input. */
static void
test_v2_fieldvalue_utf8_malformed(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, "TI=x"), -1);   /* < 5 bytes */
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, "TIT2v=x"), -1); /* [4] != '=' */
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, ""), 0);        /* empty: no-op */
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, NULL), 0);      /* NULL: no-op */
}

/* --- genre ------------------------------------------------------------- */

/** @brief A named genre -> a TCON frame in the v2 tag. */
static void
test_v2_genre(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_genre(gfp, "Rock"), 0);
    sz = get_v2(gfp);
    /* "Rock" is a standard genre, stored as a numeric reference rather than the
       literal string, so only the frame's presence is checked, not the text. */
    assert_true(mem_contains(tagbuf, sz, "TCON"));
}

/* --- ID3v1 ------------------------------------------------------------- */

/** @brief Short fields produce a 128-byte "TAG..." ID3v1 block. */
static void
test_v1_basic(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_set_title(gfp, "V1Title");
    id3tag_set_artist(gfp, "V1Artist");
    id3tag_set_album(gfp, "V1Album");
    id3tag_set_year(gfp, "2020");
    sz = lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_memory_equal(tagbuf, "TAG", 3);
    assert_true(mem_contains(tagbuf, sz, "V1Title"));
    assert_true(mem_contains(tagbuf, sz, "V1Artist"));
}

/* --- v1-only / v2-only gating ------------------------------------------ */

/** @brief id3tag_v1_only() suppresses the ID3v2 tag. */
static void
test_v1_only_suppresses_v2(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_v1_only(gfp);
    id3tag_set_title(gfp, "X");
    assert_int_equal(get_v2(gfp), 0);
}

/** @brief id3tag_v2_only() suppresses the ID3v1 tag. */
static void
test_v2_only_suppresses_v1(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_v2_only(gfp);
    id3tag_set_title(gfp, "X");
    assert_int_equal(lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf), 0);
}

/* --- ID3v2 28-bit size-field limit ------------------------------------- */

/**
 * @brief A tag whose size exceeds the 28-bit synchsafe field is refused.
 *
 * The tag length is stored in four synchsafe bytes, 28 bits in all, so a tag
 * larger than that cannot state its own size and must not be written. This
 * drives the size over the limit with a padding request, which needs no large
 * allocation, and asserts no tag is produced.
 */
static void
test_v2_size_over_synchsafe_limit_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_add_v2(gfp);
    id3tag_set_title(gfp, "X");
    /* One past the largest value the 28-bit field can hold. */
    id3tag_set_pad(gfp, (size_t) 0x10000000);
    assert_int_equal(lame_get_id3v2_tag(gfp, NULL, 0), 0);
}

/**
 * @brief The album-art path, the reported vector, is refused past the limit.
 *
 * The size is dominated here by a caller-supplied album-art buffer rather than
 * padding, matching the way the overflow was reported. The image is generated
 * in memory (about 256 MB, with a valid JPEG signature so it is accepted) and
 * freed as soon as the library has taken its own copy.
 */
static void
test_v2_albumart_over_synchsafe_limit_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t  art_size = (size_t) 0x10000000; /* past the 28-bit field on its own */
    char   *art = malloc(art_size);

    if (art == NULL) {
        skip(); /* not enough memory to exercise the >256 MB path */
        return;
    }
    art[0] = (char) 0xFF; /* JPEG SOI, so id3tag_set_albumart accepts it */
    art[1] = (char) 0xD8;
    art[2] = (char) 0xFF;
    assert_int_equal(id3tag_set_albumart(gfp, art, art_size), 0);
    free(art); /* the library holds its own copy now */
    assert_int_equal(lame_get_id3v2_tag(gfp, NULL, 0), 0);
}

/**
 * @brief A tag that fits within the field is still written.
 *
 * The guard must refuse only the tags that cannot be represented; an ordinary
 * padded tag has to keep working.
 */
static void
test_v2_size_within_limit_written(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_add_v2(gfp);
    id3tag_set_title(gfp, "MyTitle");
    id3tag_set_pad(gfp, (size_t) 1024);
    assert_true(get_v2(gfp) > 10);
}

/* --- fixture ----------------------------------------------------------- */

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

/** @brief Registers and runs the id3tag API test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        ID3_TEST(test_v2_title_latin1),
        ID3_TEST(test_v2_textinfo_utf8),
        ID3_TEST(test_v2_textinfo_utf16),
        ID3_TEST(test_v2_comment_utf8),
        ID3_TEST(test_v2_comment_utf16),
        ID3_TEST(test_v2_fieldvalue_latin1),
        ID3_TEST(test_v2_fieldvalue_utf16),
        ID3_TEST(test_v2_fieldvalue_utf8),
        ID3_TEST(test_v2_fieldvalue_utf8_malformed),
        ID3_TEST(test_v2_genre),
        ID3_TEST(test_v1_basic),
        ID3_TEST(test_v1_only_suppresses_v2),
        ID3_TEST(test_v2_only_suppresses_v1),
        ID3_TEST(test_v2_size_over_synchsafe_limit_rejected),
        ID3_TEST(test_v2_albumart_over_synchsafe_limit_rejected),
        ID3_TEST(test_v2_size_within_limit_written),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
