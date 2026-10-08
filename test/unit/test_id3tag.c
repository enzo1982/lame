/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the public id3tag_* tagging API (libmp3lame/id3tag.c).
 *
 * The first group tests the ID3v1 tag and ID3v2 tag setters with Latin-1,
 * UTF-16 and UTF-8 text. It reads the result back with lame_get_id3v1_tag()
 * and lame_get_id3v2_tag(). Each case checks that the expected frame ID is
 * present. Where a byte search can find the stored text, the case also checks
 * the text. Latin-1 and UTF-8 text is searched byte for byte. UTF-16 text is
 * searched as 2-byte code units. For the genre, the test checks only that the
 * TCON frame is present. The group also tests id3tag_set_fieldvalue_utf8() and
 * how it handles malformed input.
 *
 * The second group tests the rest of the API:
 * - the calls that select a tag version or an encoding, not a field,
 * - the calls that reset the tag or add padding,
 * - the genre list,
 * - the track number,
 * - the three deprecated UCS-2 setters.
 *
 * The UCS-2 setters are documented as aliases. So each test builds the same
 * tag through the alias and through its target, and compares the two byte for
 * byte. A test that only looks for the frame also passes when an alias calls
 * the wrong function.
 *
 * The third group tests the descriptions that identify TXXX, WXXX and COMM
 * frames, in both encodings. These tests count the frames. A search is not
 * enough: the risk is that one frame replaces another, and the remaining frame
 * still matches a search for its own text.
 *
 * The last group reads the tag as a player does. It walks the frames by the
 * sizes in their headers, and checks which frames the walk finds. A byte
 * search can find text that a reader never gets to.
 *
 * These are library-level tests. They link libmp3lame and call the exported
 * API directly. No frontend translation unit is compiled in.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include <cmocka.h>

#include "test_mem.h"

#include "test_fixture.h"

#include "lame.h"

/*
 * The three UCS-2 setters are still exported from libmp3lame for binary
 * back-compat, but their prototypes are guarded out of lame.h by
 * DEPRECATED_OR_OBSOLETE_CODE_REMOVED, so they are declared here the same way
 * test_set_get.c declares the deprecated stubs it covers. Without this the
 * calls below are implicit declarations - a warning on some compilers and an
 * error on others, which is how it was found. New code must not call them:
 * they are tested only to pin the aliases the ABI still carries.
 */
extern int id3tag_set_textinfo_ucs2(lame_t gfp, char const *id,
                                    unsigned short const *text);
extern int id3tag_set_comment_ucs2(lame_t gfp, char const *lang,
                                   unsigned short const *desc,
                                   unsigned short const *text);
extern int id3tag_set_fieldvalue_ucs2(lame_t gfp, const unsigned short *fieldvalue);

/** Scratch buffer for a built tag. It is larger than any tag these tests make. */
static unsigned char tagbuf[8192];

/** @brief Builds the ID3v2 tag in ::tagbuf and returns its size. */
static size_t
get_v2(lame_t gfp)
{
    return lame_get_id3v2_tag(gfp, tagbuf, sizeof tagbuf);
}

/**
 * @brief Walks the frames of the ID3v2 tag in ::tagbuf, as a reader does.
 *
 * The function reads each frame size as the tag version defines it. In
 * ID3v2.3 the size is a plain 32-bit integer. In ID3v2.4 it is a synchsafe
 * integer. The walk stops at the first zero byte where a frame would start. A
 * reader takes this byte as the start of the padding.
 *
 * @param sz   the size of the tag.
 * @param id   a four-character frame ID to look for, or NULL.
 * @param len  gets the data size of the frame found. It may be NULL.
 * @return With @p id: the offset of the frame data, or 0 if a reader does not
 *         get to the frame. Without @p id: the offset where the frames end, or
 *         0 if a frame size is invalid or goes past the end of the tag.
 */
static size_t
walk_v2(size_t sz, const char *id, size_t *len)
{
    size_t p = 10;
    int const v24 = sz >= 10 && tagbuf[3] == 4;
    while (p + 10 <= sz && tagbuf[p] != 0) {
        const unsigned char *h = tagbuf + p + 4;
        size_t n;
        if (v24) {
            if ((h[0] | h[1] | h[2] | h[3]) & 0x80)
                return 0;
            n = ((size_t) h[0] << 21) | ((size_t) h[1] << 14) | ((size_t) h[2] << 7) | h[3];
        }
        else {
            n = ((size_t) h[0] << 24) | ((size_t) h[1] << 16) | ((size_t) h[2] << 8) | h[3];
        }
        if (n > sz - p - 10)
            return 0;
        if (id != NULL && memcmp(tagbuf + p, id, 4) == 0) {
            if (len != NULL)
                *len = n;
            return p + 10;
        }
        p += 10 + n;
    }
    return id != NULL ? 0 : p;
}

/* --- ID3v2 text frames ------------------------------------------------- */

/** @brief Checks that a Latin-1 title gives a TIT2 frame with the text. */
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

/** @brief Checks that UTF-8 textinfo gives the named frame with the ASCII text. */
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

/** @brief Checks that UTF-16 textinfo gives the named frame with UTF-16 text. */
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

/** @brief Checks that a UTF-8 comment gives a COMM frame with the text. */
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

/** @brief Checks that a UTF-16 comment gives a COMM frame with the text. */
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

/** @brief Checks that a Latin-1 field value "ID=text" gives that frame. */
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

/** @brief Checks that a UTF-16 field value "ID=text" gives that frame. */
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

/** @brief Checks that a UTF-8 field value "ID=text" gives that frame (SF #524). */
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

/**
 * @brief Checks that id3tag_set_fieldvalue_utf8() rejects malformed "ID=..."
 *        input. Empty and NULL input return 0 and do nothing.
 */
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

/* --- ID3v2 descriptors ------------------------------------------------- */

/**
 * @brief Checks that two descriptions, where one starts with the other, give
 *        two frames.
 *
 * The description identifies a TXXX frame. So "foo" and "foobar" are two
 * frames, and both texts must stay. The --tv option uses this path:
 * id3tag_set_fieldvalue() splits "TXXX=foo=alpha" into the frame ID, the
 * description and the text.
 */
static void
test_v2_prefix_descriptions_stay_apart(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue(gfp, "TXXX=foo=alpha"), 0);
    assert_int_equal(id3tag_set_fieldvalue(gfp, "TXXX=foobar=beta"), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "TXXX"), 2);
    assert_true(mem_contains(tagbuf, sz, "alpha"));
    assert_true(mem_contains(tagbuf, sz, "beta"));
}

/**
 * @brief Checks that the same description twice still replaces the frame.
 *
 * This is the control for the test above. The setter compares descriptions,
 * so a repeated description updates its frame. A test that only counts frames
 * also passes when the comparison never finds a match.
 */
static void
test_v2_same_description_replaces_frame(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_fieldvalue(gfp, "TXXX=foo=alpha"), 0);
    assert_int_equal(id3tag_set_fieldvalue(gfp, "TXXX=foo=beta"), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "TXXX"), 1);
    assert_true(mem_contains(tagbuf, sz, "beta"));
    assert_false(mem_contains(tagbuf, sz, "alpha"));
}

/**
 * @brief Checks that a comment with a description does not replace a comment
 *        without one.
 *
 * The empty description is the extreme case of the test above, because every
 * description starts with it. The language and the description together
 * identify a COMM frame. So these are two frames.
 */
static void
test_v2_empty_description_keeps_its_comment(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_comment_latin1(gfp, "eng", 0, "plain"), 0);
    assert_int_equal(id3tag_set_comment_latin1(gfp, "eng", "desc", "described"), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "COMM"), 2);
    assert_true(mem_contains(tagbuf, sz, "plain"));
    assert_true(mem_contains(tagbuf, sz, "described"));
}

/**
 * @brief Checks that UTF-16 descriptions are compared in the same way.
 *
 * UTF-16 descriptions use a separate comparison. It never matches a frame that
 * has a non-empty description in another encoding. So this comparison needs
 * its own test, and the Latin-1 test does not cover it.
 *
 * Each string starts with a byte order mark. The mark becomes part of the
 * stored description. Both strings have the same mark, so one description
 * still starts with the other.
 */
static void
test_v2_prefix_descriptions_utf16(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const unsigned short u_foo[] = {
        0xFEFF, 'f','o','o','=','a','l','p','h','a', 0
    };
    static const unsigned short u_foobar[] = {
        0xFEFF, 'f','o','o','b','a','r','=','b','e','t','a', 0
    };
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "TXXX", u_foo), 0);
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "TXXX", u_foobar), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "TXXX"), 2);
    assert_true(mem_contains_wide(tagbuf, sz, "alpha"));
    assert_true(mem_contains_wide(tagbuf, sz, "beta"));
}

/**
 * @brief Checks that a UTF-16 comment without any description keeps its own
 *        frame.
 *
 * The UTF-16 setters accept an absent description. Only in this way does the
 * UTF-16 comparison run with nothing to compare. Two comments without a
 * description are the same frame, so the second replaces the first. A comment
 * with a description is a separate frame.
 */
static void
test_v2_utf16_absent_description(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const unsigned short u_uno[]  = { 0xFEFF, 'u','n','o', 0 };
    static const unsigned short u_dos[]  = { 0xFEFF, 'd','o','s', 0 };
    static const unsigned short u_tres[] = { 0xFEFF, 't','r','e','s', 0 };
    static const unsigned short u_desc[] = { 0xFEFF, 'd','e','s','c', 0 };
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_comment_utf16(gfp, "eng", 0, u_uno), 0);
    assert_int_equal(id3tag_set_comment_utf16(gfp, "eng", 0, u_dos), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "COMM"), 1);
    assert_true(mem_contains_wide(tagbuf, sz, "dos"));
    assert_false(mem_contains_wide(tagbuf, sz, "uno"));

    assert_int_equal(id3tag_set_comment_utf16(gfp, "eng", u_desc, u_tres), 0);
    sz = get_v2(gfp);
    assert_int_equal(mem_count(tagbuf, sz, "COMM"), 2);
    assert_true(mem_contains_wide(tagbuf, sz, "dos"));
    assert_true(mem_contains_wide(tagbuf, sz, "tres"));
}

/* --- genre ------------------------------------------------------------- */

/** @brief Checks that a named genre gives a TCON frame in the ID3v2 tag. */
static void
test_v2_genre(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;
    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_genre(gfp, "Rock"), 0);
    sz = get_v2(gfp);
    /* The TCON frame stores the genre name as text. This test only checks
       that the frame is present. */
    assert_true(mem_contains(tagbuf, sz, "TCON"));
}

/* --- ID3v1 ------------------------------------------------------------- */

/**
 * @brief Checks that short fields give a 128-byte ID3v1 tag that starts with
 *        "TAG".
 */
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

/** @brief Checks that id3tag_v1_only() turns off the ID3v2 tag. */
static void
test_v1_only_suppresses_v2(void **state)
{
    lame_t gfp = (lame_t) *state;
    id3tag_v1_only(gfp);
    id3tag_set_title(gfp, "X");
    assert_int_equal(get_v2(gfp), 0);
}

/** @brief Checks that id3tag_v2_only() turns off the ID3v1 tag. */
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
 * @brief Checks that a tag too large for the 28-bit synchsafe size field is
 *        rejected.
 *
 * The tag size is stored in four synchsafe bytes, 28 bits in total. A larger
 * tag cannot store its own size, so the library must not write it. The test
 * makes the tag too large with a padding request. This needs no large
 * allocation. The test checks that no tag is produced.
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
 * @brief Checks that album art that makes the tag too large is also rejected.
 *
 * Here most of the size comes from album art that the caller passes, not from
 * padding. The test builds the image in memory. It is about 256 MB and has a
 * valid JPEG signature, so the setter accepts it. The test frees it as soon as
 * the library has made its own copy.
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
 * @brief Checks that a tag that fits in the size field is still written.
 *
 * The check must reject only the tags whose size the field cannot store. A
 * normal tag with padding must still work.
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

/* --- ID3v2 auto play-length (TLEN) ------------------------------------- */

/**
 * @brief Checks that a play length above 2^32-1 ms is written in full, not
 *        clamped.
 *
 * The TLEN value comes from num_samples. A long enough length gives a duration
 * above a 32-bit count of milliseconds. num_samples can store such a sample
 * count only where unsigned long is wider than 32 bits. On other platforms the
 * test is skipped. The test sets the length in the configuration and encodes
 * no audio.
 *
 * The preprocessor decides the skip, not a run-time check. The sample count is
 * a constant that is too large for a 32-bit unsigned long. With a run-time
 * check, the constant stays in the code. Every build with a 32-bit unsigned
 * long then truncates the constant and warns about it.
 */
static void
test_v2_playlength_beyond_32bit(void **state)
{
#if ULONG_MAX > 0xFFFFFFFFUL
    lame_t gfp = (lame_t) *state;
    size_t sz;
    lame_set_in_samplerate(gfp, 8000);
    lame_set_num_channels(gfp, 2);
    lame_set_num_samples(gfp, 40000000000UL); /* 4e10 @ 8 kHz -> 5e9 ms */
    id3tag_add_v2(gfp);
    id3tag_set_title(gfp, "MyTitle");
    assert_int_equal(lame_init_params(gfp), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "5000000000"));  /* full value */
    assert_false(mem_contains(tagbuf, sz, "4294967295")); /* not the old clamp */
#else
    (void) state;
    skip(); /* num_samples cannot express a >2^32-1 ms length here */
#endif
}

/* --- the genre list ---------------------------------------------------- */

/**
 * @brief Compares two genre names in the order of the genre list.
 *
 * The comparison ignores case. It also skips every character that is not a
 * letter or a digit. This second part is necessary. The list is sorted by the
 * letters and digits only. So a plain case-insensitive comparison reports five
 * pairs in the wrong order that are in fact correct:
 * - "Classical" before "Classic Rock",
 * - "Eurodance" before "Euro-House",
 * - "Folklore" before "Folk-Rock",
 * - "Hardcore" before "Hard Rock",
 * - "Rave" before "R&B".
 *
 * Each pair is in order without the space, hyphen or ampersand. The function
 * is written here and does not use strcasecmp. strcasecmp is not available on
 * every platform that builds LAME.
 */
static int
genre_name_cmp(const char *a, const char *b)
{
    for (;;) {
        int ca, cb;
        while (*a && !isalnum((unsigned char) *a))
            ++a;
        while (*b && !isalnum((unsigned char) *b))
            ++b;
        ca = *a ? tolower((unsigned char) *a) : 0;
        cb = *b ? tolower((unsigned char) *b) : 0;
        if (ca != cb)
            return ca - cb;
        if (ca == 0)
            return 0;
        ++a;
        ++b;
    }
}

/**
 * @brief State for the id3tag_genre_list() callback.
 *
 * @p seen counts how often the callback gets each genre number. With it, the
 * test checks that the list is a @e permutation: every genre exactly once. A
 * correct length alone is not enough.
 */
#define GENRE_SEEN_MAX 512
struct genre_probe {
    int   calls;
    int   seen[GENRE_SEEN_MAX];
    int   out_of_range;
    int   null_name;
    int   out_of_order;
    int   found_rock;
    void *cookie_seen;
    char  last[128];
};

static void
genre_probe_handler(int num, const char *name, void *cookie)
{
    struct genre_probe *p = (struct genre_probe *) cookie;

    p->cookie_seen = cookie;
    ++p->calls;

    if (num < 0 || num >= GENRE_SEEN_MAX)
        p->out_of_range = 1;
    else
        ++p->seen[num];

    if (name == NULL) {
        p->null_name = 1;
        return;
    }
    if (strcmp(name, "Rock") == 0)
        p->found_rock = 1;
    if (p->calls > 1 && genre_name_cmp(p->last, name) > 0)
        p->out_of_order = 1;
    strncpy(p->last, name, sizeof p->last - 1);
    p->last[sizeof p->last - 1] = '\0';
}

/**
 * @brief Checks that the genre list gives every genre once, in alphabetical
 *        order.
 *
 * The test checks the exact count, not a lower limit. 148 is the ID3v1 genre
 * set plus the Winamp extensions. This is the set the format defines. A change
 * to this number must be on purpose, and it must update this line. The
 * permutation check is the stronger part. It fails when a genre comes twice or
 * is missing. A count alone does not detect this.
 */
static void
test_genre_list_enumerates_every_genre(void **state)
{
    struct genre_probe p;
    int                i;
    int                distinct = 0;

    (void) state;               /* the list is not a property of an encoder */
    memset(&p, 0, sizeof p);
    id3tag_genre_list(genre_probe_handler, &p);

    assert_int_equal(p.calls, 148);
    assert_int_equal(p.out_of_range, 0);
    assert_int_equal(p.null_name, 0);
    assert_int_equal(p.out_of_order, 0);
    assert_int_equal(p.found_rock, 1);
    assert_ptr_equal(p.cookie_seen, &p);   /* the cookie arrives untouched */

    for (i = 0; i < GENRE_SEEN_MAX; ++i) {
        assert_true(p.seen[i] <= 1);       /* never reported twice */
        distinct += p.seen[i];
    }
    assert_int_equal(distinct, p.calls);   /* ... and never skipped */
}

/** @brief Checks that a NULL handler is accepted and does nothing. */
static void
test_genre_list_null_handler(void **state)
{
    (void) state;
    id3tag_genre_list(NULL, NULL);   /* must not crash */
}

/* --- resetting the tag state ------------------------------------------- */

/**
 * @brief Checks that id3tag_init() returns the instance to its initial tag
 *        state.
 *
 * After the reset, the test sets a second, different title and gets the tag
 * once. The new title must be there, and the old one must be gone. A test
 * that asks whether an empty instance still writes a tag checks something
 * else.
 *
 * The three fields that are not strings are the important part of this test.
 * lame_close() frees the strings with the same helper. So a test that checks
 * only the strings still passes when the rest of the reset is removed. Only
 * the reset clears the track number, the genre and the request for an ID3v2
 * tag. So the test checks these three.
 */
static void
test_init_discards_previous_fields(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;

    id3tag_add_v2(gfp);
    id3tag_set_title(gfp, "BeforeInit");
    id3tag_set_artist(gfp, "BeforeArtist");
    id3tag_set_year(gfp, "1999");
    assert_int_equal(id3tag_set_track(gfp, "7"), 0);
    assert_int_equal(id3tag_set_genre(gfp, "Rock"), 0);

    sz = lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_true(mem_contains(tagbuf, sz, "BeforeInit"));
    assert_true(mem_contains(tagbuf, sz, "1999"));
    assert_int_equal(tagbuf[126], 7);            /* ID3v1.1 track byte */
    assert_int_not_equal(tagbuf[127], 255);      /* a genre was chosen */
    assert_true(get_v2(gfp) > 10);

    id3tag_init(gfp);

    id3tag_set_title(gfp, "AfterInit");
    sz = lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_true(mem_contains(tagbuf, sz, "AfterInit"));
    assert_false(mem_contains(tagbuf, sz, "BeforeInit"));
    assert_false(mem_contains(tagbuf, sz, "BeforeArtist"));
    assert_false(mem_contains(tagbuf, sz, "1999"));
    assert_int_equal(tagbuf[126], 0);            /* no track number */
    assert_int_equal(tagbuf[127], 255);          /* genre unset again */
    assert_int_equal(get_v2(gfp), 0);            /* and no ID3v2 tag was asked for */
}

/* --- ID3v2.4 / UTF-8 selection ----------------------------------------- */

/** @brief Returns the ID3v2 version byte, or -1 if there is no tag. */
static int
v2_version_byte(lame_t gfp)
{
    size_t sz = get_v2(gfp);
    if (sz < 4 || memcmp(tagbuf, "ID3", 3) != 0)
        return -1;
    return tagbuf[3];
}

/**
 * @brief Checks that id3tag_add_v2_4_UTF8() selects version 2.4 and keeps the
 *        ID3v1 tag.
 *
 * The test reads the version from the tag header. It also checks the default
 * version in the same test. This shows that the 4 comes from the call and not
 * from the format.
 */
static void
test_add_v2_4_utf8_selects_version_4(void **state)
{
    lame_t plain = lame_init();
    lame_t utf8 = (lame_t) *state;

    assert_non_null(plain);
    id3tag_add_v2(plain);
    id3tag_set_title(plain, "T");
    assert_int_equal(v2_version_byte(plain), 3);   /* the default */
    lame_close(plain);

    id3tag_add_v2_4_UTF8(utf8);
    id3tag_set_title(utf8, "T");
    assert_int_equal(v2_version_byte(utf8), 4);

    /* documented: the ID3v1 tag is unaffected */
    assert_int_equal(lame_get_id3v1_tag(utf8, tagbuf, sizeof tagbuf), 128);
}

/**
 * @brief Checks that id3tag_v2_4_UTF8_only() selects version 2.4 and writes no
 *        ID3v1 tag.
 */
static void
test_v2_4_utf8_only_suppresses_v1(void **state)
{
    lame_t gfp = (lame_t) *state;

    id3tag_v2_4_UTF8_only(gfp);
    id3tag_set_title(gfp, "T");
    assert_int_equal(v2_version_byte(gfp), 4);
    assert_int_equal(lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf), 0);
}

/* --- ID3v1 field padding ----------------------------------------------- */

/** @brief Counts the bytes equal to @p c in tagbuf[from, to). */
static int
count_byte(size_t from, size_t to, unsigned char c)
{
    int    n = 0;
    size_t i;
    for (i = from; i < to; ++i)
        if (tagbuf[i] == c)
            ++n;
    return n;
}

/**
 * @brief Checks that id3tag_space_v1() fills the unused ID3v1 bytes with
 *        spaces.
 *
 * The title uses bytes 3 to 32 of the 128-byte tag. A title of three
 * characters leaves 27 bytes. The test compares two builds with each other.
 * So the check is about the call, not about a guess of the layout.
 */
static void
test_space_v1_pads_with_spaces(void **state)
{
    lame_t spaced = (lame_t) *state;
    lame_t plain = lame_init();

    assert_non_null(plain);
    id3tag_set_title(plain, "V1T");
    assert_int_equal(lame_get_id3v1_tag(plain, tagbuf, sizeof tagbuf), 128);
    assert_int_equal(count_byte(6, 33, 0x00), 27);
    assert_int_equal(count_byte(6, 33, 0x20), 0);
    lame_close(plain);

    id3tag_space_v1(spaced);
    id3tag_set_title(spaced, "V1T");
    assert_int_equal(lame_get_id3v1_tag(spaced, tagbuf, sizeof tagbuf), 128);
    assert_int_equal(count_byte(6, 33, 0x20), 27);
    assert_int_equal(count_byte(6, 33, 0x00), 0);
}

/**
 * @brief Checks that id3tag_space_v1() also cancels an earlier
 *        id3tag_v2_only().
 */
static void
test_space_v1_cancels_v2_only(void **state)
{
    lame_t gfp = (lame_t) *state;

    id3tag_v2_only(gfp);
    id3tag_space_v1(gfp);
    id3tag_set_title(gfp, "V1T");
    assert_int_equal(lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf), 128);
}

/* --- ID3v2 padding ----------------------------------------------------- */

/**
 * @brief Checks that id3tag_pad_v2() is id3tag_set_pad() with the documented
 *        128 bytes.
 *
 * The test compares the tags of two instances and does not check a size. So
 * it checks what the function promises, and does not also fix the size of a
 * normal tag.
 */
static void
test_pad_v2_equals_set_pad_128(void **state)
{
    lame_t implicit = (lame_t) *state;
    lame_t explicit_ = lame_init();
    unsigned char other[sizeof tagbuf];
    size_t sa, sb;

    assert_non_null(explicit_);
    id3tag_add_v2(explicit_);
    id3tag_set_title(explicit_, "PadTitle");
    id3tag_set_pad(explicit_, 128);
    sb = lame_get_id3v2_tag(explicit_, other, sizeof other);
    lame_close(explicit_);

    id3tag_add_v2(implicit);
    id3tag_set_title(implicit, "PadTitle");
    id3tag_pad_v2(implicit);
    sa = get_v2(implicit);

    assert_true(sa > 10);
    assert_int_equal(sa, sb);
    assert_memory_equal(tagbuf, other, sa);
}

/* --- comments ---------------------------------------------------------- */

/**
 * @brief Checks that the simple comment setter writes a COMM frame with the
 *        text. The ID3v1 tag also gets the text.
 */
static void
test_set_comment_writes_comm(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;

    id3tag_add_v2(gfp);
    id3tag_set_comment(gfp, "PlainComment");
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "COMM"));
    assert_true(mem_contains(tagbuf, sz, "PlainComment"));

    /* the ID3v1 tag carries it too, in the comment field */
    sz = lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_true(mem_contains(tagbuf, sz, "PlainComment"));
}

/**
 * @brief Checks that the Latin-1 comment setter stores the language and the
 *        description.
 */
static void
test_set_comment_latin1(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;

    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_comment_latin1(gfp, "deu", "Beschreibung", "Kommentar"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "COMM"));
    assert_true(mem_contains(tagbuf, sz, "deu"));
    assert_true(mem_contains(tagbuf, sz, "Beschreibung"));
    assert_true(mem_contains(tagbuf, sz, "Kommentar"));
}

/* --- the deprecated UCS-2 aliases -------------------------------------- */

/*
 * Each is documented as an alias for the UTF-16 setter of the same name. The
 * contract is therefore not "it writes a frame" but "it writes exactly what
 * the other one writes", so each test builds the same tag twice - once through
 * the alias, once through its target - and compares the two byte for byte. A
 * test that only looked for the frame would pass on an alias wired to the
 * wrong function.
 */

/**
 * @brief Checks that id3tag_set_textinfo_ucs2() writes the same tag as
 *        id3tag_set_textinfo_utf16().
 */
static void
test_textinfo_ucs2_matches_utf16(void **state)
{
    static const unsigned short text[] = { 0xFEFF, 'A','l','i','a','s', 0 };
    lame_t viaucs2 = (lame_t) *state;
    lame_t viautf16 = lame_init();
    unsigned char other[sizeof tagbuf];
    size_t sa, sb;

    assert_non_null(viautf16);
    id3tag_add_v2(viautf16);
    assert_int_equal(id3tag_set_textinfo_utf16(viautf16, "TALB", text), 0);
    sb = lame_get_id3v2_tag(viautf16, other, sizeof other);
    lame_close(viautf16);

    id3tag_add_v2(viaucs2);
    assert_int_equal(id3tag_set_textinfo_ucs2(viaucs2, "TALB", text), 0);
    sa = get_v2(viaucs2);

    assert_true(sa > 10);
    assert_int_equal(sa, sb);
    assert_memory_equal(tagbuf, other, sa);
}

/**
 * @brief Checks that id3tag_set_comment_ucs2() writes the same tag as
 *        id3tag_set_comment_utf16().
 */
static void
test_comment_ucs2_matches_utf16(void **state)
{
    static const unsigned short desc[] = { 0xFEFF, 'D', 0 };
    static const unsigned short text[] = { 0xFEFF, 'H','i', 0 };
    lame_t viaucs2 = (lame_t) *state;
    lame_t viautf16 = lame_init();
    unsigned char other[sizeof tagbuf];
    size_t sa, sb;

    assert_non_null(viautf16);
    id3tag_add_v2(viautf16);
    assert_int_equal(id3tag_set_comment_utf16(viautf16, "eng", desc, text), 0);
    sb = lame_get_id3v2_tag(viautf16, other, sizeof other);
    lame_close(viautf16);

    id3tag_add_v2(viaucs2);
    assert_int_equal(id3tag_set_comment_ucs2(viaucs2, "eng", desc, text), 0);
    sa = get_v2(viaucs2);

    assert_true(sa > 10);
    assert_int_equal(sa, sb);
    assert_memory_equal(tagbuf, other, sa);
}

/**
 * @brief Checks that id3tag_set_fieldvalue_ucs2() writes the same tag as
 *        id3tag_set_fieldvalue_utf16().
 */
static void
test_fieldvalue_ucs2_matches_utf16(void **state)
{
    static const unsigned short fv[] = {
        0xFEFF, 'T','I','T','2','=','U','C','S','2', 0
    };
    lame_t viaucs2 = (lame_t) *state;
    lame_t viautf16 = lame_init();
    unsigned char other[sizeof tagbuf];
    size_t sa, sb;

    assert_non_null(viautf16);
    id3tag_add_v2(viautf16);
    assert_int_equal(id3tag_set_fieldvalue_utf16(viautf16, fv), 0);
    sb = lame_get_id3v2_tag(viautf16, other, sizeof other);
    lame_close(viautf16);

    id3tag_add_v2(viaucs2);
    assert_int_equal(id3tag_set_fieldvalue_ucs2(viaucs2, fv), 0);
    sa = get_v2(viaucs2);

    assert_true(sa > 10);
    assert_int_equal(sa, sb);
    assert_memory_equal(tagbuf, other, sa);
}

/* --- the Latin-1 text-frame setter ------------------------------------- */

/**
 * @brief Checks that id3tag_set_textinfo_latin1() writes the named frame and
 *        rejects identifiers that it cannot use.
 *
 * The test covers both documented rejections: an identifier that is not a
 * valid frame ID, and a valid identifier for a frame that this function cannot
 * write.
 */
static void
test_textinfo_latin1(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;

    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "TPE1", "Latin1Artist"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TPE1"));
    assert_true(mem_contains(tagbuf, sz, "Latin1Artist"));

    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "??", "x"), -1);    /* not an id */
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "APIC", "x"), -255); /* not writable here */
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "TPE1", NULL), 0);  /* NULL: no-op */
}

/* --- the track number --------------------------------------------------- */

/**
 * @brief Checks that id3tag_set_track() reports only whether the number fits
 *        in the ID3v1 tag.
 *
 * The documented catch: for a number that ID3v1 cannot store, the function
 * returns -1, but it still writes the ID3v2 frame. So -1 is not a failure.
 * Byte 126 of the ID3v1 tag is the ID3v1.1 track byte. A number that fits
 * must appear there.
 */
static void
test_set_track(void **state)
{
    lame_t inrange = (lame_t) *state;
    lame_t toobig = lame_init();
    size_t sz;

    assert_int_equal(id3tag_set_track(inrange, "5"), 0);
    sz = lame_get_id3v1_tag(inrange, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_int_equal(tagbuf[126], 5);

    assert_non_null(toobig);
    assert_int_equal(id3tag_set_track(toobig, "300"), -1);  /* past ID3v1's byte */
    sz = lame_get_id3v2_tag(toobig, tagbuf, sizeof tagbuf);
    assert_true(mem_contains(tagbuf, sz, "TRCK"));          /* ... but written */
    assert_true(mem_contains(tagbuf, sz, "300"));
    lame_close(toobig);
}

/** @brief Checks that "number/total" keeps the total in the ID3v2 frame. */
static void
test_set_track_with_total(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz;

    id3tag_add_v2(gfp);
    assert_int_equal(id3tag_set_track(gfp, "3/12"), 0);
    sz = get_v2(gfp);
    assert_true(mem_contains(tagbuf, sz, "TRCK"));
    assert_true(mem_contains(tagbuf, sz, "3/12"));

    /* ID3v1 has room for the number alone */
    sz = lame_get_id3v1_tag(gfp, tagbuf, sizeof tagbuf);
    assert_int_equal(sz, 128);
    assert_int_equal(tagbuf[126], 3);
}

/* --- the tag as a reader walks it -------------------------------------- */

/**
 * @brief Checks that a one-character language is padded with spaces, and that
 *        nothing after it is read.
 *
 * The language is on the heap and has exactly its own size. So a read past
 * its NUL is out of bounds, and a sanitizer reports it.
 *
 * @param state the encoder instance of the fixture.
 */
static void
test_v2_comment_one_character_language(void **state)
{
    lame_t gfp = (lame_t) *state;
    char *lang = malloc(2);
    size_t sz, at, len = 0;
    assert_non_null(lang);
    lang[0] = 'e';
    lang[1] = 0;
    assert_int_equal(id3tag_set_comment_latin1(gfp, lang, "d", "text"), 0);
    free(lang);
    sz = get_v2(gfp);
    at = walk_v2(sz, "COMM", &len);
    assert_true(at > 0);
    assert_true(len > 4);
    assert_memory_equal(tagbuf + at + 1, "e  ", 3);
}

/**
 * @brief Checks that a two-character language is padded with a space, not a
 *        NUL.
 *
 * @param state the encoder instance of the fixture.
 */
static void
test_v2_comment_two_character_language(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t sz, at, len = 0;
    assert_int_equal(id3tag_set_comment_latin1(gfp, "en", "d", "text"), 0);
    sz = get_v2(gfp);
    at = walk_v2(sz, "COMM", &len);
    assert_true(at > 0);
    assert_true(len > 4);
    assert_memory_equal(tagbuf + at + 1, "en ", 3);
}

/**
 * @brief Checks that an identifier shorter than four characters is rejected
 *        and adds no frame. A reader then still finds a frame set after it.
 *
 * @param state the encoder instance of the fixture.
 */
static void
test_v2_short_frame_id_refused(void **state)
{
    static const unsigned short u_text[] = { 0xFEFF, 'x', 0 };
    lame_t gfp = (lame_t) *state;
    size_t sz;
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "X", "abc"), -1);
    assert_int_equal(id3tag_set_textinfo_utf8(gfp, "TIT", "abc"), -1);
    assert_int_equal(id3tag_set_textinfo_utf16(gfp, "WO", u_text), -1);
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "TPE1", "artist"), 0);
    sz = get_v2(gfp);
    assert_true(walk_v2(sz, "TPE1", NULL) > 0);
    assert_int_equal(walk_v2(sz, NULL, NULL), sz);
}

/**
 * @brief Checks that an empty URL frame adds no bytes to the tag, through any
 *        of the setters.
 *
 * @param state the encoder instance of the fixture.
 */
static void
test_v2_empty_url_frame_adds_nothing(void **state)
{
    static const unsigned short u_bom[] = { 0xFEFF, 0 };
    lame_t gfp = (lame_t) *state;
    size_t sz;
    assert_int_equal(id3tag_set_textinfo_latin1(gfp, "TPE1", "artist"), 0);
    (void) id3tag_set_textinfo_latin1(gfp, "WOAR", "");
    (void) id3tag_set_textinfo_utf16(gfp, "WOAF", u_bom);
    (void) id3tag_set_fieldvalue(gfp, "WXXX==");
    sz = get_v2(gfp);
    assert_true(walk_v2(sz, "TPE1", NULL) > 0);
    assert_int_equal(walk_v2(sz, NULL, NULL), sz);
}

/**
 * @brief Checks that an ID3v2.4 tag uses synchsafe sizes for frames of 128
 *        bytes and more. A reader then finds each of these frames and the
 *        frame after them.
 *
 * The test writes one long frame through each of the four frame writers:
 * text, comment, URL and picture. Each writer writes its own size field.
 *
 * @param state the encoder instance of the fixture.
 */
static void
test_v2_4_long_frames_sized_synchsafe(void **state)
{
    lame_t gfp = (lame_t) *state;
    char text[301], url[308];
    unsigned char picture[300];
    size_t sz, len = 0;
    memset(text, 'T', 300);
    text[300] = 0;
    memcpy(url, "WXXX=d=", 7);
    memset(url + 7, 'u', 300);
    url[307] = 0;
    memset(picture, 0, sizeof picture);
    memcpy(picture, "\x89PNG", 4);
    id3tag_v2_4_UTF8_only(gfp);
    assert_int_equal(id3tag_set_textinfo_utf8(gfp, "TIT2", text), 0);
    assert_int_equal(id3tag_set_comment_utf8(gfp, "eng", "d", text), 0);
    assert_int_equal(id3tag_set_fieldvalue_utf8(gfp, url), 0);
    assert_int_equal(id3tag_set_albumart(gfp, (const char *) picture, sizeof picture), 0);
    assert_int_equal(id3tag_set_textinfo_utf8(gfp, "TPE1", "artist"), 0);
    sz = get_v2(gfp);
    assert_int_equal(tagbuf[3], 4);
    assert_true(walk_v2(sz, "TIT2", &len) > 0);
    assert_true(len > 300);
    assert_true(walk_v2(sz, "COMM", &len) > 0);
    assert_true(len > 300);
    assert_true(walk_v2(sz, "WXXX", &len) > 0);
    assert_true(len > 300);
    assert_true(walk_v2(sz, "APIC", &len) > 0);
    assert_true(len > 300);
    assert_true(walk_v2(sz, "TPE1", NULL) > 0);
    assert_int_equal(walk_v2(sz, NULL, NULL), sz);
}

/* --- fixture ----------------------------------------------------------- */


/** @brief Registers and runs the id3tag API test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        LAME_FIXTURE_TEST(test_v2_title_latin1),
        LAME_FIXTURE_TEST(test_v2_textinfo_utf8),
        LAME_FIXTURE_TEST(test_v2_textinfo_utf16),
        LAME_FIXTURE_TEST(test_v2_comment_utf8),
        LAME_FIXTURE_TEST(test_v2_comment_utf16),
        LAME_FIXTURE_TEST(test_v2_fieldvalue_latin1),
        LAME_FIXTURE_TEST(test_v2_fieldvalue_utf16),
        LAME_FIXTURE_TEST(test_v2_fieldvalue_utf8),
        LAME_FIXTURE_TEST(test_v2_fieldvalue_utf8_malformed),
        LAME_FIXTURE_TEST(test_v2_prefix_descriptions_stay_apart),
        LAME_FIXTURE_TEST(test_v2_same_description_replaces_frame),
        LAME_FIXTURE_TEST(test_v2_empty_description_keeps_its_comment),
        LAME_FIXTURE_TEST(test_v2_prefix_descriptions_utf16),
        LAME_FIXTURE_TEST(test_v2_utf16_absent_description),
        LAME_FIXTURE_TEST(test_v2_genre),
        LAME_FIXTURE_TEST(test_v1_basic),
        LAME_FIXTURE_TEST(test_v1_only_suppresses_v2),
        LAME_FIXTURE_TEST(test_v2_only_suppresses_v1),
        LAME_FIXTURE_TEST(test_v2_size_over_synchsafe_limit_rejected),
        LAME_FIXTURE_TEST(test_v2_albumart_over_synchsafe_limit_rejected),
        LAME_FIXTURE_TEST(test_v2_size_within_limit_written),
        LAME_FIXTURE_TEST(test_v2_playlength_beyond_32bit),
        LAME_FIXTURE_TEST(test_genre_list_enumerates_every_genre),
        LAME_FIXTURE_TEST(test_genre_list_null_handler),
        LAME_FIXTURE_TEST(test_init_discards_previous_fields),
        LAME_FIXTURE_TEST(test_add_v2_4_utf8_selects_version_4),
        LAME_FIXTURE_TEST(test_v2_4_utf8_only_suppresses_v1),
        LAME_FIXTURE_TEST(test_space_v1_pads_with_spaces),
        LAME_FIXTURE_TEST(test_space_v1_cancels_v2_only),
        LAME_FIXTURE_TEST(test_pad_v2_equals_set_pad_128),
        LAME_FIXTURE_TEST(test_set_comment_writes_comm),
        LAME_FIXTURE_TEST(test_set_comment_latin1),
        LAME_FIXTURE_TEST(test_textinfo_ucs2_matches_utf16),
        LAME_FIXTURE_TEST(test_comment_ucs2_matches_utf16),
        LAME_FIXTURE_TEST(test_fieldvalue_ucs2_matches_utf16),
        LAME_FIXTURE_TEST(test_textinfo_latin1),
        LAME_FIXTURE_TEST(test_set_track),
        LAME_FIXTURE_TEST(test_set_track_with_total),
        LAME_FIXTURE_TEST(test_v2_comment_one_character_language),
        LAME_FIXTURE_TEST(test_v2_comment_two_character_language),
        LAME_FIXTURE_TEST(test_v2_short_frame_id_refused),
        LAME_FIXTURE_TEST(test_v2_empty_url_frame_adds_nothing),
        LAME_FIXTURE_TEST(test_v2_4_long_frames_sized_synchsafe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
