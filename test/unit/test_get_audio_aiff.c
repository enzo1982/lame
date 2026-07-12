/**
 * @file
 * @ingroup unit_tests
 * @brief Regression test for the AIFF FORM-size integer-underflow guard in
 *        @c parse_aiff_header() (@c frontend/get_audio.c).
 *
 * A crafted AIFF/AIFC whose FORM chunk size is < 4 underflowed
 * @c ui32_ChunkSize on the first "- 4" accounting step, wrapping it to ~4.29e9
 * and driving the chunk loop through ~1e9 iterations (one @c fread each) before
 * returning -1. The guard rejects such a file up front.
 *
 * @c parse_aiff_header() is static, so the reader is compiled directly into the
 * test. @c fread() is wrapped because its return value alone cannot distinguish
 * the fix from the bug (both end at -1): once armed, exceeding a fixed read
 * budget @c longjmp()s out, so a regressed (spinning) build fails fast instead
 * of hanging the suite.
 *
 * The test is host byte-order independent. All multi-byte fields are written in
 * AIFF's native big-endian on-disk order (via @c put_be32 and the byte array
 * below), and @c get_audio.c reads them back byte by byte using value
 * arithmetic rather than memory-layout reads, so the fixtures and the code
 * under test behave identically on big- and little-endian hosts (no POSIX
 * @c <endian.h> conversion is needed or used).
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* The AIFF parser is independent of the bundled MP3 decoder, so compile
   get_audio.c's core reader without those code paths. This drops the
   <mpg123.h> / "mpglib/mpglib.h" includes (the latter needs an in-tree
   include layout the frontend gets via -I$(top_srcdir) but a unit test in a
   separate build tree would not) and keeps the test identical whether or not
   the project was configured with the decoder. get_audio.c re-includes
   config.h, but its include guard makes that a no-op, so these stay undefined. */
#undef HAVE_MPG123
#undef HAVE_MPGLIB

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

/* the code under test (pulls in the static parse_aiff_header + helpers) */
#include "get_audio.c"

/* --- fread spin-trap --------------------------------------------------- */

extern size_t __real_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);

#define FREAD_BUDGET 100        /* a valid header needs ~15 reads */
static unsigned long fread_calls;
static int           spin_trap_armed;
static jmp_buf       spin_trap;

/**
 * @brief fread() interposer that traps a runaway parser.
 *
 * While armed, once the call count exceeds ::FREAD_BUDGET it @c longjmp()s to
 * ::spin_trap instead of reading; otherwise it forwards to the real fread().
 */
size_t
__wrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (spin_trap_armed && ++fread_calls > FREAD_BUDGET) {
        spin_trap_armed = 0;
        longjmp(spin_trap, 1);
    }
    return __real_fread(ptr, size, nmemb, stream);
}

/* --- helpers ----------------------------------------------------------- */

/**
 * @brief Builds a rewound temp FILE* holding @p bytes.
 *
 * The stream represents the input positioned right after the 4-byte "FORM"
 * magic, which is where @c parse_aiff_header() begins reading.
 * @param bytes the post-"FORM" header bytes.
 * @param n     number of bytes.
 * @return an open, rewound temp stream.
 */
static FILE *
aiff_stream(const unsigned char *bytes, size_t n)
{
    FILE *f = tmpfile();
    assert_non_null(f);
    if (n > 0)
        assert_int_equal(fwrite(bytes, 1, n, f), n);
    rewind(f);
    return f;
}

/** @brief Writes @p v into @p p as 4 big-endian bytes (AIFF on-disk order). */
static void
put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) (v);
}

/* --- tests ------------------------------------------------------------- */

/**
 * @brief FORM sizes 0..3 must be rejected immediately, without spinning.
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_undersized_form_size_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    uint32_t fs;

    for (fs = 0; fs < 4; ++fs) {
        unsigned char hdr[8];
        FILE   *sf;

        put_be32(hdr, fs);              /* FORM chunk size = 0..3 */
        memcpy(hdr + 4, "AIFF", 4);     /* form type */
        sf = aiff_stream(hdr, sizeof hdr);

        fread_calls = 0;
        spin_trap_armed = 1;
        if (setjmp(spin_trap) == 0) {
            int r = parse_aiff_header(gfp, sf);
            spin_trap_armed = 0;
            assert_int_equal(r, -1);    /* rejected, not parsed */
        } else {
            /* budget exceeded => the parser is spinning */
            fail_msg("parse_aiff_header spun on FORM size %u: the "
                     "underflow guard is missing", (unsigned) fs);
        }
        fclose(sf);
    }
}

/**
 * @brief A well-formed minimal AIFF must still be accepted (guard is narrow).
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_valid_aiff_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    /* post-"FORM" bytes: size + "AIFF" + COMM(18) + SSND(8) = 46 bytes */
    static const unsigned char aiff[] = {
        0x00, 0x00, 0x00, 0x2e,                         /* FORM size = 46 */
        'A', 'I', 'F', 'F',
        'C', 'O', 'M', 'M',
        0x00, 0x00, 0x00, 0x12,                         /* cksize = 18 */
        0x00, 0x02,                                     /* numChannels = 2 */
        0x00, 0x00, 0x00, 0x04,                         /* numSampleFrames */
        0x00, 0x10,                                     /* sampleSize = 16 */
        0x40, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 44100 */
        'S', 'S', 'N', 'D',
        0x00, 0x00, 0x00, 0x08,                         /* cksize = 8 */
        0x00, 0x00, 0x00, 0x00,                         /* offset = 0 */
        0x00, 0x00, 0x00, 0x00                          /* blockSize = 0 */
    };
    FILE *sf = aiff_stream(aiff, sizeof aiff);
    int   r;

    fread_calls = 0;
    spin_trap_armed = 1;
    if (setjmp(spin_trap) == 0) {
        r = parse_aiff_header(gfp, sf);
        spin_trap_armed = 0;
        assert_int_equal(r, 1);         /* accepted */
    } else {
        fail_msg("parse_aiff_header spun on a valid AIFF header");
    }
    fclose(sf);
}

/* --- fixture ----------------------------------------------------------- */

/** @brief Per-test fixture: creates a @c lame_t into @p state. */
static int
setup_lame(void **state)
{
    lame_t gfp = lame_init();
    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

/** @brief Per-test fixture teardown: closes the @c lame_t from @p state. */
static int
teardown_lame(void **state)
{
    lame_close((lame_t) *state);
    return 0;
}

/** @brief Registers and runs the AIFF-underflow test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_undersized_form_size_rejected,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_valid_aiff_accepted,
                                        setup_lame, teardown_lame),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
