/**
 * @file
 * @ingroup unit_tests
 * @brief Regression tests for the sample rate a WAVE header may declare
 *        (@c parse_wave_header(), @c frontend/get_audio.c).
 *
 * The @c fmt chunk carries @c nSamplesPerSec as 32 unsigned bits while the
 * encoder takes the rate as an @c int, so the upper half of the field's range
 * cannot be passed on. The parser refuses those headers itself, while the
 * declared value is still intact; converting first and leaving the refusal to
 * the "not below 1" check reports a rate that is nowhere in the file.
 *
 * The accepted cases are as much of the point as the rejected ones. LAME
 * resamples any input rate down to one the format allows, and rates far above
 * anything a consumer format uses are ordinary studio and mastering material -
 * so the tests below pin that 192 kHz, 384 kHz and 768 kHz stay acceptable, and
 * that the boundary sits at @c INT_MAX rather than at some lower number nobody
 * measured.
 *
 * @c parse_wave_header() is static, so the reader is compiled directly into the
 * test, the same arrangement @c test_get_audio_aiff.c uses.
 *
 * The test is host byte-order independent: every field is written to the
 * fixture a byte at a time in WAVE's on-disk order (chunk identifiers
 * big-endian, numeric fields little-endian), which is how @c get_audio.c reads
 * them back.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* The WAVE parser is independent of the bundled MP3 decoder, so compile
   get_audio.c's core reader without those code paths - see the note in
   test_get_audio_aiff.c, which does the same for the same reason. */
#undef HAVE_MPG123

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

/* the code under test (pulls in the static parse_wave_header + helpers) */
#include "get_audio.c"

/* --- helpers ----------------------------------------------------------- */

/**
 * @brief Builds a rewound temp FILE* holding @p bytes.
 *
 * The stream represents the input positioned right after the 4-byte "RIFF"
 * magic, which is where @c parse_wave_header() begins reading.
 * @param bytes the post-"RIFF" header bytes.
 * @param n     number of bytes.
 * @return an open, rewound temp stream.
 */
static FILE *
wav_stream(const unsigned char *bytes, size_t n)
{
    FILE *f = tmpfile();
    assert_non_null(f);
    if (n > 0)
        assert_int_equal(fwrite(bytes, 1, n, f), n);
    rewind(f);
    return f;
}

/** @brief Writes @p v into @p p as 4 little-endian bytes (WAVE field order). */
static void
put_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) (v);
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

/* --- fixtures ---------------------------------------------------------- */

/** @brief Offset of nSamplesPerSec within ::valid_wav. */
#define WAV_RATE_OFFSET 20

/** @brief The rate ::valid_wav declares, so ::WAV_RATE_OFFSET stays anchored. */
#define WAV_FIXTURE_RATE 44100u

/**
 * @brief A minimal well-formed 16-bit stereo WAVE, from just past "RIFF".
 *
 * Post-"RIFF" bytes: size + "WAVE" + "fmt " + cksize + 16 bytes of fmt +
 * "data" + size = 36 bytes. Identifiers are big-endian on disk, numeric
 * fields little-endian, which is what the reader expects.
 */
static const unsigned char valid_wav[] = {
    0x00, 0x00, 0x00, 0x28,                 /* RIFF size (only tested > 0)  */
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0x00, 0x00, 0x00,                 /* fmt cksize = 16              */
    0x01, 0x00,                             /* wFormatTag = WAVE_FORMAT_PCM */
    0x02, 0x00,                             /* nChannels = 2                */
    0x44, 0xac, 0x00, 0x00,                 /* nSamplesPerSec = 44100       */
    0x10, 0xb1, 0x02, 0x00,                 /* nAvgBytesPerSec = 176400     */
    0x04, 0x00,                             /* nBlockAlign = 4              */
    0x10, 0x00,                             /* wBitsPerSample = 16          */
    'd', 'a', 't', 'a',
    0x00, 0x04, 0x00, 0x00                  /* data size = 1024             */
};

/**
 * @brief Builds ::valid_wav with only nSamplesPerSec replaced.
 *
 * The byte rate is deliberately left as the fixture's own. It only restates
 * what the other fields say, the reader cross-checks it rather than using it,
 * and a rate near the top of the field's range has no expressible byte rate
 * anyway - @c nAvgBytesPerSec is itself 32 bits, so it overflows above
 * 1073741823 Hz at this block alignment. That is a limit of the file format
 * and not something the encoder is being asked about here.
 *
 * @param hdr  destination, ::valid_wav sized.
 * @param rate the rate to declare.
 */
static void
build_wav_with_rate(unsigned char *hdr, uint32_t rate)
{
    memcpy(hdr, valid_wav, sizeof valid_wav);
    put_le32(hdr + WAV_RATE_OFFSET, rate);
}

/* --- tests ------------------------------------------------------------- */

/**
 * @brief The unmodified fixture parses, so a rejection below means the rate.
 *
 * Without this every rejection test would also pass against a fixture that was
 * malformed for some unrelated reason, which is the way a rate test stops
 * testing the rate.
 *
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_valid_wav_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    FILE  *sf = wav_stream(valid_wav, sizeof valid_wav);

    assert_int_equal(parse_wave_header(gfp, sf), 1);
    /* the rate field has not moved out from under the tests below */
    assert_int_equal(lame_get_in_samplerate(gfp), (int) WAV_FIXTURE_RATE);
    fclose(sf);
}

/**
 * @brief High rates that LAME resamples down must keep being accepted.
 *
 * Each is a real format: 192 kHz is DVD-Audio and every professional audio
 * interface, 352.8 kHz is DXD, 384 kHz its 48 kHz-family sibling. The encoder
 * handles all of them, so the parser must not be the thing that refuses.
 *
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_high_sample_rates_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const uint32_t rates[] = {
        96000u, 192000u, 352800u, 384000u, 768000u, 1536000u, 3072000u
    };
    size_t  i;

    for (i = 0; i < sizeof rates / sizeof rates[0]; ++i) {
        unsigned char hdr[sizeof valid_wav];
        FILE   *sf;
        int     r;

        build_wav_with_rate(hdr, rates[i]);
        sf = wav_stream(hdr, sizeof hdr);
        r = parse_wave_header(gfp, sf);
        if (r != 1) {
            fail_msg("a sample rate of %u Hz was refused (returned %d)",
                     (unsigned int) rates[i], r);
        }
        assert_int_equal(lame_get_in_samplerate(gfp), (int) rates[i]);
        fclose(sf);
    }
}

/**
 * @brief The highest representable rate must be accepted, not refused early.
 *
 * The limit is the width of the type the encoder takes the rate in, so it
 * belongs at @c INT_MAX. Pinning both sides of that boundary is what stops a
 * later change from quietly lowering it to a number that merely looks tidy.
 *
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_boundary_rate_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    unsigned char hdr[sizeof valid_wav];
    FILE  *sf;

    build_wav_with_rate(hdr, (uint32_t) INT_MAX);
    sf = wav_stream(hdr, sizeof hdr);
    assert_int_equal(parse_wave_header(gfp, sf), 1);
    assert_int_equal(lame_get_in_samplerate(gfp), INT_MAX);
    fclose(sf);
}

/**
 * @brief A rate the encoder cannot represent must be refused as malformed.
 *
 * Each case is ::valid_wav with only the rate replaced, so the header is
 * acceptable in every other respect and the rate is the sole reason for the
 * refusal. Without the range check the value is converted to @c int first,
 * which lands negative and is then refused by a guard meant to catch zero -
 * the file is still rejected, so the return value alone does not separate the
 * two. What separates them is that the conversion happens at all.
 *
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_unrepresentable_sample_rate_rejected(void **state)
{
    lame_t  gfp = (lame_t) *state;
    static const struct {
        char const *what;
        uint32_t    rate;
    } cases[] = {
        { "INT_MAX + 1",     2147483648u },
        { "3 GHz",           3000000000u },
        { "the whole field", 4294967295u }
    };
    size_t  i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned char hdr[sizeof valid_wav];
        FILE   *sf;
        int     r;

        build_wav_with_rate(hdr, cases[i].rate);
        sf = wav_stream(hdr, sizeof hdr);
        r = parse_wave_header(gfp, sf);
        if (r != -1) {
            fail_msg("a sample rate of %s (%u) was accepted (returned %d)",
                     cases[i].what, (unsigned int) cases[i].rate, r);
        }
        fclose(sf);
    }
}

/**
 * @brief A rate of zero must still be refused.
 *
 * The pre-existing guard for this lives in the library rather than the parser,
 * so it is worth holding: a range check written only for the upper end could
 * replace it and lose the lower one without any test noticing.
 *
 * @param state fixture state holding an initialised @c lame_t.
 */
static void
test_zero_sample_rate_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    unsigned char hdr[sizeof valid_wav];
    FILE  *sf;

    build_wav_with_rate(hdr, 0u);
    sf = wav_stream(hdr, sizeof hdr);
    assert_int_not_equal(parse_wave_header(gfp, sf), 1);
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
    /* The parser consults the forced input rate before the file's own, so a
       stale value here would mask every rate under test. */
    global_reader.input_samplerate = 0;
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

/** @brief Registers and runs the WAVE sample-rate test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_valid_wav_accepted,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_high_sample_rates_accepted,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_boundary_rate_accepted,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_unrepresentable_sample_rate_rejected,
                                        setup_lame, teardown_lame),
        cmocka_unit_test_setup_teardown(test_zero_sample_rate_rejected,
                                        setup_lame, teardown_lame),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
