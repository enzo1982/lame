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
 * The second group covers what the first left out: the calls that select a tag
 * version or an encoding rather than set a field, the ones that reset or pad,
 * the genre enumeration, the track number, and the three deprecated UCS-2
 * setters. Those three are documented as aliases, so each is checked by
 * building the same tag through the alias and through its target and comparing
 * the two byte for byte - a test that only looked for the frame would pass on
 * an alias wired to the wrong function.
 *
 * The third group covers the descriptions that TXXX, WXXX and COMM frames are
 * keyed by, in both encodings. Frames are counted there rather than looked for,
 * because what is at stake is one of them being folded into another, and the
 * surviving frame answers a search for its own text either way.
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
#include <ctype.h>
#include <limits.h>

#include <cmocka.h>

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

/** @brief How many times the byte string @p needle occurs in @p hay. */
static size_t
mem_count(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t nn = strlen(needle);
    size_t i, n = 0;
    if (nn == 0 || nn > hn)
        return 0;
    for (i = 0; i + nn <= hn; ++i) {
        if (memcmp(hay + i, needle, nn) == 0)
            ++n;
    }
    return n;
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

/* --- ID3v2 descriptors ------------------------------------------------- */

/**
 * @brief Descriptions where one begins the other name two frames, not one.
 *
 * A TXXX frame is keyed by its description, so "foo" and "foobar" are two
 * frames and both texts have to survive. This is the route the --tv option
 * takes: id3tag_set_fieldvalue() splits "TXXX=foo=alpha" into the frame id,
 * the description and the text.
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
 * @brief The same description twice still replaces the frame.
 *
 * The control for the case above: descriptions are compared so that a repeated
 * one updates its frame, and a test that only counted frames would pass just as
 * well against a comparison that never matched anything.
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
 * @brief An undescribed comment is not replaced by a described one.
 *
 * The empty description is the limit case of the one above, since it begins
 * every other description. A COMM frame is keyed by language and description
 * together, so these are two frames.
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
 * @brief UTF-16 descriptions are compared the same way.
 *
 * The UTF-16 descriptions go through a comparison of their own, which rejects a
 * Latin-1 frame outright, so it needs its own case rather than the Latin-1 one
 * taken on trust. The byte order marker each string carries becomes part of the
 * stored description and is common to both, leaving the prefix relation intact.
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
 * @brief A UTF-16 comment with no description at all keeps its own frame.
 *
 * The UTF-16 setters accept an absent description, which is the one way the
 * comparison is reached with nothing to compare against. Two undescribed
 * comments are the same frame and the second replaces the first; a described
 * one is a frame of its own.
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

/* --- ID3v2 auto play-length (TLEN) ------------------------------------- */

/**
 * @brief A play-length past 2^32-1 ms is written in full, not clamped.
 *
 * The TLEN value is derived from num_samples, so a long enough declared length
 * yields a duration beyond a 32-bit millisecond count. This is only
 * representable where unsigned long is wider than 32 bits (num_samples must
 * hold the sample count), so it is skipped elsewhere. The length is set on the
 * config, not encoded, so no audio is processed.
 *
 * The skip is decided by the preprocessor rather than at run time, because the
 * problem is a compile-time one: the sample count is a constant too large for
 * a 32-bit unsigned long, and a run-time guard leaves it in the translation
 * unit to be truncated and warned about on every build that cannot hold it.
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
 * @brief Compare two genre names the way the genre list is ordered.
 *
 * Case-insensitive, and skipping everything that is not a letter or a digit.
 * The second half is not a convenience: the list is sorted over the letters
 * alone, so a plain comparison reports five inversions that are not
 * inversions - "Classical" before "Classic Rock", "Eurodance" before
 * "Euro-House", "Folklore" before "Folk-Rock", "Hardcore" before "Hard Rock",
 * "Rave" before "R&B". Every one of them is in order once the space, hyphen
 * or ampersand is dropped. Written locally rather than with strcasecmp, which
 * is not available everywhere this builds.
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
 * @p seen counts how often each genre number was reported, which is what lets
 * the test assert the list is a *permutation* - every genre exactly once -
 * rather than merely of the right length.
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
 * @brief The genre list is enumerated once per genre, in alphabetical order.
 *
 * The count is asserted exactly rather than as a lower bound. 148 is the
 * ID3v1 genre set plus the Winamp extensions, which is what the format
 * defines; a change to it is a deliberate act and should have to update this
 * line. The permutation check is the stronger half - it fails on a genre
 * reported twice or skipped, which a count alone would not see.
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

/** @brief A NULL handler is accepted and does nothing. */
static void
test_genre_list_null_handler(void **state)
{
    (void) state;
    id3tag_genre_list(NULL, NULL);   /* must not crash */
}

/* --- resetting the tag state ------------------------------------------- */

/**
 * @brief id3tag_init() returns the instance to its initial tagging state.
 *
 * Checked by writing a second, different title afterwards and asking for the
 * tag once: the new title must be there and the old one gone. Asking whether
 * an emptied instance still emits a tag at all would test something else.
 *
 * The three fields that are not strings are what make this test worth having.
 * The strings are released by the same helper lame_close() uses, so a test
 * that only looked at those would still pass with the rest of the reset
 * removed; the track number, the genre and the request for an ID3v2 tag are
 * cleared by the reset alone, and are asserted here for that reason.
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

/** @brief The ID3v2 header's version byte, or -1 if there is no tag. */
static int
v2_version_byte(lame_t gfp)
{
    size_t sz = get_v2(gfp);
    if (sz < 4 || memcmp(tagbuf, "ID3", 3) != 0)
        return -1;
    return tagbuf[3];
}

/**
 * @brief id3tag_add_v2_4_UTF8() selects version 2.4 and keeps the ID3v1 tag.
 *
 * The version is read out of the tag header rather than inferred, and the
 * default is checked in the same test so that "it says 4" is known to be a
 * consequence of the call and not of the format.
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

/** @brief id3tag_v2_4_UTF8_only() selects version 2.4 and drops the v1 tag. */
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

/** @brief Count the bytes equal to @p c in tagbuf[from, to). */
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
 * @brief id3tag_space_v1() fills the unused ID3v1 bytes with spaces.
 *
 * The title occupies bytes 3..32 of the 128-byte block. A three-character
 * title leaves 27 bytes, and the two builds are compared against each other so
 * the assertion is about the call and not about a guess at the layout.
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

/** @brief id3tag_space_v1() also cancels a previous id3tag_v2_only(). */
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
 * @brief id3tag_pad_v2() is id3tag_set_pad() with the documented 128 bytes.
 *
 * Asserted as an equivalence between two instances rather than as a size, so
 * the test says what the function promises and does not also pin the size of
 * an ordinary tag.
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

/** @brief The simple comment setter writes a COMM frame with the text. */
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

/** @brief The Latin-1 comment setter records the language and description. */
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

/** @brief id3tag_set_textinfo_ucs2() == id3tag_set_textinfo_utf16(). */
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

/** @brief id3tag_set_comment_ucs2() == id3tag_set_comment_utf16(). */
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

/** @brief id3tag_set_fieldvalue_ucs2() == id3tag_set_fieldvalue_utf16(). */
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
 * @brief id3tag_set_textinfo_latin1() writes the named frame, and refuses.
 *
 * Both documented refusals are exercised: an identifier that is not a valid
 * frame id, and a valid identifier for a frame this function cannot write.
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
 * @brief id3tag_set_track() reports only whether ID3v1 could hold the number.
 *
 * The documented catch: a number ID3v1 cannot express returns -1 while still
 * being written to the ID3v2 frame, so -1 is not a failure. Byte 126 of the
 * ID3v1 block is the ID3v1.1 track byte, which is where the fitting case has
 * to show up.
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

/** @brief The "number/total" form keeps the total in the ID3v2 frame. */
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
        ID3_TEST(test_v2_prefix_descriptions_stay_apart),
        ID3_TEST(test_v2_same_description_replaces_frame),
        ID3_TEST(test_v2_empty_description_keeps_its_comment),
        ID3_TEST(test_v2_prefix_descriptions_utf16),
        ID3_TEST(test_v2_utf16_absent_description),
        ID3_TEST(test_v2_genre),
        ID3_TEST(test_v1_basic),
        ID3_TEST(test_v1_only_suppresses_v2),
        ID3_TEST(test_v2_only_suppresses_v1),
        ID3_TEST(test_v2_size_over_synchsafe_limit_rejected),
        ID3_TEST(test_v2_albumart_over_synchsafe_limit_rejected),
        ID3_TEST(test_v2_size_within_limit_written),
        ID3_TEST(test_v2_playlength_beyond_32bit),
        ID3_TEST(test_genre_list_enumerates_every_genre),
        ID3_TEST(test_genre_list_null_handler),
        ID3_TEST(test_init_discards_previous_fields),
        ID3_TEST(test_add_v2_4_utf8_selects_version_4),
        ID3_TEST(test_v2_4_utf8_only_suppresses_v1),
        ID3_TEST(test_space_v1_pads_with_spaces),
        ID3_TEST(test_space_v1_cancels_v2_only),
        ID3_TEST(test_pad_v2_equals_set_pad_128),
        ID3_TEST(test_set_comment_writes_comm),
        ID3_TEST(test_set_comment_latin1),
        ID3_TEST(test_textinfo_ucs2_matches_utf16),
        ID3_TEST(test_comment_ucs2_matches_utf16),
        ID3_TEST(test_fieldvalue_ucs2_matches_utf16),
        ID3_TEST(test_textinfo_latin1),
        ID3_TEST(test_set_track),
        ID3_TEST(test_set_track_with_total),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
