/**
 * @file
 * @brief Tests for the Windows ACM codec.
 *
 * The program tests the codec in two ways.
 *
 * First, the codec sources are compiled into this program. These tests call
 * the codec classes directly. They check arithmetic and configuration
 * handling that the driver interface does not expose. The @c DriverProc
 * export is in @c main.cpp, which is not compiled in. So the program does not
 * contain the driver entry point. These tests cover:
 *
 * - @c ACMStream::GetOutputSampleRate(), which selects the sample rate that
 *   smart output mode offers. Each case is paired with the integer-arithmetic
 *   form of the function, and the two must disagree. Without that pairing, a
 *   build in which the call is stubbed out passes this file unchanged.
 * - The round trip of the smart output ratio through the configuration file.
 *   The tests read and write it through public methods only.
 * - Configuration files that parse but have an unexpected shape, ABR ranges
 *   that are not valid, and a save with no file to start from. Repeated
 *   saves keep no memory allocated.
 * - The ABR bitrates of a range whose minimum is below its step.
 * - The bitrates that the configuration dialog lists, in their order.
 *
 * Second, the program loads the built @c lameACM.acm and drives it through
 * the Audio Compression Manager. These tests cover the driver details, the
 * format list, the suggested format and its name, a conversion, and a
 * destination buffer that is too small. Two tests create the configuration
 * dialog from the codec's resources. They check the version text it shows,
 * and what it returns after OK and after Cancel.
 */

#include <windows.h>
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <vector>

#include "ctest.h"
#include "dialogcheck.h"
#include "mp3frame.h"

#include <lame.h>
#include "ACMStream.h"
#include "AEncodeProperties.h"
#include "ACM.h"
#include "DialogFont.h"
#include "SettingText.h"
#include <dshow.h>
#include "resource.h"
#include "../../libmp3lame/version.h"

/** @brief How the codec's long name begins: its own name, then LAME's version. */
#define LONG_NAME_PREFIX "LAME MP3 Codec v" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION)
/** @brief LAME's version in the ACM's driver-version layout (major, minor, build). */
#define DRIVER_VERSION (((DWORD) LAME_MAJOR_VERSION << 24) | ((DWORD) LAME_MINOR_VERSION << 16)                         | (DWORD) LAME_PATCH_VERSION)

/**
 * @brief The configuration file of an AEncodeProperties created with no
 *        module. It is in the current directory.
 */
static const char CONFIG_NAME[] = "lame_acm.xml";

/**
 * @brief The configuration file of the codec under test: CONFIG_NAME in the
 *        folder of the codec, where the codec reads it however it was
 *        registered. main() fills it in.
 */
static char codec_config[MAX_PATH];

/**
 * @brief Maps a frequency to the MP3 sample-rate ladder. A copy for the
 *        control below.
 *
 * @c map2MP3Frequency() is static inside @c ACMStream.cpp, so this file
 * cannot call it. These are the nine sample rates of MPEG-1, MPEG-2 and
 * MPEG-2.5. The standards fix them, so this copy cannot drift from the
 * original.
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
 * @brief Returns the rate that the integer-arithmetic form of
 *        ACMStream::GetOutputSampleRate() gives.
 *
 * This is the control, not a second implementation. Its only job is to give
 * a different result where the ratio on doubles matters. The tests also check
 * a case where the two agree. So "they differ" is a property of the inputs.
 * It does not come from this function always returning something else.
 *
 * The numbers below are deliberately the same as in the integer form, without
 * names or explanation. A tidied control is not a copy of the code it stands
 * for.
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
 * @brief Writes a configuration file with one smart output ratio.
 *
 * The shape is the shape of the shipped @c ACM/lame_acm.xml: a named config
 * under @c encodings, with a @c Smart element. The ratio is an attribute of
 * the @c Smart element.
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
 * @brief Checks the rate that smart output mode offers for a given source and
 *        bitrate.
 *
 * In the three named cases, the ratio on doubles gives a different result
 * from the integer form. Each case is checked against the rate that the mode
 * intends. Each is also checked against the rate that the integer form gives.
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
 * @brief Checks that field values far outside any real stream give the rate
 *        that the values call for.
 *
 * The bitrate and the channel count come from the formats of the
 * application. A bitrate of 2^30 bytes per second is so high that the source
 * rate stays. 40000 channels without a bitrate count as 64 kbps each. That is
 * again far above what the source needs.
 */
static void
test_output_sample_rate_extremes(void)
{
    printf("fields far beyond any stream\n");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(44100, 1 << 30, 2), 44100,
               "a bitrate of 2^30 bytes per second leaves 44.1 kHz stereo at 44100");
    CHECK_EQ_U(ACMStream::GetOutputSampleRate(44100, 0, 40000), 44100,
               "40000 channels with no bitrate leave 44.1 kHz at 44100");
}

/**
 * @brief Checks that the smart output ratio survives a write and a read.
 *
 * The ratio has a fractional part, because this test checks that the
 * fractional part is kept. The test checks the value after the read. Then the
 * object writes the file itself, and a second instance reads it back. This
 * second half reads the file that the codec wrote, not the file that the test
 * wrote.
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
    first.ParamsSave();
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

/** @brief Writes a configuration file with the given content verbatim. */
static int
write_raw_config(const char *content)
{
    FILE *f = fopen(CONFIG_NAME, "wb");

    if (f == NULL) {
        return 0;
    }
    fputs(content, f);
    fclose(f);
    return 1;
}

/**
 * @brief Checks that the codec survives a configuration file that parses but
 *        is not one of ours.
 *
 * Both structural lookups can return nothing. This class runs inside a driver
 * that the ACM loads into the process of some application. If the code
 * follows an empty lookup, it crashes that application. The file is beside
 * the codec, and users can edit it by hand. So the shapes below are the ones
 * that a failed write or an edit produces.
 *
 * The codec must actually *read* each file. That is why the test checks the
 * ratio afterwards. A build that does not open the file at all passes a test
 * that only checks "did not crash".
 */
static void
test_malformed_config(void)
{
    /* What AEncodeProperties::ParamsRestore() assigns to SmartRatioMax before
       it consults the file. Kept in step with the codec by the checks below: a
       different default there fails every case here. */
    const double ACM_DEFAULT_SMART_RATIO = 15.0;

    static const struct {
        const char *what;
        const char *content;
    } cases[] = {
        { "a document with some other root element",
          "<not_lame_acm>\n    <encodings default=\"Current\" />\n</not_lame_acm>\n" },
        { "our root element with no encodings under it",
          "<lame_acm>\n    <something_else />\n</lame_acm>\n" },
        { "our root element, empty",
          "<lame_acm />\n" },
    };
    size_t i;

    printf("configuration files that parse and are not ours\n");
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        ::DeleteFileA(CONFIG_NAME);
        if (!write_raw_config(cases[i].content)) {
            CHECK(0, "the configuration file could be written");
            continue;
        }

        AEncodeProperties props(NULL);
        props.ParamsRestore();
        /* The default is what the constructor assigns before the file is
           consulted, so this says the read was attempted and abandoned rather
           than skipped. */
        CHECK_EQ_D(props.GetSmartRatio(), ACM_DEFAULT_SMART_RATIO, 0.0001,
                   cases[i].what);
    }

    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks that the codec keeps its defaults for an ABR range that it
 *        cannot step through.
 *
 * When the driver is opened, the codec lists its ABR formats. It steps down
 * from the maximum to the minimum. Each case sets one attribute wrong. The
 * last case is a valid range, and it reads back as written. That shows that
 * the codec reads the element at all.
 */
static void
test_abr_range_config(void)
{
    /* What AEncodeProperties::ParamsRestore() assigns before it consults the file. */
    const unsigned int ABR_DEFAULT_MIN = 80, ABR_DEFAULT_MAX = 160, ABR_DEFAULT_STEP = 8;
    static const struct {
        const char *what;
        const char *attributes;
    } cases[] = {
        { "a step of 0 keeps the default range", "min=\"96\" max=\"192\" step=\"0\"" },
        { "a minimum of 0 keeps the default range", "min=\"0\" max=\"192\" step=\"32\"" },
        { "a maximum below the minimum keeps the default range", "min=\"192\" max=\"96\" step=\"32\"" },
        { "a maximum above 320 kbit/s keeps the default range", "min=\"96\" max=\"4000000000\" step=\"1\"" },
        { "a negative step keeps the default range", "min=\"96\" max=\"192\" step=\"-8\"" },
    };
    char doc[512];
    size_t i;

    printf("ABR ranges in the configuration file\n");
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        ::DeleteFileA(CONFIG_NAME);
        sprintf(doc, "<lame_acm>\n    <encodings default=\"Current\">\n        <config name=\"Current\">\n"
                     "            <ABR use=\"true\" %s />\n        </config>\n    </encodings>\n</lame_acm>\n",
                cases[i].attributes);
        if (!write_raw_config(doc)) {
            CHECK(0, "the configuration file could be written");
            continue;
        }
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetAbrBitrateMin() == ABR_DEFAULT_MIN && props.GetAbrBitrateMax() == ABR_DEFAULT_MAX
              && props.GetAbrBitrateStep() == ABR_DEFAULT_STEP, cases[i].what);
    }

    ::DeleteFileA(CONFIG_NAME);
    sprintf(doc, "<lame_acm>\n    <encodings default=\"Current\">\n        <config name=\"Current\">\n"
                 "            <ABR use=\"true\" min=\"96\" max=\"192\" step=\"32\" />\n"
                 "        </config>\n    </encodings>\n</lame_acm>\n");
    if (write_raw_config(doc)) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetAbrBitrateMin() == 96 && props.GetAbrBitrateMax() == 192
              && props.GetAbrBitrateStep() == 32, "a valid range is read as written");
    } else {
        CHECK(0, "the configuration file could be written");
    }
    ::DeleteFileA(CONFIG_NAME);
}

/** @brief Gives the test the ABR bitrates of a codec object compiled into it. */
class AbrProbe : public ACM
{
public:
    /** @brief Creates the codec object. It reads lame_acm.xml in the current directory. */
    AbrProbe() : ACM(NULL) {}

    /**
     * @brief Returns the ABR bitrates the codec lists for one MPEG version.
     * @param lowest  the lowest bitrate of the MPEG version, in kbit/s.
     * @return the bitrates, highest first.
     */
    std::vector<unsigned int> Bitrates(unsigned int lowest) const
    {
        return AbrBitrates(my_EncodingProperties.GetFormatListSettings(), lowest);
    }
};

/** @brief The input and the result of one run of abr_ladder_worker(). */
typedef struct {
    unsigned int lowest;             /**< passed to AbrProbe::Bitrates() */
    std::vector<unsigned int> got;   /**< the bitrates it returned */
    int built;                       /**< 1 if the codec object was created */
} abr_ladder_run;

/**
 * @brief Creates a codec object and asks it for its ABR bitrates.
 *
 * A list that does not end grows until the allocation fails. The exception
 * then stops the thread, and @c built stays 0.
 *
 * @param arg  the abr_ladder_run to fill.
 * @return 0.
 */
static DWORD WINAPI
abr_ladder_worker(LPVOID arg)
{
    abr_ladder_run *run = (abr_ladder_run *) arg;

    try {
        AbrProbe acm;

        run->got = acm.Bitrates(run->lowest);
        run->built = 1;
    }
    catch (...) {
        run->built = 0;
    }
    return 0;
}

/**
 * @brief Checks the ABR bitrates for a range whose minimum is below its step.
 *
 * The codec lists the ABR bitrates when it is created. The configuration
 * dialog draws the same list as slider tics. With a minimum of 8, a maximum
 * of 200 and a step of 16, the list ends at 8. The codec object is created in
 * a thread with a time limit, so a list that does not end fails the check and
 * does not hang the run.
 */
static void
test_abr_ladder_below_step(void)
{
    static const unsigned int expected[] =
        { 200, 184, 168, 152, 136, 120, 104, 88, 72, 56, 40, 24, 8 };
    const unsigned int n = (unsigned int) (sizeof(expected) / sizeof(expected[0]));
    /* Creating the codec object takes far less than a second. */
    const DWORD TIME_LIMIT_MS = 30000;
    /* The lowest MPEG-2 bitrate. No bitrate of the range is below it. */
    const unsigned int MPEG2_LOWEST_KBPS = 8;
    abr_ladder_run run;
    HANDLE thread;
    DWORD waited;
    unsigned int i, same = 0;

    printf("an ABR range whose minimum is below its step\n");
    ::DeleteFileA(CONFIG_NAME);
    if (!write_raw_config("<lame_acm>\n    <encodings default=\"Current\">\n        <config name=\"Current\">\n"
                          "            <ABR use=\"true\" min=\"8\" max=\"200\" step=\"16\" />\n"
                          "        </config>\n    </encodings>\n</lame_acm>\n")) {
        CHECK(0, "the configuration file could be written");
        return;
    }

    run.lowest = MPEG2_LOWEST_KBPS;
    run.built = 0;
    thread = ::CreateThread(NULL, 0, abr_ladder_worker, &run, 0, NULL);
    if (thread == NULL) {
        CHECK(0, "the thread that creates the codec object starts");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }
    waited = ::WaitForSingleObject(thread, TIME_LIMIT_MS);
    if (waited != WAIT_OBJECT_0) {
        /* The thread still runs and may hold the heap. The run ends here. */
        CHECK(0, "the codec lists its ABR bitrates within the time limit");
        ::ExitProcess((UINT) ctest_summary("acm_test"));
    }
    ::CloseHandle(thread);
    ::DeleteFileA(CONFIG_NAME);

    CHECK(run.built, "the codec object is created");
    CHECK_EQ_U(run.got.size(), n, "the ABR list has 13 bitrates");
    for (i = 0; i < n && i < run.got.size(); i++) {
        if (run.got[i] == expected[i]) {
            ++same;
        }
    }
    CHECK_EQ_U(same, n, "the ABR list steps from 200 down to 8 kbit/s");
}

/**
 * @brief The fdwFlags of the codec's VBR format of a quality level: a bit
 *        beside the ABR bit, and the level in bits 24 to 27.
 */
#define ACM_FLAGS_VBR(quality) (0x40000000UL | ((unsigned long) (quality) << 24))

/** @brief Gives the test the format list and the VBR table of a codec object
 *         compiled into it. */
class VbrTable : public ACM
{
public:
    /** @brief Creates the codec object. It reads lame_acm.xml in the current directory. */
    VbrTable() : ACM(NULL) {}

    /** @brief Returns the formats the codec lists. */
    const std::vector<bitrate_item> &List() const { return bitrate_table; }

    /**
     * @brief Returns the typical bitrate of a VBR quality level.
     * @param rate      the sample rate in Hz
     * @param channels  1 or 2
     * @param quality   the level, 0 to 9
     * @return the bitrate in kbit/s, 0 for a rate the codec does not have
     */
    static unsigned Typical(DWORD rate, WORD channels, unsigned quality)
    {
        return VbrTypicalBitrate(rate, channels, quality);
    }

    /**
     * @brief Fills in the format of one entry of the list, and its name.
     * @param index  the entry
     * @param mp3    receives the format
     * @param name   receives the name, ACMFORMATDETAILS_FORMAT_CHARS wide characters
     */
    void Format(DWORD index, MPEGLAYER3WAVEFORMAT *mp3, WCHAR *name) const
    {
        memset(mp3, 0, sizeof(*mp3));
        GetMP3FormatForIndex(index, mp3->wfx, name);
    }
};

/**
 * @brief Writes a configuration with Smart Output off and the given VBR
 *        element, or none.
 * @param vbr  the VBR element, or an empty string
 * @return 1 if the file was written
 */
static int
write_vbr_config(const char *vbr)
{
    FILE *f = fopen(CONFIG_NAME, "wb");

    if (f == NULL) {
        return 0;
    }
    fprintf(f,
            "<lame_acm>\n"
            "    <encodings default=\"Current\">\n"
            "        <config name=\"Current\">\n"
            "            <Smart use=\"false\" />\n"
            "            %s\n"
            "        </config>\n"
            "    </encodings>\n"
            "</lame_acm>\n", vbr);
    return fclose(f) == 0;
}

/**
 * @brief Checks the VBR formats that the codec lists.
 *
 * With VBR on and the levels 2 to 4, the list holds one VBR format for each of
 * the 9 sample rates, both channel counts and the 3 levels, at the level's
 * typical bitrate, with the VBR bit and the level in its flags and a name that
 * says both. With VBR off it holds none. The range survives a save.
 */
static void
test_vbr_format_list(void)
{
    char detail[CTEST_DETAIL_CHARS];
    unsigned vbr = 0, right = 0, flagged = 0, named = 0;
    size_t k;

    printf("the VBR formats in the format list\n");
    if (!write_vbr_config("<VBR use=\"true\" best=\"2\" worst=\"4\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    {
        VbrTable acm;
        const std::vector<bitrate_item> &list = acm.List();

        for (k = 0; k < list.size(); k++) {
            MPEGLAYER3WAVEFORMAT mp3;
            WCHAR wide[ACMFORMATDETAILS_FORMAT_CHARS];
            char name[ACMFORMATDETAILS_FORMAT_CHARS];
            char want[64];

            if (list[k].mode != vbr_mtrh) {
                continue;
            }
            vbr++;
            if (list[k].quality >= 2 && list[k].quality <= 4
                && list[k].bitrate == VbrTable::Typical(list[k].frequency, (WORD) list[k].channels, list[k].quality)
                && list[k].bitrate != 0) {
                right++;
            }
            acm.Format((DWORD) k, &mp3, wide);
            if (mp3.fdwFlags == ACM_FLAGS_VBR(list[k].quality)
                && mp3.wfx.nAvgBytesPerSec == list[k].bitrate * 1000 / 8) {
                flagged++;
            }
            WideCharToMultiByte(CP_ACP, 0, wide, -1, name, sizeof(name), NULL, NULL);
            sprintf(want, "VBR quality %u (about %u kbps)", list[k].quality, list[k].bitrate);
            if (strstr(name, want) != NULL) {
                named++;
            }
        }
        sprintf(detail, "%u VBR formats; %u at the level's bitrate, %u flagged, %u named", vbr, right, flagged, named);
        CHECK(vbr == 9 * 2 * 3 && right == vbr && flagged == vbr && named == vbr,
              "VBR on, levels 2 to 4: 54 VBR formats, each at its typical bitrate, flagged and named");
        printf("        %s\n", detail);

        AEncodeProperties held(NULL);
        held.ParamsRestore();
        held.ParamsSave();
        {
            AEncodeProperties reread(NULL);
            reread.ParamsRestore();
            CHECK(reread.GetVbrOutputMode() && reread.GetVbrQualityBest() == 2 && reread.GetVbrQualityWorst() == 4,
                  "the VBR setting comes back after a save");
        }
    }
    if (write_vbr_config("<VBR use=\"false\" best=\"2\" worst=\"4\" />")) {
        VbrTable acm;
        const std::vector<bitrate_item> &list = acm.List();
        unsigned none = 0;

        for (k = 0; k < list.size(); k++) {
            if (list[k].mode == vbr_mtrh) {
                none++;
            }
        }
        CHECK_EQ_U(none, 0, "VBR off: no VBR formats");
    }
    if (write_vbr_config("<VBR use=\"true\" best=\"5\" worst=\"3\" />")) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetVbrQualityBest() == 0 && props.GetVbrQualityWorst() == AEncodeProperties::VBR_QUALITY_WORST,
              "a VBR range whose best level is worse than its worst keeps the defaults");
    }
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Returns the bitrate of one VBR format of a format list.
 * @param list      the formats of the codec
 * @param rate      the sample rate in Hz
 * @param channels  1 or 2
 * @param quality   the VBR quality level
 * @return the bitrate in kbit/s, 0 if the list has no such format
 */
static unsigned
vbr_list_bitrate(const std::vector<bitrate_item> &list, unsigned rate, unsigned channels, unsigned quality)
{
    size_t k;

    for (k = 0; k < list.size(); k++) {
        if (list[k].mode == vbr_mtrh && list[k].frequency == rate && list[k].channels == channels
            && list[k].quality == quality) {
            return list[k].bitrate;
        }
    }
    return 0;
}

/**
 * @brief Checks the VBR formats under bitrate limits, and the limits in the
 *        configuration file.
 *
 * The bitrate of a VBR format is the typical one of its level, held within
 * the limits that LAME takes for its rate. A 128 kbit/s maximum brings level
 * 0 at 44.1 kHz stereo down to 128. A 128 kbit/s minimum lifts level 9 there
 * to 128, and at 8 kHz, whose bitrates end at 64 kbit/s, to 64. Every level of
 * every rate stays listed. The limits survive a save. A limit that is not a
 * bitrate of the list, and a minimum above the maximum, leave no limits.
 */
static void
test_vbr_limits_in_the_list(void)
{
    /* The VBR formats of the levels 0 to 9: 9 sample rates, 2 channel counts. */
    enum { ALL_VBR_FORMATS = 9 * 2 * 10, LIMIT_KBPS = 128, MPEG25_HIGHEST_KBPS = 64 };
    char detail[CTEST_DETAIL_CHARS];
    size_t k;

    printf("the VBR bitrate limits in the format list\n");
    if (!write_vbr_config("<VBR use=\"true\" best=\"0\" worst=\"9\" max=\"128\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    {
        VbrTable acm;
        const std::vector<bitrate_item> &list = acm.List();
        unsigned vbr = 0, highest = 0;

        for (k = 0; k < list.size(); k++) {
            if (list[k].mode == vbr_mtrh) {
                vbr++;
                if (list[k].bitrate > highest) {
                    highest = list[k].bitrate;
                }
            }
        }
        sprintf(detail, "%u VBR formats, the highest at %u kbps; level 0 at 44.1 kHz stereo %u kbps", vbr, highest,
                vbr_list_bitrate(list, 44100, 2, 0));
        CHECK(vbr == ALL_VBR_FORMATS && highest == LIMIT_KBPS && vbr_list_bitrate(list, 44100, 2, 0) == LIMIT_KBPS,
              "a 128 kbps maximum: every level listed, none above 128 kbps, level 0 at 44.1 kHz stereo at 128");
        printf("        %s\n", detail);
    }
    if (!write_vbr_config("<VBR use=\"true\" best=\"0\" worst=\"9\" min=\"128\" enforce_min=\"true\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    {
        VbrTable acm;
        const std::vector<bitrate_item> &list = acm.List();
        unsigned vbr = 0;

        for (k = 0; k < list.size(); k++) {
            if (list[k].mode == vbr_mtrh) {
                vbr++;
            }
        }
        sprintf(detail, "%u VBR formats; level 9 at %u kbps at 44.1 kHz, at %u kbps at 8 kHz (typical %u and %u)", vbr,
                vbr_list_bitrate(list, 44100, 2, 9), vbr_list_bitrate(list, 8000, 2, 9),
                VbrTable::Typical(44100, 2, 9), VbrTable::Typical(8000, 2, 9));
        CHECK(vbr == ALL_VBR_FORMATS && vbr_list_bitrate(list, 44100, 2, 9) == LIMIT_KBPS
              && vbr_list_bitrate(list, 8000, 2, 9) == MPEG25_HIGHEST_KBPS,
              "a 128 kbps minimum: level 9 at 128 kbps at 44.1 kHz, at 64 kbps at 8 kHz");
        printf("        %s\n", detail);

        AEncodeProperties held(NULL);
        held.ParamsRestore();
        held.ParamsSave();
        {
            AEncodeProperties reread(NULL);
            reread.ParamsRestore();
            CHECK(reread.GetVbrBitrateMin() == LIMIT_KBPS
                  && reread.GetVbrBitrateMax() == AEncodeProperties::VBR_BITRATE_NO_LIMIT && reread.GetVbrEnforceMin(),
                  "the VBR bitrate limits come back after a save");
        }
    }
    if (write_vbr_config("<VBR use=\"true\" min=\"100\" />")) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetVbrBitrateMin() == AEncodeProperties::VBR_BITRATE_NO_LIMIT,
              "a VBR minimum that is not a bitrate of the list leaves no limit");
    }
    if (write_vbr_config("<VBR use=\"true\" min=\"192\" max=\"128\" />")) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetVbrBitrateMin() == AEncodeProperties::VBR_BITRATE_NO_LIMIT
              && props.GetVbrBitrateMax() == AEncodeProperties::VBR_BITRATE_NO_LIMIT,
              "a VBR minimum above the maximum leaves no limits");
    }
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the advanced settings in the configuration file.
 *
 * Without their elements all three are off. With each element set, each is
 * on, and the three come back after a save.
 */
static void
test_advanced_settings_file(void)
{
    printf("the advanced settings in the configuration file\n");
    if (write_vbr_config("")) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(!props.GetKeepAllFrequencies() && !props.GetStrictISO() && !props.GetForceMS(),
              "without their elements the three advanced settings are off");
    }
    if (!write_vbr_config("<Keep_all_frequencies use=\"true\" />\n"
                          "            <Strict_ISO use=\"true\" />\n"
                          "            <Forced_mid_side use=\"true\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetKeepAllFrequencies() && props.GetStrictISO() && props.GetForceMS(),
              "each element turns its setting on");
        props.ParamsSave();
    }
    {
        AEncodeProperties reread(NULL);
        reread.ParamsRestore();
        CHECK(reread.GetKeepAllFrequencies() && reread.GetStrictISO() && reread.GetForceMS(),
              "the advanced settings come back after a save");
    }
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Counts the formats of the codec's list by family.
 * @param list  the formats.
 * @param vbr   receives the VBR formats.
 * @return all formats.
 */
static size_t
count_vbr(const std::vector<bitrate_item> &list, unsigned *vbr)
{
    size_t k;

    *vbr = 0;
    for (k = 0; k < list.size(); k++) {
        if (list[k].mode == vbr_mtrh)
            ++*vbr;
    }
    return list.size();
}

/**
 * @brief Checks which formats the codec lists for each combination of the
 *        three families, the CBR range, and the names of the formats.
 *
 * The counts come from the MPEG tables: CBR, 14 bitrates for each version at
 * the 3 MPEG-1 and the 6 MPEG-2 and MPEG-2.5 rates, 252 with both channel
 * counts; ABR, the 11 bitrates from 80 to 160 kbit/s in steps of 8 at the 9
 * rates, 198; VBR, the 10 levels at the 9 rates, 180. A file that leaves out
 * all three families gets CBR. Without a file the codec lists all three. A
 * CBR range of 64 to 128 kbit/s keeps 5 bitrates of each version.
 */
static void
test_format_families(void)
{
    enum { CBR_FORMATS = 252, ABR_FORMATS = 198, VBR_FORMATS = 180, CBR_64_TO_128 = 90 };
    static const char *const use[2] = { "false", "true" };
    char element[256], detail[CTEST_DETAIL_CHARS];
    unsigned combination, right = 0, vbr;
    size_t all;

    printf("the families of formats in the list\n");
    /* Bit 0 CBR, bit 1 ABR, bit 2 VBR; 0 is the file that leaves out all three. */
    for (combination = 0; combination < 8; combination++) {
        unsigned const cbr = (combination & 1) || combination == 0;
        unsigned const abr = (combination >> 1) & 1, with_vbr = (combination >> 2) & 1;

        sprintf(element, "<CBR use=\"%s\" />\n            <ABR use=\"%s\" />\n            <VBR use=\"%s\" />",
                use[combination & 1], use[abr], use[with_vbr]);
        if (!write_vbr_config(element)) {
            CHECK(0, "the configuration file could be written");
            return;
        }
        {
            VbrTable acm;

            all = count_vbr(acm.List(), &vbr);
            if (all == cbr * CBR_FORMATS + abr * ABR_FORMATS + with_vbr * VBR_FORMATS
                && vbr == with_vbr * VBR_FORMATS)
                right++;
            else
                printf("        CBR %u ABR %u VBR %u: %u formats, %u VBR\n", cbr, abr, with_vbr, (unsigned) all, vbr);
        }
    }
    CHECK_EQ_U(right, 8, "each combination of the families lists its formats, and none at all lists CBR");

    if (write_vbr_config("<CBR use=\"true\" min=\"64\" max=\"128\" />\n            <ABR use=\"false\" />\n"
                         "            <VBR use=\"false\" />")) {
        VbrTable acm;
        AEncodeProperties held(NULL);

        all = count_vbr(acm.List(), &vbr);
        sprintf(detail, "%u formats", (unsigned) all);
        ctest_record(all == CBR_64_TO_128, "a CBR range of 64 to 128 kbps lists 5 bitrates of each version", detail);
        held.ParamsRestore();
        held.ParamsSave();
        {
            AEncodeProperties reread(NULL);
            reread.ParamsRestore();
            CHECK(reread.GetCbrOutputMode() && reread.GetCbrBitrateMin() == 64 && reread.GetCbrBitrateMax() == 128
                  && !reread.GetAbrOutputMode() && !reread.GetVbrOutputMode(),
                  "the families and the CBR range come back after a save");
        }
    }
    if (write_vbr_config("<CBR use=\"true\" min=\"100\" max=\"128\" />")) {
        AEncodeProperties props(NULL);
        props.ParamsRestore();
        CHECK(props.GetCbrBitrateMin() == 8 && props.GetCbrBitrateMax() == 320,
              "a CBR range with an end that is not a bitrate of the list keeps the whole range");
    }

    ::DeleteFileA(CONFIG_NAME);
    {
        VbrTable acm;
        AEncodeProperties props(NULL);

        props.ParamsRestore();
        all = count_vbr(acm.List(), &vbr);
        sprintf(detail, "%u formats, %u VBR", (unsigned) all, vbr);
        ctest_record(props.GetCbrOutputMode() && props.GetAbrOutputMode() && props.GetVbrOutputMode()
                     && !props.GetSmartOutputMode() && all == CBR_FORMATS + ABR_FORMATS + VBR_FORMATS
                     && vbr == VBR_FORMATS,
                     "without a file the codec lists all three families, none left out by Smart encoding", detail);

        {
            const std::vector<bitrate_item> &list = acm.List();
            static const char *const want[3] = { "44100 Hz, CBR 128 kbps, Stereo", "44100 Hz, ABR 88 kbps, Stereo",
                                                 "44100 Hz, VBR quality 2 (about " };
            int found[3] = { 0, 0, 0 };
            size_t k;

            for (k = 0; k < list.size(); k++) {
                MPEGLAYER3WAVEFORMAT mp3;
                WCHAR wide[ACMFORMATDETAILS_FORMAT_CHARS];
                char name[ACMFORMATDETAILS_FORMAT_CHARS];
                int f;

                if (list[k].frequency != 44100 || list[k].channels != 2)
                    continue;
                acm.Format((DWORD) k, &mp3, wide);
                WideCharToMultiByte(CP_ACP, 0, wide, -1, name, sizeof(name), NULL, NULL);
                for (f = 0; f < 3; f++) {
                    if (strncmp(name, want[f], strlen(want[f])) == 0)
                        found[f] = 1;
                }
            }
            CHECK(found[0] && found[1] && found[2],
                  "the formats are named \"CBR 128 kbps\", \"ABR 88 kbps\", \"VBR quality 2 (about ... kbps)\"");
        }
    }
}

/**
 * @brief Checks the bitrates that the configuration dialog lists, in order.
 *
 * The dialog and the configuration file store positions in this list. So the
 * list must stay the same: every MPEG-1 and MPEG-2 bitrate, highest first,
 * each once.
 */
static void
test_bitrate_list(void)
{
    static const unsigned int expected[] =
        { 320, 256, 224, 192, 160, 144, 128, 112, 96, 80, 64, 56, 48, 40, 32, 24, 16, 8 };
    const int n = (int) (sizeof(expected) / sizeof(expected[0]));
    AEncodeProperties props(NULL);
    char text[16];
    int i, same = 0;

    printf("the bitrates the configuration dialog lists\n");
    CHECK_EQ_U(props.GetBitrateLentgh(), n, "the dialog lists 18 bitrates");
    for (i = 0; i < n && i < props.GetBitrateLentgh(); i++) {
        if (props.GetBitrateString(text, sizeof(text), i) > 0
            && strtoul(text, NULL, 10) == expected[i]) {
            ++same;
        }
    }
    CHECK_EQ_U(same, n, "every MPEG-1 and MPEG-2 bitrate, highest first, each once");
}

/**
 * @brief Checks that saving works with no configuration file to start from.
 *
 * The installer puts a configuration file beside the codec. The file can
 * still be lost or emptied. The save then creates the elements it needs.
 * Without that, the save returns having written nothing. The settings of the
 * user are then silently not kept.
 *
 * The test checks the round trip, not that the file exists. A save that
 * writes a file the codec cannot read back passes the weaker check.
 */
static void
test_save_without_a_file(void)
{
    /* Not the default. A value the defaults would also produce could not tell
       a save that wrote it from a save that did nothing. */
    const double wanted = 11.75;

    printf("saving after the configuration file has been lost\n");

    ::DeleteFileA(CONFIG_NAME);
    if (!write_config(wanted)) {
        CHECK(0, "the configuration file could be written");
        return;
    }

    AEncodeProperties held(NULL);
    held.ParamsRestore();
    CHECK_EQ_D(held.GetSmartRatio(), wanted, 0.0001,
               "a setting is loaded from the file");

    /* The file goes away with the setting still held in memory - an uninstall
       that took it, a failed write, a user tidying up. */
    ::DeleteFileA(CONFIG_NAME);
    CHECK(::GetFileAttributesA(CONFIG_NAME) == INVALID_FILE_ATTRIBUTES,
          "the file is gone before the save");

    held.ParamsSave();

    {
        AEncodeProperties reread(NULL);

        reread.ParamsRestore();
        CHECK_EQ_D(reread.GetSmartRatio(), wanted, 0.0001,
                   "saving rebuilt the file and the setting came back");
    }

    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks that saving the configuration keeps no memory allocated.
 *
 * Each save without a file creates every element of the configuration. The
 * document holds the elements until the next save reads the file again. So
 * after the first save, the number of allocated blocks stays the same from
 * one save to the next.
 */
static void
test_save_keeps_no_memory(void)
{
    /* Each save creates eight elements. A save that keeps them grows the heap
       by at least eight blocks. */
    const long SAVES = 100;
    char detail[CTEST_DETAIL_CHARS];
    long before, after, i;

    printf("saving the configuration again and again\n");

    AEncodeProperties held(NULL);

    ::DeleteFileA(CONFIG_NAME);
    held.ParamsRestore();
    held.ParamsSave();
    before = ctest_heap_blocks();
    for (i = 0; i < SAVES; i++) {
        ::DeleteFileA(CONFIG_NAME);
        held.ParamsSave();
    }
    after = ctest_heap_blocks();
    ::DeleteFileA(CONFIG_NAME);

    CHECK(before > 0 && after > 0, "the heap blocks are counted");
    snprintf(detail, sizeof detail, "%ld blocks before, %ld after %ld saves", before, after, SAVES);
    ctest_record(after - before < SAVES, "100 saves without a file keep fewer than 100 blocks", detail);
}

/**
 * @brief Leaves every message of the dialog to the default handling.
 * @param dialog   the dialog.
 * @param message  the message.
 * @param wparam   the first message parameter.
 * @param lparam   the second message parameter.
 * @return FALSE, for "not handled".
 */
static INT_PTR CALLBACK
quiet_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    return FALSE;
}

/**
 * @brief Checks the version text that the configuration dialog shows.
 *
 * The test creates the dialog from the resources of the built codec and fills
 * it through AEncodeProperties::InitConfigDlg(). The text is "v" and the whole
 * text of get_lame_version(). In alpha and beta builds, that text includes the
 * build date and time.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_version(const char *driver)
{
    /* Room for "v" and any version text, with space to spare. */
    enum { DIALOG_TEXT_CHARS = 256 };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    char want[DIALOG_TEXT_CHARS], got[DIALOG_TEXT_CHARS];
    HMODULE codec;
    HWND dialog;

    printf("the version text of the configuration dialog\n");
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    /* The codec object reads the version text that the dialog shows. */
    ACM acm(NULL);
    AEncodeProperties props(NULL);

    dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
    CHECK(dialog != NULL, "the configuration dialog is created from the codec's resources");
    if (dialog != NULL) {
        props.InitConfigDlg(dialog);
        got[0] = '\0';
        ::GetDlgItemTextA(dialog, IDC_STATIC_CONFIG_VERSION, got, sizeof got);
        snprintf(want, sizeof want, "v%s", get_lame_version());
        CHECK(strncmp(got, want, sizeof want) == 0, "the dialog shows the whole LAME version");
        printf("        %s\n", got);
        ::DestroyWindow(dialog);
    }
    ::FreeLibrary(codec);
}

/**
 * @brief Checks the names of the channel modes in the configuration dialog
 *        and in the settings file.
 *
 * The dialog lists the modes with the names that the DirectShow filter uses
 * too, and the settings file stores the same names: a file that says "Dual
 * channel" selects dual channel, and saving writes "Dual channel" back.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_channel_modes(const char *driver)
{
    static const char *const labels[] = { "Stereo", "Joint stereo", "Dual channel", "Mono" };
    /* Room for the longest label and for the settings file, with space to spare. */
    enum { LABEL_CHARS = 64, FILE_CHARS = 4096 };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    HMODULE codec;
    HWND dialog;

    printf("the channel modes of the configuration dialog\n");
    if (!write_vbr_config("<Channel mode=\"Dual channel\" force=\"false\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }

    ACM acm(NULL);
    AEncodeProperties props(NULL);

    props.ParamsRestore();
    dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
    CHECK(dialog != NULL, "the configuration dialog is created from the codec's resources");
    if (dialog != NULL) {
        HWND const combo = ::GetDlgItem(dialog, IDC_COMBO_ENC_STEREO);
        char item[LABEL_CHARS];
        int listed = 0;
        LRESULT selected;
        size_t i;

        props.InitConfigDlg(dialog);
        for (i = 0; i < sizeof labels / sizeof labels[0]; i++) {
            item[0] = '\0';
            if (::SendMessageA(combo, CB_GETLBTEXTLEN, i, 0) < LABEL_CHARS) {
                ::SendMessageA(combo, CB_GETLBTEXT, i, (LPARAM) item);
            }
            if (strcmp(item, labels[i]) == 0) {
                listed++;
            }
        }
        CHECK(listed == 4 && ::SendMessageA(combo, CB_GETCOUNT, 0, 0) == 4,
              "the dialog lists Stereo, Joint stereo, Dual channel and Mono");
        selected = ::SendMessageA(combo, CB_GETCURSEL, 0, 0);
        item[0] = '\0';
        if (selected >= 0 && ::SendMessageA(combo, CB_GETLBTEXTLEN, selected, 0) < LABEL_CHARS) {
            ::SendMessageA(combo, CB_GETLBTEXT, selected, (LPARAM) item);
        }
        CHECK(strcmp(item, "Dual channel") == 0, "a file that says \"Dual channel\" selects dual channel");
        printf("        selected \"%s\"\n", item);
        ::DestroyWindow(dialog);
    }
    {
        char text[FILE_CHARS];
        size_t n = 0;
        FILE *f;

        props.ParamsSave();
        f = fopen(CONFIG_NAME, "rb");
        if (f != NULL) {
            n = fread(text, 1, sizeof text - 1, f);
            fclose(f);
        }
        text[n] = '\0';
        CHECK(strstr(text, "mode=\"Dual channel\"") != NULL,
              "saving stores the name \"Dual channel\"");
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the encoding quality in the configuration dialog and in the
 *        settings file.
 *
 * Level 7 from the file sets the slider and its text; without the element,
 * and with a level LAME does not have, the dialog shows LAME's default, 3.
 * Saving writes the level.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_quality(const char *driver)
{
    static const struct {
        const char *element;
        unsigned level;
        const char *text;
        const char *what;
    } cases[] = {
        { "<Quality level=\"7\" />", 7, "7 (very fast)", "a file that says level 7 sets the slider and its text" },
        { "", 3, "3 (default)", "without the element the dialog shows LAME's default, 3" },
        { "<Quality level=\"12\" />", 3, "3 (default)", "a level LAME does not have keeps the default" },
    };
    /* Room for the text of a level and for the settings file. */
    enum { TEXT_CHARS = 64, FILE_CHARS = 4096 };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    HMODULE codec;
    size_t i;

    printf("the encoding quality of the configuration dialog\n");
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    ACM acm(NULL);

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char text[TEXT_CHARS];
        HWND dialog;

        if (!write_vbr_config(cases[i].element)) {
            CHECK(0, "the configuration file could be written");
            break;
        }

        AEncodeProperties props(NULL);

        props.ParamsRestore();
        dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
        CHECK(dialog != NULL, "the configuration dialog is created from the codec's resources");
        if (dialog == NULL)
            continue;
        props.InitConfigDlg(dialog);
        text[0] = '\0';
        ::GetDlgItemTextA(dialog, IDC_STATIC_QUALITY_TEXT, text, sizeof text);
        CHECK(::SendDlgItemMessageA(dialog, IDC_SLIDER_QUALITY, TBM_GETPOS, 0, 0) == (LRESULT) cases[i].level
              && strcmp(text, cases[i].text) == 0 && props.GetQuality() == cases[i].level, cases[i].what);
        printf("        \"%s\"\n", text);
        ::DestroyWindow(dialog);
        if (i == 0) {
            char saved[FILE_CHARS];
            size_t n = 0;
            FILE *f;

            props.ParamsSave();
            f = fopen(CONFIG_NAME, "rb");
            if (f != NULL) {
                n = fread(saved, 1, sizeof saved - 1, f);
                fclose(f);
            }
            saved[n] = '\0';
            CHECK(strstr(saved, "<Quality level=\"7\"") != NULL, "saving writes the level");
        }
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Returns the text of the entry a combo box shows.
 * @param dialog  the dialog
 * @param id      the combo box
 * @param text    receives the text
 * @param size    the size of @p text; at least the longest entry and its NUL
 */
static void
combo_text(HWND dialog, int id, char *text, size_t size)
{
    LRESULT const item = ::SendDlgItemMessageA(dialog, id, CB_GETCURSEL, 0, 0);

    text[0] = '\0';
    if (item != CB_ERR && ::SendDlgItemMessageA(dialog, id, CB_GETLBTEXTLEN, (WPARAM) item, 0) < (LRESULT) size) {
        ::SendDlgItemMessageA(dialog, id, CB_GETLBTEXT, (WPARAM) item, (LPARAM) text);
    }
}

/**
 * @brief Selects the entry of a combo box with the given text.
 * @param dialog  the dialog
 * @param id      the combo box
 * @param text    the text of the entry
 */
static void
combo_select(HWND dialog, int id, const char *text)
{
    LRESULT const item = ::SendDlgItemMessageA(dialog, id, CB_FINDSTRINGEXACT, (WPARAM) -1, (LPARAM) text);

    ::SendDlgItemMessageA(dialog, id, CB_SETCURSEL, (WPARAM) item, 0);
}

/**
 * @brief Checks the line of the settings dialog that lists the ABR bitrates,
 *        and the lowest ABR bitrate it shows.
 *
 * For each ABR range, the test writes a configuration, creates the dialog from
 * the resources of the built codec and fills it through
 * AEncodeProperties::InitConfigDlg(). The ends of the range are bitrates of
 * the list: a saved end that is not one shows as the nearest, the lower of
 * two as near (88 between 80 and 96). A range of up to four bitrates is listed
 * in full; a longer one shows its first three bitrates and its last one.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_abr_list(const char *driver)
{
    /* Room for the longest line of the cases, with space to spare. */
    enum { ABR_LIST_TEXT_CHARS = 128 };
    static const struct {
        const char *abr;
        const char *list;
        const char *min;
    } cases[] = {
        { "<ABR use=\"true\" min=\"88\" max=\"160\" step=\"6\" />",
          "14 bitrates: 160, 154, 148, ..., 82 kbps", "80 kbps" },
        { "<ABR use=\"true\" min=\"128\" max=\"160\" step=\"16\" />",
          "3 bitrates: 160, 144, 128 kbps", "128 kbps" },
        { "<ABR use=\"true\" min=\"128\" max=\"128\" step=\"8\" />",
          "1 bitrate: 128 kbps", "128 kbps" },
    };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    HMODULE codec;
    size_t i;

    printf("the ABR bitrates of the settings dialog\n");
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    ACM acm(NULL);

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char list[ABR_LIST_TEXT_CHARS], min[ABR_LIST_TEXT_CHARS];
        HWND dialog;

        if (!write_vbr_config(cases[i].abr)) {
            CHECK(0, "the configuration file could be written");
            break;
        }

        AEncodeProperties props(NULL);

        props.ParamsRestore();
        dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
        CHECK(dialog != NULL, "the settings dialog is created from the codec's resources");
        if (dialog == NULL)
            continue;
        props.InitConfigDlg(dialog);
        list[0] = '\0';
        ::GetDlgItemTextA(dialog, IDC_STATIC_AVERAGE_LIST, list, sizeof list);
        combo_text(dialog, IDC_COMBO_ABR_MIN, min, sizeof min);
        CHECK(strcmp(list, cases[i].list) == 0 && strcmp(min, cases[i].min) == 0,
              "the dialog lists the bitrates of the ABR range, the lowest a bitrate of the list");
        printf("        \"%s\", lowest \"%s\"\n", list, min);
        ::DestroyWindow(dialog);
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the VBR quality levels of the settings dialog.
 *
 * The dialog is created as in test_config_dialog_version() and filled from a
 * configuration with VBR on and the levels 3 to 7. The two lists
 * show them, each level with its typical 44.1 kHz stereo bitrate. A best level
 * chosen past the worst one takes the worst one with it. Read back, the dialog
 * gives the settings it shows, and with VBR cleared its controls are disabled
 * and VBR is off.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_vbr(const char *driver)
{
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    char best_text[CONTROL_TEXT_CHARS], worst_text[CONTROL_TEXT_CHARS];
    char want_best[CONTROL_TEXT_CHARS], want_worst[CONTROL_TEXT_CHARS];
    HMODULE codec;
    HWND dialog;

    printf("the VBR controls of the settings dialog\n");
    if (!write_vbr_config("<VBR use=\"true\" best=\"3\" worst=\"7\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }

    ACM acm(NULL);
    AEncodeProperties props(NULL);

    props.ParamsRestore();
    dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
    CHECK(dialog != NULL, "the settings dialog is created from the codec's resources");
    if (dialog != NULL) {
        HWND const best = ::GetDlgItem(dialog, IDC_COMBO_VBR_BEST);
        HWND const worst = ::GetDlgItem(dialog, IDC_COMBO_VBR_WORST);

        props.InitConfigDlg(dialog);
        combo_text(dialog, IDC_COMBO_VBR_BEST, best_text, sizeof best_text);
        combo_text(dialog, IDC_COMBO_VBR_WORST, worst_text, sizeof worst_text);
        VbrQualityText(3, want_best, sizeof want_best);
        VbrQualityText(7, want_worst, sizeof want_worst);
        CHECK(::IsDlgButtonChecked(dialog, IDC_CHECK_ENC_VBR) == BST_CHECKED && strcmp(best_text, want_best) == 0
              && strcmp(worst_text, want_worst) == 0 && ::IsWindowEnabled(best) && ::IsWindowEnabled(worst),
              "the dialog shows VBR on, levels 3 to 7, each with its typical bitrate");
        printf("        \"%s\", \"%s\"\n", best_text, worst_text);

        VbrQualityText(9, want_best, sizeof want_best);
        combo_select(dialog, IDC_COMBO_VBR_BEST, want_best);
        AEncodeProperties::KeepRangeInOrder(dialog, IDC_COMBO_VBR_BEST, IDC_COMBO_VBR_WORST, IDC_COMBO_VBR_BEST);
        props.UpdateValueFromDlg(dialog);
        CHECK(props.GetVbrOutputMode() && props.GetVbrQualityBest() == 9 && props.GetVbrQualityWorst() == 9,
              "a best level chosen past the worst one takes it along, and is read back");

        ::CheckDlgButton(dialog, IDC_CHECK_ENC_VBR, BST_UNCHECKED);
        props.UpdateValueFromDlg(dialog);
        CHECK(!props.GetVbrOutputMode() && !::IsWindowEnabled(best) && !::IsWindowEnabled(worst),
              "with VBR cleared, VBR is off and its controls are disabled");
        ::DestroyWindow(dialog);
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the families of the "Formats" tab: the check boxes, the CBR
 *        range, the last family, and the line that counts the list.
 *
 * A file with CBR from 64 to 128 kbit/s and neither ABR nor VBR: the dialog
 * shows it, the controls of ABR and VBR are disabled, and the count says 90
 * formats, all CBR, as the codec lists them (test_format_families()). The
 * last ticked family cannot be cleared. A lowest CBR bitrate chosen above the
 * highest takes the highest along. With VBR ticked the count adds its 180
 * formats. Read back, the families and ranges are the ones on the tab.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_families(const char *driver)
{
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    char count[CONTROL_TEXT_CHARS], low[CONTROL_TEXT_CHARS], high[CONTROL_TEXT_CHARS];
    HMODULE codec;
    HWND dialog;

    printf("the families of the settings dialog\n");
    if (!write_vbr_config("<CBR use=\"true\" min=\"64\" max=\"128\" />\n            <ABR use=\"false\" />\n"
                          "            <VBR use=\"false\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }

    ACM acm(NULL);
    AEncodeProperties props(NULL);

    props.ParamsRestore();
    dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
    CHECK(dialog != NULL, "the settings dialog is created from the codec's resources");
    if (dialog != NULL) {
        props.InitConfigDlg(dialog);
        combo_text(dialog, IDC_COMBO_CBR_MIN, low, sizeof low);
        combo_text(dialog, IDC_COMBO_CBR_MAX, high, sizeof high);
        count[0] = '\0';
        ::GetDlgItemTextA(dialog, IDC_STATIC_FORMAT_COUNT, count, sizeof count);
        CHECK(::IsDlgButtonChecked(dialog, IDC_CHECK_ENC_CBR) == BST_CHECKED && strcmp(low, "64 kbps") == 0
              && strcmp(high, "128 kbps") == 0 && ::IsDlgButtonChecked(dialog, IDC_CHECK_ENC_ABR) != BST_CHECKED
              && ::IsDlgButtonChecked(dialog, IDC_CHECK_ENC_VBR) != BST_CHECKED
              && !::IsWindowEnabled(::GetDlgItem(dialog, IDC_COMBO_ABR_MIN))
              && !::IsWindowEnabled(::GetDlgItem(dialog, IDC_COMBO_VBR_BEST)),
              "the dialog shows CBR from 64 to 128 kbps, ABR and VBR cleared and their controls disabled");
        CHECK(strcmp(count, "The list offers 90 formats: 90 CBR, 0 ABR, 0 VBR.") == 0,
              "the count line counts the list as the codec builds it");
        printf("        \"%s\"\n", count);

        ::CheckDlgButton(dialog, IDC_CHECK_ENC_CBR, BST_UNCHECKED);
        AEncodeProperties::KeepOneFamily(dialog, IDC_CHECK_ENC_CBR);
        CHECK(::IsDlgButtonChecked(dialog, IDC_CHECK_ENC_CBR) == BST_CHECKED,
              "the last ticked family cannot be cleared");

        combo_select(dialog, IDC_COMBO_CBR_MIN, "160 kbps");
        AEncodeProperties::KeepRangeInOrder(dialog, IDC_COMBO_CBR_MIN, IDC_COMBO_CBR_MAX, IDC_COMBO_CBR_MIN);
        combo_text(dialog, IDC_COMBO_CBR_MAX, high, sizeof high);
        CHECK(strcmp(high, "160 kbps") == 0, "a lowest CBR bitrate chosen above the highest takes it along");

        ::CheckDlgButton(dialog, IDC_CHECK_ENC_VBR, BST_CHECKED);
        props.UpdateFormatCount(dialog);
        ::GetDlgItemTextA(dialog, IDC_STATIC_FORMAT_COUNT, count, sizeof count);
        props.UpdateValueFromDlg(dialog);
        CHECK(strstr(count, ", 180 VBR.") != NULL && props.GetCbrOutputMode() && props.GetVbrOutputMode()
              && !props.GetAbrOutputMode() && props.GetCbrBitrateMin() == 160 && props.GetCbrBitrateMax() == 160,
              "with VBR ticked the count adds its formats; read back, the dialog gives what it shows");
        printf("        \"%s\"\n", count);
        ::DestroyWindow(dialog);
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the VBR bitrate limits of the configuration dialog.
 *
 * Each list holds "No limit" and the 18 bitrates. A file with 96 and 192
 * kbit/s and the minimum enforced shows them. A minimum chosen above the
 * maximum takes the maximum along, and a maximum chosen below the minimum the
 * minimum; "No limit" leaves the other one alone. Read back, the dialog gives
 * what it shows. With VBR cleared the three controls are disabled, and a file
 * without limits shows none.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_vbr_limits(const char *driver)
{
    /* "No limit" and the 18 bitrates of the list. */
    enum { LIMIT_ENTRIES = 1 + 18 };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    char low[16], high[16], first[16];
    HMODULE codec;
    HWND dialog;

    printf("the VBR bitrate limits of the configuration dialog\n");
    if (!write_vbr_config("<VBR use=\"true\" min=\"96\" max=\"192\" enforce_min=\"true\" />")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }

    ACM acm(NULL);
    {
        AEncodeProperties props(NULL);

        props.ParamsRestore();
        dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
        CHECK(dialog != NULL, "the configuration dialog is created from the codec's resources");
        if (dialog != NULL) {
            props.InitConfigDlg(dialog);
            combo_text(dialog, IDC_COMBO_VBR_MIN, low, sizeof low);
            combo_text(dialog, IDC_COMBO_VBR_MAX, high, sizeof high);
            first[0] = '\0';
            if (::SendDlgItemMessageA(dialog, IDC_COMBO_VBR_MIN, CB_GETLBTEXTLEN, 0, 0) < (LRESULT) sizeof first) {
                ::SendDlgItemMessageA(dialog, IDC_COMBO_VBR_MIN, CB_GETLBTEXT, 0, (LPARAM) first);
            }
            CHECK(::SendDlgItemMessageA(dialog, IDC_COMBO_VBR_MIN, CB_GETCOUNT, 0, 0) == LIMIT_ENTRIES
                  && ::SendDlgItemMessageA(dialog, IDC_COMBO_VBR_MAX, CB_GETCOUNT, 0, 0) == LIMIT_ENTRIES
                  && strcmp(first, "No limit") == 0,
                  "each list holds \"No limit\" and the 18 bitrates");
            CHECK(strcmp(low, "96 kbps") == 0 && strcmp(high, "192 kbps") == 0
                  && ::IsDlgButtonChecked(dialog, IDC_CHECK_VBR_ENFORCE_MIN) == BST_CHECKED
                  && ::IsWindowEnabled(::GetDlgItem(dialog, IDC_COMBO_VBR_MIN)),
                  "the dialog shows the limits of the file, 96 to 192 kbps, the minimum enforced");
            printf("        \"%s\", \"%s\"\n", low, high);

            combo_select(dialog, IDC_COMBO_VBR_MIN, "256 kbps");
            AEncodeProperties::KeepVbrLimitsInOrder(dialog, IDC_COMBO_VBR_MIN);
            combo_text(dialog, IDC_COMBO_VBR_MAX, high, sizeof high);
            CHECK(strcmp(high, "256 kbps") == 0, "a minimum chosen above the maximum takes the maximum along");
            combo_select(dialog, IDC_COMBO_VBR_MAX, "64 kbps");
            AEncodeProperties::KeepVbrLimitsInOrder(dialog, IDC_COMBO_VBR_MAX);
            combo_text(dialog, IDC_COMBO_VBR_MIN, low, sizeof low);
            CHECK(strcmp(low, "64 kbps") == 0, "a maximum chosen below the minimum takes the minimum along");

            ::CheckDlgButton(dialog, IDC_CHECK_VBR_ENFORCE_MIN, BST_UNCHECKED);
            props.UpdateValueFromDlg(dialog);
            CHECK(props.GetVbrBitrateMin() == 64 && props.GetVbrBitrateMax() == 64 && !props.GetVbrEnforceMin(),
                  "read back, the dialog gives 64 to 64 kbps, the minimum not enforced");

            combo_select(dialog, IDC_COMBO_VBR_MAX, "No limit");
            combo_select(dialog, IDC_COMBO_VBR_MIN, "320 kbps");
            AEncodeProperties::KeepVbrLimitsInOrder(dialog, IDC_COMBO_VBR_MIN);
            props.UpdateValueFromDlg(dialog);
            CHECK(props.GetVbrBitrateMin() == 320 && props.GetVbrBitrateMax() == AEncodeProperties::VBR_BITRATE_NO_LIMIT,
                  "with no maximum, any minimum stays");

            ::CheckDlgButton(dialog, IDC_CHECK_ENC_VBR, BST_UNCHECKED);
            props.UpdateValueFromDlg(dialog);
            CHECK(!::IsWindowEnabled(::GetDlgItem(dialog, IDC_COMBO_VBR_MIN))
                  && !::IsWindowEnabled(::GetDlgItem(dialog, IDC_COMBO_VBR_MAX))
                  && !::IsWindowEnabled(::GetDlgItem(dialog, IDC_CHECK_VBR_ENFORCE_MIN)),
                  "with VBR cleared, the limits are disabled");
            ::DestroyWindow(dialog);
        }
    }
    if (write_vbr_config("<VBR use=\"true\" />")) {
        AEncodeProperties props(NULL);

        props.ParamsRestore();
        dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
        if (dialog != NULL) {
            props.InitConfigDlg(dialog);
            combo_text(dialog, IDC_COMBO_VBR_MIN, low, sizeof low);
            combo_text(dialog, IDC_COMBO_VBR_MAX, high, sizeof high);
            CHECK(strcmp(low, "No limit") == 0 && strcmp(high, "No limit") == 0
                  && ::IsDlgButtonChecked(dialog, IDC_CHECK_VBR_ENFORCE_MIN) == BST_UNCHECKED,
                  "a file without limits: no limit either way, the minimum not enforced");
            ::DestroyWindow(dialog);
        }
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the "Encoding" tab of the settings dialog.
 *
 * The tab shows the encoding controls and hides those of the formats; the
 * other tab the other way round. The advanced settings of the file show on
 * it. Forced mid / side can be chosen only with joint stereo. Read back,
 * each setting is the one its control shows.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_encoding_tab(const char *driver)
{
    static const char *const files[2] = {
        "<Channel mode=\"Joint stereo\" />\n            <Keep_all_frequencies use=\"true\" />\n"
        "            <Strict_ISO use=\"true\" />\n            <Forced_mid_side use=\"true\" />",
        "<Channel mode=\"Stereo\" />\n            <Forced_mid_side use=\"true\" />",
    };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    HMODULE codec;
    int f;

    printf("the Encoding tab of the settings dialog\n");
    ::InitCommonControlsEx(&controls);
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    ACM acm(NULL);

    for (f = 0; f < 2; f++) {
        HWND dialog;

        if (!write_vbr_config(files[f])) {
            CHECK(0, "the configuration file could be written");
            break;
        }

        AEncodeProperties props(NULL);

        props.ParamsRestore();
        dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(IDD_CONFIG), NULL, quiet_dialog_proc, 0);
        CHECK(dialog != NULL, "the settings dialog is created from the codec's resources");
        if (dialog == NULL)
            continue;
        props.InitConfigDlg(dialog);
        if (f == 0) {
            int const formats_first = (::GetWindowLongW(::GetDlgItem(dialog, IDC_CHECK_ENC_CBR), GWL_STYLE) & WS_VISIBLE)
                && !(::GetWindowLongW(::GetDlgItem(dialog, IDC_CHECK_KEEP_ALL), GWL_STYLE) & WS_VISIBLE);

            AEncodeProperties::ShowSettingsTab(dialog, AEncodeProperties::TAB_ENCODING);
            CHECK(formats_first && (::GetWindowLongW(::GetDlgItem(dialog, IDC_CHECK_KEEP_ALL), GWL_STYLE) & WS_VISIBLE)
                  && !(::GetWindowLongW(::GetDlgItem(dialog, IDC_CHECK_ENC_CBR), GWL_STYLE) & WS_VISIBLE),
                  "the dialog opens on Formats; the Encoding tab shows its controls and hides the others");
            CHECK(::IsDlgButtonChecked(dialog, IDC_CHECK_KEEP_ALL) == BST_CHECKED
                  && ::IsDlgButtonChecked(dialog, IDC_CHECK_STRICT_ISO) == BST_CHECKED
                  && ::IsDlgButtonChecked(dialog, IDC_CHECK_FORCE_MS) == BST_CHECKED
                  && ::IsWindowEnabled(::GetDlgItem(dialog, IDC_CHECK_FORCE_MS)),
                  "with joint stereo the tab shows the three advanced settings of the file, forced mid / side enabled");
            ::CheckDlgButton(dialog, IDC_CHECK_KEEP_ALL, BST_UNCHECKED);
            props.UpdateValueFromDlg(dialog);
            CHECK(!props.GetKeepAllFrequencies() && props.GetStrictISO() && props.GetForceMS(),
                  "read back, the tab gives what it shows");
        } else {
            CHECK(!::IsWindowEnabled(::GetDlgItem(dialog, IDC_CHECK_FORCE_MS)),
                  "with stereo, forced mid / side is disabled");
        }
        ::DestroyWindow(dialog);
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/** @brief The button that close_config_dialog() presses: IDOK or IDCANCEL. */
static WORD config_dialog_button;
/** @brief Set by close_config_dialog() when it finds the dialog. */
static int config_dialog_found;
/** @brief The font of the dialog that close_config_dialog() found. */
static LOGFONTW config_dialog_font;

/**
 * @brief Finds the first dialog window of this thread.
 * @param window  a top-level window of this thread.
 * @param found   the HWND that receives the dialog.
 * @return FALSE once the dialog is found, to stop the enumeration.
 */
static BOOL CALLBACK
find_dialog(HWND window, LPARAM found)
{
    static const char DIALOG_CLASS[] = "#32770";
    char name[sizeof DIALOG_CLASS];

    if (::GetClassNameA(window, name, sizeof name) > 0
        && strncmp(name, DIALOG_CLASS, sizeof DIALOG_CLASS) == 0) {
        *(HWND *) found = window;
        return FALSE;
    }
    return TRUE;
}

/** @brief The period of the timer that drives a modal dialog of the test, in
 *         milliseconds: often enough that the dialog closes soon after it opens. */
static const UINT DIALOG_TIMER_MS = 200;

/**
 * @brief Presses config_dialog_button in the dialog of this thread.
 *
 * A timer calls it from the message loop of the modal dialog. Once it finds
 * the dialog, it keeps the dialog's font in config_dialog_font and stops the
 * timer.
 *
 * @param window   NULL, the timer has no window.
 * @param message  WM_TIMER.
 * @param id       the timer.
 * @param time     the tick count.
 */
static void CALLBACK
close_config_dialog(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    HWND dialog = NULL;

    ::EnumThreadWindows(::GetCurrentThreadId(), find_dialog, (LPARAM) &dialog);
    if (dialog != NULL) {
        HFONT const font = (HFONT) ::SendMessageW(dialog, WM_GETFONT, 0, 0);

        config_dialog_found = 1;
        if (font == NULL || ::GetObjectW(font, sizeof config_dialog_font, &config_dialog_font) == 0)
            memset(&config_dialog_font, 0, sizeof config_dialog_font);
        ::KillTimer(NULL, id);
        ::PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(config_dialog_button, BN_CLICKED), 0);
    }
}

/**
 * @brief Checks what AEncodeProperties::Config() returns after OK and after
 *        Cancel.
 *
 * The codec reports the result to the host of the DRV_CONFIGURE message. OK
 * saves the configuration, so the test removes the file afterwards. The
 * dialog that Config() shows uses the font of the desktop: the typeface that
 * the system reports (desktop_dialog_font()) and its height, to the nearest
 * point.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_result(const char *driver)
{
    static const struct {
        WORD button;
        bool result;
        const char *what;
    } cases[] = {
        { IDCANCEL, false, "the configuration dialog returns false after Cancel" },
        { IDOK, true, "the configuration dialog returns true after OK" },
    };
    HMODULE codec;
    size_t i;

    printf("the result of the configuration dialog\n");
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    AEncodeProperties props(NULL);

    ::DeleteFileA(CONFIG_NAME);
    props.ParamsRestore();
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        UINT_PTR timer;
        bool result;

        config_dialog_button = cases[i].button;
        config_dialog_found = 0;
        timer = ::SetTimer(NULL, 0, DIALOG_TIMER_MS, close_config_dialog);
        result = props.Config(codec, NULL);
        ::KillTimer(NULL, timer);
        CHECK(config_dialog_found, "the configuration dialog opens");
        CHECK(result == cases[i].result, cases[i].what);
    }
    {
        LOGFONTW desktop;
        LONG height_difference;

        memset(&desktop, 0, sizeof desktop);
        CHECK(desktop_dialog_font(&desktop), "the desktop reports its dialog font");
        height_difference = config_dialog_font.lfHeight - desktop.lfHeight;
        CHECK(wcsncmp(config_dialog_font.lfFaceName, desktop.lfFaceName, LF_FACESIZE) == 0
              && height_difference >= -1 && height_difference <= 1,
              "the configuration dialog uses the font of the desktop");
        printf("        dialog \"%ls\" %ld, desktop \"%ls\" %ld\n", config_dialog_font.lfFaceName,
               (long) config_dialog_font.lfHeight, desktop.lfFaceName, (long) desktop.lfHeight);
    }
    ::DeleteFileA(CONFIG_NAME);
    ::FreeLibrary(codec);
}

/** @brief What drive_settings_tabs() saw, and the button it presses. */
static struct {
    WORD button;            /**< IDOK or IDCANCEL, for the settings dialog */
    int switched;           /**< the Encoding tab showed after the tab changed */
    int done;               /**< the dialog was closed */
} tabs_drive;

/**
 * @brief Drives the settings dialog of the codec as a user does: changes to
 *        the "Encoding" tab with the tab control, ticks "Keep all
 *        frequencies", and presses tabs_drive.button.
 *
 * It runs as close_config_dialog() does.
 *
 * @param window   NULL, the timer has no window.
 * @param message  WM_TIMER.
 * @param id       the timer.
 * @param time     the tick count.
 */
static void CALLBACK
drive_settings_tabs(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    HWND dialog = NULL;

    ::EnumThreadWindows(::GetCurrentThreadId(), find_dialog, (LPARAM) &dialog);
    if (dialog == NULL)
        return;
    ::KillTimer(NULL, id);
    /* Moving the focus of a tab control changes its tab and notifies the
       dialog, as a click does. */
    ::SendMessageA(::GetDlgItem(dialog, IDC_TAB_SETTINGS), TCM_SETCURFOCUS, AEncodeProperties::TAB_ENCODING, 0);
    tabs_drive.switched = ::IsWindowVisible(::GetDlgItem(dialog, IDC_CHECK_KEEP_ALL))
        && !::IsWindowVisible(::GetDlgItem(dialog, IDC_CHECK_ENC_CBR));
    ::CheckDlgButton(dialog, IDC_CHECK_KEEP_ALL, BST_CHECKED);
    ::PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(tabs_drive.button, BN_CLICKED), 0);
    tabs_drive.done = 1;
}

/**
 * @brief Checks the tabs of the settings dialog as the codec shows it.
 *
 * The test changes to the "Encoding" tab and ticks "Keep all frequencies"
 * (drive_settings_tabs()). OK keeps and saves the setting; Cancel neither.
 *
 * @param driver  the path of the built codec.
 */
static void
test_settings_tabs_through_the_codec(const char *driver)
{
    static const struct {
        WORD button;
        bool kept;
        const char *what;
    } cases[] = {
        { IDOK, true, "OK keeps and saves what the Encoding tab set" },
        { IDCANCEL, false, "Cancel neither keeps nor saves what the Encoding tab set" },
    };
    /* Room for the settings file. */
    enum { FILE_CHARS = 4096 };
    HMODULE codec;
    size_t i;

    printf("the tabs of the settings dialog through the codec\n");
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char saved[FILE_CHARS];
        size_t n = 0;
        UINT_PTR timer;
        FILE *f;

        if (!write_vbr_config("")) {
            CHECK(0, "the configuration file could be written");
            break;
        }

        AEncodeProperties props(NULL);

        props.ParamsRestore();
        memset(&tabs_drive, 0, sizeof tabs_drive);
        tabs_drive.button = cases[i].button;
        timer = ::SetTimer(NULL, 0, DIALOG_TIMER_MS, drive_settings_tabs);
        (void) props.Config(codec, NULL);
        ::KillTimer(NULL, timer);
        f = fopen(CONFIG_NAME, "rb");
        if (f != NULL) {
            n = fread(saved, 1, sizeof saved - 1, f);
            fclose(f);
        }
        saved[n] = '\0';
        CHECK(tabs_drive.done && tabs_drive.switched, "the tab control changes to the Encoding tab");
        CHECK(props.GetKeepAllFrequencies() == cases[i].kept
              && (strstr(saved, "<Keep_all_frequencies use=\"true\"") != NULL) == cases[i].kept,
              cases[i].what);
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks the layout of the two dialogs of the codec in three fonts, the
 *        settings dialog one tab at a time.
 *
 * The fonts are the one of the resource, the font of the desktop
 * (desktop_dialog_font()), and that font one and a half times as high, as
 * a larger text size of the desktop gives it. The test creates each dialog
 * as the codec does, through DialogTemplateWithFont(), fills in the
 * texts that the codec sets at run time and runs check_dialog_layout().
 *
 * @param driver  the path of the built codec.
 */
static void
test_dialog_layout(const char *driver)
{
    enum { RESOURCE_FONT, DESKTOP_FONT, LARGER_FONT, FONTS };
    /* The controls with an access key on each tab of the settings dialog:
       Formats, the three families and their ten range controls and Smart;
       Encoding, the channel mode and Force, the encoding quality, five frame
       options and three advanced settings. The about dialog has none. */
    static const size_t tab_keys[AEncodeProperties::SETTINGS_TABS] = { 14, 11 };
    static const char *const tab_names[AEncodeProperties::SETTINGS_TABS] = { "Formats", "Encoding" };
    static const char *const font_names[FONTS] = { "the resource font", "the desktop font", "a larger desktop font" };
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    LOGFONTW desktop;
    HMODULE codec;
    int f;

    printf("the layout of the codec's dialogs\n");
    ::InitCommonControlsEx(&controls);
    memset(&desktop, 0, sizeof desktop);
    if (!desktop_dialog_font(&desktop)) {
        CHECK(0, "the desktop reports its dialog font");
        return;
    }
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        return;
    }

    ACM acm(NULL);
    AEncodeProperties props(NULL);

    props.ParamsRestore();
    for (f = 0; f < FONTS; f++) {
        static const WORD dialogs[] = { IDD_CONFIG, IDD_ABOUT };
        LOGFONTW font = desktop;
        size_t d;

        if (f == LARGER_FONT)
            font.lfHeight = desktop.lfHeight * 3 / 2;
        for (d = 0; d < sizeof dialogs / sizeof dialogs[0]; d++) {
            std::vector<BYTE> dialog_template;
            char what[100];
            HWND dialog;

            snprintf(what, sizeof what, "%s in %s", dialogs[d] == IDD_CONFIG ? "settings" : "about", font_names[f]);
            if (f == RESOURCE_FONT) {
                dialog = ::CreateDialogParamA(codec, MAKEINTRESOURCEA(dialogs[d]), NULL, quiet_dialog_proc, 0);
            } else {
                CHECK(DialogTemplateWithFont(codec, dialogs[d], font, dialog_template),
                      "the dialog template takes another font");
                dialog = dialog_template.empty() ? NULL
                    : ::CreateDialogIndirectParamA(codec, (LPCDLGTEMPLATEA) &dialog_template[0], NULL,
                                                   quiet_dialog_proc, 0);
            }
            CHECK(dialog != NULL, "the dialog is created");
            if (dialog == NULL)
                continue;
            if (dialogs[d] == IDD_CONFIG) {
                int tab;

                props.InitConfigDlg(dialog);
                for (tab = 0; tab < AEncodeProperties::SETTINGS_TABS; tab++) {
                    char tab_what[sizeof what + 32];

                    AEncodeProperties::ShowSettingsTab(dialog, tab);
                    snprintf(tab_what, sizeof tab_what, "%s, %s", what, tab_names[tab]);
                    check_dialog_layout(dialog, tab_keys[tab], tab_what);
                }
            } else {
                char title[CONTROL_TEXT_CHARS];

                snprintf(title, sizeof title, "LAME MP3 codec v%s", ACM::GetVersionString());
                ::SetDlgItemTextA(dialog, IDC_STATIC_ABOUT_TITLE, title);
                check_dialog_layout(dialog, 0, what);
            }
            ::DestroyWindow(dialog);
        }
    }
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Checks that opening and cancelling the configuration dialog keeps the
 *        saved settings.
 *
 * The file names its configuration "Current" and holds the ABR range 96 to
 * 192 kbps in steps of 32. After each of three openings, each closed with
 * Cancel, the file still names "Current" and the codec still reads that range.
 *
 * @param driver  the path of the built codec.
 */
static void
test_config_dialog_keeps_the_settings(const char *driver)
{
    /* Openings; and room for the settings file. */
    enum { OPENINGS = 3, FILE_CHARS = 4096 };
    HMODULE codec;
    int i, named = 0, kept = 0;

    printf("the configuration dialog keeps the saved settings\n");
    if (!write_raw_config("<lame_acm>\n    <encodings default=\"Current\">\n        <config name=\"Current\">\n"
                          "            <ABR use=\"true\" min=\"96\" max=\"192\" step=\"32\" />\n"
                          "        </config>\n    </encodings>\n</lame_acm>\n")) {
        CHECK(0, "the configuration file could be written");
        return;
    }
    codec = ::LoadLibraryExA(driver, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (codec == NULL) {
        CHECK(0, "the codec's resources load");
        ::DeleteFileA(CONFIG_NAME);
        return;
    }
    for (i = 0; i < OPENINGS; i++) {
        char saved[FILE_CHARS];
        size_t n = 0;
        UINT_PTR timer;
        FILE *f;

        {
            AEncodeProperties props(NULL);

            props.ParamsRestore();
            config_dialog_button = IDCANCEL;
            config_dialog_found = 0;
            timer = ::SetTimer(NULL, 0, DIALOG_TIMER_MS, close_config_dialog);
            (void) props.Config(codec, NULL);
            ::KillTimer(NULL, timer);
        }
        f = fopen(CONFIG_NAME, "rb");
        if (f != NULL) {
            n = fread(saved, 1, sizeof saved - 1, f);
            fclose(f);
        }
        saved[n] = '\0';
        if (config_dialog_found && strstr(saved, "<encodings default=\"Current\"") != NULL)
            named++;
        {
            AEncodeProperties reread(NULL);

            reread.ParamsRestore();
            if (reread.GetAbrOutputMode() && reread.GetAbrBitrateMin() == 96 && reread.GetAbrBitrateMax() == 192
                && reread.GetAbrBitrateStep() == 32)
                kept++;
        }
    }
    CHECK_EQ_U(named, OPENINGS, "after each opening the file still names the configuration \"Current\"");
    CHECK_EQ_U(kept, OPENINGS, "after each opening the codec still reads the saved ABR range");
    ::FreeLibrary(codec);
    ::DeleteFileA(CONFIG_NAME);
}

/**
 * @brief Asserts that a multimedia call returned MMSYSERR_NOERROR. The detail
 *        line shows the result.
 */
#define CHECK_MM(mr, what)                                               \
    do {                                                                 \
        MMRESULT ctest_mr_ = (mr);                                       \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "mmresult %u", (unsigned) ctest_mr_);           \
        ctest_record(ctest_mr_ == MMSYSERR_NOERROR, (what), ctest_d_);   \
    } while (0)

/** @brief Concert A, the frequency of the test tone in the source buffer. */
#define TONE_HZ         440.0
/** @brief The amplitude of the tone. It is well below full scale, so nothing clips. */
#define TONE_AMPLITUDE  16000.0

/**
 * @brief The fdwFlags values of the codec's own MP3 formats. The codec writes
 *        a bit outside the padding modes into an ABR format and 4 into a CBR
 *        format, and reads the bit back when a stream opens.
 */
#define ACM_FLAGS_ABR 0x80000000UL
#define ACM_FLAGS_CBR 4

/** @brief Fills in the MPEG Layer-3 format that an application passes to the ACM. */
static void
fill_mp3_format(MPEGLAYER3WAVEFORMAT *mp3, DWORD rate, WORD channels, DWORD bps)
{
    memset(mp3, 0, sizeof(*mp3));
    mp3->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    mp3->wfx.nChannels = channels;
    mp3->wfx.nSamplesPerSec = rate;
    mp3->wfx.nAvgBytesPerSec = bps / 8;
    mp3->wfx.nBlockAlign = 1;
    mp3->wfx.wBitsPerSample = 0;
    mp3->wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
    mp3->wID = MPEGLAYER3_ID_MPEG;
    mp3->fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
    mp3->nBlockSize = MP3_SAMPLES_PER_FRAME;
    mp3->nFramesPerBlock = 1;
    mp3->nCodecDelay = 0;
}

/** @brief Fills in the PCM format for the source side of the conversion. */
static void
fill_pcm_format(WAVEFORMATEX *pcm, DWORD rate, WORD channels)
{
    memset(pcm, 0, sizeof(*pcm));
    pcm->wFormatTag = WAVE_FORMAT_PCM;
    pcm->nChannels = channels;
    pcm->nSamplesPerSec = rate;
    pcm->wBitsPerSample = 16;
    pcm->nBlockAlign = (WORD) (channels * 2);
    pcm->nAvgBytesPerSec = rate * pcm->nBlockAlign;
    pcm->cbSize = 0;
}

/*
 * The ACM types below are named with an explicit A suffix, and so are the two
 * calls that take them.
 *
 * ACM/ddk/msacmdrv.h redefines ACMDRIVERDETAILS, ACMFORMATDETAILS and their
 * pointer forms to the wide variants unconditionally - not under UNICODE, which
 * this build does not define. That is the driver side's convention, and it is
 * right for the codec's own sources, which is why the header does it. But this
 * file reaches the ACM from the application side in the same translation unit,
 * where acmDriverDetails() and acmFormatEnum() resolve to their narrow forms.
 * Spelling both halves explicitly is what keeps the pair consistent; leaving
 * the macros to decide gives a wide structure to a narrow function.
 */

/** @brief Counts the formats that the codec offers, and prints the first three. */
static BOOL CALLBACK
format_cb(HACMDRIVERID hadid, LPACMFORMATDETAILSA pafd, DWORD_PTR user, DWORD fdw)
{
    unsigned *count = (unsigned *) user;

    (void) hadid;
    (void) fdw;
    if (*count < 3) {
        printf("        %s\n", pafd->szFormat);
    }
    ++*count;
    return TRUE;
}

/** @brief A format to look for in the enumeration, and the name the codec gives it. */
typedef struct {
    int match_any;                          /**< take the first format offered */
    DWORD rate;                             /**< sample rate to match */
    DWORD bytes_per_sec;                    /**< byte rate to match */
    WORD channels;                          /**< channel count to match */
    DWORD flags;                            /**< Layer-3 tail flags to match */
    int found;                              /**< set when a format matches */
    MPEGLAYER3WAVEFORMAT format;            /**< the format that matched */
    char name[ACMFORMATDETAILS_FORMAT_CHARS]; /**< the name the codec gives it */
} format_search;

/**
 * @brief Keeps the first enumerated format that matches the search, and its
 *        name.
 *
 * The tail flags are part of the key. The codec can offer a constant-rate and
 * an average-rate format with the same sample rate, bitrate and channel
 * count. The two formats have different names. A search without the flags
 * matches whichever format comes first. It then compares the names of two
 * different formats.
 *
 * @param hadid the driver being enumerated, unused
 * @param pafd one format the driver offers, and the name it gives it
 * @param user the @c format_search that this call fills in
 * @param fdw the enumeration flags, unused
 * @return TRUE, so the enumeration runs to the end
 */
static BOOL CALLBACK
find_format_cb(HACMDRIVERID hadid, LPACMFORMATDETAILSA pafd, DWORD_PTR user, DWORD fdw)
{
    format_search *want = (format_search *) user;
    const MPEGLAYER3WAVEFORMAT *mp3 = (const MPEGLAYER3WAVEFORMAT *) pafd->pwfx;

    (void) hadid;
    (void) fdw;
    if (want->found || pafd->pwfx->cbSize < MPEGLAYER3_WFX_EXTRA_BYTES) {
        return TRUE;
    }
    if (want->match_any
        || (pafd->pwfx->nSamplesPerSec == want->rate
            && pafd->pwfx->nAvgBytesPerSec == want->bytes_per_sec
            && pafd->pwfx->nChannels == want->channels
            && mp3->fdwFlags == want->flags)) {
        want->format = *mp3;
        strncpy(want->name, pafd->szFormat, sizeof(want->name) - 1);
        want->name[sizeof(want->name) - 1] = '\0';
        want->found = 1;
    }
    return TRUE;
}

/**
 * @brief Walks the MPEG Layer-3 format list of the codec and fills in a search.
 * @param had the opened driver
 * @param want what to look for. It receives the match.
 * @return the result of the enumeration call
 */
static MMRESULT
enumerate_formats(HACMDRIVER had, format_search *want)
{
    MPEGLAYER3WAVEFORMAT probe;
    ACMFORMATDETAILSA fd;

    memset(&fd, 0, sizeof(fd));
    fd.cbStruct = sizeof(fd);
    fd.pwfx = (WAVEFORMATEX *) &probe;
    fd.cbwfx = sizeof(probe);
    fd.dwFormatTag = WAVE_FORMAT_MPEGLAYER3;
    fill_mp3_format(&probe, 44100, 2, 128000);
    return acmFormatEnumA(had, &fd, find_format_cb, (DWORD_PTR) want, 0);
}

/**
 * @brief Checks the format that the codec suggests for a PCM source, and the
 *        name it gives that format.
 *
 * The compression chooser of an application makes these calls before it can
 * list this codec. First it asks which format to encode the PCM stream to.
 * Then it asks for a description of that format to show in the list. Suppose
 * the codec fills in the suggestion as if the destination were PCM. The ACM
 * then lists it under a wording that the ACM generates from those fields. The
 * encoded file then has a header that the player must correct.
 *
 * The test compares the description with the format list of the codec, not
 * with a fixed string. So a change to the wording of the format string does
 * not fail this test.
 *
 * @param had the opened driver
 */
static void
test_format_negotiation(HACMDRIVER had)
{
    const DWORD rate = 44100;
    const WORD channels = 2;
    /* What the suggestion carries: 64 kbit/s for each channel. */
    const DWORD suggested_bytes_per_sec = channels * 64000 / 8;

    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT sug;
    ACMFORMATDETAILSA fd;
    ACMFORMATTAGDETAILSA ftd;
    format_search first;
    format_search want;
    MMRESULT mr;

    printf("the format the codec suggests for a PCM source\n");

    fill_pcm_format(&pcm, rate, channels);
    memset(&sug, 0, sizeof(sug));
    sug.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    sug.wfx.nChannels = channels;
    sug.wfx.nSamplesPerSec = rate;
    mr = acmFormatSuggest(had, &pcm, (WAVEFORMATEX *) &sug, sizeof(sug),
                          ACM_FORMATSUGGESTF_NCHANNELS
                          | ACM_FORMATSUGGESTF_NSAMPLESPERSEC
                          | ACM_FORMATSUGGESTF_WFORMATTAG);
    CHECK_MM(mr, "the codec suggests a destination for 44100/16/stereo PCM");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }

    CHECK_EQ_U(sug.wfx.wFormatTag, WAVE_FORMAT_MPEGLAYER3,
               "the suggestion is an MPEG Layer-3 format");
    /* A compressed format has no sample width and no fixed alignment, and the
       Layer-3 tail is where the frame layout is stated. Each of these three is
       a field a player reads and, finding a PCM value, has to work around. */
    CHECK_EQ_U(sug.wfx.wBitsPerSample, 0, "it has no bits per sample");
    CHECK_EQ_U(sug.wfx.nBlockAlign, 1, "its block alignment is one byte");
    CHECK_EQ_U(sug.wfx.cbSize, MPEGLAYER3_WFX_EXTRA_BYTES,
               "it declares the Layer-3 tail");
    CHECK_EQ_U(sug.wID, MPEGLAYER3_ID_MPEG, "the tail names MPEG Layer-3");
    CHECK_EQ_U(sug.wfx.nAvgBytesPerSec, suggested_bytes_per_sec,
               "the byte rate is 64 kbit/s per channel");

    /* The enumeration is not touched by this work, so the name it gives an
       entry is the control for the description call: handed that same
       structure back, the codec has to produce the same words. This runs
       whatever the suggestion looked like, which the check below does not. */
    printf("a format out of the codec's own list, described by the codec\n");
    memset(&first, 0, sizeof(first));
    first.match_any = 1;
    CHECK_MM(enumerate_formats(had, &first), "the format list is walked");
    CHECK(first.found, "the list yields a format to describe");
    if (first.found) {
        memset(&fd, 0, sizeof(fd));
        fd.cbStruct = sizeof(fd);
        fd.pwfx = (WAVEFORMATEX *) &first.format;
        fd.cbwfx = sizeof(first.format);
        fd.dwFormatTag = first.format.wfx.wFormatTag;
        mr = acmFormatDetailsA(had, &fd, ACM_FORMATDETAILSF_FORMAT);
        CHECK_MM(mr, "the codec describes a format from its own list");
        printf("        listed as \"%s\", described as \"%s\"\n",
               first.name, fd.szFormat);
        CHECK(strcmp(fd.szFormat, first.name) == 0,
              "a listed format is described in the wording the list uses");
    }

    printf("the name the codec gives its own suggestion\n");
    memset(&want, 0, sizeof(want));
    want.rate = rate;
    want.bytes_per_sec = suggested_bytes_per_sec;
    want.channels = channels;
    want.flags = sug.fdwFlags;
    CHECK_MM(enumerate_formats(had, &want),
             "the format list is walked for the suggestion's own parameters");
    CHECK(want.found, "the codec's own list holds the format it suggested");
    if (want.found) {
        memset(&fd, 0, sizeof(fd));
        fd.cbStruct = sizeof(fd);
        fd.pwfx = (WAVEFORMATEX *) &sug;
        fd.cbwfx = sizeof(sug);
        fd.dwFormatTag = sug.wfx.wFormatTag;
        mr = acmFormatDetailsA(had, &fd, ACM_FORMATDETAILSF_FORMAT);
        CHECK_MM(mr, "the codec describes the format it suggested");
        printf("        listed as \"%s\", described as \"%s\"\n",
               want.name, fd.szFormat);
        CHECK(strcmp(fd.szFormat, want.name) == 0,
              "the suggestion is described in the wording the list uses");
    }

    /* Not a check on the codec: the ACM range-checks the format index before
       the driver is asked, so the driver's own bound cannot be reached from
       here and this passes whether the driver has one or not. It is left in
       to say what the framework does, and printed rather than asserted. */
    printf("a format index past the end of the list\n");
    memset(&ftd, 0, sizeof(ftd));
    ftd.cbStruct = sizeof(ftd);
    ftd.dwFormatTag = WAVE_FORMAT_MPEGLAYER3;
    mr = acmFormatTagDetailsA(had, &ftd, ACM_FORMATTAGDETAILSF_FORMATTAG);
    CHECK_MM(mr, "the codec reports how many formats it offers");
    if (mr == MMSYSERR_NOERROR) {
        MPEGLAYER3WAVEFORMAT past;
        ACMFORMATDETAILSA pfd;

        memset(&past, 0, sizeof(past));
        memset(&pfd, 0, sizeof(pfd));
        pfd.cbStruct = sizeof(pfd);
        pfd.pwfx = (WAVEFORMATEX *) &past;
        pfd.cbwfx = sizeof(past);
        pfd.dwFormatTag = WAVE_FORMAT_MPEGLAYER3;
        pfd.dwFormatIndex = ftd.cStandardFormats;
        printf("        %u format(s); index %u answers mmresult %u\n",
               (unsigned) ftd.cStandardFormats, (unsigned) pfd.dwFormatIndex,
               (unsigned) acmFormatDetailsA(had, &pfd, ACM_FORMATDETAILSF_INDEX));
    }

    printf("a PCM format handed to the same description call\n");
    {
        WAVEFORMATEX src;
        ACMFORMATDETAILSA pcmfd;

        fill_pcm_format(&src, rate, channels);
        memset(&pcmfd, 0, sizeof(pcmfd));
        pcmfd.cbStruct = sizeof(pcmfd);
        pcmfd.pwfx = &src;
        pcmfd.cbwfx = sizeof(src);
        pcmfd.dwFormatTag = WAVE_FORMAT_PCM;
        mr = acmFormatDetailsA(had, &pcmfd, ACM_FORMATDETAILSF_FORMAT);
        CHECK_MM(mr, "a PCM format is still described rather than refused");
        if (mr == MMSYSERR_NOERROR) {
            printf("        described as \"%s\"\n", pcmfd.szFormat);
            /* The codec writes nothing for a PCM format, which is the cue for
               the ACM to generate a localised description from the fields. */
            CHECK(pcmfd.szFormat[0] != '\0',
                  "the description the ACM generates comes back for it");
        }
    }
}

/**
 * @brief Checks what the codec returns for its format tags.
 *
 * - Asked by index, the codec returns the index that was asked for: 0 for
 *   MPEG Layer-3, 1 for PCM.
 * - Asked for the largest format structure of all tags, the codec returns
 *   the MPEG Layer-3 tag and the size of an MPEGLAYER3WAVEFORMAT. Asked for
 *   that of the PCM tag, it returns the PCM tag.
 * - acmMetrics() returns the same size as the largest format of the driver.
 *   The ACM computes this size from the format tags that it reads by index,
 *   so this check passes whatever the codec returns for the largest size
 *   query.
 *
 * @param had the opened driver
 */
static void
test_format_tags(HACMDRIVER had)
{
    static const DWORD tags[] = { WAVE_FORMAT_MPEGLAYER3, WAVE_FORMAT_PCM };
    ACMFORMATTAGDETAILSA ftd;
    DWORD index, largest = 0;
    MMRESULT mr;

    printf("the format tags of the codec\n");
    for (index = 0; index < sizeof(tags) / sizeof(tags[0]); index++) {
        char what[CTEST_DETAIL_CHARS];

        memset(&ftd, 0, sizeof(ftd));
        ftd.cbStruct = sizeof(ftd);
        ftd.dwFormatTagIndex = index;
        mr = acmFormatTagDetailsA(had, &ftd, ACM_FORMATTAGDETAILSF_INDEX);
        sprintf(what, "the codec describes format tag %lu", (unsigned long) index);
        CHECK_MM(mr, what);
        if (mr == MMSYSERR_NOERROR) {
            sprintf(what, "format tag %lu is 0x%04lX", (unsigned long) index, (unsigned long) tags[index]);
            CHECK_EQ_U(ftd.dwFormatTag, tags[index], what);
            sprintf(what, "format tag %lu is reported with index %lu", (unsigned long) index,
                    (unsigned long) index);
            CHECK_EQ_U(ftd.dwFormatTagIndex, index, what);
        }
    }

    memset(&ftd, 0, sizeof(ftd));
    ftd.cbStruct = sizeof(ftd);
    ftd.dwFormatTag = WAVE_FORMAT_UNKNOWN;
    mr = acmFormatTagDetailsA(had, &ftd, ACM_FORMATTAGDETAILSF_LARGESTSIZE);
    CHECK_MM(mr, "the codec returns the format tag with the largest format");
    if (mr == MMSYSERR_NOERROR) {
        CHECK_EQ_U(ftd.dwFormatTag, WAVE_FORMAT_MPEGLAYER3, "it is MPEG Layer-3");
        CHECK_EQ_U(ftd.cbFormatSize, sizeof(MPEGLAYER3WAVEFORMAT),
                   "its size is that of an MPEGLAYER3WAVEFORMAT");
    }

    memset(&ftd, 0, sizeof(ftd));
    ftd.cbStruct = sizeof(ftd);
    ftd.dwFormatTag = WAVE_FORMAT_PCM;
    mr = acmFormatTagDetailsA(had, &ftd, ACM_FORMATTAGDETAILSF_LARGESTSIZE);
    CHECK_MM(mr, "the codec returns the largest PCM format");
    if (mr == MMSYSERR_NOERROR) {
        CHECK_EQ_U(ftd.dwFormatTag, WAVE_FORMAT_PCM, "it is PCM");
    }

    mr = acmMetrics((HACMOBJ) had, ACM_METRIC_MAX_SIZE_FORMAT, &largest);
    CHECK_MM(mr, "acmMetrics() returns the largest format size of the driver");
    CHECK_EQ_U(largest, sizeof(MPEGLAYER3WAVEFORMAT),
               "the largest format size is that of an MPEGLAYER3WAVEFORMAT");
}

/** @brief What the wrapper below returns when the codec faults. */
static const MMRESULT CALL_FAULTED = (MMRESULT) -1;

/**
 * @brief Calls acmFormatSuggest(), and returns CALL_FAULTED when the codec
 *        faults.
 * @param had the opened driver
 * @param src the source format
 * @param dst receives the suggestion
 * @param cb the size of @a dst
 * @param flags which fields of @a dst are fixed
 * @return the result of the call, or CALL_FAULTED
 */
static MMRESULT
suggest_catching_faults(HACMDRIVER had, WAVEFORMATEX *src, WAVEFORMATEX *dst,
                        DWORD cb, DWORD flags)
{
    __try {
        return acmFormatSuggest(had, src, dst, cb, flags);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return CALL_FAULTED;
    }
}

/**
 * @brief Checks that a PCM source at a sample rate that no MPEG Layer-3
 *        stream has gets no suggestion.
 *
 * The codec does not resample, so the rate it suggests is the rate of the
 * source. Zero is no rate at all. No MPEG Layer-3 stream has a rate of
 * 96000 Hz. 44100 Hz is the control: the same call, which succeeds. A fault
 * inside the codec returns CALL_FAULTED. So a fault fails the check and does
 * not end the program.
 *
 * @param had the opened driver
 */
static void
test_suggest_unencodable_rate(HACMDRIVER had)
{
    static const DWORD rates[] = { 0, 96000 };
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT sug;
    size_t i;

    printf("a suggestion for a PCM rate no MPEG Layer-3 stream has\n");
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        char what[CTEST_DETAIL_CHARS];

        fill_pcm_format(&pcm, rates[i], 2);
        memset(&sug, 0, sizeof(sug));
        sug.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        sprintf(what, "a %lu Hz source gets no suggestion", (unsigned long) rates[i]);
        CHECK_EQ_U(suggest_catching_faults(had, &pcm, (WAVEFORMATEX *) &sug, sizeof(sug),
                                           ACM_FORMATSUGGESTF_WFORMATTAG),
                   ACMERR_NOTPOSSIBLE, what);
    }
    fill_pcm_format(&pcm, 44100, 2);
    memset(&sug, 0, sizeof(sug));
    sug.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    CHECK_MM(suggest_catching_faults(had, &pcm, (WAVEFORMATEX *) &sug, sizeof(sug),
                                     ACM_FORMATSUGGESTF_WFORMATTAG),
             "a 44100 Hz source still gets one");
}

/**
 * @brief Checks that the codec suggests no format for an MP3 source.
 *
 * The codec only encodes. An application that asks it what to convert MP3
 * to gets ACMERR_NOTPOSSIBLE, and can ask another codec. The stream that the
 * suggestion would lead to does not open either. A PCM source is the
 * control: the same call returns a suggestion.
 *
 * @param had the opened driver
 */
static void
test_suggest_for_mp3_source(HACMDRIVER had)
{
    MPEGLAYER3WAVEFORMAT mp3;
    MPEGLAYER3WAVEFORMAT any;
    WAVEFORMATEX pcm;
    HACMSTREAM has = NULL;
    MMRESULT mr;

    printf("a suggestion for an MP3 source\n");
    fill_mp3_format(&mp3, 44100, 2, 128000);
    /* With no field fixed, the destination buffer has to hold the largest
       format of the driver. */
    memset(&any, 0, sizeof(any));
    CHECK_EQ_U(suggest_catching_faults(had, (WAVEFORMATEX *) &mp3, (WAVEFORMATEX *) &any,
                                       sizeof(any), 0),
               ACMERR_NOTPOSSIBLE, "the codec suggests nothing for an MP3 source");
    memset(&pcm, 0, sizeof(pcm));
    pcm.wFormatTag = WAVE_FORMAT_PCM;
    CHECK_EQ_U(suggest_catching_faults(had, (WAVEFORMATEX *) &mp3, &pcm, sizeof(pcm),
                                       ACM_FORMATSUGGESTF_WFORMATTAG),
               ACMERR_NOTPOSSIBLE, "the codec suggests no PCM format for an MP3 source");
    fill_pcm_format(&pcm, 44100, 2);
    mr = acmStreamOpen(&has, had, (WAVEFORMATEX *) &mp3, &pcm, NULL, 0, 0, 0);
    CHECK(mr != MMSYSERR_NOERROR, "an MP3 to PCM stream does not open");
    if (mr == MMSYSERR_NOERROR) {
        acmStreamClose(has, 0);
    }

    fill_pcm_format(&pcm, 44100, 2);
    memset(&mp3, 0, sizeof(mp3));
    CHECK_MM(suggest_catching_faults(had, &pcm, (WAVEFORMATEX *) &mp3, sizeof(mp3), 0),
             "the codec suggests a format for a PCM source");
}

/**
 * @brief Checks that the codec returns an error for a destination buffer that
 *        is too small for its output, and never writes past the buffer.
 *
 * The conversion carries ACM_STREAMCONVERTF_END, so the codec also flushes
 * the encoder into the destination buffer. Here the application chooses the
 * buffer size. It is far
 * below what acmStreamSize() recommends. Guard bytes follow the buffer, and
 * the codec does not know about them. Every guard byte must be unchanged
 * after the conversion and the unprepare. A conversion that fails must also
 * not report more bytes than the buffer has.
 *
 * @param had the opened driver.
 */
static void
test_small_destination_buffer(HACMDRIVER had)
{
    enum { SMALL = 64, GUARD = 8192 };
    const DWORD rate = 44100;
    const DWORD frames = rate / 2;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    HACMSTREAM has = NULL;
    ACMSTREAMHEADER hdr;
    short *src = NULL;
    BYTE *dst = NULL;
    DWORD i;
    int untouched = 1;
    MMRESULT mr;

    fill_pcm_format(&pcm, rate, 1);
    fill_mp3_format(&mp3, rate, 1, 128000);
    mr = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0);
    CHECK_MM(mr, "44100/16/mono to 128 kbps is negotiated");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }
    src = (short *) calloc(frames, sizeof(short));
    dst = (BYTE *) malloc(SMALL + GUARD);
    if (src == NULL || dst == NULL) {
        CHECK(0, "the buffers could be allocated");
        goto out;
    }
    for (i = 0; i < frames; i++) {
        src[i] = ctest_tone(i, rate, TONE_HZ, TONE_AMPLITUDE);
    }
    memset(dst, 0xA5, SMALL + GUARD);

    memset(&hdr, 0, sizeof(hdr));
    hdr.cbStruct = sizeof(hdr);
    hdr.pbSrc = (BYTE *) src;
    hdr.cbSrcLength = frames * sizeof(short);
    hdr.pbDst = dst;
    hdr.cbDstLength = SMALL;
    mr = acmStreamPrepareHeader(has, &hdr, 0);
    CHECK_MM(mr, "a header with a small destination buffer is prepared");
    if (mr != MMSYSERR_NOERROR) {
        goto out;
    }
    mr = acmStreamConvert(has, &hdr, ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_END);
    CHECK(hdr.cbDstLengthUsed <= SMALL, "the conversion reports no more bytes than the buffer holds");
    CHECK_MM(acmStreamUnprepareHeader(has, &hdr, 0),
             "the header is released although what is left does not fit");
    for (i = SMALL; i < SMALL + GUARD; i++) {
        if (dst[i] != 0xA5) {
            untouched = 0;
            break;
        }
    }
    CHECK(untouched, "nothing is written past the destination buffer");
    CHECK_MM(acmStreamClose(has, 0), "the stream closes afterwards");
    has = NULL;

out:
    free(src);
    free(dst);
    if (has != NULL) {
        acmStreamClose(has, 0);
    }
}

/** @brief The sample rate of the stream lifetime tests, in Hz. */
#define LIFETIME_RATE 44100
/** @brief One second of source for the stream lifetime tests, in sample frames. */
#define LIFETIME_FRAMES LIFETIME_RATE
/** @brief Stereo input for the stream lifetime tests. */
#define LIFETIME_CHANNELS 2
/** @brief LAME's encoder delay: the samples that the MP3 stream holds before the first input sample. */
#define ENCODER_DELAY_SAMPLES 576
/** @brief How many streams the leak check opens. */
#define LEAK_ROUNDS 8

/** @brief The bytes of one stereo sample frame of the stream lifetime tests. */
#define LIFETIME_FRAME_BYTES (LIFETIME_CHANNELS * sizeof(short))

/**
 * @brief One prepared header, and the buffers it owns.
 *
 * The source buffer has room for the part and for one sample frame in front
 * of it: the codec may leave the last sample frame of a conversion unused,
 * and a client passes what was not used in front of the next part.
 */
typedef struct {
    ACMSTREAMHEADER hdr;    /**< the header */
    int prepared;           /**< set while the header is prepared */
    const BYTE *part;       /**< the samples of the part */
    DWORD part_bytes;       /**< their bytes */
    DWORD src_room;         /**< the prepared size of the source buffer */
} stream_part;

/**
 * @brief Prepares a header for part of a stereo source, with a destination
 *        buffer of the given size, or of the size that the codec asks for.
 * @param has        the stream
 * @param part       receives the header. Release it with release_part().
 * @param src        the first sample frame of the part
 * @param frames     the sample frames of the part
 * @param dst_bytes  the size of the destination buffer, 0 for the size that
 *                   the codec asks for
 * @return the result of the first call that failed, or MMSYSERR_NOERROR
 */
static MMRESULT
prepare_part_with_room(HACMSTREAM has, stream_part *part, const short *src, DWORD frames,
                       DWORD dst_bytes)
{
    MMRESULT mr = MMSYSERR_NOERROR;

    memset(part, 0, sizeof(*part));
    part->part = (const BYTE *) src;
    part->part_bytes = frames * LIFETIME_FRAME_BYTES;
    part->src_room = part->part_bytes + LIFETIME_FRAME_BYTES;
    if (dst_bytes == 0) {
        mr = acmStreamSize(has, part->src_room, &dst_bytes, ACM_STREAMSIZEF_SOURCE);
        if (mr != MMSYSERR_NOERROR) {
            return mr;
        }
    }
    part->hdr.cbStruct = sizeof(part->hdr);
    part->hdr.pbSrc = (BYTE *) malloc(part->src_room);
    part->hdr.cbSrcLength = part->src_room;
    part->hdr.pbDst = (BYTE *) malloc(dst_bytes);
    part->hdr.cbDstLength = dst_bytes;
    if (part->hdr.pbSrc == NULL || part->hdr.pbDst == NULL) {
        return MMSYSERR_NOMEM;
    }
    mr = acmStreamPrepareHeader(has, &part->hdr, 0);
    part->prepared = (mr == MMSYSERR_NOERROR);
    return mr;
}

/**
 * @brief Prepares a header for part of a stereo source, with a destination
 *        buffer of the size that the codec asks for.
 * @param has     the stream
 * @param part    receives the header. Release it with release_part().
 * @param src     the first sample frame of the part
 * @param frames  the sample frames of the part
 * @return the result of the first call that failed, or MMSYSERR_NOERROR
 */
static MMRESULT
prepare_part(HACMSTREAM has, stream_part *part, const short *src, DWORD frames)
{
    return prepare_part_with_room(has, part, src, frames, 0);
}

/**
 * @brief Converts a prepared header with exactly the flags given, as a client
 *        that uses the used length as documented: the source is what the previous
 *        conversion left unused, then the part. Appends what the conversion
 *        returns to a stream.
 *
 * ACM_STREAMCONVERTF_START drops what was left unused: it belongs to the
 * stream before. A failed conversion leaves @a carry as it was.
 *
 * @param has    the stream
 * @param part   the prepared header
 * @param flags  the conversion flags
 * @param out    receives the bytes of the conversion at its end
 * @param carry  what the previous conversion left unused; receives what this
 *               one leaves unused
 * @return the result of acmStreamConvert(), or MMSYSERR_NOMEM if @a carry
 *         does not fit in front of the part
 */
static MMRESULT
convert_exact(HACMSTREAM has, stream_part *part, DWORD flags, std::vector<BYTE> *out,
              std::vector<BYTE> *carry)
{
    MMRESULT mr;

    if ((flags & ACM_STREAMCONVERTF_START) != 0) {
        carry->clear();
    }
    if (carry->size() > part->src_room - part->part_bytes) {
        return MMSYSERR_NOMEM;
    }
    if (!carry->empty()) {
        memcpy(part->hdr.pbSrc, carry->data(), carry->size());
    }
    memcpy(part->hdr.pbSrc + carry->size(), part->part, part->part_bytes);
    part->hdr.cbSrcLength = (DWORD) carry->size() + part->part_bytes;
    mr = acmStreamConvert(has, &part->hdr, flags);
    if (mr == MMSYSERR_NOERROR) {
        out->insert(out->end(), part->hdr.pbDst, part->hdr.pbDst + part->hdr.cbDstLengthUsed);
        carry->assign(part->hdr.pbSrc + part->hdr.cbSrcLengthUsed,
                      part->hdr.pbSrc + part->hdr.cbSrcLength);
    }
    /* the prepared size again, as the unprepare needs it */
    part->hdr.cbSrcLength = part->src_room;
    return mr;
}

/**
 * @brief As convert_exact(), with ACM_STREAMCONVERTF_BLOCKALIGN added.
 * @param has    the stream
 * @param part   the prepared header
 * @param flags  the conversion flags besides ACM_STREAMCONVERTF_BLOCKALIGN
 * @param out    receives the bytes of the conversion at its end
 * @param carry  as for convert_exact()
 * @return as convert_exact()
 */
static MMRESULT
convert_part(HACMSTREAM has, stream_part *part, DWORD flags, std::vector<BYTE> *out,
             std::vector<BYTE> *carry)
{
    return convert_exact(has, part, ACM_STREAMCONVERTF_BLOCKALIGN | flags, out, carry);
}

/**
 * @brief Unprepares a header if it is prepared, and frees its buffers.
 * @param has   the stream
 * @param part  the header
 */
static void
release_part(HACMSTREAM has, stream_part *part)
{
    if (part->prepared) {
        acmStreamUnprepareHeader(has, &part->hdr, 0);
        part->prepared = 0;
    }
    free(part->hdr.pbSrc);
    part->hdr.pbSrc = NULL;
    free(part->hdr.pbDst);
    part->hdr.pbDst = NULL;
}

/**
 * @brief Opens a stream from 44100 Hz stereo PCM to 128 kbit/s MP3.
 * @param had  the opened driver
 * @param has  receives the stream
 * @return the result of acmStreamOpen()
 */
static MMRESULT
open_lifetime_stream(HACMDRIVER had, HACMSTREAM *has)
{
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;

    fill_pcm_format(&pcm, LIFETIME_RATE, LIFETIME_CHANNELS);
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    *has = NULL;
    return acmStreamOpen(has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0);
}

/**
 * @brief Encodes a stereo source to the given format on a stream of its own,
 *        in one conversion with ACM_STREAMCONVERTF_END.
 * @param had     the opened driver
 * @param mp3     the format, at the rate of the source
 * @param src     the source
 * @param frames  its sample frames
 * @param out     receives the MP3 stream
 * @return 1 if every call succeeded, else 0
 */
static int
encode_whole_as(HACMDRIVER had, const MPEGLAYER3WAVEFORMAT *mp3, const short *src, DWORD frames,
                std::vector<BYTE> *out)
{
    WAVEFORMATEX pcm;
    HACMSTREAM has;
    stream_part part;
    std::vector<BYTE> carry;
    int ok;

    out->clear();
    fill_pcm_format(&pcm, mp3->wfx.nSamplesPerSec, LIFETIME_CHANNELS);
    if (acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) mp3, NULL, 0, 0, 0) != MMSYSERR_NOERROR) {
        return 0;
    }
    ok = prepare_part(has, &part, src, frames) == MMSYSERR_NOERROR
        && convert_part(has, &part, ACM_STREAMCONVERTF_END, out, &carry) == MMSYSERR_NOERROR;
    release_part(has, &part);
    acmStreamClose(has, 0);
    return ok;
}

/**
 * @brief As encode_whole_as(), to the format of open_lifetime_stream().
 * @param had     the opened driver
 * @param src     the source
 * @param frames  its sample frames
 * @param out     receives the MP3 stream
 * @return 1 if every call succeeded, else 0
 */
static int
encode_whole(HACMDRIVER had, const short *src, DWORD frames, std::vector<BYTE> *out)
{
    MPEGLAYER3WAVEFORMAT mp3;

    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    return encode_whole_as(had, &mp3, src, frames, out);
}

/**
 * @brief Encodes a stereo source on one stream, in two halves with a header
 *        each.
 *
 * With @a prepare_late, the second header is prepared after the first one is
 * converted, as by an application that prepares each buffer when it fills
 * it. Otherwise both headers are prepared before the first conversion, as by
 * an application with two buffers in turn. The second conversion carries
 * ACM_STREAMCONVERTF_END.
 *
 * @param had           the opened driver
 * @param src           the source
 * @param frames        its sample frames, an even number
 * @param prepare_late  1 to prepare the second header after the first conversion
 * @param first_flags   further flags of the first conversion
 * @param second_flags  further flags of the second conversion
 * @param first         receives what the first conversion returns
 * @param second        receives what the second conversion returns
 * @return 1 if every call succeeded, else 0
 */
static int
encode_two_headers(HACMDRIVER had, const short *src, DWORD frames, int prepare_late,
                   DWORD first_flags, DWORD second_flags, std::vector<BYTE> *first,
                   std::vector<BYTE> *second)
{
    const DWORD half = frames / 2;
    HACMSTREAM has;
    stream_part a, b;
    std::vector<BYTE> carry;
    int ok;

    first->clear();
    second->clear();
    memset(&b, 0, sizeof(b));
    if (open_lifetime_stream(had, &has) != MMSYSERR_NOERROR) {
        return 0;
    }
    ok = prepare_part(has, &a, src, half) == MMSYSERR_NOERROR
        && (prepare_late
            || prepare_part(has, &b, src + half * LIFETIME_CHANNELS, half) == MMSYSERR_NOERROR)
        && convert_part(has, &a, first_flags, first, &carry) == MMSYSERR_NOERROR
        && (!prepare_late
            || prepare_part(has, &b, src + half * LIFETIME_CHANNELS, half) == MMSYSERR_NOERROR)
        && convert_part(has, &b, ACM_STREAMCONVERTF_END | second_flags, second, &carry) == MMSYSERR_NOERROR;
    release_part(has, &a);
    release_part(has, &b);
    acmStreamClose(has, 0);
    return ok;
}

/**
 * @brief Converts the two halves of a stereo source with exactly the flags
 *        given: no ACM_STREAMCONVERTF_BLOCKALIGN is added.
 *
 * @param had           the opened driver
 * @param src           the source
 * @param frames        its sample frames, an even number
 * @param first_flags   the flags of the first conversion
 * @param second_flags  the flags of the second conversion
 * @param joined        receives what both conversions return, one after the other
 * @return 1 if every call succeeded, else 0
 */
static int
encode_halves_with_flags(HACMDRIVER had, const short *src, DWORD frames, DWORD first_flags,
                         DWORD second_flags, std::vector<BYTE> *joined)
{
    const DWORD half = frames / 2;
    HACMSTREAM has;
    stream_part a, b;
    std::vector<BYTE> carry;
    int ok;

    joined->clear();
    memset(&b, 0, sizeof(b));
    if (open_lifetime_stream(had, &has) != MMSYSERR_NOERROR) {
        return 0;
    }
    ok = prepare_part(has, &a, src, half) == MMSYSERR_NOERROR
        && prepare_part(has, &b, src + half * LIFETIME_CHANNELS, half) == MMSYSERR_NOERROR
        && convert_exact(has, &a, first_flags, joined, &carry) == MMSYSERR_NOERROR
        && convert_exact(has, &b, second_flags, joined, &carry) == MMSYSERR_NOERROR;
    release_part(has, &a);
    release_part(has, &b);
    acmStreamClose(has, 0);
    return ok;
}

/**
 * @brief Checks that one stream holds one encoder from the open to the
 *        close, whatever the headers.
 *
 * - One conversion with ACM_STREAMCONVERTF_END returns the whole MP3 stream.
 *   Its frames hold every input sample and the encoder delay.
 * - The same source in two headers, the second one prepared after the first
 *   conversion, gives the same bytes as one header.
 * - A stream with two headers keeps no memory after it is closed. Each of
 *   ::LEAK_ROUNDS streams would otherwise keep one encoder.
 * - ACM_STREAMCONVERTF_START on the second conversion begins a new MP3
 *   stream: the second half is encoded as on a stream of its own. Both
 *   headers are prepared first here. The second half without START is the
 *   control. It continues the first half, so it differs.
 * - After a conversion with ACM_STREAMCONVERTF_END, the next conversion
 *   begins a new MP3 stream without START too.
 * - A client that never sends END: the first conversion without
 *   ACM_STREAMCONVERTF_BLOCKALIGN after one with it returns the end of the
 *   MP3 stream, so the halves give the bytes of the one-header encode. A
 *   client that never sets BLOCKALIGN gets no end before its END: flags 0,
 *   then END, give the same bytes.
 *
 * @param had the opened driver
 */
static void
test_stream_lifetime(HACMDRIVER had)
{
    const DWORD half = LIFETIME_FRAMES / 2;
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> whole, first, second, restarted, half_alone, after_end, joined;
    mp3_scan scan;
    long blocks_before, blocks_after;
    DWORD i;
    int round;

    printf("one encoder for the life of a stream\n");
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }

    CHECK(encode_whole(had, &src[0], LIFETIME_FRAMES, &whole),
          "one second is encoded with one header");
    mp3_scan_frames(whole.data(), (long) whole.size(), LIFETIME_RATE, &scan);
    printf("        %lu bytes, %d frame(s)\n", (unsigned long) whole.size(), scan.frames);
    CHECK((DWORD) scan.frames * MP3_SAMPLES_PER_FRAME >= LIFETIME_FRAMES + ENCODER_DELAY_SAMPLES,
          "the conversion with END returns the end of the MP3 stream");

    CHECK(encode_two_headers(had, &src[0], LIFETIME_FRAMES, 1, 0, 0, &first, &second),
          "the same second is encoded with two headers on one stream");
    printf("        %lu + %lu bytes\n", (unsigned long) first.size(), (unsigned long) second.size());
    first.insert(first.end(), second.begin(), second.end());
    CHECK(first == whole, "two headers give the same MP3 stream as one header");

    blocks_before = ctest_heap_blocks();
    for (round = 0; round < LEAK_ROUNDS; round++) {
        encode_two_headers(had, &src[0], LIFETIME_FRAMES, 1, 0, 0, &first, &second);
    }
    blocks_after = ctest_heap_blocks();
    printf("        heap blocks %ld before and %ld after %d streams\n", blocks_before,
           blocks_after, LEAK_ROUNDS);
    CHECK(blocks_before >= 0 && blocks_after - blocks_before < LEAK_ROUNDS,
          "a stream with two headers keeps no memory after it is closed");

    CHECK(encode_two_headers(had, &src[0], LIFETIME_FRAMES, 0, 0, ACM_STREAMCONVERTF_START,
                             &first, &restarted),
          "the second header starts the stream again");
    CHECK(encode_two_headers(had, &src[0], LIFETIME_FRAMES, 0, 0, 0, &first, &second),
          "the second header continues the stream");
    CHECK(encode_whole(had, &src[half * LIFETIME_CHANNELS], half, &half_alone),
          "the second half is encoded on a stream of its own");
    CHECK(restarted == half_alone, "after START the second half is a new MP3 stream");
    CHECK(second != half_alone, "without START the second half continues the stream");
    CHECK(encode_two_headers(had, &src[0], LIFETIME_FRAMES, 0, ACM_STREAMCONVERTF_END, 0,
                             &first, &after_end),
          "the second header follows a header with END");
    CHECK(after_end == half_alone, "after END the second half is a new MP3 stream");

    CHECK(encode_halves_with_flags(had, &src[0], LIFETIME_FRAMES, ACM_STREAMCONVERTF_BLOCKALIGN,
                                   0, &joined),
          "two halves are encoded, the second without BLOCKALIGN and without END");
    printf("        %lu bytes, one header with END %lu\n", (unsigned long) joined.size(),
           (unsigned long) whole.size());
    CHECK(joined == whole, "the conversion that drops BLOCKALIGN returns the end of the MP3 stream");
    CHECK(encode_halves_with_flags(had, &src[0], LIFETIME_FRAMES, 0, ACM_STREAMCONVERTF_END,
                                   &joined),
          "two halves are encoded without BLOCKALIGN, the second with END");
    CHECK(joined == whole, "without BLOCKALIGN at all, only END ends the MP3 stream");
}

/**
 * @brief Checks that the codec returns buffer sizes in both directions.
 *
 * For a destination size, the codec returns a source size in whole sample
 * frames. The destination size that the codec asks for that source is not
 * larger than the one the application has.
 *
 * @param had the opened driver
 */
static void
test_size_both_directions(HACMDRIVER had)
{
    const DWORD dst_bytes = 65536;
    const DWORD frame_bytes = LIFETIME_CHANNELS * sizeof(short);
    HACMSTREAM has;
    DWORD src_bytes = 0, back = 0;
    MMRESULT mr;

    printf("the size of a source buffer for a destination buffer\n");
    mr = open_lifetime_stream(had, &has);
    CHECK_MM(mr, "a stream for the size query opens");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }
    mr = acmStreamSize(has, dst_bytes, &src_bytes, ACM_STREAMSIZEF_DESTINATION);
    CHECK_MM(mr, "the codec sizes the source for a destination size");
    printf("        %lu destination bytes take %lu source bytes\n", (unsigned long) dst_bytes,
           (unsigned long) src_bytes);
    if (mr == MMSYSERR_NOERROR) {
        CHECK(src_bytes > 0 && src_bytes % frame_bytes == 0,
              "the source size is a whole number of sample frames");
        CHECK_MM(acmStreamSize(has, src_bytes, &back, ACM_STREAMSIZEF_SOURCE),
                 "the codec sizes the destination for that source");
        CHECK(back <= dst_bytes, "that source needs no larger destination buffer");
    }
    acmStreamClose(has, 0);
}

/**
 * @brief Checks that a conversion that ends the MP3 stream into a buffer too
 *        small for the flush converts nothing, so that a retry with a larger
 *        buffer continues the stream.
 *
 * One second, in two halves on one stream. A dry run gives the bytes that the
 * second half returns without END. The END conversion of the second half gets
 * a buffer of that size and 100 bytes more: its frames fit, its flush does
 * not. It must fail with nothing used. The retry, with the buffer the codec
 * asks for, must give the bytes of the one-header encode.
 *
 * @param had the opened driver
 */
static void
test_flush_room(HACMDRIVER had)
{
    const DWORD half = LIFETIME_FRAMES / 2;
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> whole, joined, dry, carry, dry_carry;
    HACMSTREAM has;
    stream_part a, b;
    DWORD second_bytes = 0, i;
    MMRESULT mr;
    char detail[CTEST_DETAIL_CHARS];
    int ok;

    printf("the room for the end of a stream\n");
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    CHECK(encode_whole(had, &src[0], LIFETIME_FRAMES, &whole), "one second is encoded with one header");

    /* the dry run: the second half without END */
    ok = open_lifetime_stream(had, &has) == MMSYSERR_NOERROR;
    if (ok) {
        ok = prepare_part(has, &a, &src[0], half) == MMSYSERR_NOERROR
            && prepare_part(has, &b, &src[half * LIFETIME_CHANNELS], half) == MMSYSERR_NOERROR
            && convert_part(has, &a, 0, &dry, &dry_carry) == MMSYSERR_NOERROR
            && convert_part(has, &b, 0, &dry, &dry_carry) == MMSYSERR_NOERROR;
        if (ok) {
            second_bytes = b.hdr.cbDstLengthUsed;
        }
        release_part(has, &a);
        release_part(has, &b);
        acmStreamClose(has, 0);
    }
    CHECK(ok && second_bytes > 0, "the second half alone returns its frames");

    ok = open_lifetime_stream(had, &has) == MMSYSERR_NOERROR;
    CHECK(ok, "a stream for the room test opens");
    if (!ok) {
        return;
    }
    ok = prepare_part(has, &a, &src[0], half) == MMSYSERR_NOERROR
        && convert_part(has, &a, 0, &joined, &carry) == MMSYSERR_NOERROR;
    CHECK(ok, "the first half is converted");
    mr = prepare_part_with_room(has, &b, &src[half * LIFETIME_CHANNELS], half, second_bytes + 100);
    if (mr == MMSYSERR_NOERROR) {
        mr = convert_part(has, &b, ACM_STREAMCONVERTF_END, &joined, &carry);
    }
    sprintf(detail, "mmresult %u, %lu bytes of the source used, %lu returned, buffer %lu", mr,
            (unsigned long) b.hdr.cbSrcLengthUsed, (unsigned long) b.hdr.cbDstLengthUsed,
            (unsigned long) (second_bytes + 100));
    ctest_record(mr != MMSYSERR_NOERROR && b.hdr.cbSrcLengthUsed == 0 && b.hdr.cbDstLengthUsed == 0,
                 "an END conversion without room for the flush converts nothing", detail);
    release_part(has, &b);
    ok = prepare_part(has, &b, &src[half * LIFETIME_CHANNELS], half) == MMSYSERR_NOERROR
        && convert_part(has, &b, ACM_STREAMCONVERTF_END, &joined, &carry) == MMSYSERR_NOERROR;
    CHECK(ok, "the retry with the buffer the codec asks for is converted");
    sprintf(detail, "%lu bytes, one header %lu", (unsigned long) joined.size(), (unsigned long) whole.size());
    ctest_record(joined == whole, "the retry continues the stream", detail);
    release_part(has, &a);
    release_part(has, &b);
    acmStreamClose(has, 0);
}

/**
 * @brief Checks that the sizes of a 128 kbit/s CBR stream suit a client that
 *        makes its destination buffer a quarter second of output.
 *
 * DirectShow's ACM Wrapper makes its destination buffer a quarter second at
 * the byte rate of the format, or the size that the codec asks for one sample
 * frame if that is more. Into it, it converts at most the source size that
 * the codec returns for that buffer. So the codec must ask less than a
 * quarter second for one sample frame, and take at least one MP3 frame of
 * input into a quarter second.
 *
 * @param had the opened driver
 */
static void
test_sizes_for_a_quarter_second(HACMDRIVER had)
{
    const DWORD quarter = 128000 / 8 / 4;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    HACMSTREAM has;
    DWORD dst_bytes = 0, src_bytes = 0;
    char detail[CTEST_DETAIL_CHARS];
    MMRESULT mr;

    printf("the sizes for a quarter second of output\n");
    fill_pcm_format(&pcm, LIFETIME_RATE, LIFETIME_CHANNELS);
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    mp3.fdwFlags = ACM_FLAGS_CBR;
    mr = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0);
    CHECK_MM(mr, "a 128 kbit/s CBR stream opens");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }
    CHECK_MM(acmStreamSize(has, LIFETIME_FRAME_BYTES, &dst_bytes, ACM_STREAMSIZEF_SOURCE),
             "the codec sizes the destination for one sample frame");
    sprintf(detail, "%lu bytes, a quarter second is %lu", (unsigned long) dst_bytes, (unsigned long) quarter);
    ctest_record(dst_bytes < quarter, "one sample frame needs less than a quarter second of output", detail);
    CHECK_MM(acmStreamSize(has, quarter, &src_bytes, ACM_STREAMSIZEF_DESTINATION),
             "the codec sizes the source for a quarter second of output");
    sprintf(detail, "%lu bytes, one MP3 frame of input is %lu", (unsigned long) src_bytes,
            (unsigned long) (MP3_SAMPLES_PER_FRAME * LIFETIME_FRAME_BYTES));
    ctest_record(src_bytes >= MP3_SAMPLES_PER_FRAME * LIFETIME_FRAME_BYTES,
                 "a quarter second of output takes at least one MP3 frame of input", detail);
    acmStreamClose(has, 0);
}

/**
 * @brief Checks that conversions into a destination buffer smaller than their
 *        output keep the rest for the next conversion.
 *
 * One second of 128 kbit/s CBR in tenths, each into a buffer of 1000 bytes:
 * a tenth of a second returns 1600. The last tenth ends the MP3 stream into a
 * buffer that holds everything kept. The joined bytes must be the one-header
 * encode.
 *
 * @param had the opened driver
 */
static void
test_small_destination_kept(HACMDRIVER had)
{
    enum { PIECES = 10, SMALL_ROOM = 1000, LAST_ROOM = 65536 };
    const DWORD piece = LIFETIME_FRAMES / PIECES;
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> whole, joined, carry;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    HACMSTREAM has;
    char detail[CTEST_DETAIL_CHARS];
    DWORD i;
    int k, ok;

    printf("MP3 data that does not fit is kept\n");
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    fill_pcm_format(&pcm, LIFETIME_RATE, LIFETIME_CHANNELS);
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    mp3.fdwFlags = ACM_FLAGS_CBR;
    CHECK(encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &whole),
          "one second of 128 kbit/s CBR is encoded with one header");
    ok = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0) == MMSYSERR_NOERROR;
    CHECK(ok, "a stream for the small buffers opens");
    if (!ok) {
        return;
    }
    for (k = 0; ok && k < PIECES; k++) {
        int const last = k == PIECES - 1;
        stream_part part;

        ok = prepare_part_with_room(has, &part, &src[k * piece * LIFETIME_CHANNELS], piece,
                                    last ? LAST_ROOM : SMALL_ROOM) == MMSYSERR_NOERROR
            && convert_part(has, &part, last ? ACM_STREAMCONVERTF_END : 0, &joined, &carry)
            == MMSYSERR_NOERROR;
        release_part(has, &part);
    }
    CHECK(ok, "every tenth is converted, the first nine into 1000 bytes");
    sprintf(detail, "%lu bytes, one header %lu", (unsigned long) joined.size(), (unsigned long) whole.size());
    ctest_record(joined == whole, "the kept data comes with the next conversions", detail);
    acmStreamClose(has, 0);
}

/**
 * @brief Checks which part of its source a conversion uses.
 *
 * A conversion with ACM_STREAMCONVERTF_BLOCKALIGN that does not end the MP3
 * stream leaves its last sample frame unused, so that a client that marks
 * the end of the data by dropping BLOCKALIGN always has source data left for
 * that last conversion. A conversion of that one sample frame with
 * BLOCKALIGN uses nothing. A conversion that ends the MP3 stream uses
 * everything.
 *
 * @param had the opened driver
 */
static void
test_last_frame_unused(HACMDRIVER had)
{
    const DWORD frames = LIFETIME_FRAMES / 4;
    std::vector<short> src(frames * LIFETIME_CHANNELS);
    std::vector<BYTE> out, carry;
    HACMSTREAM has;
    stream_part part, empty;
    DWORD i;
    int ok;

    printf("the sample frame a conversion leaves unused\n");
    for (i = 0; i < frames; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    memset(&part, 0, sizeof(part));
    memset(&empty, 0, sizeof(empty));
    ok = open_lifetime_stream(had, &has) == MMSYSERR_NOERROR;
    CHECK(ok, "a stream for the used lengths opens");
    if (!ok) {
        return;
    }
    ok = prepare_part(has, &part, &src[0], frames) == MMSYSERR_NOERROR
        && prepare_part(has, &empty, &src[0], 0) == MMSYSERR_NOERROR
        && convert_part(has, &part, 0, &out, &carry) == MMSYSERR_NOERROR;
    CHECK(ok, "a quarter second is converted with BLOCKALIGN");
    CHECK_EQ_U(carry.size(), LIFETIME_FRAME_BYTES, "the conversion leaves its last sample frame unused");
    ok = ok && convert_part(has, &empty, 0, &out, &carry) == MMSYSERR_NOERROR;
    CHECK(ok, "that sample frame alone is converted with BLOCKALIGN");
    CHECK_EQ_U(carry.size(), LIFETIME_FRAME_BYTES, "a conversion of one sample frame uses nothing");
    ok = ok && convert_part(has, &empty, ACM_STREAMCONVERTF_END, &out, &carry) == MMSYSERR_NOERROR;
    CHECK(ok, "that sample frame is converted with END");
    CHECK_EQ_U(carry.size(), 0, "a conversion that ends the MP3 stream uses everything");
    release_part(has, &part);
    release_part(has, &empty);
    acmStreamClose(has, 0);
}

/** @brief The MPEG Layer-3 formats of the codec's format list. */
typedef std::vector<MPEGLAYER3WAVEFORMAT> format_list;

/**
 * @brief Adds one entry of the codec's format list to a format_list.
 * @param hadid  the driver (unused)
 * @param pafd   the entry
 * @param user   the format_list
 * @param fdw    the support flags (unused)
 * @return TRUE, to continue
 */
static BOOL CALLBACK
collect_format_cb(HACMDRIVERID hadid, LPACMFORMATDETAILSA pafd, DWORD_PTR user, DWORD fdw)
{
    (void) hadid;
    (void) fdw;
    if (pafd->pwfx->wFormatTag == WAVE_FORMAT_MPEGLAYER3 && pafd->pwfx->cbSize >= MPEGLAYER3_WFX_EXTRA_BYTES) {
        ((format_list *) user)->push_back(*(const MPEGLAYER3WAVEFORMAT *) pafd->pwfx);
    }
    return TRUE;
}

/**
 * @brief Returns the next sample of a noise source, so that ABR uses large
 *        frames.
 * @param state the state of the source, any start value
 * @return the sample
 */
static short
noise_sample(DWORD *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (short) (*state >> 16);
}

/** @brief What check_conversion_sizes() counted. */
typedef struct {
    unsigned opened;    /**< the formats that opened */
    unsigned over;      /**< the conversions that returned more than asked room for */
    unsigned failed;    /**< the formats whose conversions failed */
    long least_left;    /**< the smallest room left over, -1 before the first */
} size_counts;

/**
 * @brief Converts one second of noise to each format, in pieces of several
 *        sizes and at the end of the MP3 stream, and counts the conversions
 *        that return more than the codec asks room for.
 *
 * The source is at the rate and channels of the format. Noise, so that ABR
 * and VBR use large frames. A format that does not open from PCM at its own
 * rate is listed. The destination buffer is larger than any size the codec
 * gives, so a conversion that returns more than it asked room for is seen and
 * not cut.
 *
 * @param had      the opened driver
 * @param formats  the formats
 * @param c        receives the counts; set @c least_left to -1 first
 */
static void
check_conversion_sizes(HACMDRIVER had, const format_list &formats, size_counts *c)
{
    static const DWORD piece_frames[] = { 4096, 1, 577, 11025, 2000 };
    enum { PIECE_KINDS = sizeof(piece_frames) / sizeof(piece_frames[0]), MOST_PIECE = 11025 };
    size_t f;

    for (f = 0; f < formats.size(); f++) {
        const WORD channels = formats[f].wfx.nChannels;
        const DWORD rate = formats[f].wfx.nSamplesPerSec;
        const DWORD frame_bytes = channels * sizeof(short);
        std::vector<short> src(rate * channels);
        std::vector<BYTE> carry;
        WAVEFORMATEX pcm;
        HACMSTREAM has;
        ACMSTREAMHEADER hdr;
        DWORD state = 1, done = 0, i, room = 0;
        unsigned k = 0;
        int ok;

        for (i = 0; i < rate * channels; i++) {
            src[i] = noise_sample(&state);
        }
        fill_pcm_format(&pcm, rate, channels);
        if (acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &formats[f], NULL, 0, 0, 0)
            != MMSYSERR_NOERROR) {
            printf("        does not open: %lu Hz, %u channel(s), %lu bytes/s, flags %lu\n",
                   (unsigned long) rate, channels, (unsigned long) formats[f].wfx.nAvgBytesPerSec,
                   (unsigned long) formats[f].fdwFlags);
            continue;
        }
        c->opened++;
        memset(&hdr, 0, sizeof(hdr));
        hdr.cbStruct = sizeof(hdr);
        hdr.cbSrcLength = (MOST_PIECE + 1) * frame_bytes;
        ok = acmStreamSize(has, hdr.cbSrcLength, &room, ACM_STREAMSIZEF_SOURCE) == MMSYSERR_NOERROR;
        hdr.cbDstLength = 2 * room + 65536;
        hdr.pbSrc = (BYTE *) malloc(hdr.cbSrcLength);
        hdr.pbDst = (BYTE *) malloc(hdr.cbDstLength);
        ok = ok && hdr.pbSrc != NULL && hdr.pbDst != NULL
            && acmStreamPrepareHeader(has, &hdr, 0) == MMSYSERR_NOERROR;
        while (ok) {
            DWORD const left = rate - done;
            DWORD const wanted = piece_frames[k++ % PIECE_KINDS];
            DWORD const take = wanted < left ? wanted : left;
            int const last = take == left;
            DWORD const prepared = (MOST_PIECE + 1) * frame_bytes;
            DWORD asked = 0;

            if (!carry.empty()) {
                memcpy(hdr.pbSrc, carry.data(), carry.size());
            }
            memcpy(hdr.pbSrc + carry.size(), &src[done * channels], take * frame_bytes);
            hdr.cbSrcLength = (DWORD) carry.size() + take * frame_bytes;
            ok = acmStreamSize(has, hdr.cbSrcLength, &asked, ACM_STREAMSIZEF_SOURCE) == MMSYSERR_NOERROR
                && acmStreamConvert(has, &hdr, ACM_STREAMCONVERTF_BLOCKALIGN
                                    | (last ? ACM_STREAMCONVERTF_END : 0)) == MMSYSERR_NOERROR;
            if (ok) {
                if (hdr.cbDstLengthUsed > asked) {
                    printf("        beyond its size: %lu Hz, %u channel(s), %lu bytes/s, flags %lu: "
                           "%lu bytes for %lu asked, %lu source bytes%s\n",
                           (unsigned long) rate, channels, (unsigned long) formats[f].wfx.nAvgBytesPerSec,
                           (unsigned long) formats[f].fdwFlags, (unsigned long) hdr.cbDstLengthUsed,
                           (unsigned long) asked, (unsigned long) hdr.cbSrcLength, last ? ", the end" : "");
                    c->over++;
                } else if (c->least_left < 0 || (long) (asked - hdr.cbDstLengthUsed) < c->least_left) {
                    c->least_left = (long) (asked - hdr.cbDstLengthUsed);
                }
                carry.assign(hdr.pbSrc + hdr.cbSrcLengthUsed, hdr.pbSrc + hdr.cbSrcLength);
            }
            hdr.cbSrcLength = prepared;
            done += take;
            if (last) {
                break;
            }
        }
        if (!ok) {
            c->failed++;
        }
        if (hdr.fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED) {
            acmStreamUnprepareHeader(has, &hdr, 0);
        }
        free(hdr.pbSrc);
        free(hdr.pbDst);
        acmStreamClose(has, 0);
    }
}

/**
 * @brief Checks, for every MP3 format the codec offers, that no conversion
 *        returns more than the codec asks room for: in pieces of several
 *        sizes, and at the end of the MP3 stream.
 *
 * A format that does not open from PCM at its own rate fails the check. The
 * smallest room left over is printed, as a measure of how close the sizes are.
 *
 * @param had the opened driver
 */
static void
test_sizes_hold_every_conversion(HACMDRIVER had)
{
    format_list formats;
    MPEGLAYER3WAVEFORMAT probe;
    ACMFORMATDETAILSA fd;
    size_counts c = { 0, 0, 0, -1 };
    char detail[CTEST_DETAIL_CHARS];

    printf("the destination size holds every conversion, for every format\n");
    memset(&fd, 0, sizeof(fd));
    fd.cbStruct = sizeof(fd);
    fd.pwfx = (WAVEFORMATEX *) &probe;
    fd.cbwfx = sizeof(probe);
    fd.dwFormatTag = WAVE_FORMAT_MPEGLAYER3;
    fill_mp3_format(&probe, 44100, 2, 128000);
    CHECK_MM(acmFormatEnumA(had, &fd, collect_format_cb, (DWORD_PTR) &formats, ACM_FORMATENUMF_WFORMATTAG),
             "the format list is walked");
    check_conversion_sizes(had, formats, &c);
    sprintf(detail, "%u of %u formats opened, %u failed, %u conversions beyond their size, %ld bytes left at least",
            c.opened, (unsigned) formats.size(), c.failed, c.over, c.least_left);
    ctest_record(c.opened > 0 && c.opened == formats.size() && c.failed == 0 && c.over == 0,
                 "no conversion of any format returns more than the codec asked room for", detail);
}

/**
 * @brief Fills in a VBR format of the codec at its typical bitrate.
 * @param mp3       receives the format
 * @param rate      the sample rate in Hz
 * @param channels  1 or 2
 * @param quality   the VBR quality level, 0 to 9
 */
static void
fill_vbr_format(MPEGLAYER3WAVEFORMAT *mp3, DWORD rate, WORD channels, unsigned quality)
{
    fill_mp3_format(mp3, rate, channels, 1000 * VbrTable::Typical(rate, channels, quality));
    mp3->fdwFlags = ACM_FLAGS_VBR(quality);
}

/**
 * @brief Checks the conversion sizes of the VBR formats of the best quality
 *        level, whose frames are the largest, at every sample rate and with
 *        one and two channels.
 *
 * The configuration of the codec under test may offer no VBR formats, so the
 * formats are built here, as an application that stored one would pass it.
 *
 * @param had the opened driver
 */
static void
test_vbr_sizes_hold(HACMDRIVER had)
{
    static const DWORD rates[] = { 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000 };
    format_list formats;
    size_counts c = { 0, 0, 0, -1 };
    char detail[CTEST_DETAIL_CHARS];
    size_t r;
    WORD ch;

    printf("the destination size holds every conversion of the VBR formats\n");
    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        for (ch = 1; ch <= 2; ch++) {
            MPEGLAYER3WAVEFORMAT mp3;
            fill_vbr_format(&mp3, rates[r], ch, 0);
            formats.push_back(mp3);
        }
    }
    check_conversion_sizes(had, formats, &c);
    sprintf(detail, "%u of %u formats opened, %u failed, %u conversions beyond their size, %ld bytes left at least",
            c.opened, (unsigned) formats.size(), c.failed, c.over, c.least_left);
    ctest_record(c.opened == formats.size() && c.failed == 0 && c.over == 0,
                 "no conversion of a VBR format returns more than the codec asked room for", detail);
}

/** @brief The worse VBR level that the VBR encode test compares with level 0. */
#define VBR_LEVEL_COMPARED 6

/**
 * @brief Checks that the codec's VBR formats encode VBR at their quality level.
 *
 * The source is half a second of silence and half a second of noise, so that
 * VBR uses frames of several bitrates. A CBR format at the same average
 * bitrate is the control: one bitrate. The best level must give a larger
 * stream than a worse one with the same average bitrate in its format, which
 * it does only if the level reaches the encoder: a stream that encoded the
 * average bitrate would come out the same size twice.
 *
 * @param had the opened driver
 */
static void
test_vbr_formats_encode_vbr(HACMDRIVER had)
{
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS, 0);
    std::vector<BYTE> best, worst, cbr;
    MPEGLAYER3WAVEFORMAT mp3;
    mp3_scan scan_best, scan_cbr;
    char detail[CTEST_DETAIL_CHARS];
    DWORD state = 1, i;

    printf("the VBR formats of the codec\n");
    for (i = LIFETIME_FRAMES / 2 * LIFETIME_CHANNELS; i < LIFETIME_FRAMES * LIFETIME_CHANNELS; i++) {
        src[i] = noise_sample(&state);
    }
    memset(&scan_best, 0, sizeof(scan_best));
    memset(&scan_cbr, 0, sizeof(scan_cbr));
    fill_vbr_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 0);
    if (encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &best)) {
        mp3_scan_frames(best.data(), (long) best.size(), LIFETIME_RATE, &scan_best);
    }
    fill_vbr_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 0);
    mp3.fdwFlags = ACM_FLAGS_VBR(VBR_LEVEL_COMPARED);
    (void) encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &worst);
    fill_vbr_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 0);
    mp3.fdwFlags = ACM_FLAGS_CBR;
    if (encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &cbr)) {
        mp3_scan_frames(cbr.data(), (long) cbr.size(), LIFETIME_RATE, &scan_cbr);
    }
    sprintf(detail, "%d frame(s), %d bitrate(s); the CBR control %d frame(s), %d bitrate(s)",
            scan_best.frames, scan_best.distinct, scan_cbr.frames, scan_cbr.distinct);
    ctest_record(scan_best.frames > 0 && scan_best.distinct > 1 && scan_cbr.frames > 0 && scan_cbr.distinct == 1,
                 "a VBR format of the codec encodes frames of several bitrates", detail);
    sprintf(detail, "%lu bytes at level 0, %lu at level %d",
            (unsigned long) best.size(), (unsigned long) worst.size(), VBR_LEVEL_COMPARED);
    ctest_record(!best.empty() && !worst.empty() && best.size() > worst.size(),
                 "the best VBR level encodes more bytes than a worse one at the same average", detail);
}

/**
 * @brief Checks that a format built with any padding mode of fdwFlags encodes
 *        CBR, and the codec's own ABR entry ABR.
 *
 * Windows defines fdwFlags as the padding mode. An application that builds
 * its own MPEG Layer-3 format sets one of the three values, and gets the
 * bitrate it set: every frame of one second of noise at 128 kbit/s. Noise,
 * so that ABR would vary the frames. The codec's ABR bit is the control.
 *
 * @param had the opened driver
 */
static void
test_padding_modes_are_cbr(HACMDRIVER had)
{
    static const struct {
        DWORD flags;
        const char *what;
    } modes[] = {
        { MPEGLAYER3_FLAG_PADDING_ISO, "a format with MPEGLAYER3_FLAG_PADDING_ISO encodes 128 kbit/s CBR" },
        { MPEGLAYER3_FLAG_PADDING_ON, "a format with MPEGLAYER3_FLAG_PADDING_ON encodes 128 kbit/s CBR" },
        { MPEGLAYER3_FLAG_PADDING_OFF, "a format with MPEGLAYER3_FLAG_PADDING_OFF encodes 128 kbit/s CBR" },
    };
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> out;
    MPEGLAYER3WAVEFORMAT mp3;
    mp3_scan scan;
    char detail[CTEST_DETAIL_CHARS];
    DWORD state = 1, i;
    size_t m;

    printf("the padding modes of fdwFlags\n");
    for (i = 0; i < LIFETIME_FRAMES * LIFETIME_CHANNELS; i++) {
        src[i] = noise_sample(&state);
    }
    for (m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
        mp3.fdwFlags = modes[m].flags;
        memset(&scan, 0, sizeof(scan));
        if (encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &out)) {
            mp3_scan_frames(out.data(), (long) out.size(), LIFETIME_RATE, &scan);
        }
        sprintf(detail, "%d frame(s), %d bitrate(s), %d kbit/s", scan.frames, scan.distinct, scan.sole_kbps);
        ctest_record(scan.frames > 0 && scan.distinct == 1 && scan.sole_kbps == 128, modes[m].what, detail);
    }
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    mp3.fdwFlags = ACM_FLAGS_ABR;
    memset(&scan, 0, sizeof(scan));
    if (encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &out)) {
        mp3_scan_frames(out.data(), (long) out.size(), LIFETIME_RATE, &scan);
    }
    sprintf(detail, "%d frame(s), %d bitrate(s)", scan.frames, scan.distinct);
    ctest_record(scan.frames > 0 && scan.distinct > 1, "the codec's ABR entry encodes ABR", detail);
}

/**
 * @brief Checks that a partial sample frame at the end of the source is
 *        reported as not used.
 *
 * The source holds whole stereo sample frames and two bytes more. The
 * conversion has no ACM_STREAMCONVERTF_BLOCKALIGN, so the codec gets the
 * partial frame. The application continues from the used length, so the two
 * bytes must not be counted.
 *
 * @param had the opened driver
 */
static void
test_partial_sample_frame(HACMDRIVER had)
{
    const DWORD frames = LIFETIME_FRAMES / 4;
    const DWORD whole_bytes = frames * LIFETIME_CHANNELS * sizeof(short);
    const DWORD partial_bytes = sizeof(short);
    std::vector<short> src(frames * LIFETIME_CHANNELS + 1);
    HACMSTREAM has;
    stream_part part;
    MMRESULT mr;

    printf("a partial sample frame at the end of the source\n");
    mr = open_lifetime_stream(had, &has);
    CHECK_MM(mr, "a stream for the partial frame opens");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }
    memset(&part, 0, sizeof(part));
    part.hdr.cbStruct = sizeof(part.hdr);
    part.hdr.pbSrc = (BYTE *) &src[0];
    part.hdr.cbSrcLength = whole_bytes + partial_bytes;
    mr = acmStreamSize(has, part.hdr.cbSrcLength, &part.hdr.cbDstLength, ACM_STREAMSIZEF_SOURCE);
    if (mr == MMSYSERR_NOERROR) {
        part.hdr.pbDst = (BYTE *) malloc(part.hdr.cbDstLength);
        mr = part.hdr.pbDst != NULL ? acmStreamPrepareHeader(has, &part.hdr, 0) : MMSYSERR_NOMEM;
        part.prepared = (mr == MMSYSERR_NOERROR);
    }
    if (mr == MMSYSERR_NOERROR) {
        mr = acmStreamConvert(has, &part.hdr, ACM_STREAMCONVERTF_END);
    }
    CHECK_MM(mr, "a source with a partial sample frame at its end is converted");
    if (mr == MMSYSERR_NOERROR) {
        CHECK_EQ_U(part.hdr.cbSrcLengthUsed, whole_bytes,
                   "the used length stops at the last whole sample frame");
    }
    /* the source is the vector's, not the part's */
    if (part.prepared) {
        acmStreamUnprepareHeader(has, &part.hdr, 0);
    }
    free(part.hdr.pbDst);
    acmStreamClose(has, 0);
}

/** @brief A stream object that records that it was deleted. */
class DeleteProbe : public ACMStream
{
public:
    /**
     * @brief Creates the object.
     * @param flag  set to 1 when the object is deleted.
     */
    explicit DeleteProbe(int *flag) : deleted(flag) {}

    /** @brief Sets the flag. */
    ~DeleteProbe() { *deleted = 1; }

private:
    int *deleted;   /**< the flag */
};

/**
 * @brief Checks that the close of a stream that a query opened deletes
 *        nothing.
 *
 * Wine's ACM sends ACMDM_STREAM_CLOSE after a successful query open. A query
 * creates no stream. So the close must not delete what the application put
 * into dwInstance, nor what dwDriver held before the open. The test sends
 * the two messages to a codec object compiled into it. Each of the two
 * fields holds an object that records its deletion.
 */
static void
test_close_after_query(void)
{
    ACM codec(NULL);
    ACMDRVSTREAMINSTANCE inst;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    int app_deleted = 0, driver_deleted = 0;
    DeleteProbe *app = new DeleteProbe(&app_deleted);
    DeleteProbe *before = new DeleteProbe(&driver_deleted);

    printf("a close after a query open\n");
    fill_pcm_format(&pcm, LIFETIME_RATE, LIFETIME_CHANNELS);
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    memset(&inst, 0, sizeof(inst));
    inst.cbStruct = sizeof(inst);
    inst.pwfxSrc = &pcm;
    inst.pwfxDst = (WAVEFORMATEX *) &mp3;
    inst.fdwOpen = ACM_STREAMOPENF_QUERY;
    inst.dwInstance = (DWORD_PTR) app;
    inst.dwDriver = (DWORD_PTR) before;
    CHECK_EQ_U(codec.DriverProcedure(NULL, ACMDM_STREAM_OPEN, (LONG) &inst, 0), MMSYSERR_NOERROR,
               "the query open succeeds");
    codec.DriverProcedure(NULL, ACMDM_STREAM_CLOSE, (LONG) &inst, 0);
    CHECK(!app_deleted, "the close keeps the object in dwInstance");
    CHECK(!driver_deleted, "the close keeps the object that dwDriver held before the open");
    if (!app_deleted) {
        delete app;
    }
    if (!driver_deleted) {
        delete before;
    }
}

static int write_settings(const char *elements);

/**
 * @brief Checks the conversion sizes of CBR formats at low bitrates and high
 *        sample rates, whose frames are the smallest.
 *
 * The bit reservoir reaches back up to 511 bytes in MPEG-1, which is several
 * of these frames: LAME holds them back until the frames whose data begin in
 * them are encoded, so the end of the stream returns them too. The formats are
 * built here, 32 to 56 kbit/s at 32, 44.1 and 48 kHz with one and two
 * channels; Smart encoding, which would leave them out, is off.
 *
 * @param had the opened driver
 */
static void
test_low_bitrate_sizes_hold(HACMDRIVER had)
{
    static const DWORD rates[] = { 48000, 44100, 32000 };
    static const DWORD kbps[] = { 32, 40, 48, 56 };
    format_list formats;
    size_counts c = { 0, 0, 0, -1 };
    char detail[CTEST_DETAIL_CHARS];
    size_t r, b;
    WORD ch;

    printf("the destination size holds every conversion of low-bitrate CBR formats\n");
    if (!write_settings("            <Smart use=\"false\" />\n")) {
        CHECK(0, "the settings file could be written");
        return;
    }
    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        for (b = 0; b < sizeof(kbps) / sizeof(kbps[0]); b++) {
            for (ch = 1; ch <= 2; ch++) {
                MPEGLAYER3WAVEFORMAT mp3;
                fill_mp3_format(&mp3, rates[r], ch, kbps[b] * MP3_BITS_PER_KBIT);
                mp3.fdwFlags = ACM_FLAGS_CBR;
                formats.push_back(mp3);
            }
        }
    }
    check_conversion_sizes(had, formats, &c);
    ::DeleteFileA(codec_config);
    sprintf(detail, "%u of %u formats opened, %u failed, %u conversions beyond their size, %ld bytes left at least",
            c.opened, (unsigned) formats.size(), c.failed, c.over, c.least_left);
    ctest_record(c.opened == formats.size() && c.failed == 0 && c.over == 0,
                 "no conversion of a low-bitrate CBR format returns more than the codec asked room for", detail);
}

/**
 * @brief Checks that the encoding quality of the settings file reaches the
 *        encoder.
 *
 * The same noise is encoded as 128 kbit/s CBR at quality 0, at 9 and at 9
 * again. The two levels give different streams; the same level twice gives
 * the same bytes, so the difference is the level's.
 *
 * @param had  the codec, opened through the ACM.
 */
static void
test_quality_reaches_the_encoder(HACMDRIVER had)
{
    static const char *const levels[] = {
        "            <Quality level=\"0\" />\n",
        "            <Quality level=\"9\" />\n",
        "            <Quality level=\"9\" />\n",
    };
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> out[3];
    MPEGLAYER3WAVEFORMAT mp3;
    char detail[CTEST_DETAIL_CHARS];
    DWORD state = 1, i;
    int encoded = 0;

    printf("the encoding quality reaches the encoder\n");
    for (i = 0; i < LIFETIME_FRAMES * LIFETIME_CHANNELS; i++)
        src[i] = noise_sample(&state);
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    for (i = 0; i < 3; i++) {
        if (write_settings(levels[i]) && encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &out[i]))
            encoded++;
    }
    ::DeleteFileA(codec_config);
    sprintf(detail, "%d of 3 encoded; %u, %u and %u bytes", encoded, (unsigned) out[0].size(),
            (unsigned) out[1].size(), (unsigned) out[2].size());
    ctest_record(encoded == 3 && out[0] != out[1] && out[1] == out[2],
                 "quality 0 and 9 give different streams, 9 twice the same", detail);
}

/**
 * @brief Returns the destination size that the codec asks for one second of
 *        the source.
 * @param had  the codec, opened through the ACM
 * @param mp3  the format, at the rate of the source
 * @return the size in bytes, 0 if the stream does not open or the size fails
 */
static DWORD
room_for_a_second(HACMDRIVER had, const MPEGLAYER3WAVEFORMAT *mp3)
{
    WAVEFORMATEX pcm;
    HACMSTREAM has;
    DWORD room = 0;

    fill_pcm_format(&pcm, mp3->wfx.nSamplesPerSec, LIFETIME_CHANNELS);
    if (acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) mp3, NULL, 0, 0, 0) != MMSYSERR_NOERROR) {
        return 0;
    }
    if (acmStreamSize(has, (DWORD) (LIFETIME_FRAMES * LIFETIME_FRAME_BYTES), &room, ACM_STREAMSIZEF_SOURCE)
        != MMSYSERR_NOERROR) {
        room = 0;
    }
    acmStreamClose(has, 0);
    return room;
}

/**
 * @brief Checks that the VBR bitrate limits of the settings file reach the
 *        encoder, and the room the codec asks for.
 *
 * The source of test_vbr_formats_encode_vbr(), encoded to the best VBR level,
 * in stereo. Without limits, the control, the noise takes
 * frames above 128 kbit/s and the silence frames below it. A 128 kbit/s
 * maximum keeps every frame at or below it, and the codec asks less room for
 * a second. A 128 kbit/s minimum still lets the silent frames below it; with
 * the minimum enforced no frame is below it.
 *
 * @param had  the codec, opened through the ACM.
 */
static void
test_vbr_limits_reach_the_encoder(HACMDRIVER had)
{
    static const struct {
        const char *elements;
        const char *what;
    } cases[] = {
        { "", "no limits" },
        { "            <VBR max=\"128\" />\n", "maximum 128 kbps" },
        { "            <VBR min=\"128\" />\n", "minimum 128 kbps" },
        { "            <VBR min=\"128\" enforce_min=\"true\" />\n", "minimum 128 kbps, enforced" },
    };
    enum { NO_LIMITS, MAXIMUM, MINIMUM, ENFORCED, CASES, LIMIT_KBPS = 128 };
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS, 0);
    MPEGLAYER3WAVEFORMAT mp3;
    mp3_scan scan[CASES];
    DWORD room[CASES];
    char detail[CTEST_DETAIL_CHARS];
    DWORD state = 1, i;

    printf("the VBR bitrate limits reach the encoder\n");
    for (i = LIFETIME_FRAMES / 2 * LIFETIME_CHANNELS; i < LIFETIME_FRAMES * LIFETIME_CHANNELS; i++) {
        src[i] = noise_sample(&state);
    }
    fill_vbr_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 0);
    for (i = 0; i < CASES; i++) {
        std::vector<BYTE> out;

        memset(&scan[i], 0, sizeof(scan[i]));
        room[i] = 0;
        if (write_settings(cases[i].elements)) {
            room[i] = room_for_a_second(had, &mp3);
            if (encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &out)) {
                mp3_scan_frames(out.data(), (long) out.size(), LIFETIME_RATE, &scan[i]);
            }
        }
        printf("        %s: %d frames from %d to %d kbps; %lu bytes of room for a second\n", cases[i].what,
               scan[i].frames, scan[i].lowest_kbps, scan[i].highest_kbps, (unsigned long) room[i]);
    }
    ::DeleteFileA(codec_config);
    sprintf(detail, "frames from %d to %d kbps", scan[NO_LIMITS].lowest_kbps, scan[NO_LIMITS].highest_kbps);
    ctest_record(scan[NO_LIMITS].frames > 0 && scan[NO_LIMITS].lowest_kbps < LIMIT_KBPS
                 && scan[NO_LIMITS].highest_kbps > LIMIT_KBPS,
                 "without limits, frames below and above 128 kbps", detail);
    sprintf(detail, "frames up to %d kbps", scan[MAXIMUM].highest_kbps);
    ctest_record(scan[MAXIMUM].frames > 0 && scan[MAXIMUM].highest_kbps <= LIMIT_KBPS,
                 "a 128 kbps maximum keeps every frame at or below it", detail);
    sprintf(detail, "%lu bytes with the maximum, %lu without", (unsigned long) room[MAXIMUM],
            (unsigned long) room[NO_LIMITS]);
    ctest_record(room[MAXIMUM] > 0 && room[MAXIMUM] < room[NO_LIMITS],
                 "with the maximum the codec asks less room for a second", detail);
    sprintf(detail, "frames from %d kbps", scan[MINIMUM].lowest_kbps);
    ctest_record(scan[MINIMUM].frames > 0 && scan[MINIMUM].lowest_kbps < LIMIT_KBPS,
                 "a 128 kbps minimum still lets the silent frames below it", detail);
    sprintf(detail, "frames from %d kbps", scan[ENFORCED].lowest_kbps);
    ctest_record(scan[ENFORCED].frames > 0 && scan[ENFORCED].lowest_kbps >= LIMIT_KBPS,
                 "with the minimum enforced, no frame is below it", detail);
}

/**
 * @brief Fills in an MPEG-1 Layer III format for Windows' own MP3 decoder,
 *        which sizes its buffers from the block of one frame.
 * @param mp3       receives the format
 * @param rate      the sample rate in Hz
 * @param channels  1 or 2
 * @param kbps      the bitrate of the stream, in kbit/s
 */
static void
fill_decode_format(MPEGLAYER3WAVEFORMAT *mp3, DWORD rate, WORD channels, DWORD kbps)
{
    fill_mp3_format(mp3, rate, channels, kbps * MP3_BITS_PER_KBIT);
    mp3->fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO;
    mp3->nBlockSize = (WORD) (MP3_SAMPLES_PER_FRAME / MP3_BITS_PER_BYTE * kbps * MP3_BITS_PER_KBIT / rate);
}

/**
 * @brief Decodes an MP3 stream with the decoder that Windows has. The codec
 *        under test only encodes, so the ACM chooses another driver.
 * @param mp3     the stream
 * @param format  its format, from fill_decode_format()
 * @param pcm     receives the 16-bit samples
 * @return 1 if the stream decoded, else 0. A call that fails is printed with
 *         its result.
 */
static int
decode_with_windows(const std::vector<BYTE> &mp3, const MPEGLAYER3WAVEFORMAT *format, std::vector<short> *pcm)
{
    WAVEFORMATEX out;
    ACMSTREAMHEADER head;
    HACMSTREAM has;
    DWORD room = 0;
    MMRESULT mr;
    const char *failed = NULL;

    pcm->clear();
    if (mp3.empty())
        return 0;
    memset(&head, 0, sizeof head);
    head.cbStruct = sizeof head;
    fill_pcm_format(&out, format->wfx.nSamplesPerSec, format->wfx.nChannels);
    mr = acmStreamOpen(&has, NULL, (WAVEFORMATEX *) format, &out, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME);
    if (mr != MMSYSERR_NOERROR) {
        printf("        decoding: acmStreamOpen() returned %u\n", (unsigned) mr);
        return 0;
    }
    mr = acmStreamSize(has, (DWORD) mp3.size(), &room, ACM_STREAMSIZEF_SOURCE);
    if (mr != MMSYSERR_NOERROR || room == 0) {
        failed = "acmStreamSize()";
    } else {
        pcm->assign(room / sizeof(short), 0);
        head.pbSrc = (LPBYTE) &mp3[0];
        head.cbSrcLength = (DWORD) mp3.size();
        head.pbDst = (LPBYTE) &(*pcm)[0];
        head.cbDstLength = room;
        mr = acmStreamPrepareHeader(has, &head, 0);
        if (mr != MMSYSERR_NOERROR) {
            failed = "acmStreamPrepareHeader()";
        } else {
            mr = acmStreamConvert(has, &head, ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END);
            if (mr != MMSYSERR_NOERROR)
                failed = "acmStreamConvert()";
            acmStreamUnprepareHeader(has, &head, 0);
        }
    }
    acmStreamClose(has, 0);
    if (failed != NULL)
        printf("        decoding: %s returned %u, %lu bytes of room\n", failed, (unsigned) mr, (unsigned long) room);
    pcm->resize(failed == NULL ? head.cbDstLengthUsed / sizeof(short) : 0);
    return failed == NULL && !pcm->empty();
}

/**
 * @brief Returns the power of one frequency in the first channel of 16-bit
 *        samples, by the Goertzel algorithm, per sample frame.
 * @param pcm        the samples, channels interleaved
 * @param channels   the channels
 * @param frequency  the frequency in Hz
 * @param rate       the sample rate in Hz
 * @return the power, 0 without samples
 */
static double
tone_power(const std::vector<short> &pcm, unsigned channels, double frequency, double rate)
{
    double const coefficient = 2.0 * cos(CTEST_TWO_PI * frequency / rate);
    double s1 = 0.0, s2 = 0.0;
    size_t const frames = pcm.size() / channels;
    size_t i;

    if (frames == 0)
        return 0.0;
    for (i = 0; i < frames; i++) {
        double const s0 = pcm[i * channels] + coefficient * s1 - s2;

        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coefficient * s1 * s2) / ((double) frames * (double) frames);
}

/**
 * @brief Checks that the advanced settings of the configuration file reach
 *        the encoder.
 *
 * - Strict ISO compliance: noise at 320 kbit/s. Without it frames use the bit
 *   reservoir; with it LAME's limit leaves none, so main_data_begin is 0 in
 *   every frame.
 * - Forced mid / side: noise left and silence right at 128 kbit/s, in joint
 *   stereo. Without it most frames code left and right; with it every frame
 *   codes mid and side. In stereo the setting changes nothing, nor with Mono,
 *   which encodes a stereo source in joint stereo.
 * - Keep all frequencies: a 15 kHz tone at 64 kbit/s, where LAME's lowpass is
 *   at 11 kHz. Windows' MP3 decoder decodes both streams: without the setting
 *   the tone is gone, with it the tone is there, 20 dB and more above.
 *
 * @param had  the codec, opened through the ACM.
 */
static void
test_advanced_settings_reach_the_encoder(HACMDRIVER had)
{
    /* A tone above the lowpass of 64 kbit/s, and how much stronger it must
       be where it is kept. */
    enum { HIGH_TONE_HZ = 15000, KEPT_RATIO = 100 };
    std::vector<short> noise(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<short> left(LIFETIME_FRAMES * LIFETIME_CHANNELS, 0);
    std::vector<short> tone(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> out;
    MPEGLAYER3WAVEFORMAT mp3;
    mp3_scan scan[2];
    char detail[CTEST_DETAIL_CHARS];
    DWORD state = 1, i;
    double power[2] = { 0.0, 0.0 };
    size_t decoded[2] = { 0, 0 };
    int k;

    printf("the advanced settings reach the encoder\n");
    for (i = 0; i < LIFETIME_FRAMES * LIFETIME_CHANNELS; i++)
        noise[i] = noise_sample(&state);
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        left[i * LIFETIME_CHANNELS] = noise[i * LIFETIME_CHANNELS];
        tone[i * LIFETIME_CHANNELS] = tone[i * LIFETIME_CHANNELS + 1] =
            ctest_tone(i, LIFETIME_RATE, HIGH_TONE_HZ, TONE_AMPLITUDE);
    }

    {
        static const char *const settings[2] = { "", "            <Strict_ISO use=\"true\" />\n" };

        fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 320000);
        for (k = 0; k < 2; k++) {
            memset(&scan[k], 0, sizeof scan[k]);
            if (write_settings(settings[k]) && encode_whole_as(had, &mp3, &noise[0], LIFETIME_FRAMES, &out))
                mp3_scan_frames(out.data(), (long) out.size(), LIFETIME_RATE, &scan[k]);
        }
        sprintf(detail, "frames using the reservoir: %d of %d without, %d of %d with", scan[0].reservoir_frames,
                scan[0].frames, scan[1].reservoir_frames, scan[1].frames);
        ctest_record(scan[0].reservoir_frames > 0 && scan[1].frames > 0 && scan[1].reservoir_frames == 0,
                     "with strict ISO compliance no frame uses the bit reservoir at 320 kbps", detail);
    }
    {
        enum { JOINT, FORCED, STEREO, MONO_SETTING, CASES };
        static const char *const settings[CASES] = {
            "            <Channel mode=\"Joint stereo\" />\n",
            "            <Channel mode=\"Joint stereo\" />\n            <Forced_mid_side use=\"true\" />\n",
            "            <Channel mode=\"Stereo\" />\n            <Forced_mid_side use=\"true\" />\n",
            "            <Channel mode=\"Mono\" />\n            <Forced_mid_side use=\"true\" />\n",
        };
        mp3_scan ms[CASES];

        fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
        for (k = 0; k < CASES; k++) {
            memset(&ms[k], 0, sizeof ms[k]);
            if (write_settings(settings[k]) && encode_whole_as(had, &mp3, &left[0], LIFETIME_FRAMES, &out))
                mp3_scan_frames(out.data(), (long) out.size(), LIFETIME_RATE, &ms[k]);
        }
        sprintf(detail, "mid / side frames: %d of %d in joint stereo, %d of %d forced, %d of %d in stereo, "
                "%d of %d with Mono", ms[JOINT].ms_frames, ms[JOINT].frames, ms[FORCED].ms_frames, ms[FORCED].frames,
                ms[STEREO].ms_frames, ms[STEREO].frames, ms[MONO_SETTING].ms_frames, ms[MONO_SETTING].frames);
        ctest_record(ms[JOINT].frames > 0 && ms[JOINT].ms_frames < ms[JOINT].frames && ms[FORCED].frames > 0
                     && ms[FORCED].ms_frames == ms[FORCED].frames && ms[STEREO].frames > 0
                     && ms[STEREO].ms_frames == 0 && ms[MONO_SETTING].frames > 0
                     && ms[MONO_SETTING].ms_frames < ms[MONO_SETTING].frames,
                     "forced mid / side codes every frame as mid and side with joint stereo only", detail);
    }
    {
        /* Smart encoding would reject 64 kbit/s at this rate. */
        static const char *const settings[2] = {
            "            <Smart use=\"false\" />\n",
            "            <Smart use=\"false\" />\n            <Keep_all_frequencies use=\"true\" />\n",
        };
        enum { TONE_KBPS = 64 };
        MPEGLAYER3WAVEFORMAT decode;

        fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, TONE_KBPS * MP3_BITS_PER_KBIT);
        fill_decode_format(&decode, LIFETIME_RATE, LIFETIME_CHANNELS, TONE_KBPS);
        for (k = 0; k < 2; k++) {
            std::vector<short> pcm;

            if (write_settings(settings[k]) && encode_whole_as(had, &mp3, &tone[0], LIFETIME_FRAMES, &out)
                && decode_with_windows(out, &decode, &pcm)) {
                decoded[k] = pcm.size() / LIFETIME_CHANNELS;
                power[k] = tone_power(pcm, LIFETIME_CHANNELS, HIGH_TONE_HZ, LIFETIME_RATE);
            }
        }
        sprintf(detail, "%u and %u sample frames decoded; tone power %.3g without, %.3g with",
                (unsigned) decoded[0], (unsigned) decoded[1], power[0], power[1]);
        ctest_record(decoded[0] >= LIFETIME_FRAMES / 2 && decoded[1] >= LIFETIME_FRAMES / 2 && power[1] > 0.0
                     && power[1] > KEPT_RATIO * power[0],
                     "keeping all frequencies keeps a 15 kHz tone that LAME's lowpass removes at 64 kbps", detail);
    }
    ::DeleteFileA(codec_config);
}

/**
 * @brief Checks the codec when a program registers it as a function, with
 *        acmDriverAdd() and @c ACM_DRIVERADDF_FUNCTION.
 *
 * Windows gives such a driver no module handle; the codec then uses its own.
 * Its configuration dialog opens, its configuration file is the one in its
 * own folder, and no file appears in the current directory of the program.
 * The test runs from an empty folder of its own, so that the current
 * directory is never the folder of the codec.
 *
 * @param driver the path of the codec.
 */
static void
test_registered_as_a_function(const char *driver)
{
    char previous[MAX_PATH], empty[MAX_PATH], temp[MAX_PATH], local_config[MAX_PATH];
    HMODULE mod;
    FARPROC proc;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    MMRESULT mr;

    printf("the codec registered as a function\n");
    if (::GetCurrentDirectoryA(sizeof previous, previous) == 0
        || ::GetTempPathA(sizeof temp, temp) == 0
        || snprintf(empty, sizeof empty, "%sacm_test_%lu", temp, (unsigned long) ::GetCurrentProcessId())
               >= (int) sizeof empty
        || snprintf(local_config, sizeof local_config, "%s\\%s", empty, CONFIG_NAME)
               >= (int) sizeof local_config
        || (!::CreateDirectoryA(empty, NULL) && ::GetLastError() != ERROR_ALREADY_EXISTS)
        || !::SetCurrentDirectoryA(empty)) {
        CHECK(0, "the test changes to an empty folder of its own");
        return;
    }
    ::DeleteFileA(local_config);
    ::DeleteFileA(codec_config);
    mod = ::LoadLibraryA(driver);
    proc = (mod != NULL) ? ::GetProcAddress(mod, "DriverProc") : NULL;
    CHECK(proc != NULL, "DriverProc resolves");
    if (proc != NULL) {
        mr = acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION);
        CHECK_MM(mr, "acmDriverAdd() registers the codec as a function");
        if (mr == MMSYSERR_NOERROR) {
            mr = acmDriverOpen(&had, hadid, 0);
            CHECK_MM(mr, "acmDriverOpen() opens it");
        }
        if (had != NULL) {
            UINT_PTR timer;

            config_dialog_button = IDCANCEL;
            config_dialog_found = 0;
            timer = ::SetTimer(NULL, 0, DIALOG_TIMER_MS, close_config_dialog);
            acmDriverMessage(had, DRV_CONFIGURE, 0, 0);
            ::KillTimer(NULL, timer);
            CHECK(config_dialog_found, "its configuration dialog opens");
            acmDriverClose(had, 0);
        }
        if (hadid != NULL) {
            acmDriverRemove(hadid, 0);
        }
        CHECK(::GetFileAttributesA(codec_config) != INVALID_FILE_ATTRIBUTES,
              "its configuration file is the one in its own folder");
        CHECK(::GetFileAttributesA(local_config) == INVALID_FILE_ATTRIBUTES,
              "no configuration file appears in the current directory");
    }
    if (mod != NULL) {
        ::FreeLibrary(mod);
    }
    ::DeleteFileA(local_config);
    ::DeleteFileA(codec_config);
    ::SetCurrentDirectoryA(previous);
    ::RemoveDirectoryA(empty);
}

/** @brief What inspect_about_dialog() found, for test_about_dialog(). */
static struct {
    int found;
    int title_ok;
    int license_ok;
    int texts_ok;
    int font_ok;
    int focus_on_ok;
} about_result;

/**
 * @brief Checks the about dialog of this thread, then closes it.
 *
 * It runs as close_config_dialog() does. Once it finds the dialog, it stops
 * the timer and notes whether the title names the codec
 * and its version, whether the licence box holds LAME's licence notice,
 * whether the credits, the address and the icon credit are the ones the
 * DirectShow filter shows too, whether the dialog uses the font of the
 * desktop, and whether it
 * starts with the focus on OK rather than in the licence box. It runs
 * check_dialog_layout() and presses OK.
 *
 * @param window   NULL, the timer has no window.
 * @param message  WM_TIMER.
 * @param id       the timer.
 * @param time     the tick count.
 */
static void CALLBACK
inspect_about_dialog(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    /* Room for the licence notice, with space to spare. */
    enum { LICENSE_CHARS = 2048 };
    char title[CONTROL_TEXT_CHARS], license[LICENSE_CHARS];
    HWND dialog = NULL;
    HFONT font;
    LOGFONTW used, desktop;

    ::EnumThreadWindows(::GetCurrentThreadId(), find_dialog, (LPARAM) &dialog);
    if (dialog == NULL)
        return;
    ::KillTimer(NULL, id);
    about_result.found = 1;
    about_result.focus_on_ok = ::GetFocus() == ::GetDlgItem(dialog, IDOK);
    title[0] = license[0] = '\0';
    ::GetDlgItemTextA(dialog, IDC_STATIC_ABOUT_TITLE, title, sizeof title);
    ::GetDlgItemTextA(dialog, IDC_EDIT_ABOUT_LICENSE, license, sizeof license);
    about_result.title_ok = strncmp(title, "LAME MP3 Codec v", strlen("LAME MP3 Codec v")) == 0;
    about_result.license_ok = strcmp(license, LICENSE_NOTICE) == 0;
    {
        char credits[CONTROL_TEXT_CHARS], url[CONTROL_TEXT_CHARS], icon[CONTROL_TEXT_CHARS];

        credits[0] = url[0] = icon[0] = '\0';
        ::GetDlgItemTextA(dialog, IDC_STATIC_ABOUT_CREDITS, credits, sizeof credits);
        ::GetDlgItemTextA(dialog, IDC_STATIC_ABOUT_URL, url, sizeof url);
        ::GetDlgItemTextA(dialog, IDC_STATIC_ABOUT_ICON, icon, sizeof icon);
        about_result.texts_ok = strcmp(credits, ABOUT_CREDITS) == 0 && strcmp(url, LAME_URL) == 0
            && strcmp(icon, ABOUT_ICON_CREDIT) == 0;
    }
    font = (HFONT) ::SendMessageW(dialog, WM_GETFONT, 0, 0);
    memset(&used, 0, sizeof used);
    if (font != NULL && ::GetObjectW(font, sizeof used, &used) != 0 && desktop_dialog_font(&desktop))
        about_result.font_ok = wcsncmp(used.lfFaceName, desktop.lfFaceName, LF_FACESIZE) == 0;
    printf("        \"%s\"\n", title);
    check_dialog_layout(dialog, 0, "the about dialog of the codec");
    ::PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
}

/**
 * @brief Opens the about dialog as a program does, through the
 *        ACMDM_DRIVER_ABOUT message, and checks it (inspect_about_dialog()).
 *
 * @param driver  the path of the built codec.
 */
static void
test_about_dialog(const char *driver)
{
    HMODULE codec;
    UINT_PTR timer;
    LONG result;

    printf("the about dialog of the codec\n");
    codec = ::LoadLibraryA(driver);
    if (codec == NULL) {
        CHECK(0, "the codec loads");
        return;
    }
    {
        ACM acm(codec);

        memset(&about_result, 0, sizeof about_result);
        timer = ::SetTimer(NULL, 0, DIALOG_TIMER_MS, inspect_about_dialog);
        result = acm.DriverProcedure(NULL, ACMDM_DRIVER_ABOUT, 0, 0);
        ::KillTimer(NULL, timer);
    }
    CHECK(result == DRVCNF_OK && about_result.found, "the about dialog opens and closes with OK");
    CHECK(about_result.title_ok, "the about dialog names the codec and its version");
    CHECK(about_result.license_ok, "the about dialog shows LAME's licence notice");
    CHECK(about_result.texts_ok, "the about dialog shows the credits, the address and the icon credit of both components");
    CHECK(about_result.font_ok, "the about dialog uses the font of the desktop");
    CHECK(about_result.focus_on_ok, "the about dialog starts with the focus on OK");
    ::FreeLibrary(codec);
    ::DeleteFileA(codec_config);
}

/**
 * @brief Drives the built codec through the Audio Compression Manager.
 *
 * The smoke test checks that the DLL loads and exports what it should. This
 * test checks that the codec works when Windows drives it. msacm does the
 * driver message dispatch, the format negotiation and the buffer handling,
 * exactly as for an application that calls acmStreamConvert().
 *
 * The test needs no registry change and no administrator. acmDriverAdd()
 * with ACM_DRIVERADDF_FUNCTION registers a DriverProc with the real
 * framework, for the calling process only. Everything after that point is
 * the real framework, not a stand-in. The test cannot cover the machine-wide
 * registration, because that needs a registry change.
 */
static void
test_under_the_acm(const char *driver)
{
    const DWORD seconds = 1;
    const DWORD rate = 44100;
    const WORD channels = 2;
    const DWORD frames = rate * seconds;

    HMODULE mod;
    FARPROC proc;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    HACMSTREAM has = NULL;
    ACMDRIVERDETAILSA details;
    ACMFORMATDETAILSA fd;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    ACMSTREAMHEADER hdr;
    MMRESULT mr;
    DWORD dst_bytes = 0;
    DWORD src_bytes;
    unsigned formats = 0;
    short *src = NULL;
    BYTE *dst = NULL;
    DWORD i;

    printf("the codec under the real Audio Compression Manager\n");
    printf("        driver: %s\n", driver);

    mod = LoadLibraryA(driver);
    if (mod == NULL) {
        char detail[CTEST_DETAIL_CHARS];
        sprintf(detail, "Win32 error %lu", GetLastError());
        ctest_record(0, "the driver image loads", detail);
        return;
    }
    CHECK(mod != NULL, "the driver image loads");

    proc = GetProcAddress(mod, "DriverProc");
    CHECK(proc != NULL, "DriverProc resolves");
    if (proc == NULL) {
        return;
    }

    mr = acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0,
                      ACM_DRIVERADDF_FUNCTION);
    CHECK_MM(mr, "the ACM accepts it as a driver");
    if (mr != MMSYSERR_NOERROR) {
        return;
    }

    mr = acmDriverOpen(&had, hadid, 0);
    CHECK_MM(mr, "the driver handles being opened");
    if (mr != MMSYSERR_NOERROR) {
        goto out;
    }

    memset(&details, 0, sizeof(details));
    details.cbStruct = sizeof(details);
    mr = acmDriverDetailsA(hadid, &details, 0);
    CHECK_MM(mr, "the driver reports its details");
    if (mr == MMSYSERR_NOERROR) {
        printf("        \"%s\", %u format tag(s)\n",
               details.szLongName, (unsigned) details.cFormatTags);
        CHECK_EQ_U(details.vdwDriver, DRIVER_VERSION,
                   "the driver version is LAME's major, minor and patch level");
        CHECK(strncmp(details.szLongName, LONG_NAME_PREFIX, strlen(LONG_NAME_PREFIX)) == 0,
              "the long name carries the LAME version and no other");
    }

    memset(&fd, 0, sizeof(fd));
    fd.cbStruct = sizeof(fd);
    fd.pwfx = (WAVEFORMATEX *) &mp3;
    fd.cbwfx = sizeof(mp3);
    fd.dwFormatTag = WAVE_FORMAT_MPEGLAYER3;
    fill_mp3_format(&mp3, rate, channels, 128000);
    mr = acmFormatEnumA(had, &fd, format_cb, (DWORD_PTR) &formats, 0);
    CHECK_MM(mr, "the driver enumerates its formats");
    CHECK(formats > 0, "it offers at least one MPEG Layer-3 format");

    test_format_negotiation(had);
    test_format_tags(had);
    test_suggest_unencodable_rate(had);
    test_suggest_for_mp3_source(had);

    fill_pcm_format(&pcm, rate, channels);
    fill_mp3_format(&mp3, rate, channels, 128000);
    mr = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0);
    CHECK_MM(mr, "44100/16/stereo to 128 kbps is negotiated");
    if (mr != MMSYSERR_NOERROR) {
        goto out;
    }

    src_bytes = frames * pcm.nBlockAlign;
    src = (short *) calloc(1, src_bytes);
    if (src == NULL) {
        CHECK(0, "the source buffer could be allocated");
        goto out;
    }
    /* A sine rather than silence: an encoder that drops everything still
       produces output for silence, so silence would prove nothing. */
    for (i = 0; i < frames; i++) {
        short v = ctest_tone(i, rate, TONE_HZ, TONE_AMPLITUDE);
        src[2 * i] = v;
        src[2 * i + 1] = v;
    }

    mr = acmStreamSize(has, src_bytes, &dst_bytes, ACM_STREAMSIZEF_SOURCE);
    CHECK_MM(mr, "the driver sizes the destination buffer");
    CHECK(dst_bytes > 0, "the size it asks for is not zero");
    if (mr != MMSYSERR_NOERROR || dst_bytes == 0) {
        goto out;
    }

    dst = (BYTE *) calloc(1, dst_bytes);
    if (dst == NULL) {
        CHECK(0, "the destination buffer could be allocated");
        goto out;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.cbStruct = sizeof(hdr);
    hdr.pbSrc = (BYTE *) src;
    hdr.cbSrcLength = src_bytes;
    hdr.pbDst = dst;
    hdr.cbDstLength = dst_bytes;

    mr = acmStreamPrepareHeader(has, &hdr, 0);
    CHECK_MM(mr, "the header is prepared");
    if (mr != MMSYSERR_NOERROR) {
        goto out;
    }

    /* BLOCKALIGN | END is the documented single-shot form: convert everything,
       then flush. Without END the encoder's last frames stay in its own buffer
       and the byte count lands well below the requested rate. */
    mr = acmStreamConvert(has, &hdr,
                          ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_END);
    CHECK_MM(mr, "a second of audio is converted");
    if (mr == MMSYSERR_NOERROR) {
        mp3_scan scan;

        mp3_scan_frames(dst, (long) hdr.cbDstLengthUsed, rate, &scan);

        CHECK(hdr.cbDstLengthUsed > 0, "the conversion produced output");
        CHECK_EQ_U(hdr.cbSrcLengthUsed, src_bytes, "all of the PCM was consumed");
        /* An MPEG audio frame begins with eleven set bits. Without this the
           test would accept any non-empty buffer, which is what "it produced
           output" usually means and rarely proves. */
        CHECK(hdr.cbDstLengthUsed >= MP3_HEADER_BYTES && mp3_is_frame_sync(dst),
              "the output begins with an MPEG frame sync");
        printf("        %lu bytes, %d frame(s), %d distinct bitrate(s)\n",
               (unsigned long) hdr.cbDstLengthUsed, scan.frames, scan.distinct);
        /* The first and the last frame are allowed to be missing: the encoder
           may hold one back, and the tail is only as long as what is left. */
        CHECK(scan.frames >= (int) (mp3_frames_per_second(rate) * seconds) - 2,
              "the whole second is there in frames, tail included");
        /* More than one bitrate means the encoder chose a variable rate, where
           the average is expected to differ from the nominal one. A single rate
           that is not the requested one is a substitution, and the byte total
           alone cannot tell the two apart. */
        if (scan.distinct == 1) {
            CHECK_EQ_U(scan.sole_kbps, 128,
                       "a constant rate is the 128 kbps that was asked for");
        } else {
            CHECK(scan.distinct > 1, "a variable rate, so no single rate to check");
        }
        acmStreamUnprepareHeader(has, &hdr, 0);
    }

    test_small_destination_buffer(had);
    test_stream_lifetime(had);
    test_size_both_directions(had);
    test_flush_room(had);
    test_sizes_for_a_quarter_second(had);
    test_small_destination_kept(had);
    test_last_frame_unused(had);
    test_sizes_hold_every_conversion(had);
    test_padding_modes_are_cbr(had);
    test_vbr_formats_encode_vbr(had);
    test_vbr_sizes_hold(had);
    test_partial_sample_frame(had);
    test_quality_reaches_the_encoder(had);
    test_vbr_limits_reach_the_encoder(had);
    test_advanced_settings_reach_the_encoder(had);
    test_low_bitrate_sizes_hold(had);

out:
    free(src);
    free(dst);
    if (has != NULL) {
        acmStreamClose(has, 0);
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        CHECK_MM(acmDriverRemove(hadid, 0), "the driver handles being withdrawn");
    }
}

/** @brief Length of each encode in the settings test. */
#define SETTINGS_TEST_SECONDS 2

/** @brief What one encode through the ACM produced. */
typedef struct {
    int frames;     /**< frames in the output */
    int borrowed;   /**< frames whose main_data_begin is not 0 */
    int joint;      /**< frames in joint stereo mode */
    int mono;       /**< frames in mono mode */
    int off_rate;   /**< frames whose sample rate is not the rate of the stream */
    long samples;   /**< samples per channel in all frames */
    int min_kbps;   /**< the lowest bitrate of a frame, in kbit/s */
    int max_kbps;   /**< the highest bitrate of a frame, in kbit/s */
} frame_counts;

/**
 * @brief Encodes two seconds of a stereo tone through the ACM, and counts the
 *        frames by what they use.
 *
 * The driver reads its configuration when it is opened, so the caller writes
 * the configuration file before this call.
 *
 * @param driver        the path of the codec.
 * @param out_rate      the sample rate of the MP3 format to open, in Hz.
 * @param out_channels  the number of channels of that format.
 * @param bps           the bitrate of that format, in bit/s.
 * @param flags         the fdwFlags of that format, ::ACM_FLAGS_ABR or
 *                      ::ACM_FLAGS_CBR.
 * @param c             receives the counts. They are 0 if the encode did not
 *                      run.
 * @return the result of acmStreamOpen(). If the stream opens and a later step
 *         fails, the step is recorded as a failed check.
 */
static MMRESULT
encode_stereo_tone(const char *driver, DWORD out_rate, WORD out_channels, DWORD bps,
                   DWORD flags, frame_counts *c)
{
    const DWORD rate = 44100;
    const DWORD samples = rate * SETTINGS_TEST_SECONDS;
    HMODULE mod;
    FARPROC proc;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    HACMSTREAM has = NULL;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    ACMSTREAMHEADER hdr;
    DWORD src_bytes, dst_bytes = 0, off = 0, i;
    short *src = NULL;
    BYTE *dst = NULL;
    MMRESULT opened = MMSYSERR_ERROR;
    MMRESULT mr;

    memset(c, 0, sizeof(*c));
    mod = LoadLibraryA(driver);
    proc = (mod != NULL) ? GetProcAddress(mod, "DriverProc") : NULL;
    if (proc == NULL) {
        CHECK(0, "the driver loads for the settings encode");
        return opened;
    }
    mr = acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION);
    if (mr == MMSYSERR_NOERROR) {
        mr = acmDriverOpen(&had, hadid, 0);
    }
    CHECK_MM(mr, "the driver opens for the settings encode");
    if (mr != MMSYSERR_NOERROR) {
        goto out;
    }
    fill_pcm_format(&pcm, rate, 2);
    fill_mp3_format(&mp3, out_rate, out_channels, bps);
    mp3.fdwFlags = flags;
    opened = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, 0);
    if (opened != MMSYSERR_NOERROR) {
        has = NULL;
        goto out;
    }
    src_bytes = samples * pcm.nBlockAlign;
    src = (short *) calloc(1, src_bytes);
    if (src == NULL || acmStreamSize(has, src_bytes, &dst_bytes, ACM_STREAMSIZEF_SOURCE)
        != MMSYSERR_NOERROR || (dst = (BYTE *) calloc(1, dst_bytes)) == NULL) {
        CHECK(0, "the buffers for the settings encode are ready");
        goto out;
    }
    for (i = 0; i < samples; i++) {
        short v = ctest_tone(i, rate, TONE_HZ, TONE_AMPLITUDE);
        src[2 * i] = v;
        src[2 * i + 1] = v;
    }
    memset(&hdr, 0, sizeof(hdr));
    hdr.cbStruct = sizeof(hdr);
    hdr.pbSrc = (BYTE *) src;
    hdr.cbSrcLength = src_bytes;
    hdr.pbDst = dst;
    hdr.cbDstLength = dst_bytes;
    mr = acmStreamPrepareHeader(has, &hdr, 0);
    if (mr != MMSYSERR_NOERROR) {
        CHECK_MM(mr, "the header for the settings encode is prepared");
        goto out;
    }
    mr = acmStreamConvert(has, &hdr, ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_END);
    CHECK_MM(mr, "the settings encode converts");
    if (mr != MMSYSERR_NOERROR) {
        acmStreamUnprepareHeader(has, &hdr, 0);
        goto out;
    }
    /* The conversion carries END, so the buffer holds the whole stream. The
       side information follows the header and the CRC, so a frame is read
       only when that much of it is in the buffer. */
    while (off + MP3_HEADER_BYTES + MP3_CRC_BYTES + 2 <= hdr.cbDstLengthUsed) {
        const BYTE *h = dst + off;
        int framelen;

        if (!mp3_is_frame_sync(h)) {
            ++off;
            continue;
        }
        framelen = mp3_frame_length(h);
        if (framelen <= 0) {
            break;
        }
        ++c->frames;
        c->samples += mp3_frame_samples(h);
        if (c->min_kbps == 0 || mp3_frame_kbps(h) < c->min_kbps) {
            c->min_kbps = mp3_frame_kbps(h);
        }
        if (mp3_frame_kbps(h) > c->max_kbps) {
            c->max_kbps = mp3_frame_kbps(h);
        }
        if (mp3_sample_rate(h) != out_rate) {
            ++c->off_rate;
        }
        if (mp3_main_data_begin(h) != 0) {
            ++c->borrowed;
        }
        if (mp3_channel_mode(h) == MP3_MODE_JOINT_STEREO) {
            ++c->joint;
        }
        if (mp3_channel_mode(h) == MP3_MODE_MONO) {
            ++c->mono;
        }
        off += (DWORD) framelen;
    }
    acmStreamUnprepareHeader(has, &hdr, 0);

out:
    free(src);
    free(dst);
    if (has != NULL) {
        acmStreamClose(has, 0);
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    return opened;
}

/**
 * @brief Returns the number of channels that the codec suggests for stereo PCM.
 *
 * The driver reads its configuration when it is opened, so the caller writes
 * the configuration file before this call.
 *
 * @param driver the path of the codec.
 * @return the channel count of the suggested MP3 format. 0 if a step fails.
 */
static WORD
suggested_channels(const char *driver)
{
    HMODULE mod = LoadLibraryA(driver);
    FARPROC proc = (mod != NULL) ? GetProcAddress(mod, "DriverProc") : NULL;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT sug;
    WORD channels = 0;
    MMRESULT mr;

    if (proc == NULL) {
        return 0;
    }
    mr = acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION);
    if (mr == MMSYSERR_NOERROR) {
        mr = acmDriverOpen(&had, hadid, 0);
    }
    if (mr == MMSYSERR_NOERROR) {
        fill_pcm_format(&pcm, 44100, 2);
        memset(&sug, 0, sizeof(sug));
        sug.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        mr = acmFormatSuggest(had, &pcm, (WAVEFORMATEX *) &sug, sizeof(sug),
                              ACM_FORMATSUGGESTF_WFORMATTAG);
        if (mr == MMSYSERR_NOERROR) {
            channels = sug.wfx.nChannels;
        }
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    return channels;
}

/**
 * @brief Tries to open a stream from stereo PCM to MP3, on a driver opened for
 *        this call alone.
 *
 * The driver reads its configuration when it is opened, so the caller writes
 * the configuration file before this call.
 *
 * @param driver      the path of the codec.
 * @param rate        the sample rate of the PCM format, in Hz.
 * @param mp3_rate    the sample rate of the MP3 format, in Hz.
 * @param open_flags  the flags for acmStreamOpen().
 * @return the result of acmStreamOpen().
 */
static MMRESULT
stream_open_result(const char *driver, DWORD rate, DWORD mp3_rate, DWORD open_flags)
{
    HMODULE mod = LoadLibraryA(driver);
    FARPROC proc = (mod != NULL) ? GetProcAddress(mod, "DriverProc") : NULL;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    HACMSTREAM has = NULL;
    WAVEFORMATEX pcm;
    MPEGLAYER3WAVEFORMAT mp3;
    MMRESULT mr = MMSYSERR_ERROR;

    if (proc == NULL) {
        return mr;
    }
    mr = acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION);
    if (mr == MMSYSERR_NOERROR) {
        mr = acmDriverOpen(&had, hadid, 0);
    }
    if (mr == MMSYSERR_NOERROR) {
        fill_pcm_format(&pcm, rate, 2);
        fill_mp3_format(&mp3, mp3_rate, 2, 128000);
        mr = acmStreamOpen(&has, had, &pcm, (WAVEFORMATEX *) &mp3, NULL, 0, 0, open_flags);
        if (mr == MMSYSERR_NOERROR && has != NULL) {
            acmStreamClose(has, 0);
        }
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    return mr;
}

/**
 * @brief Writes the configuration file of the codec under test
 *        (codec_config) with the given elements in its current configuration.
 * @param elements the XML elements, one or more lines.
 * @return 1 on success, 0 if the file cannot be written.
 */
static int
write_settings(const char *elements)
{
    FILE *f = fopen(codec_config, "wb");

    if (f == NULL) {
        return 0;
    }
    fprintf(f,
            "<lame_acm>\n"
            "    <encodings default=\"Current\">\n"
            "        <config name=\"Current\">\n"
            "%s"
            "        </config>\n"
            "    </encodings>\n"
            "</lame_acm>\n",
            elements);
    fclose(f);
    return 1;
}

/**
 * @brief Checks that the frames of an encode describe as many seconds as its
 *        input.
 *
 * The encode ends with ACM_STREAMCONVERTF_END, so its frames hold the whole
 * input. They also hold the encoder delay at the start and the padding of the
 * last frame. Together these are less than four frames.
 *
 * @param c     the counts of the encode.
 * @param rate  the sample rate of the stream, in Hz.
 * @param what  the name of the check.
 */
static void
check_duration(const frame_counts *c, DWORD rate, const char *what)
{
    const double seconds = (double) c->samples / (double) rate;
    const double frame = (c->frames > 0) ? seconds / c->frames : 0.0;

    printf("        %d frame(s) at %lu Hz, %.3f s, %d to %d kbit/s\n", c->frames, (unsigned long) rate,
           seconds, c->min_kbps, c->max_kbps);
    CHECK(c->frames > 0 && seconds >= SETTINGS_TEST_SECONDS
          && seconds < SETTINGS_TEST_SECONDS + 4 * frame, what);
}

/**
 * @brief Counts the MPEG frames of an encode, and those that carry a CRC, in
 *        the part of the stream from a given byte on.
 *
 * A conversion need not end at a frame boundary, so the stream is walked from
 * its start, and a frame counts for the part its header lies in.
 *
 * @param mp3     the encoded bytes.
 * @param from    the first byte of the part.
 * @param rate    their sample rate, in Hz.
 * @param frames  receives the number of frames in the part.
 * @return the number of frames in the part with a CRC.
 */
static int
crc_frames(const std::vector<BYTE> &mp3, size_t from, unsigned long rate, int *frames)
{
    size_t i = 0;
    int crc = 0;

    *frames = 0;
    while (i + MP3_HEADER_BYTES <= mp3.size() && mp3_is_frame_sync(&mp3[i])) {
        int const n = mp3_frame_bytes(mp3_bitrate_index(&mp3[i]), mp3_padding_bytes(&mp3[i]), rate);

        if (n <= 0)
            break;
        if (i >= from) {
            ++*frames;
            if ((mp3[i + MP3_HEADER_SYNC_BYTE] & MP3_PROTECTION_MASK) == 0)
                crc++;
        }
        i += (size_t) n;
    }
    return crc;
}

/**
 * @brief Encodes the first half of one second, then changes the CRC setting
 *        in the configuration file and converts the second half.
 * @param driver       the path of the codec.
 * @param second_flags further flags of the second conversion, besides
 *                     BLOCKALIGN and END.
 * @param first        receives what the first conversion returns.
 * @param second       receives what the second conversion returns.
 * @return 1 if every call succeeded, else 0.
 */
static int
encode_across_a_crc_change(const char *driver, DWORD second_flags, std::vector<BYTE> *first,
                           std::vector<BYTE> *second)
{
    HMODULE mod = LoadLibraryA(driver);
    FARPROC proc = (mod != NULL) ? GetProcAddress(mod, "DriverProc") : NULL;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    HACMSTREAM has = NULL;
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    const DWORD half = LIFETIME_FRAMES / 2;
    stream_part a, b;
    std::vector<BYTE> carry;
    DWORD i;
    int ok = 0;

    first->clear();
    second->clear();
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    if (proc == NULL || !write_settings("            <CRC use=\"true\" />\n")
        || acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION) != MMSYSERR_NOERROR
        || acmDriverOpen(&had, hadid, 0) != MMSYSERR_NOERROR
        || !write_settings("            <CRC use=\"false\" />\n")
        || open_lifetime_stream(had, &has) != MMSYSERR_NOERROR) {
        goto out;
    }
    ok = prepare_part(has, &a, &src[0], half) == MMSYSERR_NOERROR
        && prepare_part(has, &b, &src[half * LIFETIME_CHANNELS], half) == MMSYSERR_NOERROR
        && convert_part(has, &a, 0, first, &carry) == MMSYSERR_NOERROR
        && write_settings("            <CRC use=\"true\" />\n")
        && convert_part(has, &b, ACM_STREAMCONVERTF_END | second_flags, second, &carry) == MMSYSERR_NOERROR;
    release_part(has, &a);
    release_part(has, &b);
out:
    if (has != NULL) {
        acmStreamClose(has, 0);
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    if (mod != NULL) {
        FreeLibrary(mod);
    }
    return ok;
}

/**
 * @brief Checks that a START takes the settings that the configuration file
 *        holds at that moment, and that a stream keeps its settings without
 *        one.
 *
 * The driver opens with the CRC on. The file switches it off after that,
 * before the stream opens, and on again after the first half. So the first
 * half carries no CRC only if the stream read the file when it opened. With
 * START on the second conversion, its frames carry a CRC; without, they do
 * not.
 *
 * @param driver the path of the codec.
 */
static void
test_settings_on_start(const char *driver)
{
    std::vector<BYTE> first, second;
    int frames_first, frames_second, crc_first, crc_second;
    char detail[CTEST_DETAIL_CHARS];

    /* With START the second conversion begins an MP3 stream of its own. */
    CHECK(encode_across_a_crc_change(driver, ACM_STREAMCONVERTF_START, &first, &second),
          "one second is encoded across a change of the CRC setting, with START");
    crc_first = crc_frames(first, 0, LIFETIME_RATE, &frames_first);
    crc_second = crc_frames(second, 0, LIFETIME_RATE, &frames_second);
    sprintf(detail, "%d of %d frames, then %d of %d", crc_first, frames_first, crc_second, frames_second);
    ctest_record(frames_first > 0 && crc_first == 0, "before the change no frame carries a CRC", detail);
    ctest_record(frames_second > 0 && crc_second == frames_second,
                 "after START every frame carries the CRC the file now asks for", detail);

    /* Without START the second conversion continues the stream. */
    CHECK(encode_across_a_crc_change(driver, 0, &first, &second),
          "the same encode without START");
    {
        size_t const split = first.size();

        first.insert(first.end(), second.begin(), second.end());
        crc_second = crc_frames(first, split, LIFETIME_RATE, &frames_second);
    }
    sprintf(detail, "%d of %d frames", crc_second, frames_second);
    ctest_record(frames_second > 0 && crc_second == 0, "without START the stream keeps its settings",
                 detail);
}

/**
 * @brief Checks that the codec reads a settings file that appears after a
 *        load of it failed.
 *
 * The file is deleted and a stream is encoded without it, so the codec's load
 * of the file fails. Then the file says CRC off, on and on again, and the same
 * tone is encoded each time: the two settings give different streams only if
 * the codec reads the file again.
 *
 * @param driver the path of the codec.
 */
static void
test_settings_after_a_failed_load(const char *driver)
{
    static const char *const settings[] = {
        "            <CRC use=\"false\" />\n",
        "            <CRC use=\"true\" />\n",
        "            <CRC use=\"true\" />\n",
    };
    HMODULE mod = LoadLibraryA(driver);
    FARPROC proc = (mod != NULL) ? GetProcAddress(mod, "DriverProc") : NULL;
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    std::vector<short> src(LIFETIME_FRAMES * LIFETIME_CHANNELS);
    std::vector<BYTE> without, out[3];
    MPEGLAYER3WAVEFORMAT mp3;
    char detail[CTEST_DETAIL_CHARS];
    DWORD i;
    int encoded = 0, first = 0;

    printf("the settings file after a load of it failed\n");
    for (i = 0; i < LIFETIME_FRAMES; i++) {
        short v = ctest_tone(i, LIFETIME_RATE, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    fill_mp3_format(&mp3, LIFETIME_RATE, LIFETIME_CHANNELS, 128000);
    ::DeleteFileA(codec_config);
    if (proc != NULL
        && acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) proc, 0, ACM_DRIVERADDF_FUNCTION) == MMSYSERR_NOERROR
        && acmDriverOpen(&had, hadid, 0) == MMSYSERR_NOERROR) {
        first = encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &without);
        for (i = 0; i < 3; i++) {
            if (write_settings(settings[i]) && encode_whole_as(had, &mp3, &src[0], LIFETIME_FRAMES, &out[i]))
                encoded++;
        }
    }
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    if (mod != NULL) {
        FreeLibrary(mod);
    }
    ::DeleteFileA(codec_config);
    sprintf(detail, "without the file %s; %d of 3 encoded; %u, %u and %u bytes", first ? "encoded" : "failed",
            encoded, (unsigned) out[0].size(), (unsigned) out[1].size(), (unsigned) out[2].size());
    ctest_record(first && encoded == 3 && out[0] != out[1] && out[1] == out[2],
                 "a settings file written after a failed load reaches the encoder", detail);
}

/**
 * @brief Checks that the defaults, the bit reservoir setting, the forced Mono
 *        setting and Smart Output reach the encoder.
 *
 * - Without a configuration file, every frame is joint stereo, and some frames
 *   use bytes of earlier frames. A CBR stream at 128 kbit/s has every frame at
 *   128 kbit/s; the default stream, ABR, is the control and varies.
 * - With @c Bit_reservoir set to false, no frame uses bytes of earlier frames.
 * - With Mono forced, the codec suggests mono for stereo input, opens a stereo
 *   to mono stream, and writes mono frames.
 * - With Mono not forced, the control: the codec suggests stereo, does not
 *   open the stereo to mono stream, and encodes a stereo stream as joint
 *   stereo.
 * - With Smart Output, 44100 Hz stereo at 32 kbit/s opens as an 11025 Hz
 *   stream, in CBR and in ABR. Every frame is 11025 Hz, and the frames last as
 *   long as the input. A 44100 Hz stream is the control for the length. The
 *   ABR stream uses the MPEG-2.5 range, so some frames are below 32 kbit/s.
 *   A 50 Hz stream, which Smart Output gives 8000 Hz, does not open: LAME
 *   rejects the ratio of the two rates.
 * - Without Smart Output, the 11025 Hz stream does not open, nor does a stream
 *   that LAME rejects: one at 50 Hz, or at 96000 Hz, which MP3 does not have.
 *
 * The codec reads its configuration file from its own folder, also when the
 * test adds it with @c ACM_DRIVERADDF_FUNCTION. So the test writes the file
 * there. It keeps a file that was there before, and puts it back at the end.
 *
 * @param driver the path of the codec.
 */
static void
test_settings_reach_the_encoder(const char *driver)
{
    const char *const config = codec_config;
    char *saved = NULL;
    long saved_len = -1;
    FILE *f;
    const DWORD rate = 44100;
    /* The rate and bitrate that Smart Output picks for 44100 Hz stereo. */
    const DWORD low_rate = 11025, low_bps = 32000;
    frame_counts c;
    MMRESULT mr;
    /* The first and the last frame may be missing, as in test_under_the_acm(). */
    const int expected_frames =
        (int) (mp3_frames_per_second(rate) * SETTINGS_TEST_SECONDS) - 2;

    printf("the settings that reach the encoder\n");

    /* Keep a file that is already there. */
    f = fopen(config, "rb");
    if (f != NULL) {
        if (fseek(f, 0, SEEK_END) == 0 && (saved_len = ftell(f)) >= 0
            && fseek(f, 0, SEEK_SET) == 0
            && (saved = (char *) malloc((size_t) saved_len + 1)) != NULL) {
            saved_len = (long) fread(saved, 1, (size_t) saved_len, f);
        } else {
            saved_len = -1;
        }
        fclose(f);
        if (saved_len < 0) {
            CHECK(0, "the existing configuration file can be kept");
            free(saved);
            return;
        }
    }

    /* The defaults: joint stereo, and the reservoir in use. */
    ::DeleteFileA(config);
    mr = encode_stereo_tone(driver, rate, 2, 128000, ACM_FLAGS_ABR, &c);
    CHECK_MM(mr, "the default settings open a stereo stream");
    printf("        default: %d frame(s), %d use earlier bytes, %d joint stereo\n",
           c.frames, c.borrowed, c.joint);
    CHECK(c.frames >= expected_frames, "the default encode has all of its frames");
    CHECK(c.borrowed > 0, "with the default setting, some frames use the bit reservoir");
    CHECK_EQ_U(c.joint, c.frames, "without a configuration file, every frame is joint stereo");
    CHECK_NE_U(c.min_kbps, c.max_kbps, "the default stream, ABR, varies its bitrate");

    /* A CBR format keeps every frame at its bitrate. */
    mr = encode_stereo_tone(driver, rate, 2, 128000, ACM_FLAGS_CBR, &c);
    CHECK_MM(mr, "without a configuration file, a 128 kbit/s CBR stereo stream opens");
    printf("        CBR: %d frame(s), %d to %d kbit/s\n", c.frames, c.min_kbps, c.max_kbps);
    CHECK(c.frames >= expected_frames, "the CBR encode has all of its frames");
    CHECK_EQ_U(c.min_kbps, 128, "the lowest bitrate of the CBR stream is 128 kbit/s");
    CHECK_EQ_U(c.max_kbps, 128, "the highest bitrate of the CBR stream is 128 kbit/s");

    /* The resampling and VBR keys are settings the codec does not have. A
       file from an older release can carry them, and the codec must still
       load the rest of it. */
    if (!write_settings("            <resampling use=\"true\" freq=\"22050\" />\n"
                        "            <VBR use=\"true\" header=\"false\" quality=\"2\" />\n"
                        "            <Bit_reservoir use=\"false\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        mr = encode_stereo_tone(driver, rate, 2, 128000, ACM_FLAGS_ABR, &c);
        CHECK_MM(mr, "a stereo stream opens with the reservoir switched off");
        printf("        switched off: %d frame(s), %d use earlier bytes\n", c.frames, c.borrowed);
        CHECK(c.frames >= expected_frames, "the encode without the reservoir has all of its frames");
        CHECK_EQ_U(c.borrowed, 0, "with the reservoir switched off, no frame uses it");
    }

    if (!write_settings("            <Channel mode=\"Mono\" force=\"true\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        CHECK_EQ_U(suggested_channels(driver), 1, "with Mono forced, the codec suggests mono for stereo");
        mr = encode_stereo_tone(driver, rate, 1, 64000, ACM_FLAGS_ABR, &c);
        CHECK_MM(mr, "with Mono forced, a stereo to mono stream opens");
        printf("        Mono forced: %d frame(s), %d mono\n", c.frames, c.mono);
        CHECK(c.frames >= expected_frames, "the forced mono encode has all of its frames");
        CHECK_EQ_U(c.mono, c.frames, "with Mono forced, every frame is mono");
    }

    if (!write_settings("            <Channel mode=\"Mono\" force=\"false\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        CHECK_EQ_U(suggested_channels(driver), 2, "with Mono not forced, the codec suggests stereo");
        mr = encode_stereo_tone(driver, rate, 1, 64000, ACM_FLAGS_ABR, &c);
        CHECK(mr != MMSYSERR_NOERROR, "with Mono not forced, a stereo to mono stream does not open");
        mr = encode_stereo_tone(driver, rate, 2, 128000, ACM_FLAGS_ABR, &c);
        CHECK_MM(mr, "with Mono not forced, a stereo stream opens");
        printf("        Mono not forced: %d frame(s), %d joint stereo\n", c.frames, c.joint);
        CHECK(c.frames >= expected_frames, "the stereo encode has all of its frames");
        CHECK_EQ_U(c.joint, c.frames, "with Mono not forced, a stereo stream is joint stereo");
    }

    /* 44100 Hz stereo at 32 kbit/s is compressed 1:44, more than the ratio of
       15, so Smart Output encodes it at 11025 Hz. */
    if (!write_settings("            <Smart use=\"true\" ratio=\"15\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        mr = encode_stereo_tone(driver, rate, 2, 128000, ACM_FLAGS_CBR, &c);
        CHECK_MM(mr, "with Smart Output, a 44100 Hz stream opens");
        check_duration(&c, rate, "a 44100 Hz stream lasts as long as its input");
        mr = encode_stereo_tone(driver, low_rate, 2, low_bps, ACM_FLAGS_CBR, &c);
        CHECK_MM(mr, "with Smart Output, a 44100 Hz to 11025 Hz CBR stream opens");
        CHECK_EQ_U(c.off_rate, 0, "every frame of the CBR stream is 11025 Hz");
        check_duration(&c, low_rate, "the 11025 Hz CBR stream lasts as long as its input");
        mr = encode_stereo_tone(driver, low_rate, 2, low_bps, ACM_FLAGS_ABR, &c);
        CHECK_MM(mr, "with Smart Output, a 44100 Hz to 11025 Hz ABR stream opens");
        CHECK_EQ_U(c.off_rate, 0, "every frame of the ABR stream is 11025 Hz");
        check_duration(&c, low_rate, "the 11025 Hz ABR stream lasts as long as its input");
        /* With the MPEG-1 range, the lowest rate is the mean, and ABR becomes
           CBR. Index 0 of the table is free format. */
        CHECK(c.min_kbps < mp3_bitrate_kbps[1],
              "the 11025 Hz ABR stream goes below 32 kbit/s, the lowest MPEG-1 rate");
        /* For 50 Hz PCM Smart Output picks the lowest MP3 rate, 8000 Hz.
           Every setter takes that, and lame_init_params() rejects it and
           reports why: 8000 Hz is more than 128 times as high. */
        CHECK_EQ_U(stream_open_result(driver, 50, 8000, 0), ACMERR_NOTPOSSIBLE,
                   "with Smart Output, a 50 Hz to 8000 Hz stream, which LAME rejects, does not open");
    }

    if (!write_settings("            <Smart use=\"false\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        mr = encode_stereo_tone(driver, low_rate, 2, low_bps, ACM_FLAGS_CBR, &c);
        CHECK(mr != MMSYSERR_NOERROR, "without Smart Output, a 44100 Hz to 11025 Hz stream does not open");
        /* The codec accepts the formats of a 50 Hz stream, and LAME rejects
           it: MP3 has no such rate. 48000 Hz is the control. */
        CHECK_EQ_U(stream_open_result(driver, 50, 50, 0), ACMERR_NOTPOSSIBLE,
                   "a 50 Hz stream, which LAME rejects, does not open");
        CHECK_EQ_U(stream_open_result(driver, 50, 50, ACM_STREAMOPENF_QUERY), ACMERR_NOTPOSSIBLE,
                   "a query for a 50 Hz stream fails");
        CHECK_MM(stream_open_result(driver, 48000, 48000, 0), "a 48000 Hz stream opens");
        /* MP3 has no 96000 Hz rate, so lame_set_out_samplerate() rejects
           it. LAME would otherwise pick 48000 Hz for a stream whose format
           says 96000 Hz. */
        CHECK_EQ_U(stream_open_result(driver, 96000, 96000, 0), ACMERR_NOTPOSSIBLE,
                   "a 96000 Hz stream, whose rate MP3 does not have, does not open");
        CHECK_EQ_U(stream_open_result(driver, 96000, 96000, ACM_STREAMOPENF_QUERY), ACMERR_NOTPOSSIBLE,
                   "a query for a 96000 Hz stream fails");
    }

    test_settings_on_start(driver);
    test_settings_after_a_failed_load(driver);

    ::DeleteFileA(config);
    if (saved != NULL) {
        f = fopen(config, "wb");
        CHECK(f != NULL && fwrite(saved, 1, (size_t) saved_len, f) == (size_t) saved_len,
              "the configuration file that was there before is put back");
        if (f != NULL) {
            fclose(f);
        }
        free(saved);
    }
}

/** @brief The DriverProc of the codec, behind capture_proc(). */
static DRIVERPROC capture_real_proc;
/** @brief The MP3 data that the codec returned, through capture_proc(). */
static std::vector<BYTE> capture_out;
/** @brief The source bytes that the conversions used. */
static DWORD capture_used;
/** @brief The conversions without ACM_STREAMCONVERTF_BLOCKALIGN. */
static unsigned capture_unaligned;

/**
 * @brief Passes every message to the codec, and keeps what each conversion
 *        returns.
 * @param id   the driver instance
 * @param h    the driver handle
 * @param msg  the message
 * @param l1   its first parameter
 * @param l2   its second parameter
 * @return what the codec returns
 */
static LRESULT CALLBACK
capture_proc(DWORD_PTR id, HDRVR h, UINT msg, LPARAM l1, LPARAM l2)
{
    LRESULT const result = capture_real_proc(id, h, msg, l1, l2);

    if (msg == ACMDM_STREAM_CONVERT && result == MMSYSERR_NOERROR) {
        LPACMDRVSTREAMHEADER sh = (LPACMDRVSTREAMHEADER) l2;

        capture_out.insert(capture_out.end(), sh->pbDst, sh->pbDst + sh->cbDstLengthUsed);
        capture_used += sh->cbSrcLengthUsed;
        if ((sh->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN) == 0) {
            capture_unaligned++;
        }
    }
    return result;
}

/** @brief The CLSID of the Null Renderer; the SDK has no qedit.h, which declares it. */
static const CLSID CLSID_NullRendererFilter =
    { 0xC1F400A4, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

/** @brief The WAV file of the ACM Wrapper test, beside the codec's settings file. */
#define WRAPPER_WAV_NAME L"acm_test_wrapper.wav"
/** @brief The length of the ACM Wrapper test's source, in seconds. */
#define WRAPPER_SECONDS 3

/**
 * @brief Writes a 16-bit PCM WAV file.
 * @param name     the file
 * @param src      the samples, interleaved
 * @param frames   the sample frames
 * @param rate     the sample rate
 * @param channels the channels
 * @return 1 if the file was written, else 0
 */
static int
write_wav(const wchar_t *name, const short *src, DWORD frames, DWORD rate, WORD channels)
{
    WAVEFORMATEX w;
    DWORD const data = frames * channels * sizeof(short), fmt = 16, riff = 4 + 8 + fmt + 8 + data;
    FILE *f = _wfopen(name, L"wb");
    int ok;

    if (f == NULL) {
        return 0;
    }
    fill_pcm_format(&w, rate, channels);
    ok = fwrite("RIFF", 1, 4, f) == 4 && fwrite(&riff, 4, 1, f) == 1
        && fwrite("WAVEfmt ", 1, 8, f) == 8 && fwrite(&fmt, 4, 1, f) == 1
        && fwrite(&w, 1, fmt, f) == fmt && fwrite("data", 1, 4, f) == 4 && fwrite(&data, 4, 1, f) == 1
        && fwrite(src, 1, data, f) == data;
    return fclose(f) == 0 && ok;
}

/**
 * @brief Returns the first pin of a filter in one direction.
 * @param filter the filter
 * @param want   the direction
 * @return the pin, with a reference, or NULL
 */
static IPin *
pin_of(IBaseFilter *filter, PIN_DIRECTION want)
{
    IEnumPins *pins = NULL;
    IPin *pin = NULL;

    if (FAILED(filter->EnumPins(&pins))) {
        return NULL;
    }
    while (pins->Next(1, &pin, NULL) == S_OK) {
        PIN_DIRECTION dir;

        if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == want) {
            pins->Release();
            return pin;
        }
        pin->Release();
    }
    pins->Release();
    return NULL;
}

/**
 * @brief Returns the ACM Wrapper for a format tag, from the Audio Compressors
 *        category of the system device enumerator.
 * @param tag the format tag
 * @return the filter, with a reference, or NULL
 */
static IBaseFilter *
audio_compressor(WORD tag)
{
    ICreateDevEnum *devices = NULL;
    IEnumMoniker *entries = NULL;
    IMoniker *entry = NULL;
    IBaseFilter *filter = NULL;

    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                                (void **) &devices))) {
        return NULL;
    }
    if (devices->CreateClassEnumerator(CLSID_AudioCompressorCategory, &entries, 0) == S_OK) {
        while (filter == NULL && entries->Next(1, &entry, NULL) == S_OK) {
            IPropertyBag *bag = NULL;
            VARIANT id;

            VariantInit(&id);
            if (SUCCEEDED(entry->BindToStorage(NULL, NULL, IID_IPropertyBag, (void **) &bag))) {
                if (SUCCEEDED(bag->Read(L"AcmId", &id, NULL)) && id.vt == VT_I4 && id.lVal == tag) {
                    entry->BindToObject(NULL, NULL, IID_IBaseFilter, (void **) &filter);
                }
                bag->Release();
            }
            VariantClear(&id);
            entry->Release();
        }
        entries->Release();
    }
    devices->Release();
    return filter;
}

/**
 * @brief Sets the output format of the ACM Wrapper to the entry of the codec's
 *        list with the given rate, channels, byte rate and flags.
 * @param filter         the ACM Wrapper
 * @param rate           the sample rate
 * @param channels       the channels
 * @param bytes_per_sec  the byte rate
 * @param flags          the fdwFlags of the entry, ::ACM_FLAGS_CBR or ::ACM_FLAGS_ABR
 * @param chosen         receives the entry
 * @return 1 if such an entry was set, else 0
 */
static int
set_wrapper_format(IBaseFilter *filter, DWORD rate, WORD channels, DWORD bytes_per_sec, DWORD flags,
                   MPEGLAYER3WAVEFORMAT *chosen)
{
    IPin *out = pin_of(filter, PINDIR_OUTPUT);
    IAMStreamConfig *config = NULL;
    int count = 0, size = 0, k, set = 0;

    if (out == NULL) {
        return 0;
    }
    if (SUCCEEDED(out->QueryInterface(IID_IAMStreamConfig, (void **) &config))) {
        config->GetNumberOfCapabilities(&count, &size);
        for (k = 0; k < count && !set; k++) {
            AM_MEDIA_TYPE *type = NULL;
            AUDIO_STREAM_CONFIG_CAPS caps;

            if (config->GetStreamCaps(k, &type, (BYTE *) &caps) != S_OK || type == NULL) {
                continue;
            }
            if (type->formattype == FORMAT_WaveFormatEx && type->pbFormat != NULL) {
                const MPEGLAYER3WAVEFORMAT *w = (const MPEGLAYER3WAVEFORMAT *) type->pbFormat;

                set = type->cbFormat >= sizeof(*w) && w->wfx.nSamplesPerSec == rate
                    && w->wfx.nChannels == channels && w->wfx.nAvgBytesPerSec == bytes_per_sec
                    && w->fdwFlags == flags && SUCCEEDED(config->SetFormat(type));
                if (set) {
                    *chosen = *w;
                }
            }
            CoTaskMemFree(type->pbFormat);
            if (type->pUnk != NULL) {
                type->pUnk->Release();
            }
            CoTaskMemFree(type);
        }
        config->Release();
    }
    out->Release();
    return set;
}

/** @brief One output format of the ACM Wrapper test. */
typedef struct {
    DWORD rate;             /**< the sample rate, of the WAV file too */
    DWORD bytes_per_sec;    /**< the byte rate */
    DWORD flags;            /**< ::ACM_FLAGS_CBR or ::ACM_FLAGS_ABR */
    const char *what;       /**< names the format in the output */
} wrapper_format;

/**
 * @brief Runs a WAV file through the ACM Wrapper for MPEG Layer-3 into a Null
 *        Renderer, as a DirectShow application does with an entry of the
 *        Audio Compressors category.
 * @param wav     the file, stereo
 * @param want    the output format
 * @param chosen  receives the MP3 format of the wrapper
 * @return 1 if the graph was built and ran to its end, else 0
 */
static int
run_wrapper_graph(const wchar_t *wav, const wrapper_format *want, MPEGLAYER3WAVEFORMAT *chosen)
{
    IGraphBuilder *graph = NULL;
    IBaseFilter *source = NULL, *wrapper = NULL, *sink = NULL;
    IMediaControl *control = NULL;
    IMediaEvent *events = NULL;
    IMediaFilter *timing = NULL;
    IPin *from = NULL, *to = NULL;
    long code = 0;
    int ok;

    ok = SUCCEEDED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
                                    (void **) &graph))
        && SUCCEEDED(graph->AddSourceFilter(wav, L"source", &source))
        && (wrapper = audio_compressor(WAVE_FORMAT_MPEGLAYER3)) != NULL
        && SUCCEEDED(graph->AddFilter(wrapper, L"wrapper"));
    if (ok) {
        from = pin_of(source, PINDIR_OUTPUT);
        to = pin_of(wrapper, PINDIR_INPUT);
        ok = from != NULL && to != NULL && SUCCEEDED(graph->Connect(from, to));
        if (from != NULL) from->Release();
        if (to != NULL) to->Release();
    }
    ok = ok && set_wrapper_format(wrapper, want->rate, LIFETIME_CHANNELS, want->bytes_per_sec, want->flags,
                                  chosen)
        && SUCCEEDED(CoCreateInstance(CLSID_NullRendererFilter, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                                      (void **) &sink))
        && SUCCEEDED(graph->AddFilter(sink, L"sink"));
    if (ok) {
        from = pin_of(wrapper, PINDIR_OUTPUT);
        to = pin_of(sink, PINDIR_INPUT);
        ok = from != NULL && to != NULL && SUCCEEDED(graph->ConnectDirect(from, to, NULL));
        if (from != NULL) from->Release();
        if (to != NULL) to->Release();
    }
    /* no clock: the graph runs as fast as it can */
    ok = ok && SUCCEEDED(graph->QueryInterface(IID_IMediaFilter, (void **) &timing))
        && SUCCEEDED(timing->SetSyncSource(NULL))
        && SUCCEEDED(graph->QueryInterface(IID_IMediaControl, (void **) &control))
        && SUCCEEDED(graph->QueryInterface(IID_IMediaEvent, (void **) &events))
        && SUCCEEDED(control->Run())
        && events->WaitForCompletion(20000, &code) == S_OK && code == EC_COMPLETE;
    if (control != NULL) {
        control->Stop();
        control->Release();
    }
    if (events != NULL) events->Release();
    if (timing != NULL) timing->Release();
    if (sink != NULL) sink->Release();
    if (wrapper != NULL) wrapper->Release();
    if (source != NULL) source->Release();
    if (graph != NULL) graph->Release();
    return ok;
}

/**
 * @brief Runs three seconds of stereo through the ACM Wrapper in one output
 *        format, and checks what the codec returned to the wrapper.
 *
 * The wrapper must convert all of the WAV file, end the data with one
 * conversion without ACM_STREAMCONVERTF_BLOCKALIGN, and get the MP3 stream of
 * the one-header encode of the same samples to the same format.
 *
 * @param had   the opened driver, behind capture_proc()
 * @param want  the output format
 */
static void
check_behind_the_wrapper(HACMDRIVER had, const wrapper_format *want)
{
    const DWORD frames = want->rate * WRAPPER_SECONDS;
    std::vector<short> src(frames * LIFETIME_CHANNELS);
    std::vector<BYTE> whole, through;
    MPEGLAYER3WAVEFORMAT chosen;
    DWORD used, i;
    unsigned unaligned;
    char detail[CTEST_DETAIL_CHARS];
    int ran;

    printf("        %s\n", want->what);
    for (i = 0; i < frames; i++) {
        short v = ctest_tone(i, want->rate, TONE_HZ, TONE_AMPLITUDE);
        src[LIFETIME_CHANNELS * i] = v;
        src[LIFETIME_CHANNELS * i + 1] = v;
    }
    CHECK(write_wav(WRAPPER_WAV_NAME, &src[0], frames, want->rate, LIFETIME_CHANNELS),
          "the three seconds are written to a WAV file");
    capture_out.clear();
    capture_used = 0;
    capture_unaligned = 0;
    memset(&chosen, 0, sizeof(chosen));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ran = run_wrapper_graph(WRAPPER_WAV_NAME, want, &chosen);
    CoUninitialize();
    /* what the wrapper's conversions did, before the encode below adds its own */
    through = capture_out;
    used = capture_used;
    unaligned = capture_unaligned;
    CHECK(ran, "the WAV file runs through the ACM Wrapper to its end");
    CHECK(ran && encode_whole_as(had, &chosen, &src[0], frames, &whole),
          "the three seconds are encoded to the wrapper's format with one header");
    CHECK_EQ_U(used, frames * LIFETIME_FRAME_BYTES, "the wrapper's conversions use all of the PCM");
    CHECK_EQ_U(unaligned, 1, "one conversion without BLOCKALIGN ends the data");
    sprintf(detail, "%lu bytes, one header %lu", (unsigned long) through.size(), (unsigned long) whole.size());
    ctest_record(ran && through == whole, "the wrapper gets the MP3 stream of the one-header encode", detail);
    _wremove(WRAPPER_WAV_NAME);
}

/**
 * @brief Checks the codec behind DirectShow's ACM Wrapper, the way a
 *        DirectShow application uses an ACM encoder.
 *
 * The codec is added for this process behind capture_proc(), which keeps
 * what the conversions return. The ACM Wrapper comes from the Audio
 * Compressors category, as documented. One format of each MPEG version
 * that LAME writes: MPEG-1, MPEG-2, and MPEG-2.5, the extension of MPEG-2 to
 * 8, 11.025 and 12 kHz that is not part of ISO/IEC 13818-3. The codec
 * offers its MPEG-2 and MPEG-2.5 formats as ABR.
 *
 * @param driver the path of the codec
 */
static void
test_behind_the_acm_wrapper(const char *driver)
{
    static const wrapper_format formats[] = {
        { 44100, 128000 / 8, ACM_FLAGS_CBR, "44100 Hz, 128 kbit/s CBR (MPEG-1)" },
        { 22050, 64000 / 8, ACM_FLAGS_ABR, "22050 Hz, 64 kbit/s ABR (MPEG-2)" },
        { 8000, 32000 / 8, ACM_FLAGS_ABR, "8000 Hz, 32 kbit/s ABR (MPEG-2.5)" },
    };
    HMODULE mod = LoadLibraryA(driver);
    HACMDRIVERID hadid = NULL;
    HACMDRIVER had = NULL;
    size_t f;

    printf("the codec behind DirectShow's ACM Wrapper\n");
    capture_real_proc = mod != NULL ? (DRIVERPROC) GetProcAddress(mod, "DriverProc") : NULL;
    if (capture_real_proc == NULL
        || acmDriverAdd(&hadid, (HINSTANCE) mod, (LPARAM) capture_proc, 0,
                        ACM_DRIVERADDF_FUNCTION | ACM_DRIVERADDF_LOCAL) != MMSYSERR_NOERROR
        || acmDriverOpen(&had, hadid, 0) != MMSYSERR_NOERROR) {
        CHECK(0, "the codec is added for this process");
        goto out;
    }
    for (f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
        check_behind_the_wrapper(had, &formats[f]);
    }
out:
    if (had != NULL) {
        acmDriverClose(had, 0);
    }
    if (hadid != NULL) {
        acmDriverRemove(hadid, 0);
    }
    if (mod != NULL) {
        FreeLibrary(mod);
    }
}

int
main(int argc, char **argv)
{
    char driver[MAX_PATH];
    int require;

    ctest_start("acm_test: the ACM codec's rate selection, configuration and conversion");
    test_output_sample_rate();
    test_output_sample_rate_extremes();
    test_smart_ratio_round_trip();
    test_malformed_config();
    test_abr_range_config();
    test_abr_ladder_below_step();
    test_vbr_format_list();
    test_vbr_limits_in_the_list();
    test_advanced_settings_file();
    test_format_families();
    test_bitrate_list();
    test_save_without_a_file();
    test_save_keeps_no_memory();
    test_close_after_query();

    if (ctest_component_path(argc, argv, "lameACM.acm", driver, sizeof(driver), &require)
        == CTEST_FOUND) {
        ctest_stderr codec_stderr;

        CHECK(ctest_load_with_stderr_file(driver, &codec_stderr),
              "the codec loads with its stderr going to a file");
        {
            char folder[MAX_PATH];
            char *name = NULL;
            DWORD const n = ::GetFullPathNameA(driver, sizeof folder, folder, &name);

            if (n > 0 && n < sizeof folder && name != NULL) {
                *name = '\0';
            } else {
                folder[0] = '\0';
            }
            CHECK(folder[0] != '\0'
                  && snprintf(codec_config, sizeof codec_config, "%s%s", folder, CONFIG_NAME)
                         < (int) sizeof codec_config,
                  "the folder of the codec is known");
        }
        test_under_the_acm(driver);
        test_registered_as_a_function(driver);
        test_about_dialog(driver);
        test_behind_the_acm_wrapper(driver);
        test_settings_reach_the_encoder(driver);
        test_config_dialog_version(driver);
        test_config_dialog_vbr(driver);
        test_config_dialog_families(driver);
        test_config_dialog_vbr_limits(driver);
        test_config_dialog_encoding_tab(driver);
        test_config_dialog_abr_list(driver);
        test_config_dialog_channel_modes(driver);
        test_config_dialog_quality(driver);
        test_config_dialog_result(driver);
        test_config_dialog_keeps_the_settings(driver);
        test_settings_tabs_through_the_codec(driver);
        test_dialog_layout(driver);
        /* LAME reports why it rejects the 50 Hz to 8000 Hz stream of
           test_settings_reach_the_encoder(). */
        ctest_stderr_empty(&codec_stderr, "the codec writes nothing to the stderr of its host");
    } else {
        /* Not a skip, with or without --require. The codec is built by the
           same solution as this test, so its absence is a failure of the
           build and not a missing option. */
        CHECK(0, "the built codec is beside this executable or named on the command line");
    }

    return ctest_summary("acm_test");
}
