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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "ctest.h"
#include "mp3frame.h"

#include <lame.h>
#include "ACMStream.h"
#include "AEncodeProperties.h"
#include "ACM.h"
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
    std::vector<unsigned int> Bitrates(unsigned int lowest) const { return AbrBitrates(lowest); }
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

/** @brief The button that close_config_dialog() presses: IDOK or IDCANCEL. */
static WORD config_dialog_button;
/** @brief Set by close_config_dialog() when it finds the dialog. */
static int config_dialog_found;

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

/**
 * @brief Presses config_dialog_button in the dialog of this thread.
 *
 * A timer calls it from the message loop of the modal dialog. Once it finds
 * the dialog, it stops the timer.
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
        config_dialog_found = 1;
        ::KillTimer(NULL, id);
        ::PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(config_dialog_button, BN_CLICKED), 0);
    }
}

/**
 * @brief Checks what AEncodeProperties::Config() returns after OK and after
 *        Cancel.
 *
 * The codec reports the result to the host of the DRV_CONFIGURE message. OK
 * saves the configuration, so the test removes the file afterwards.
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
    /* Often enough that the dialog closes soon after it opens. */
    const UINT TIMER_MS = 200;
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
        timer = ::SetTimer(NULL, 0, TIMER_MS, close_config_dialog);
        result = props.Config(codec, NULL);
        ::KillTimer(NULL, timer);
        CHECK(config_dialog_found, "the configuration dialog opens");
        CHECK(result == cases[i].result, cases[i].what);
    }
    ::DeleteFileA(CONFIG_NAME);
    ::FreeLibrary(codec);
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
 * @brief Checks that the codec returns an error for a destination buffer that
 *        is too small for its output, and never writes past the buffer.
 *
 * When the header is unprepared, the codec flushes the encoder into the same
 * destination buffer. Here the application chooses the buffer size. It is far
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
    test_suggest_unencodable_rate(had);

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

/**
 * @brief The fdwFlags values of the codec's own MP3 formats. The codec writes
 *        2 into an ABR format and 4 into a CBR format, and reads the value back
 *        when a stream opens.
 */
#define ACM_FLAGS_ABR 2
#define ACM_FLAGS_CBR 4

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
    /* Walk the frames before the header is unprepared: the unprepare flushes
       the encoder into the same buffer. The side information follows the
       header and the CRC, so a frame is read only when that much of it is in
       the buffer. */
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
 * @brief Writes a configuration file with the given elements in its current
 *        configuration.
 * @param elements the XML elements, one or more lines.
 * @return 1 on success, 0 if the file cannot be written.
 */
static int
write_settings(const char *elements)
{
    FILE *f = fopen(CONFIG_NAME, "wb");

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
 * The frames are read before the encoder is flushed, so the last frames can
 * be missing. The first frames hold the encoder delay. So the length may fall
 * short by up to three frames and run over by up to one.
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
    CHECK(c->frames > 0 && seconds >= SETTINGS_TEST_SECONDS - 3 * frame
          && seconds <= SETTINGS_TEST_SECONDS + frame, what);
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
 * - Without Smart Output, the 11025 Hz stream does not open.
 *
 * An installed codec reads its configuration file from its own folder. Here
 * the test adds the codec with @c ACM_DRIVERADDF_FUNCTION, and then the codec
 * gets no module handle and reads the file from the current directory. So the
 * test writes the file there. It keeps a file that was there before, and puts
 * it back at the end.
 *
 * @param driver the path of the codec.
 */
static void
test_settings_reach_the_encoder(const char *driver)
{
    const char *const config = CONFIG_NAME;
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
    }

    if (!write_settings("            <Smart use=\"false\" />\n")) {
        CHECK(0, "the configuration file can be written");
    } else {
        mr = encode_stereo_tone(driver, low_rate, 2, low_bps, ACM_FLAGS_CBR, &c);
        CHECK(mr != MMSYSERR_NOERROR, "without Smart Output, a 44100 Hz to 11025 Hz stream does not open");
    }

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
    test_bitrate_list();
    test_save_without_a_file();
    test_save_keeps_no_memory();

    if (ctest_component_path(argc, argv, "lameACM.acm", driver, sizeof(driver), &require)
        == CTEST_FOUND) {
        test_under_the_acm(driver);
        test_settings_reach_the_encoder(driver);
        test_config_dialog_version(driver);
        test_config_dialog_result(driver);
    } else {
        /* Not a skip, with or without --require. The codec is built by the
           same solution as this test, so its absence is a failure of the
           build and not a missing option. */
        CHECK(0, "the built codec is beside this executable or named on the command line");
    }

    return ctest_summary("acm_test");
}
