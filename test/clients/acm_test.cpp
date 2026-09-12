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
#include <stdlib.h>
#include <string.h>

#include "ctest.h"
#include "mp3frame.h"

#include "ACMStream.h"
#include "AEncodeProperties.h"
#include "../../libmp3lame/version.h"

/** @brief How the codec's long name begins: its own name, then LAME's version. */
#define LONG_NAME_PREFIX "LAME MP3 Codec v" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION)
/** @brief LAME's version in the ACM's driver-version layout (major, minor, build). */
#define DRIVER_VERSION (((DWORD) LAME_MAJOR_VERSION << 24) | ((DWORD) LAME_MINOR_VERSION << 16)                         | (DWORD) LAME_PATCH_VERSION)

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
 * @brief A configuration file that parses but is not one of ours is survived.
 *
 * Both structural lookups can come back empty, and this class runs inside a
 * driver the ACM has loaded into some application's process, so following an
 * empty one takes that application down rather than the codec. The file is
 * hand-editable and sits beside the codec, so the shapes below are the ones a
 * failed write or an edit produce.
 *
 * Reaching this at all is the point: each case has to be *read*, which is why
 * the ratio is checked afterwards. A build that stopped opening the file would
 * pass a test that only asserted "did not crash".
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
 * @brief Saving works with no configuration file to start from.
 *
 * The installer lays one down beside the codec, so the writer used to be able
 * to assume the document was there - and returned having written nothing when
 * it was not, which is what a lost or emptied file looks like. From then on the
 * user's settings silently stopped being kept.
 *
 * The round trip is what is asserted, not the file's existence: a save that
 * produced a file the codec cannot read back would pass the weaker check.
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

/** @brief Asserts a multimedia call succeeded, reporting the code when not. */
#define CHECK_MM(mr, what)                                               \
    do {                                                                 \
        MMRESULT ctest_mr_ = (mr);                                       \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "mmresult %u", (unsigned) ctest_mr_);           \
        ctest_record(ctest_mr_ == MMSYSERR_NOERROR, (what), ctest_d_);   \
    } while (0)

/** @brief Concert A, the test tone the source buffer is filled with. */
#define TONE_HZ         440.0
/** @brief Its amplitude, comfortably below full scale, so nothing clips. */
#define TONE_AMPLITUDE  16000.0
/** @brief One turn of the circle, for the sine's argument. */
#define TWO_PI          6.283185307179586

/** @brief The MPEG Layer-3 descriptor an application hands to the ACM. */
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

/** @brief The PCM descriptor for the source side of the conversion. */
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

/** @brief Counts the formats the codec offers, printing the first few. */
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

/** @brief A format the enumeration is asked to look for, and what it was called. */
typedef struct {
    int match_any;                          /**< take the first format offered */
    DWORD rate;                             /**< sample rate to match */
    DWORD bytes_per_sec;                    /**< byte rate to match */
    WORD channels;                          /**< channel count to match */
    DWORD flags;                            /**< Layer-3 tail flags to match */
    int found;                              /**< set once a format matched */
    MPEGLAYER3WAVEFORMAT format;            /**< the structure it was given */
    char name[ACMFORMATDETAILS_FORMAT_CHARS]; /**< the name the codec gave it */
} format_search;

/**
 * @brief Keeps the first enumerated format matching the search, and its name.
 *
 * The tail flags are part of the key: the codec offers a constant-rate and an
 * average-rate format at each rate, bitrate and channel count, and the two are
 * named differently. A search that left them out would match whichever came
 * first and compare two different formats' names - which is what it did, until
 * a run showed the list naming the same numbers ABR and the suggestion CBR.
 *
 * @param hadid the driver being enumerated, unused
 * @param pafd one format the driver offers, and the name it gives it
 * @param user the @c format_search this pass is filling in
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
 * @brief Walks the codec's MPEG Layer-3 format list, filling in a search.
 * @param had the opened driver
 * @param want what to look for, and where the match is left
 * @return what the enumeration call returned
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
 * @brief The format the codec suggests for a PCM source, and how it names it.
 *
 * These are the calls an application's compression chooser makes before it can
 * list this codec: ask what the PCM stream should be encoded to, then ask for a
 * description of the answer to show in the list. A suggestion filled in as
 * though the destination were PCM gets listed under whatever wording the ACM
 * generates from those fields, and encodes to a file whose header the player
 * then has to correct.
 *
 * The description is compared against the codec's own format list rather than
 * against a literal, so rewording the format string cannot fail this.
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
 * @brief Counts MPEG frames in an encoded buffer and the distinct bitrates.
 *
 * A byte total that comes out low has three possible causes - a tail the codec
 * never flushed, a bitrate it silently substituted, or a variable rate - and
 * the total cannot tell them apart. The frame headers can.
 */
static int
count_frames(const BYTE *buf, DWORD len, DWORD rate, int *distinct, int *sole_kbps)
{
    int seen = 0;
    int rates[MP3_BITRATE_INDEX_COUNT];
    int j;
    DWORD off = 0;

    memset(rates, 0, sizeof(rates));
    *distinct = 0;
    *sole_kbps = 0;
    while (off + MP3_HEADER_BYTES <= len) {
        const BYTE *h = buf + off;
        int index, padding, framelen;

        if (!mp3_is_frame_sync(h)) {
            ++off;
            continue;
        }
        index = mp3_bitrate_index(h);
        padding = mp3_padding_bytes(h);
        if (index == MP3_BITRATE_FREE_FORMAT || index == MP3_BITRATE_INVALID) {
            break;
        }
        framelen = mp3_frame_bytes(index, padding, rate);
        if (framelen <= 0) {
            break;
        }
        if (!rates[index]) {
            rates[index] = 1;
            ++*distinct;
        }
        ++seen;
        off += (DWORD) framelen;
    }
    if (*distinct == 1) {
        for (j = MP3_BITRATE_FREE_FORMAT + 1; j < MP3_BITRATE_INVALID; j++) {
            if (rates[j]) {
                *sole_kbps = mp3_bitrate_kbps[j];
            }
        }
    }
    return seen;
}

/**
 * @brief Drives the built codec through the Audio Compression Manager.
 *
 * The smoke test asks whether the DLL loads and exports what it should. This
 * asks whether the codec works when Windows drives it: msacm does the driver
 * message dispatch, the format negotiation and the buffer handling, exactly as
 * an application calling acmStreamConvert() would.
 *
 * No registry and no administrator are involved. acmDriverAdd() with
 * ACM_DRIVERADDF_FUNCTION registers a driver with the real framework for the
 * calling process only, given a DriverProc, and everything past that point is
 * the framework rather than a stand-in. What it cannot cover is the
 * machine-wide registration itself, which needs a registry change.
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
        double t = (double) i / (double) rate;
        short v = (short) (TONE_AMPLITUDE * sin(TWO_PI * TONE_HZ * t));
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
        int distinct = 0;
        int sole_kbps = 0;
        int seen = count_frames(dst, hdr.cbDstLengthUsed, rate, &distinct, &sole_kbps);

        CHECK(hdr.cbDstLengthUsed > 0, "the conversion produced output");
        CHECK_EQ_U(hdr.cbSrcLengthUsed, src_bytes, "all of the PCM was consumed");
        /* An MPEG audio frame begins with eleven set bits. Without this the
           test would accept any non-empty buffer, which is what "it produced
           output" usually means and rarely proves. */
        CHECK(hdr.cbDstLengthUsed >= MP3_HEADER_BYTES && mp3_is_frame_sync(dst),
              "the output begins with an MPEG frame sync");
        printf("        %lu bytes, %d frame(s), %d distinct bitrate(s)\n",
               (unsigned long) hdr.cbDstLengthUsed, seen, distinct);
        /* The first and the last frame are allowed to be missing: the encoder
           may hold one back, and the tail is only as long as what is left. */
        CHECK(seen >= (int) (mp3_frames_per_second(rate) * seconds) - 2,
              "the whole second is there in frames, tail included");
        /* More than one bitrate means the encoder chose a variable rate, where
           the average is expected to differ from the nominal one. A single rate
           that is not the requested one is a substitution, and the byte total
           alone cannot tell the two apart. */
        if (distinct == 1) {
            CHECK_EQ_U(sole_kbps, 128,
                       "a constant rate is the 128 kbps that was asked for");
        } else {
            CHECK(distinct > 1, "a variable rate, so no single rate to check");
        }
        acmStreamUnprepareHeader(has, &hdr, 0);
    }

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

/**
 * @brief Finds the codec beside this executable, where the build puts both.
 * @return 1 and fills @a out, or 0 if it is not there.
 */
static int
driver_beside_us(char *out, size_t n)
{
    char self[MAX_PATH];
    char *slash;

    if (::GetModuleFileNameA(NULL, self, MAX_PATH) == 0) {
        return 0;
    }
    slash = strrchr(self, '\\');
    if (slash == NULL) {
        return 0;
    }
    *(slash + 1) = '\0';
    if (strlen(self) + strlen("lameACM.acm") >= n) {
        return 0;
    }
    sprintf(out, "%slameACM.acm", self);
    return ::GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

int
main(int argc, char **argv)
{
    char driver[MAX_PATH];

    ctest_start("acm_test: the ACM codec's rate selection, configuration and conversion");
    test_output_sample_rate();
    test_smart_ratio_round_trip();
    test_malformed_config();
    test_save_without_a_file();

    if (argc > 1) {
        strncpy(driver, argv[1], sizeof(driver) - 1);
        driver[sizeof(driver) - 1] = '\0';
        test_under_the_acm(driver);
    } else if (driver_beside_us(driver, sizeof(driver))) {
        test_under_the_acm(driver);
    } else {
        /* Not a skip. The codec is built by the same solution as this test, so
           its absence is a failure of the build and not a missing option. */
        CHECK(0, "the built codec is beside this executable or named on the command line");
    }

    return ctest_summary("acm_test");
}
