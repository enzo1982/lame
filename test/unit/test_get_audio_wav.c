/**
 * @file
 * @ingroup unit_tests
 * @brief Regression tests for the sample rate and the sample width that a WAVE
 *        header may declare (@c parse_wave_header(), @c frontend/get_audio.c).
 *
 * The @c fmt chunk stores @c nSamplesPerSec as an unsigned 32-bit value. The
 * encoder takes the sample rate as an @c int. So the upper half of the field's
 * range cannot be passed on. The parser rejects these headers itself, while it
 * still has the declared value. If it converts the value first and lets the
 * "not below 1" check reject it, the error message shows a rate that is not in
 * the file.
 *
 * The accepted cases matter as much as the rejected ones. LAME resamples a high
 * input rate down to a rate that MP3 allows. Rates far above any consumer
 * format are normal in studio and mastering work. So the tests check that
 * 192 kHz, 384 kHz and 768 kHz are accepted. They also check that the limit is
 * exactly @c INT_MAX, and not some lower value.
 *
 * Two tests check the other direction: the data size that the decoder writes
 * into the header of a WAV file (@c wav_data_size(), @c WriteWaveHeader()).
 * Two more check the byte order of the samples it writes (@c put_audio16()).
 *
 * @c parse_wave_header() is static. So the test compiles the reader directly
 * into the test, in the same way as @c test_get_audio_aiff.c.
 *
 * The test does not depend on the byte order of the host. It writes every
 * field of the fixture one byte at a time, in the WAVE on-disk order. Chunk
 * identifiers are big-endian, and numeric fields are little-endian.
 * @c get_audio.c reads them back in the same way.
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

#include "test_fixture.h"
#include "test_bytes.h"

/* the code under test (pulls in the static parse_wave_header + helpers) */
#include "get_audio.c"

/* --- fixtures ---------------------------------------------------------- */

/** @brief Offset of nSamplesPerSec within ::valid_wav. */
#define WAV_RATE_OFFSET 20

/** @brief The rate in ::valid_wav. A test uses it to check ::WAV_RATE_OFFSET. */
#define WAV_FIXTURE_RATE 44100u

/**
 * @brief A minimal valid 16-bit stereo WAVE header, starting after "RIFF".
 *
 * @c parse_wave_header() starts to read at this position.
 * The bytes after "RIFF" are: size + "WAVE" + "fmt " + cksize + 16 bytes of
 * fmt + "data" + size = 40 bytes. Identifiers are big-endian on disk, and
 * numeric fields are little-endian. The reader expects this order.
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
 * @brief Builds a copy of ::valid_wav with only nSamplesPerSec replaced.
 *
 * The byte rate stays the fixture's own value, on purpose:
 * - It only repeats what the other fields say.
 * - The reader only compares it with the other fields and does not use it.
 * - A rate near the top of the field's range has no valid byte rate.
 *   @c nAvgBytesPerSec is also 32 bits, so at this block alignment it
 *   overflows above 1073741823 Hz. This is a limit of the file format. These
 *   tests do not test it.
 *
 * @param hdr  destination, ::valid_wav sized.
 * @param rate the rate to declare.
 */
static void
build_wav_with_rate(unsigned char *hdr, uint32_t rate)
{
    memcpy(hdr, valid_wav, sizeof valid_wav);
    put_le(hdr + WAV_RATE_OFFSET, rate, 4);
}

/* --- tests ------------------------------------------------------------- */

/**
 * @brief Checks that the unchanged fixture parses. A rejection in a later test
 *        is then caused by the rate.
 *
 * Without this test, every rejection test also passes when the fixture is
 * broken for some other reason. The rate tests then do not test the rate.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_valid_wav_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    FILE  *sf = bytes_stream(valid_wav, sizeof valid_wav);

    assert_int_equal(parse_wave_header(gfp, sf), 1);
    /* the rate field has not moved out from under the tests below */
    assert_int_equal(lame_get_in_samplerate(gfp), (int) WAV_FIXTURE_RATE);
    fclose(sf);
}

/**
 * @brief Checks that high rates, which LAME resamples down, are accepted.
 *
 * Several of them are real formats. 192 kHz is a DVD-Audio rate, and every
 * professional audio interface supports it. 352.8 kHz is DXD. 384 kHz is the
 * matching rate in the 48 kHz family. The encoder accepts all of these rates.
 * So the parser must not reject them.
 *
 * @param state the fixture state, an initialized encoder instance.
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
        sf = bytes_stream(hdr, sizeof hdr);
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
 * @brief Checks that the highest rate an @c int can store is accepted.
 *
 * The encoder takes the rate as an @c int. So the limit is @c INT_MAX. This
 * test checks @c INT_MAX, and the next test checks @c INT_MAX + 1. Together
 * they fail if a change moves the limit to a lower number that only looks
 * neat.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_boundary_rate_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    unsigned char hdr[sizeof valid_wav];
    FILE  *sf;

    build_wav_with_rate(hdr, (uint32_t) INT_MAX);
    sf = bytes_stream(hdr, sizeof hdr);
    assert_int_equal(parse_wave_header(gfp, sf), 1);
    assert_int_equal(lame_get_in_samplerate(gfp), INT_MAX);
    fclose(sf);
}

/**
 * @brief Checks that a rate an @c int cannot store is rejected as a malformed
 *        header (return value -1).
 *
 * Each case is ::valid_wav with only the rate replaced. The header is valid in
 * every other way, so the rate is the only reason for the rejection.
 *
 * Without the range check, the parser converts the value to @c int first. The
 * result is negative. The "not below 1" check then rejects it, and the parser
 * returns 0, not -1. The error message then shows a negative rate that is not
 * in the file. The test requires -1, so it fails without the range check.
 *
 * @param state the fixture state, an initialized encoder instance.
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
        sf = bytes_stream(hdr, sizeof hdr);
        r = parse_wave_header(gfp, sf);
        if (r != -1) {
            fail_msg("a sample rate of %s (%u) was accepted (returned %d)",
                     cases[i].what, (unsigned int) cases[i].rate, r);
        }
        fclose(sf);
    }
}

/**
 * @brief Checks that a rate of zero is rejected.
 *
 * The check for zero is in the library (@c lame_set_in_samplerate()), not in
 * the parser. A range check in the parser that covers only the upper end
 * could replace that path. Without this test, nothing then notices that the
 * lower end is lost.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_zero_sample_rate_rejected(void **state)
{
    lame_t gfp = (lame_t) *state;
    unsigned char hdr[sizeof valid_wav];
    FILE  *sf;

    build_wav_with_rate(hdr, 0u);
    sf = bytes_stream(hdr, sizeof hdr);
    assert_int_not_equal(parse_wave_header(gfp, sf), 1);
    fclose(sf);
}

/* --- bits per sample --------------------------------------------------- */

/** @brief Offset of wFormatTag within ::valid_wav. */
#define WAV_FORMAT_OFFSET 16
/** @brief Offset of wBitsPerSample within ::valid_wav. */
#define WAV_BITS_OFFSET 30

/** @brief One (format tag, sample width) pair for the parser. */
struct wav_width_case {
    uint16_t    tag;    /**< the format tag to declare */
    uint16_t    bits;   /**< the sample width to declare */
    char const *what;   /**< the name of the case in a failure message */
};

/**
 * @brief Builds a copy of ::valid_wav with the format tag and sample width
 *        replaced.
 * @param hdr  destination, ::valid_wav sized.
 * @param tag  the format tag to declare.
 * @param bits the sample width to declare.
 */
static void
build_wav_with_format(unsigned char *hdr, uint16_t tag, uint16_t bits)
{
    memcpy(hdr, valid_wav, sizeof valid_wav);
    put_le(hdr + WAV_FORMAT_OFFSET, tag, 2);
    put_le(hdr + WAV_BITS_OFFSET, bits, 2);
}

/**
 * @brief Checks that the widths the sample reader supports are accepted.
 *
 * These are the four integer widths that the unpacker supports: 8, 16, 24 and
 * 32 bits. The fifth is 32-bit float, which @c read_samples_float() reads. The
 * test fails if a change makes the list of accepted widths stricter than the
 * reader.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_supported_sample_widths_accepted(void **state)
{
    lame_t gfp = (lame_t) *state;
    static const struct {
        uint16_t tag;
        uint16_t bits;
    } cases[] = {
        { WAVE_FORMAT_PCM,        8 },
        { WAVE_FORMAT_PCM,       16 },
        { WAVE_FORMAT_PCM,       24 },
        { WAVE_FORMAT_PCM,       32 },
        { WAVE_FORMAT_IEEE_FLOAT, 32 }
    };
    size_t  i;

    /* the fields have not moved out from under this test */
    assert_int_equal(valid_wav[WAV_FORMAT_OFFSET], WAVE_FORMAT_PCM);
    assert_int_equal(valid_wav[WAV_BITS_OFFSET], 16);

    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned char hdr[sizeof valid_wav];
        FILE   *sf;
        int     r;

        build_wav_with_format(hdr, cases[i].tag, cases[i].bits);
        sf = bytes_stream(hdr, sizeof hdr);
        r = parse_wave_header(gfp, sf);
        if (r != 1) {
            fail_msg("format 0x%04X at %u bits was refused (returned %d)",
                     (unsigned int) cases[i].tag, (unsigned int) cases[i].bits, r);
        }
        fclose(sf);
    }
}

/**
 * @brief Parses each (format, width) pair in a table and requires a rejection
 *        (return value -1).
 * @param gfp   the encoder instance.
 * @param cases the table.
 * @param n     entries in @p cases.
 */
static void
expect_widths_rejected(lame_t gfp, const struct wav_width_case *cases, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i) {
        unsigned char hdr[sizeof valid_wav];
        FILE   *sf;
        int     r;

        build_wav_with_format(hdr, cases[i].tag, cases[i].bits);
        sf = bytes_stream(hdr, sizeof hdr);
        r = parse_wave_header(gfp, sf);
        if (r != -1) {
            fail_msg("%s (format 0x%04X, %u bits) was accepted (returned %d)",
                     cases[i].what, (unsigned int) cases[i].tag,
                     (unsigned int) cases[i].bits, r);
        }
        fclose(sf);
    }
}

/**
 * @brief Checks that the header parser rejects an integer width that the
 *        unpacker does not support.
 *
 * Without the check, most of these widths get to the sample reader. It rejects
 * them at the first read, with a message that names neither the header nor
 * the field. A width of 0 makes the parser divide by zero when it computes the
 * number of samples.
 *
 * This test is separate from the floating point test below, on purpose. A
 * single test stops at its first failure. Against a build without the width
 * check, a combined test then shows only the integer failures. The floating
 * point failures are the more important ones.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_unsupported_integer_widths_rejected(void **state)
{
    static const struct wav_width_case cases[] = {
        { WAVE_FORMAT_PCM,     0, "zero" },
        { WAVE_FORMAT_PCM,     3, "3 bit" },
        { WAVE_FORMAT_PCM,    12, "12 bit" },
        { WAVE_FORMAT_PCM,    64, "64 bit integer" },
        { WAVE_FORMAT_PCM, 65535, "the whole field" }
    };
    expect_widths_rejected((lame_t) *state, cases,
                           sizeof cases / sizeof cases[0]);
}

/**
 * @brief Checks that the header parser rejects a floating point width other
 *        than 32.
 *
 * This is the most important case. Without the check, the float reader reads
 * every 4 bytes of such a file as one 32-bit float. The result is noise, and
 * no step reports an error.
 *
 * @param state the fixture state, an initialized encoder instance.
 */
static void
test_unsupported_float_widths_rejected(void **state)
{
    static const struct wav_width_case cases[] = {
        { WAVE_FORMAT_IEEE_FLOAT,  8, "8 bit float" },
        { WAVE_FORMAT_IEEE_FLOAT, 16, "16 bit float" },
        { WAVE_FORMAT_IEEE_FLOAT, 24, "24 bit float" },
        { WAVE_FORMAT_IEEE_FLOAT, 64, "64 bit float" }
    };
    expect_widths_rejected((lame_t) *state, cases,
                           sizeof cases / sizeof cases[0]);
}

/** @brief The size of the header that @c WriteWaveHeader() writes. */
#define WAV_HEADER_BYTES 44
/** @brief Offset of the RIFF size in that header. */
#define WAV_RIFF_SIZE_OFFSET 4
/** @brief Offset of the data size in that header. */
#define WAV_DATA_SIZE_OFFSET 40
/** @brief The RIFF size counts the header without "RIFF" and the RIFF size. */
#define WAV_RIFF_SIZE_EXTRA (WAV_HEADER_BYTES - 8)

/**
 * @brief Writes the WAV header for 16 bit stereo samples to a temporary
 *        stream and reads it back.
 * @param hdr       receives the header, ::WAV_HEADER_BYTES long.
 * @param frames    the number of samples per channel.
 */
static void
write_stereo_header(unsigned char *hdr, double frames)
{
    FILE   *f = tmpfile();

    assert_non_null(f);
    assert_int_equal(WriteWaveHeader(f, wav_data_size(frames, 4), 44100, 2, 16), 0);
    rewind(f);
    assert_int_equal(fread(hdr, 1, WAV_HEADER_BYTES, f), WAV_HEADER_BYTES);
    fclose(f);
}

/**
 * @brief Checks a header for 3 GiB of data.
 *
 * The data size is above @c INT_MAX. The header must hold it exactly, in the
 * data size and in the RIFF size. The test writes only the header.
 *
 * @param state unused.
 */
static void
test_header_above_2gib(LAME_UNUSED void **state)
{
    uint32_t const bytes = 0xC0000000u;
    unsigned char hdr[WAV_HEADER_BYTES];

    write_stereo_header(hdr, bytes / 4.0);
    assert_int_equal(uint32_low_high(hdr + WAV_DATA_SIZE_OFFSET), bytes);
    assert_int_equal(uint32_low_high(hdr + WAV_RIFF_SIZE_OFFSET), bytes + WAV_RIFF_SIZE_EXTRA);
}

/**
 * @brief Checks that more data than a header can hold gives
 *        ::WAV_DATA_SIZE_MAX as the data size.
 *
 * @param state unused.
 */
static void
test_header_size_capped(LAME_UNUSED void **state)
{
    unsigned char hdr[WAV_HEADER_BYTES];

    write_stereo_header(hdr, WAV_DATA_SIZE_MAX / 4.0 + 1.0);
    assert_int_equal(uint32_low_high(hdr + WAV_DATA_SIZE_OFFSET), WAV_DATA_SIZE_MAX);
    assert_int_equal(uint32_low_high(hdr + WAV_RIFF_SIZE_OFFSET),
                     WAV_DATA_SIZE_MAX + WAV_RIFF_SIZE_EXTRA);
}

/** @brief The number of samples per channel in the writer tests. */
#define WRITER_SAMPLES 4
/** @brief The largest number of bytes the writer tests read back. */
#define WRITER_BYTES_MAX (2 * 2 * WRITER_SAMPLES)

/**
 * @brief Writes ::WRITER_SAMPLES samples per channel with @c put_audio16()
 *        to a temporary stream and compares the bytes with @p expected.
 *
 * The first channel holds 0x1234, -2, -32768 and 32767. The second channel
 * holds 0x0102, -256, 1 and -1.
 *
 * @param nch       the number of channels, 1 or 2.
 * @param raw       1 for raw output (-t), 0 for WAV output.
 * @param swap      1 for -x, 0 without.
 * @param expected  the bytes the file must hold.
 * @param n         the size of @p expected.
 */
static void
expect_samples(int nch, int raw, int swap, const unsigned char *expected, size_t n)
{
    static const short left[WRITER_SAMPLES] = { 0x1234, -2, -32768, 32767 };
    static const short right[WRITER_SAMPLES] = { 0x0102, -256, 1, -1 };
    static short buffer[2][FRAME_BUFFER_SAMPLES];
    unsigned char out[WRITER_BYTES_MAX];
    FILE   *f = tmpfile();

    assert_non_null(f);
    memcpy(buffer[0], left, sizeof(left));
    memcpy(buffer[1], right, sizeof(right));
    global_decoder.disable_wav_header = raw;
    global_reader.swapbytes = swap;
    global_writer.flush_write = 0;
    assert_int_equal(put_audio16(f, buffer, WRITER_SAMPLES, nch), 0);
    global_decoder.disable_wav_header = 0;
    global_reader.swapbytes = 0;
    rewind(f);
    assert_int_equal(fread(out, 1, sizeof(out), f), n);
    fclose(f);
    assert_memory_equal(out, expected, n);
}

/** @brief What ::expect_samples() writes for one channel, little endian. */
static const unsigned char mono_le[] = {
    0x34, 0x12, 0xfe, 0xff, 0x00, 0x80, 0xff, 0x7f
};
/** @brief Two interleaved channels, little endian. */
static const unsigned char stereo_le[] = {
    0x34, 0x12, 0x02, 0x01, 0xfe, 0xff, 0x00, 0xff,
    0x00, 0x80, 0x01, 0x00, 0xff, 0x7f, 0xff, 0xff
};
/** @brief Big endian bytes for one channel. */
static const unsigned char mono_be[] = {
    0x12, 0x34, 0xff, 0xfe, 0x80, 0x00, 0x7f, 0xff
};
/** @brief Big endian bytes for two interleaved channels. */
static const unsigned char stereo_be[] = {
    0x12, 0x34, 0x01, 0x02, 0xff, 0xfe, 0xff, 0x00,
    0x80, 0x00, 0x00, 0x01, 0x7f, 0xff, 0xff, 0xff
};

/**
 * @brief Checks that WAV output and raw output without -x are little endian.
 *
 * A WAV file is little endian with -x too.
 *
 * @param state unused.
 */
static void
test_writer_little_endian(LAME_UNUSED void **state)
{
    expect_samples(1, 0, 0, mono_le, sizeof(mono_le));
    expect_samples(2, 0, 0, stereo_le, sizeof(stereo_le));
    expect_samples(1, 0, 1, mono_le, sizeof(mono_le));
    expect_samples(2, 0, 1, stereo_le, sizeof(stereo_le));
    expect_samples(1, 1, 0, mono_le, sizeof(mono_le));
    expect_samples(2, 1, 0, stereo_le, sizeof(stereo_le));
}

/**
 * @brief Checks that raw output with -x is big endian.
 *
 * @param state unused.
 */
static void
test_writer_big_endian(LAME_UNUSED void **state)
{
    expect_samples(1, 1, 1, mono_be, sizeof(mono_be));
    expect_samples(2, 1, 1, stereo_be, sizeof(stereo_be));
}

/* --- fixture ----------------------------------------------------------- */

/** @brief Per-test setup: stores a new encoder instance in @p state. */
static int
setup_lame(void **state)
{
    if (lame_fixture_setup(state) != 0)
        return -1;
    /* The parser consults the forced input rate before the file's own, so a
       stale value here would mask every rate under test. */
    global_reader.input_samplerate = 0;
    return 0;
}

/** @brief Registers and runs the WAVE header test group. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_valid_wav_accepted,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_high_sample_rates_accepted,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_boundary_rate_accepted,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_unrepresentable_sample_rate_rejected,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_zero_sample_rate_rejected,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_supported_sample_widths_accepted,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_unsupported_integer_widths_rejected,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test_setup_teardown(test_unsupported_float_widths_rejected,
                                        setup_lame, lame_fixture_teardown),
        cmocka_unit_test(test_header_above_2gib),
        cmocka_unit_test(test_header_size_capped),
        cmocka_unit_test(test_writer_little_endian),
        cmocka_unit_test(test_writer_big_endian),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
