/**
 * @file
 * @ingroup unit_tests
 * @brief Regression tests for two checks in @c parse_aiff_header()
 *        (@c frontend/get_audio.c) that reject a crafted AIFF header.
 *
 * The first check rejects a FORM chunk size below 4. The parser subtracts 4
 * from @c ui32_ChunkSize before the chunk loop. Without the check, a size
 * below 4 wraps to about 4.29e9. The chunk loop then reads chunks until the
 * input ends.
 *
 * The second check rejects a sample rate that does not fit in an @c int. The
 * COMM chunk stores the sample rate as an 80-bit extended float. The parser
 * converts it to an @c int for lame. This conversion is undefined for a value
 * outside the range of @c int: an infinity, a finite value that is too large,
 * or a negative value. The parser rejects each of them as a malformed field.
 * The test reads the rates from a stream at run time. It does not write them
 * as constants. So the fast floating-point math of the frontend build cannot
 * fold a rate away before the parser sees it.
 *
 * @c parse_aiff_header() is static, so the test compiles the reader into
 * itself. The test also wraps @c fread(). The return value alone cannot tell
 * the fixed code from the faulty code, because both return -1. When the trap
 * is armed, a call past a fixed read budget calls @c longjmp(). So a parser
 * that loops fails the test fast and does not hang the suite.
 *
 * The test does not depend on the byte order of the host. It writes all
 * multi-byte fields in big-endian order, the byte order of AIFF files. It does
 * this with @c put_be32 and the byte array below. @c get_audio.c reads them
 * back byte by byte, with arithmetic on values, not with reads of the memory
 * layout. So the fixtures and the code under test behave the same on
 * big-endian and little-endian hosts. The test does not need or use the POSIX
 * @c <endian.h> conversions.
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
 * @brief Replaces fread() and stops a parser that loops.
 *
 * When the trap is armed and the call count exceeds ::FREAD_BUDGET, it calls
 * @c longjmp() to ::spin_trap and does not read. In all other cases it calls
 * the real fread().
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
 * @brief Creates a temporary stream that contains @p bytes.
 *
 * The stream is the input from the point right after the 4-byte "FORM" magic.
 * @c parse_aiff_header() starts to read at this point.
 * @param bytes the header bytes after "FORM".
 * @param n     number of bytes.
 * @return an open temporary stream, positioned at its start.
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

/* --- fixtures ---------------------------------------------------------- */

/** @brief Offset of the 80-bit extended sample rate within ::valid_aiff. */
#define AIFF_RATE_OFFSET 24

/**
 * @brief A minimal well-formed AIFF, from the byte after the "FORM" magic.
 *
 * The array has 50 bytes: the FORM size, "AIFF", a COMM chunk with 18 data
 * bytes and an SSND chunk with 8 data bytes. The FORM size is 46, because it
 * does not count its own 4 bytes.
 */
static const unsigned char valid_aiff[] = {
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

/**
 * @brief The sample rate in ::valid_aiff.
 *
 * A test compares it with the bytes at ::AIFF_RATE_OFFSET. This checks that
 * the offset is correct.
 */
static const unsigned char rate_44100[10] = {
    0x40, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* --- tests ------------------------------------------------------------- */

/**
 * @brief Checks that the parser rejects FORM sizes 0 to 3 at once, without a
 *        loop.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_undersized_form_size_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    /* Modified between setjmp() and the longjmp() below, and read on the
       far side of it, so it has to survive the jump. */
    volatile uint32_t fs;

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
 * @brief Checks that the parser accepts a minimal well-formed AIFF.
 *
 * This shows that the checks do not reject a valid file.
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_valid_aiff_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    FILE *sf = aiff_stream(valid_aiff, sizeof valid_aiff);
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

/**
 * @brief Checks that the parser rejects a sample rate that does not fit in an
 *        @c int.
 *
 * Each case is ::valid_aiff with only the rate field replaced. The header is
 * valid in every other way, so the rate is the only reason to reject it.
 * Without the range check, the parser converts the value with @c (int). The
 * result of this conversion is undefined. The header then parses to the end,
 * and the parser returns 0 or 1, not -1.
 *
 * @param state fixture state that contains an initialized encoder instance.
 */
static void
test_unrepresentable_sample_rate_rejected(void **state)
{
    lame_t  gfp = (lame_t) *state;
    static const struct {
        char const *what;
        unsigned char rate[10];
    } cases[] = {
        /* an all-ones exponent is an infinity */
        { "infinity",
          { 0x7f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        /* the same exponent with a mantissa, which the reader also reads as one */
        { "not a number",
          { 0x7f, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        /* finite, but 2^40 Hz is far past INT_MAX */
        { "2^40 Hz",
          { 0x40, 0x27, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        /* an ordinary 44100 with the sign bit set */
        { "-44100 Hz",
          { 0xc0, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }
    };
    size_t  i;

    /* the rate field has not moved out from under this test */
    assert_memory_equal(valid_aiff + AIFF_RATE_OFFSET, rate_44100,
                        sizeof rate_44100);

    /* a valid FORM size bounds the chunk loop, so the spin trap is not needed */
    spin_trap_armed = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned char hdr[sizeof valid_aiff];
        FILE   *sf;
        int     r;

        memcpy(hdr, valid_aiff, sizeof hdr);
        memcpy(hdr + AIFF_RATE_OFFSET, cases[i].rate, sizeof cases[i].rate);
        sf = aiff_stream(hdr, sizeof hdr);

        r = parse_aiff_header(gfp, sf);
        if (r != -1) {
            fail_msg("a sample rate of %s was accepted (returned %d)",
                     cases[i].what, r);
        }
        fclose(sf);
    }
}

/* --- fixture ----------------------------------------------------------- */

/** @brief Per-test setup: creates an encoder instance and stores it in @p state. */
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

/** @brief Registers and runs the AIFF header-rejection test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_undersized_form_size_rejected,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_valid_aiff_accepted,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_unrepresentable_sample_rate_rejected,
                                        setup_lame, teardown_lame),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
