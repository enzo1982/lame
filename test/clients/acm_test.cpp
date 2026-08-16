/**
 * @file
 * @brief Tests for the Windows ACM codec.
 *
 * The codec's sources are compiled into this program rather than loaded from
 * the built @c lameACM.acm, because what is under test here is arithmetic and
 * configuration handling that the driver interface does not expose. The
 * @c DriverProc export lives in @c main.cpp, which stays out, so nothing here
 * pulls in the driver entry point.
 *
 * Two things are covered:
 *
 * - @c ACMStream::GetOutputSampleRate(), which decides what sample rate smart
 *   output mode offers. Each case is paired with the integer-arithmetic form
 *   the function used to have, which has to disagree - otherwise a build in
 *   which the call had been stubbed out would pass this file unchanged.
 * - the smart output ratio's round trip through the configuration file, read
 *   and written through public methods only.
 */

#include <windows.h>
#include <stdio.h>

#include "ctest.h"

#include "ACMStream.h"
#include "AEncodeProperties.h"

/** @brief The configuration file an AEncodeProperties built with no module uses. */
static const char CONFIG_NAME[] = "lame_acm.xml";

/**
 * @brief The MP3 sample-rate ladder, duplicated for the control below.
 *
 * @c map2MP3Frequency() is static inside @c ACMStream.cpp and cannot be called
 * from here. These are the eight rates MPEG-1 and MPEG-2 define, so this copy
 * has nothing to drift with.
 */
static int
ladder(int freq)
{
    if (freq <= 8000)  return 8000;
    if (freq <= 11025) return 11025;
    if (freq <= 12000) return 12000;
    if (freq <= 16000) return 16000;
    if (freq <= 22050) return 22050;
    if (freq <= 24000) return 24000;
    if (freq <= 32000) return 32000;
    if (freq <= 44100) return 44100;
    return 48000;
}

/**
 * @brief The rate the function returned before the ratio was computed on
 *        doubles - the control, not a second implementation.
 *
 * Its only job is to answer differently where the fix matters. A case where
 * the two agree is asserted as well, so "they differ" is a property of the
 * inputs and not of this function always saying something else.
 *
 * The numbers below are deliberately written as the shipped code wrote them,
 * unnamed and unexplained. A control that has been tidied is no longer a copy
 * of what it stands for.
 */
static unsigned int
legacy_output_sample_rate(int samples_per_sec, int bitrate, int channels)
{
    int compression_ratio;

    if (bitrate == 0) {
        bitrate = (64000 * channels) / 8;
    }
    compression_ratio = (samples_per_sec * 16 * channels) / (bitrate * 8);
    if (compression_ratio > 13) {
        return (unsigned int) ladder((10 * bitrate * 8) / (16 * channels));
    }
    return (unsigned int) ladder((int) (0.97 * samples_per_sec));
}

/**
 * @brief Writes a configuration file carrying one smart output ratio.
 *
 * The shape is the one @c ACM/lame_acm.xml ships: a named config under
 * @c encodings, with the ratio on the @c Smart element.
 */
static int
write_config(double ratio)
{
    FILE *f = fopen(CONFIG_NAME, "wb");

    if (f == NULL) {
        return 0;
    }
    fprintf(f,
            "<lame_acm>\n"
            "    <encodings default=\"Current\">\n"
            "        <config name=\"Current\">\n"
            "            <Smart use=\"true\" ratio=\"%.6g\" />\n"
            "        </config>\n"
            "    </encodings>\n"
            "</lame_acm>\n",
            ratio);
    fclose(f);
    return 1;
}

/**
 * @brief The rate smart output mode offers for a given source and bitrate.
 *
 * The three named cases are the ones whose result the fix changed; each is
 * asserted against the rate the mode intends and against the rate the integer
 * form produced.
 */
static void
test_output_sample_rate(void)
{
    /* 112 kbps, 56 kbps: the ACM passes bytes per second, not bits. */
    unsigned int r48s = ACMStream::GetOutputSampleRate(48000, 112000 / 8, 2);
    unsigned int r48m = ACMStream::GetOutputSampleRate(48000, 56000 / 8, 1);
    unsigned int r24s = ACMStream::GetOutputSampleRate(24000, 56000 / 8, 2);

    printf("the three cases the fix changed\n");
    CHECK_EQ_U(r48s, 44100, "48 kHz stereo at 112 kbps resamples to 44100");
    CHECK_EQ_U(r48m, 44100, "48 kHz mono at 56 kbps resamples to 44100");
    CHECK_EQ_U(r24s, 22050, "24 kHz stereo at 56 kbps resamples to 22050");

    printf("the same cases under the integer form, which must disagree\n");
    CHECK_NE_U(r48s, legacy_output_sample_rate(48000, 112000 / 8, 2),
               "48 kHz stereo at 112 kbps: the flooring form answers differently");
    CHECK_NE_U(r48m, legacy_output_sample_rate(48000, 56000 / 8, 1),
               "48 kHz mono at 56 kbps: the flooring form answers differently");
    CHECK_NE_U(r24s, legacy_output_sample_rate(24000, 56000 / 8, 2),
               "24 kHz stereo at 56 kbps: the flooring form answers differently");

    printf("a case far from the boundary, where both forms must agree\n");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(44100, 128000 / 8, 2),
               legacy_output_sample_rate(44100, 128000 / 8, 2),
               "44.1 kHz stereo at 128 kbps: flooring changes nothing there");

    printf("the compression ratio 13 boundary\n");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(48000, 112000 / 8, 2), 44100,
               "just above the boundary, the rate comes from the bitrate");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(48000, 120000 / 8, 2), 48000,
               "just below it, the rate comes from the source");
    CHECK_NE_U(ACMStream::GetOutputSampleRate(48000, 112000 / 8, 2),
               ACMStream::GetOutputSampleRate(48000, 120000 / 8, 2),
               "the two sides of the boundary do not answer the same");

    printf("no bitrate given\n");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(44100, 0, 2),
               ACMStream::GetOutputSampleRate(44100, (64000 * 2) / 8, 2),
               "a zero bitrate stands for 64 kbps per channel");
}

/**
 * @brief The smart output ratio survives being written and read back.
 *
 * A ratio with a fractional part is what the fix preserved, so the value is
 * checked after the read, then written out by the object itself and checked
 * again from a second instance - which is the half that reads the file the
 * codec wrote rather than the one this test wrote.
 */
static void
test_smart_ratio_round_trip(void)
{
    const double wanted = 15.5;
    const double other = 12.25;

    ::DeleteFileA(CONFIG_NAME);
    if (!write_config(wanted)) {
        CHECK(0, "the configuration file could be written");
        return;
    }

    printf("a fractional ratio read from the configuration file\n");
    AEncodeProperties first(NULL);
    first.ParamsRestore();
    CHECK_EQ_D(first.GetSmartRatio(), wanted, 0.0001,
               "the fractional part survives being read");

    printf("the same ratio written back out by the codec and read again\n");
    first.SaveValuesToStringKey("Current");
    AEncodeProperties second(NULL);
    second.ParamsRestore();
    CHECK_EQ_D(second.GetSmartRatio(), wanted, 0.0001,
               "the fractional part survives being written");

    printf("a different ratio, so a fixed answer cannot pass\n");
    ::DeleteFileA(CONFIG_NAME);
    if (!write_config(other)) {
        CHECK(0, "the second configuration file could be written");
        return;
    }
    AEncodeProperties third(NULL);
    third.ParamsRestore();
    CHECK_EQ_D(third.GetSmartRatio(), other, 0.0001,
               "a second fractional ratio reads back as itself");
    CHECK(third.GetSmartRatio() != second.GetSmartRatio(),
          "the two configurations do not report the same ratio");

    ::DeleteFileA(CONFIG_NAME);
}

int
main(void)
{
    printf("acm_test: the ACM codec's rate selection and configuration\n");
    test_output_sample_rate();
    test_smart_ratio_round_trip();
    return ctest_summary("acm_test");
}
