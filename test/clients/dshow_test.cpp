/**
 * @file
 * @brief Tests for the LAME DirectShow filter.
 *
 * The smoke test checks that @c lame.ax loads and exports the COM entry
 * points. This test checks that it works as a filter. The graph manager
 * builds the graph and inserts the parser that the source needs. It
 * negotiates media types across both pin connections. Then it streams a WAV
 * file through the encoder into a file. All of this is DirectShow code that
 * drives the code of the filter.
 *
 * The test needs no registry change and no administrator. A registration
 * adds only the CLSID lookup. So the test does not call @c CoCreateInstance.
 * It gets the filter from the class factory of the DLL, through
 * @c DllGetClassObject. That is the same object through the same code path
 * inside the filter, without the registry step. So this test cannot check
 * that @c DllRegisterServer writes correct entries. It also cannot check that
 * the filter can be found by category and merit. Both need a machine-wide
 * registration and are out of scope.
 *
 * The filter is built only where the DirectShow base class sources are
 * installed. This test does not need them and is always built. When the
 * filter is missing, the test skips, or fails under --require.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>
#include <dshow.h>
#include <commctrl.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vector>

#include "ctest.h"
#include "dialogcheck.h"
#include "mp3frame.h"
#include "DialogFont.h"
#include "SettingText.h"
#include "../../libmp3lame/version.h"

/*
 * The filter's CLSID and its property interface's IID, spelled as bytes rather
 * than by including the filter's own headers. Those declare their GUIDs with
 * DEFINE_GUID, which needs the INITGUID dance to produce definitions; naming
 * them here keeps this file independent of that. If the filter's identity ever
 * changes, the class factory below fails loudly with CLASS_E_CLASSNOTAVAILABLE
 * rather than quietly testing nothing.
 */
static const GUID CLSID_LAMEDShowFilter_local =
    { 0xb8d27088, 0xff5f, 0x4b7c, { 0x98, 0xdc, 0x0e, 0x91, 0xa1, 0x69, 0x62, 0x86 } };
static const GUID IID_IAudioEncoderProperties_local =
    { 0xca7e9ef0, 0x1cbe, 0x11d3, { 0x8d, 0x29, 0x00, 0xa0, 0xc9, 0x4b, 0xbf, 0xee } };
static const GUID IID_IAudioEncoderProperties2_local =
    { 0xcaaa1fc6, 0x2a4f, 0x42f4, { 0x83, 0x75, 0x23, 0xd7, 0xdd, 0x84, 0x2c, 0xa7 } };

typedef HRESULT (STDAPICALLTYPE *PFN_DllGetClassObject)(REFCLSID, REFIID, void **);

/*
 * The property interface itself comes from the filter's public header, the one
 * an application configuring the encoder includes; its IID above is still
 * spelled locally, since the header's DEFINE_GUID declares without defining.
 */
#include "iaudioprops.h"

/**
 * @brief Releases a media type that the filter allocated for the caller.
 *
 * The DeleteMediaType() function of the base classes does this. But it is in
 * the library of the filter, not in anything a client links. So a client
 * frees the two allocations itself.
 *
 * @param pmt  the media type to release. A null pointer is ignored.
 */
static void
free_media_type(AM_MEDIA_TYPE *pmt)
{
    if (pmt == NULL) {
        return;
    }
    if (pmt->cbFormat != 0 && pmt->pbFormat != NULL) {
        CoTaskMemFree(pmt->pbFormat);
    }
    if (pmt->pUnk != NULL) {
        pmt->pUnk->Release();
    }
    CoTaskMemFree(pmt);
}

/** @brief Returns the first pin of the given direction, or NULL. */
static IPin *
find_pin(IBaseFilter *f, PIN_DIRECTION want)
{
    IEnumPins *e = NULL;
    IPin *p = NULL;

    if (FAILED(f->EnumPins(&e))) {
        return NULL;
    }
    while (e->Next(1, &p, NULL) == S_OK) {
        PIN_DIRECTION d;

        if (SUCCEEDED(p->QueryDirection(&d)) && d == want) {
            e->Release();
            return p;
        }
        p->Release();
        p = NULL;
    }
    e->Release();
    return NULL;
}

/** @brief Bytes in the canonical WAV header that this test writes. */
#define WAV_HEADER_BYTES        44
/** @brief Bytes in a four-character chunk identifier. */
#define WAV_CHUNK_ID_BYTES      4

/** @name Field offsets in that header */
/**@{*/
#define WAV_OFF_RIFF_ID          0
#define WAV_OFF_RIFF_SIZE        4
#define WAV_OFF_WAVE_ID          8
#define WAV_OFF_FMT_SIZE        16
#define WAV_OFF_FORMAT_TAG      20
#define WAV_OFF_CHANNELS        22
#define WAV_OFF_SAMPLE_RATE     24
#define WAV_OFF_BYTE_RATE       28
#define WAV_OFF_BLOCK_ALIGN     32
#define WAV_OFF_BITS_PER_SAMPLE 34
#define WAV_OFF_DATA_ID         36
#define WAV_OFF_DATA_SIZE       40
/**@}*/

/** @brief The RIFF size does not count the chunk ID and the size field itself. */
#define WAV_RIFF_SIZE_EXCLUDES  8
/** @brief Size of a PCM format chunk. It has no extension. */
#define WAV_PCM_FMT_BYTES       16
/** @brief The format tag for uncompressed PCM. */
#define WAV_FORMAT_PCM          1
/** @brief The sample width this test writes. */
#define WAV_BITS_PER_SAMPLE     16
/** @brief The same width in bytes. Several fields count in bytes. */
#define WAV_BYTES_PER_SAMPLE    (WAV_BITS_PER_SAMPLE / 8)

/**
 * @brief How long the graph may take to run the test audio through.
 *
 * The limit is generous. It turns a filter that never completes into a
 * failed check, so the test does not hang.
 */
#define GRAPH_TIMEOUT_MS        60000

/** @brief Concert A, the frequency of the test tone in the file. */
#define TONE_HZ                 440.0
/** @brief The amplitude of the tone. It is well below full scale, so nothing clips. */
#define TONE_AMPLITUDE          16000.0

/**
 * @brief The first bitrate that the property test sets. The test reads it
 *        back and then replaces it.
 */
#define FIRST_BITRATE_KBPS      128
/**
 * @brief The bitrate that the property test leaves set. The graph runs with
 *        this bitrate, so the output stream must use it.
 */
#define SECOND_BITRATE_KBPS     192
/** @brief Flag properties of the interface take a DWORD. This is the "on" value. */
#define SWITCH_ON               1
/** @brief What the LAME tag reports for the lowpass when there is no filter. */
#define NO_LOWPASS_HZ           0

/** @brief What write_wav() writes: the test tone in every channel, or noise. */
enum wav_content { WAV_TONE, WAV_NOISE };

/**
 * @brief Writes a WAV file with a sine or with noise. The graph reads this
 *        file.
 *
 * The header is the canonical 44-byte header: a RIFF chunk, a PCM format
 * chunk of the minimum size, and a data chunk. Each field is at an offset
 * that the format fixes. So the offsets have names, and the code does not
 * count them.
 *
 * @param path      the file.
 * @param rate      the sample rate in Hz.
 * @param channels  the channels.
 * @param frames    the sample frames.
 * @param content   the tone or noise, a different sample in each channel.
 * @return 1 if the file was written, else 0.
 */
static int
write_wav(const char *path, DWORD rate, WORD channels, DWORD frames, wav_content content)
{
    FILE *f = fopen(path, "wb");
    DWORD data_bytes = frames * channels * WAV_BYTES_PER_SAMPLE;
    unsigned char h[WAV_HEADER_BYTES];
    DWORD i, state = 1;

    if (f == NULL) {
        return 0;
    }
    memcpy(h + WAV_OFF_RIFF_ID, "RIFF", WAV_CHUNK_ID_BYTES);
    *(DWORD *) (h + WAV_OFF_RIFF_SIZE) =
        (WAV_HEADER_BYTES - WAV_RIFF_SIZE_EXCLUDES) + data_bytes;
    memcpy(h + WAV_OFF_WAVE_ID, "WAVEfmt ", 2 * WAV_CHUNK_ID_BYTES);
    *(DWORD *) (h + WAV_OFF_FMT_SIZE) = WAV_PCM_FMT_BYTES;
    *(WORD *) (h + WAV_OFF_FORMAT_TAG) = WAV_FORMAT_PCM;
    *(WORD *) (h + WAV_OFF_CHANNELS) = channels;
    *(DWORD *) (h + WAV_OFF_SAMPLE_RATE) = rate;
    *(DWORD *) (h + WAV_OFF_BYTE_RATE) = rate * channels * WAV_BYTES_PER_SAMPLE;
    *(WORD *) (h + WAV_OFF_BLOCK_ALIGN) = (WORD) (channels * WAV_BYTES_PER_SAMPLE);
    *(WORD *) (h + WAV_OFF_BITS_PER_SAMPLE) = WAV_BITS_PER_SAMPLE;
    memcpy(h + WAV_OFF_DATA_ID, "data", WAV_CHUNK_ID_BYTES);
    *(DWORD *) (h + WAV_OFF_DATA_SIZE) = data_bytes;
    fwrite(h, 1, sizeof(h), f);
    for (i = 0; i < frames; i++) {
        short v = ctest_tone(i, rate, TONE_HZ, TONE_AMPLITUDE);
        WORD c;

        for (c = 0; c < channels; c++) {
            if (content == WAV_NOISE)
                v = ctest_noise(&state);
            fwrite(&v, WAV_BYTES_PER_SAMPLE, 1, f);
        }
    }
    fclose(f);
    return 1;
}

/**
 * @brief Reads the output file back as MPEG frames.
 *
 * The reason is the same as in the ACM test. A byte count cannot tell a
 * truncated stream from a variable-bitrate stream, and the frame headers can.
 * Also as in the ACM test, a stream at one bitrate must be at the bitrate
 * that was set. Without that check, a filter that stores a bitrate, reads it
 * back and then encodes at any bitrate passes every check here.
 *
 * @param path         the file the graph wrote.
 * @param seconds      how much audio went in.
 * @param rate         its sample rate.
 * @param nominal_kbps the bitrate that the property interface was left set to.
 */
static void
inspect_mp3(const char *path, double seconds, DWORD rate, int nominal_kbps)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;
    mp3_scan scan;
    int want;
    int lowpass;

    if (f == NULL) {
        CHECK(0, "the graph produced an output file");
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        CHECK(0, "the output file is not empty");
        return;
    }
    buf = (unsigned char *) malloc((size_t) size);
    if (buf == NULL || fread(buf, 1, (size_t) size, f) != (size_t) size) {
        fclose(f);
        free(buf);
        CHECK(0, "the output file could be read back");
        return;
    }
    fclose(f);

    mp3_scan_frames(buf, size, rate, &scan);
    printf("        %ld bytes, %d frame(s), %d distinct bitrate(s)\n",
           size, scan.frames, scan.distinct);
    CHECK(size >= MP3_HEADER_BYTES && mp3_is_frame_sync(buf),
          "the file begins with an MPEG frame sync");
    /* Allow the first frame and the last: the encoder may hold one back, and
       the tail is only as long as what is left of the input. */
    want = (int) (seconds * mp3_frames_per_second(rate)) - 2;
    CHECK(scan.frames >= want, "the whole input is present as MPEG frames");
    if (scan.distinct == 1) {
        CHECK_EQ_U(scan.sole_kbps, nominal_kbps,
                   "a constant rate is the one the properties were left set to");
    }
    else {
        CHECK(scan.distinct > 1, "a variable rate, so no single rate to check");
    }
    /* The two switches the property test left set: the tag has to be there,
       and with keep-all-frequencies it has to say the encoder applied no
       lowpass. The tag is the encoder's own record of the filter it ran with,
       so this reads what reached the encoder, not what the filter stored. */
    lowpass = mp3_lame_tag_lowpass_hz(buf + scan.first_off, scan.first_len);
    printf("        LAME tag lowpass: %d Hz\n", lowpass);
    CHECK(lowpass != MP3_TAG_ABSENT, "the first frame carries a LAME tag");
    if (lowpass != MP3_TAG_ABSENT) {
        CHECK_EQ_U(lowpass, NO_LOWPASS_HZ,
                   "with keep-all-frequencies set, the tag reports no lowpass filter");
    }
    free(buf);
}

/**
 * @brief Checks that the property interface of the filter works and keeps the
 *        values it is given.
 *
 * @c iaudioprops.h says to configure the encoder with its input pin already
 * connected. Before that, the defaults for the input media type override the
 * parameters. So the test calls this function between the two connections,
 * not before either one.
 *
 * Afterwards the test reads two of the switches back from the stream. With
 * the LAME tag switch, the first frame must have a LAME tag. With
 * keep-all-frequencies, that tag must report no lowpass filter. A switch that
 * is stored and read back, but never gets to the encoder, passes the round
 * trip here. It fails the check on the stream.
 */
static void
test_encoder_properties(IBaseFilter *lame)
{
    IAudioEncoderProperties *props = NULL;
    HRESULT hr;
    DWORD first = 0;
    DWORD second = 0;
    DWORD keep = 0;
    DWORD tag = 0;

    hr = lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props);
    REQUIRE_HR(hr, "the filter offers its audio encoder properties");
    if (FAILED(hr)) {
        return;
    }

    REQUIRE_HR(props->set_Bitrate(FIRST_BITRATE_KBPS), "a bitrate of 128 kbps is accepted");
    REQUIRE_HR(props->get_Bitrate(&first), "the bitrate reads back");
    CHECK_EQ_U(first, FIRST_BITRATE_KBPS, "it reads back as the value that was set");

    REQUIRE_HR(props->set_Bitrate(SECOND_BITRATE_KBPS), "a bitrate of 192 kbps is accepted");
    REQUIRE_HR(props->get_Bitrate(&second), "the second bitrate reads back");
    CHECK_EQ_U(second, SECOND_BITRATE_KBPS, "it reads back as the second value");
    CHECK_NE_U(first, second, "the interface is not returning a fixed number");

    REQUIRE_HR(props->set_KeepAllFreq(SWITCH_ON), "keep-all-frequencies is accepted");
    REQUIRE_HR(props->get_KeepAllFreq(&keep), "keep-all-frequencies reads back");
    CHECK_EQ_U(keep, SWITCH_ON, "keep-all-frequencies reads back as set");

    REQUIRE_HR(props->set_XingTag(SWITCH_ON), "writing the LAME tag is accepted");
    REQUIRE_HR(props->get_XingTag(&tag), "the tag switch reads back");
    CHECK_EQ_U(tag, SWITCH_ON, "the tag switch reads back as set");

    props->Release();
}

/**
 * @brief Calls IPin::QueryAccept(), and returns the exception code when the
 *        filter faults.
 * @param pin  the pin to ask.
 * @param mt   the media type to propose.
 * @return the result of the call, or the exception code if it faults.
 */
static HRESULT
query_accept_catching_faults(IPin *pin, const AM_MEDIA_TYPE *mt)
{
    __try {
        return pin->QueryAccept(mt);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return (HRESULT) GetExceptionCode();
    }
}

/**
 * @brief Walks the media types of a pin, with the same fault guard as
 *        query_accept_catching_faults().
 * @param pin    the pin to ask.
 * @param count  receives the number of types that the walk returned.
 * @return S_OK when the walk ends, the failure code of the enumeration, or
 *         the exception code if it faults.
 */
static HRESULT
enum_types_catching_faults(IPin *pin, ULONG *count)
{
    IEnumMediaTypes *e = NULL;
    AM_MEDIA_TYPE *mt = NULL;
    HRESULT hr;

    *count = 0;
    __try {
        hr = pin->EnumMediaTypes(&e);
        if (FAILED(hr)) {
            return hr;
        }
        while (e->Next(1, &mt, NULL) == S_OK) {
            ++*count;
            free_media_type(mt);
        }
        e->Release();
        return S_OK;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return (HRESULT) GetExceptionCode();
    }
}

/**
 * @brief Checks that the output pin still accepts an audio media type and
 *        lists its types when the output sample rate is set to 0.
 *
 * The test sets the rate through the property interface. The output pin
 * checks a proposed audio type against the configured output rate. The test
 * runs after the main encode, because a fault inside the filter can leave the
 * filter locked. The test puts the earlier rate back afterwards.
 *
 * @param lame      the filter, its input connected.
 * @param lame_out  its output pin.
 */
static void
test_zero_output_rate(IBaseFilter *lame, IPin *lame_out)
{
    IAudioEncoderProperties *props = NULL;
    MPEGLAYER3WAVEFORMAT wf;
    AM_MEDIA_TYPE mt;
    DWORD   before = 0;
    HRESULT hr;
    char    detail[CTEST_DETAIL_CHARS];

    if (FAILED(lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "the filter offers its audio encoder properties again");
        return;
    }
    REQUIRE_HR(props->get_SampleRate(&before), "the output rate reads back");
    REQUIRE_HR(props->set_SampleRate(0), "an output rate of 0 is stored");

    memset(&wf, 0, sizeof(wf));
    wf.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    wf.wfx.nChannels = 2;
    wf.wfx.nSamplesPerSec = 44100;
    wf.wfx.nAvgBytesPerSec = 128000 / 8;
    wf.wfx.nBlockAlign = 1;
    wf.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
    memset(&mt, 0, sizeof(mt));
    mt.majortype = MEDIATYPE_Audio;
    mt.formattype = FORMAT_WaveFormatEx;
    mt.cbFormat = sizeof(wf);
    mt.pbFormat = (BYTE *) &wf;
    hr = query_accept_catching_faults(lame_out, &mt);
    sprintf(detail, "hr 0x%08lX", (unsigned long) hr);
    ctest_record(hr == S_OK, "the output pin accepts an audio type with the output rate at 0", detail);

    {
        ULONG   types = 0;

        hr = enum_types_catching_faults(lame_out, &types);
        sprintf(detail, "hr 0x%08lX, %lu type(s)", (unsigned long) hr, (unsigned long) types);
        ctest_record(hr == S_OK && types > 0, "the output pin lists its types with the output rate at 0", detail);
    }

    props->set_SampleRate(before);
    props->Release();
}

/**
 * @brief Checks that the graph does not run with a setting that the encoder
 *        rejects.
 *
 * The property interface stores a variable bitrate range whose minimum is
 * above its maximum. The library rejects that range when the stream starts.
 * Then it stores a LAME tag switch of 2, which the setter of the library
 * rejects.
 * The graph is the one that the main test ran to completion. The test puts
 * the settings back afterwards.
 *
 * @param lame  the filter, both pins connected.
 * @param mc    the graph's media control.
 */
static void
test_refused_setting_fails_run(IBaseFilter *lame, IMediaControl *mc)
{
    IAudioEncoderProperties *props = NULL;
    DWORD   variable = 0, vmin = 0, vmax = 0, xing = 0;
    HRESULT hr;
    char    detail[CTEST_DETAIL_CHARS];

    if (FAILED(lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "the filter offers its audio encoder properties for the refused setting");
        return;
    }
    REQUIRE_HR(props->get_Variable(&variable), "the VBR switch reads back");
    REQUIRE_HR(props->get_VariableMin(&vmin), "the VBR minimum reads back");
    REQUIRE_HR(props->get_VariableMax(&vmax), "the VBR maximum reads back");
    REQUIRE_HR(props->set_Variable(1), "VBR is switched on");
    REQUIRE_HR(props->set_VariableMin(320), "a VBR minimum of 320 kbps is stored");
    REQUIRE_HR(props->set_VariableMax(32), "a VBR maximum of 32 kbps is stored");
    hr = mc->Run();
    sprintf(detail, "hr 0x%08lX", (unsigned long) hr);
    ctest_record(FAILED(hr), "the graph refuses to run with a VBR range LAME refuses", detail);
    mc->Stop();
    props->set_VariableMax(vmax);
    props->set_VariableMin(vmin);
    props->set_Variable(variable);

    /* A setter that rejects its value: the LAME tag switch takes 0 or 1. */
    REQUIRE_HR(props->get_XingTag(&xing), "the LAME tag switch reads back");
    REQUIRE_HR(props->set_XingTag(2), "a LAME tag switch of 2 is stored");
    hr = mc->Run();
    sprintf(detail, "hr 0x%08lX", (unsigned long) hr);
    ctest_record(FAILED(hr), "the graph refuses to run with a LAME tag switch LAME rejects", detail);
    mc->Stop();
    props->set_XingTag(xing);
    props->Release();
}

/**
 * @brief Checks that seeking passes through the encoder to the filter
 *        upstream of it.
 *
 * A transform filter is expected to pass @c IMediaSeeking and
 * @c IMediaPosition from its output pin back to the pin that feeds its input.
 * Then an application that asks the graph for the length or the position of
 * the stream gets a value from the source. The base class of the output pin
 * does this. The class below that base class does not, and returns
 * E_NOINTERFACE.
 *
 * So this checks which parent class gets the interface query of the pin. The
 * check works only after the input pin is connected. Before that, there is
 * nothing to pass through to. The test deliberately asks for the duration as
 * well as for the interface. An object that returns the interface and then
 * knows nothing passes the first half alone.
 *
 * @param lame_out  the filter's output pin, its input already connected.
 */
static void
test_seeking_passes_through(IPin *lame_out)
{
    IMediaSeeking *seek = NULL;
    IMediaPosition *pos = NULL;
    HRESULT hr;

    hr = lame_out->QueryInterface(IID_IMediaSeeking, (void **) &seek);
    CHECK(SUCCEEDED(hr) && seek != NULL,
          "the encoder's output pin offers IMediaSeeking");
    if (SUCCEEDED(hr) && seek != NULL) {
        LONGLONG duration = 0;

        hr = seek->GetDuration(&duration);
        CHECK(SUCCEEDED(hr), "and answers how long the stream is");
        CHECK(duration > 0,
              "with a duration that came from upstream, not from nowhere");
        seek->Release();
    }

    hr = lame_out->QueryInterface(IID_IMediaPosition, (void **) &pos);
    CHECK(SUCCEEDED(hr) && pos != NULL,
          "the encoder's output pin offers IMediaPosition");
    if (SUCCEEDED(hr) && pos != NULL) {
        pos->Release();
    }
}

/**
 * @brief Checks the capability list of the encoder, which a caller reads
 *        before it connects.
 *
 * @c IAMStreamConfig::GetStreamCaps() describes what the encoder can produce.
 * So it must work in every connection state of the pin:
 * - not connected at all, as an application that lists the formats finds it,
 * - connected as a stream, as a file writer downstream negotiates it.
 *
 * Neither state has an audio format block. A result built from the media type
 * of the pin follows a null pointer (SF bug #424).
 *
 * The test also checks the index range. The entry one past the end is not in
 * the table. If the code read that entry, it would divide by a zero sample
 * rate.
 *
 * @param lame_out  the output pin of the filter.
 * @param when      names the pin state. It is inserted into the description
 *                  of each check.
 */
static void
test_stream_caps(IPin *lame_out, const char *when)
{
    IAMStreamConfig *cfg = NULL;
    AUDIO_STREAM_CONFIG_CAPS scc;
    AM_MEDIA_TYPE *pmt = NULL;
    char what[CTEST_DETAIL_CHARS];
    int count = 0, size = 0;
    HRESULT hr;

    hr = lame_out->QueryInterface(IID_IAMStreamConfig, (void **) &cfg);
    sprintf(what, "the output pin offers IAMStreamConfig %s", when);
    CHECK(SUCCEEDED(hr) && cfg != NULL, what);
    if (FAILED(hr) || cfg == NULL) {
        return;
    }

    hr = cfg->GetNumberOfCapabilities(&count, &size);
    CHECK(SUCCEEDED(hr) && count > 0, "it reports how many entries it has");
    CHECK_EQ_U(size, sizeof(AUDIO_STREAM_CONFIG_CAPS),
               "and the size of the companion structure");

    hr = cfg->GetStreamCaps(0, &pmt, (BYTE *) &scc);
    sprintf(what, "the first entry can be read %s", when);
    CHECK(hr == S_OK && pmt != NULL, what);
    if (hr == S_OK && pmt != NULL) {
        MPEGLAYER3WAVEFORMAT *wf = (MPEGLAYER3WAVEFORMAT *) pmt->pbFormat;

        CHECK(pmt->cbFormat >= sizeof(MPEGLAYER3WAVEFORMAT) && wf != NULL,
              "and it carries an MP3 format block");
        if (wf != NULL && pmt->cbFormat >= sizeof(MPEGLAYER3WAVEFORMAT)) {
            CHECK(wf->wfx.nSamplesPerSec > 0 && wf->wfx.nAvgBytesPerSec > 0,
                  "describing a real sample rate and data rate");
            CHECK(wf->nBlockSize > 0, "and a frame length");
        }
        free_media_type(pmt);
        pmt = NULL;
    }

    hr = cfg->GetStreamCaps(count - 1, &pmt, (BYTE *) &scc);
    CHECK(hr == S_OK, "so can the last one");
    free_media_type(pmt);
    pmt = NULL;

    /* One past the end. The array holds `count` entries, so this index is not
       one of them; it used to be accepted and read the zeroed slot after the
       table, whose 0 Hz sample rate reaches a division. */
    hr = cfg->GetStreamCaps(count, &pmt, (BYTE *) &scc);
    CHECK(hr != S_OK, "one past the last entry is refused");
    free_media_type(pmt);
    pmt = NULL;

    hr = cfg->GetStreamCaps(-1, &pmt, (BYTE *) &scc);
    CHECK(hr == E_INVALIDARG, "a negative index is refused");
    free_media_type(pmt);

    cfg->Release();
}

/** @brief A getter of the property interface that reads one DWORD setting. */
typedef HRESULT (STDMETHODCALLTYPE IAudioEncoderProperties::*dword_getter)(DWORD *);
/** @brief The setter that goes with it. */
typedef HRESULT (STDMETHODCALLTYPE IAudioEncoderProperties::*dword_setter)(DWORD);

/** @brief One entry of ::stored_settings. */
struct stored_setting {
    dword_getter get;           /**< reads the setting */
    dword_setter set;           /**< writes the setting */
    const char *name;           /**< the name of the setting in a failure */
};

/** @brief The 20 settings that the filter stores as they are. */
static const stored_setting stored_settings[] = {
    { &IAudioEncoderProperties::get_Bitrate, &IAudioEncoderProperties::set_Bitrate, "Bitrate" },
    { &IAudioEncoderProperties::get_VariableMin, &IAudioEncoderProperties::set_VariableMin, "VariableMin" },
    { &IAudioEncoderProperties::get_VariableMax, &IAudioEncoderProperties::set_VariableMax, "VariableMax" },
    { &IAudioEncoderProperties::get_Quality, &IAudioEncoderProperties::set_Quality, "Quality" },
    { &IAudioEncoderProperties::get_VariableQ, &IAudioEncoderProperties::set_VariableQ, "VariableQ" },
    { &IAudioEncoderProperties::get_SampleRate, &IAudioEncoderProperties::set_SampleRate, "SampleRate" },
    { &IAudioEncoderProperties::get_ForceMS, &IAudioEncoderProperties::set_ForceMS, "ForceMS" },
    { &IAudioEncoderProperties::get_CRCFlag, &IAudioEncoderProperties::set_CRCFlag, "CRCFlag" },
    { &IAudioEncoderProperties::get_ForceMono, &IAudioEncoderProperties::set_ForceMono, "ForceMono" },
    { &IAudioEncoderProperties::get_SetDuration, &IAudioEncoderProperties::set_SetDuration, "SetDuration" },
    { &IAudioEncoderProperties::get_SampleOverlap, &IAudioEncoderProperties::set_SampleOverlap, "SampleOverlap" },
    { &IAudioEncoderProperties::get_EnforceVBRmin, &IAudioEncoderProperties::set_EnforceVBRmin, "EnforceVBRmin" },
    { &IAudioEncoderProperties::get_VoiceMode, &IAudioEncoderProperties::set_VoiceMode, "VoiceMode" },
    { &IAudioEncoderProperties::get_KeepAllFreq, &IAudioEncoderProperties::set_KeepAllFreq, "KeepAllFreq" },
    { &IAudioEncoderProperties::get_StrictISO, &IAudioEncoderProperties::set_StrictISO, "StrictISO" },
    { &IAudioEncoderProperties::get_NoShortBlock, &IAudioEncoderProperties::set_NoShortBlock, "NoShortBlock" },
    { &IAudioEncoderProperties::get_XingTag, &IAudioEncoderProperties::set_XingTag, "XingTag" },
    { &IAudioEncoderProperties::get_OriginalFlag, &IAudioEncoderProperties::set_OriginalFlag, "OriginalFlag" },
    { &IAudioEncoderProperties::get_CopyrightFlag, &IAudioEncoderProperties::set_CopyrightFlag, "CopyrightFlag" },
    { &IAudioEncoderProperties::get_ModeFixed, &IAudioEncoderProperties::set_ModeFixed, "ModeFixed" }
};

/**
 * @brief Checks that the filter's defaults are LAME's: encoding quality 3,
 *        original and the LAME tag on, copyright, checksum and private off,
 *        VBR level 4 without bitrate limits, the sample rate of the input.
 *
 * DefaultAudioEncoderProperties() needs a connected input. The test runs after
 * the graph has, so no later test sees the defaults.
 *
 * @param lame  the filter, with its input connected.
 */
static void
test_default_settings(IBaseFilter *lame)
{
    IAudioEncoderProperties *props = NULL;
    DWORD quality = 0;
    HRESULT hr;

    printf("the default settings of the filter\n");
    hr = lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props);
    REQUIRE_HR(hr, "the filter offers its audio encoder properties");
    if (FAILED(hr))
        return;
    REQUIRE_HR(props->DefaultAudioEncoderProperties(), "the filter resets its settings to the defaults");
    REQUIRE_HR(props->get_Quality(&quality), "the encoding quality reads back");
    CHECK_EQ_U(quality, 3, "the default encoding quality is LAME's, 3");
    {
        DWORD original = 0, copyright = 1, crc = 1, tag = 0, vbr_min = 1, vbr_max = 1, level = 0, rate = 1;
        DWORD private_flag = 1;
        IAudioEncoderProperties2 *props2 = NULL;

        props->get_OriginalFlag(&original);
        props->get_CopyrightFlag(&copyright);
        props->get_CRCFlag(&crc);
        props->get_XingTag(&tag);
        props->get_VariableMin(&vbr_min);
        props->get_VariableMax(&vbr_max);
        props->get_VariableQ(&level);
        props->get_SampleRate(&rate);
        if (SUCCEEDED(props->QueryInterface(IID_IAudioEncoderProperties2_local, (void **) &props2))) {
            props2->get_PrivateFlag(&private_flag);
            props2->Release();
        }
        CHECK(original == 1 && copyright == 0 && crc == 0 && private_flag == 0 && tag == 1,
              "the default frame options are LAME's: original and the LAME tag on, the others off");
        CHECK(vbr_min == VBR_BITRATE_NO_LIMIT && vbr_max == VBR_BITRATE_NO_LIMIT && level == 4,
              "the default VBR is LAME's: level 4, no bitrate limits");
        CHECK_EQ_U(rate, 0, "by default the output keeps the sample rate of the input, as lame does");
    }
    props->Release();
}

/**
 * @brief Checks that each of the settings that the filter stores as they are
 *        reads back the value written to it, and only that one.
 *
 * Every setting gets a value that no other one gets. Then all of them are
 * read back. An accessor that wrote or read the field of another setting
 * returns that other value. The settings are put back afterwards.
 *
 * @param lame  the filter.
 */
static void
test_property_round_trip(IBaseFilter *lame)
{
    enum { N = sizeof(stored_settings) / sizeof(stored_settings[0]), FIRST_VALUE = 1000 };
    IAudioEncoderProperties *props = NULL;
    DWORD   saved[N], got;
    char    detail[CTEST_DETAIL_CHARS];
    int     i, same = 0;

    if (FAILED(lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "the filter offers its audio encoder properties for the round trip");
        return;
    }
    for (i = 0; i < N; i++) {
        saved[i] = 0;
        (props->*stored_settings[i].get)(&saved[i]);
    }
    for (i = 0; i < N; i++) {
        (props->*stored_settings[i].set)((DWORD) (FIRST_VALUE + i));
    }
    for (i = 0; i < N; i++) {
        got = 0;
        if ((props->*stored_settings[i].get)(&got) == S_OK && got == (DWORD) (FIRST_VALUE + i)) {
            ++same;
        } else {
            sprintf(detail, "%s reads back %lu, not %d", stored_settings[i].name, (unsigned long) got,
                    FIRST_VALUE + i);
            CHECK(0, detail);
        }
    }
    CHECK_EQ_U(same, N, "each of the 20 stored settings reads back its own value");
    for (i = 0; i < N; i++) {
        (props->*stored_settings[i].set)(saved[i]);
    }
    props->Release();
}

/** @brief The registry key in which the filter keeps its settings, under HKCU. */
static const char FILTER_SETTINGS_KEY[] = "SOFTWARE\\GNU\\LAME MPEG Layer III Audio Encoder Filter";
/** @brief The most values of that key that saved_key keeps. */
#define SAVED_VALUES_MAX 64
/** @brief The longest value name that saved_key keeps, with its NUL. */
#define SAVED_NAME_BYTES 128
/** @brief The longest value that saved_key keeps, in bytes. */
#define SAVED_DATA_BYTES 64

/** @brief The values of the settings key as they were before a test wrote it. */
typedef struct {
    int existed;                                    /**< whether the key was there */
    int count;                                      /**< the values kept */
    char name[SAVED_VALUES_MAX][SAVED_NAME_BYTES];  /**< their names */
    DWORD type[SAVED_VALUES_MAX];                   /**< their types */
    BYTE data[SAVED_VALUES_MAX][SAVED_DATA_BYTES];  /**< their data */
    DWORD size[SAVED_VALUES_MAX];                   /**< the bytes of each */
} saved_key;

/**
 * @brief Keeps every value of the settings key, so that restore_key() can put
 *        them back.
 * @param s  receives the values.
 * @return 1, or 0 when a value is too large or too many to keep.
 */
static int
save_key(saved_key *s)
{
    HKEY key;
    DWORD i;

    memset(s, 0, sizeof(*s));
    if (RegOpenKeyExA(HKEY_CURRENT_USER, FILTER_SETTINGS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 1;
    }
    s->existed = 1;
    for (i = 0;; i++) {
        DWORD name_len = SAVED_NAME_BYTES, data_len = SAVED_DATA_BYTES;
        LONG rc;

        if (s->count == SAVED_VALUES_MAX) {
            RegCloseKey(key);
            return 0;
        }
        rc = RegEnumValueA(key, i, s->name[s->count], &name_len, NULL, &s->type[s->count],
                           s->data[s->count], &data_len);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            RegCloseKey(key);
            return 0;
        }
        s->size[s->count] = data_len;
        s->count++;
    }
    RegCloseKey(key);
    return 1;
}

/**
 * @brief Puts the settings key back as save_key() found it: removed when it
 *        was not there, otherwise with exactly the values it had.
 * @param s  the values that save_key() kept.
 * @return 1, or 0 when the key could not be written.
 */
static int
restore_key(const saved_key *s)
{
    HKEY key;
    int i, ok = 1;
    LONG rc = RegDeleteTreeA(HKEY_CURRENT_USER, FILTER_SETTINGS_KEY);

    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (!s->existed) {
        return 1;
    }
    if (RegCreateKeyExA(HKEY_CURRENT_USER, FILTER_SETTINGS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key,
                        NULL) != ERROR_SUCCESS) {
        return 0;
    }
    for (i = 0; i < s->count; i++) {
        if (RegSetValueExA(key, s->name[i], 0, s->type[i], s->data[i], s->size[i]) != ERROR_SUCCESS) {
            ok = 0;
        }
    }
    RegCloseKey(key);
    return ok;
}

/**
 * @brief Checks that the 20 settings of ::stored_settings reach a new filter
 *        through the registry.
 *
 * One filter gets a value for each setting that no other gets, and saves
 * them. A second filter reads its settings from the
 * registry when it is created, and must report the same values. The user's
 * own settings key is kept before and put back after.
 *
 * @param cf  the class factory of the filter.
 */
static void
test_settings_survive_a_save(IClassFactory *cf)
{
    enum { N = sizeof(stored_settings) / sizeof(stored_settings[0]), FIRST_VALUE = 2000 };
    static saved_key saved;
    IBaseFilter *first = NULL, *second = NULL;
    IAudioEncoderProperties *props = NULL;
    DWORD got;
    char detail[CTEST_DETAIL_CHARS];
    int i, same = 0;

    if (!save_key(&saved)) {
        CHECK(0, "the settings key can be kept before the test writes it");
        return;
    }
    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &first))
        || FAILED(first->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "a first filter offers its audio encoder properties");
        goto out;
    }
    for (i = 0; i < N; i++) {
        (props->*stored_settings[i].set)((DWORD) (FIRST_VALUE + i));
    }
    REQUIRE_HR(props->SaveAudioEncoderPropertiesToRegistry(), "the first filter saves its settings");
    props->Release();
    props = NULL;

    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &second))
        || FAILED(second->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "a second filter offers its audio encoder properties");
        goto out;
    }
    for (i = 0; i < N; i++) {
        got = 0;
        if ((props->*stored_settings[i].get)(&got) == S_OK && got == (DWORD) (FIRST_VALUE + i)) {
            ++same;
        } else {
            sprintf(detail, "%s comes back as %lu, not %d", stored_settings[i].name,
                    (unsigned long) got, FIRST_VALUE + i);
            CHECK(0, detail);
        }
    }
    CHECK_EQ_U(same, N, "a new filter reads each of the 20 saved settings");

out:
    if (props) props->Release();
    if (second) second->Release();
    if (first) first->Release();
    CHECK(restore_key(&saved), "the settings key is put back as it was");
}

/** @brief How many saves test_save_keeps_no_handle() makes. */
#define HANDLE_ROUNDS 20

/**
 * @brief Checks that saving the settings keeps no registry key open.
 *
 * A filter saves its settings ::HANDLE_ROUNDS times, and each save opens the
 * settings key. The handle count of the process is taken after the first
 * save and after the last one. It must not grow by one handle per save. The
 * user's settings key is kept before and put back after.
 *
 * @param cf  the class factory of the filter.
 */
static void
test_save_keeps_no_handle(IClassFactory *cf)
{
    static saved_key saved;
    IBaseFilter *f = NULL;
    IAudioEncoderProperties *props = NULL;
    DWORD before = 0, after = 0;
    char detail[CTEST_DETAIL_CHARS];
    int i;

    if (!save_key(&saved)) {
        CHECK(0, "the settings key can be kept before the handle test writes it");
        return;
    }
    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &f))
        || FAILED(f->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
        CHECK(0, "a filter offers its audio encoder properties for the handle test");
        goto out;
    }
    REQUIRE_HR(props->SaveAudioEncoderPropertiesToRegistry(), "the filter saves its settings");
    GetProcessHandleCount(GetCurrentProcess(), &before);
    for (i = 0; i < HANDLE_ROUNDS; i++) {
        props->SaveAudioEncoderPropertiesToRegistry();
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    sprintf(detail, "%lu handles after the first save, %lu after %d more", (unsigned long) before,
            (unsigned long) after, HANDLE_ROUNDS);
    ctest_record(before > 0 && after < before + HANDLE_ROUNDS,
                 "saving the settings keeps no registry key open", detail);

out:
    if (props) props->Release();
    if (f) f->Release();
    CHECK(restore_key(&saved), "the settings key is put back after the handle test");
}

/**
 * @brief Checks every entry of the capability list for a 44.1 kHz input, in
 *        order.
 *
 * The filter offers the CBR formats whose sample rate divides the input rate.
 * For 44.1 kHz input these are the MPEG-1 bitrates at 44100 Hz, the MPEG-2
 * bitrates at 22050 Hz, and the MPEG-2.5 bitrates up to 64 kbit/s at
 * 11025 Hz, each highest first.
 *
 * @param lame_out  the output pin of the filter, with a 44.1 kHz input.
 */
static void
test_stream_caps_list(IPin *lame_out)
{
    static const DWORD mpeg1[] = { 320, 256, 224, 192, 160, 128, 112, 96, 80, 64, 56, 48, 40, 32 };
    static const DWORD mpeg2[] = { 160, 144, 128, 112, 96, 80, 64, 56, 48, 40, 32, 24, 16, 8 };
    static const DWORD mpeg25[] = { 64, 56, 48, 40, 32, 24, 16, 8 };
    const int n1 = (int) (sizeof(mpeg1) / sizeof(mpeg1[0]));
    const int n2 = (int) (sizeof(mpeg2) / sizeof(mpeg2[0]));
    const int n25 = (int) (sizeof(mpeg25) / sizeof(mpeg25[0]));
    IAMStreamConfig *cfg = NULL;
    AUDIO_STREAM_CONFIG_CAPS scc;
    AM_MEDIA_TYPE *pmt = NULL;
    int count = 0, size = 0, i, same = 0;
    HRESULT hr;

    hr = lame_out->QueryInterface(IID_IAMStreamConfig, (void **) &cfg);
    if (FAILED(hr) || cfg == NULL) {
        CHECK(0, "the output pin offers IAMStreamConfig for the list");
        return;
    }
    hr = cfg->GetNumberOfCapabilities(&count, &size);
    CHECK_EQ_U(count, n1 + n2 + n25, "a 44.1 kHz input has 36 CBR formats");
    for (i = 0; SUCCEEDED(hr) && i < count && i < n1 + n2 + n25; i++) {
        DWORD rate, kbps;

        if (i < n1) {
            rate = 44100;
            kbps = mpeg1[i];
        } else if (i < n1 + n2) {
            rate = 22050;
            kbps = mpeg2[i - n1];
        } else {
            rate = 11025;
            kbps = mpeg25[i - n1 - n2];
        }
        pmt = NULL;
        if (cfg->GetStreamCaps(i, &pmt, (BYTE *) &scc) == S_OK && pmt != NULL
            && pmt->cbFormat >= sizeof(MPEGLAYER3WAVEFORMAT) && pmt->pbFormat != NULL) {
            MPEGLAYER3WAVEFORMAT *wf = (MPEGLAYER3WAVEFORMAT *) pmt->pbFormat;

            if (wf->wfx.nSamplesPerSec == rate && wf->wfx.nAvgBytesPerSec * 8 / 1000 == kbps) {
                ++same;
            }
        }
        free_media_type(pmt);
    }
    CHECK_EQ_U(same, n1 + n2 + n25, "each has the expected sample rate and bitrate, in order");
    cfg->Release();
}

/**
 * @brief An allocator that fails one request for a buffer and passes every
 *        other call to a memory allocator of the system.
 *
 * It lives in the sink pin that hands it out. It does not need reference
 * counting and does not do it.
 */
class FailingAllocator : public IMemAllocator {
public:
    IMemAllocator *inner;   /**< the system allocator that does the work */
    int     fail_at;        /**< the request for a buffer that fails, counted from 1 */
    int     requests;       /**< the requests for a buffer so far */

    /** @brief Creates an allocator whose request @p n fails. @param n the request. */
    FailingAllocator(int n) : inner(NULL), fail_at(n), requests(0)
    {
        CoCreateInstance(CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER, IID_IMemAllocator,
                         (void **) &inner);
    }
    /** @brief Releases the system allocator. */
    ~FailingAllocator()
    {
        if (inner)
            inner->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IMemAllocator) {
            *ppv = static_cast<IMemAllocator *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    STDMETHODIMP SetProperties(ALLOCATOR_PROPERTIES *want, ALLOCATOR_PROPERTIES *got)
    {
        return inner ? inner->SetProperties(want, got) : E_FAIL;
    }
    STDMETHODIMP GetProperties(ALLOCATOR_PROPERTIES *p)
    {
        return inner ? inner->GetProperties(p) : E_FAIL;
    }
    STDMETHODIMP Commit() { return inner ? inner->Commit() : E_FAIL; }
    STDMETHODIMP Decommit() { return inner ? inner->Decommit() : E_FAIL; }
    STDMETHODIMP GetBuffer(IMediaSample **s, REFERENCE_TIME *start, REFERENCE_TIME *stop,
                           DWORD flags)
    {
        if (++requests == fail_at) {
            *s = NULL;
            return E_OUTOFMEMORY;
        }
        return inner ? inner->GetBuffer(s, start, stop, flags) : E_FAIL;
    }
    STDMETHODIMP ReleaseBuffer(IMediaSample *s) { return inner ? inner->ReleaseBuffer(s) : E_FAIL; }
};

/** @brief What a sink pin received in one media sample. */
typedef struct {
    long    offset;         /**< where its bytes start in the received stream */
    long    length;         /**< its bytes */
    REFERENCE_TIME start;   /**< its start time, if it has one */
    REFERENCE_TIME stop;    /**< its stop time, if it has one */
    int     timed;          /**< 0 without times, 1 with a start time, 2 with both */
    int     sync;           /**< set if it is a sync point */
    int     preroll;        /**< set if it is a preroll sample */
} received_sample;

/**
 * @brief A sink whose input pin asks for an allocator alignment.
 *
 * The stock File Writer asks for no alignment. So a graph of stock filters
 * never shows how the encoder pads a stream to an alignment. This pin accepts
 * one major type: a byte stream, or audio, which the encoder delivers as one
 * MP3 frame per sample. It asks for @c cbAlign bytes, appends what it receives
 * to one buffer, and keeps the length, the times and the sync point flag of
 * each sample. It lives on the stack of the test that uses it. It does not
 * need reference counting and does not do it.
 */
class AlignedSinkPin : public IPin, public IMemInputPin {
public:
    long    align;          /**< the alignment the pin asks for */
    GUID    major;          /**< the major type the pin accepts */
    IPin   *peer;           /**< the connected output pin */
    IBaseFilter *owner;     /**< the filter the pin reports as its own */
    HANDLE  eos;            /**< signaled by EndOfStream() */
    BYTE   *stream;         /**< everything received, in order */
    long    length;         /**< bytes in #stream */
    long    capacity;       /**< bytes allocated for #stream */
    received_sample *samples; /**< each sample received, in order */
    int     sample_count;   /**< entries in #samples */
    int     sample_capacity; /**< entries allocated for #samples */
    int     deliveries;     /**< number of samples received */
    int     reject_from;    /**< the first sample that Receive() rejects, 0 for none */
    int     rejected;       /**< number of samples rejected */
    FailingAllocator *failing; /**< the allocator the pin hands out, NULL for none */
    int     flushing;       /**< set between BeginFlush() and EndFlush() */
    int     flush_mark;     /**< #sample_count at the last BeginFlush(), -1 before one */
    int     while_flushing; /**< samples that arrived during a flush, and were rejected */
    DWORD   avg_bytes_per_sec; /**< of the connection's WAVEFORMATEX, 0 without one */

    /**
     * @brief Creates a pin that asks for @p a bytes of alignment.
     * @param a the alignment.
     * @param m the major type the pin accepts.
     */
    AlignedSinkPin(long a, const GUID &m = MEDIATYPE_Stream) : align(a), major(m), peer(NULL),
        owner(NULL), stream(NULL), length(0), capacity(0), samples(NULL), sample_count(0),
        sample_capacity(0), deliveries(0), reject_from(0), rejected(0), failing(NULL),
        flushing(0), flush_mark(-1), while_flushing(0), avg_bytes_per_sec(0)
    {
        eos = CreateEvent(NULL, TRUE, FALSE, NULL);
    }
    /** @brief Releases the peer pin, and frees what was received and the event. */
    ~AlignedSinkPin()
    {
        if (peer)
            peer->Release();
        free(stream);
        free(samples);
        CloseHandle(eos);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IPin) {
            *ppv = static_cast<IPin *>(this);
            return S_OK;
        }
        if (riid == IID_IMemInputPin) {
            *ppv = static_cast<IMemInputPin *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    STDMETHODIMP Connect(IPin *, const AM_MEDIA_TYPE *) { return E_UNEXPECTED; }
    STDMETHODIMP ReceiveConnection(IPin *p, const AM_MEDIA_TYPE *mt)
    {
        if (mt == NULL || mt->majortype != major)
            return VFW_E_TYPE_NOT_ACCEPTED;
        if (mt->formattype == FORMAT_WaveFormatEx && mt->cbFormat >= sizeof(WAVEFORMATEX) && mt->pbFormat != NULL)
            avg_bytes_per_sec = ((const WAVEFORMATEX *) mt->pbFormat)->nAvgBytesPerSec;
        peer = p;
        peer->AddRef();
        return S_OK;
    }
    STDMETHODIMP Disconnect()
    {
        if (peer == NULL)
            return S_FALSE;
        peer->Release();
        peer = NULL;
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin **p)
    {
        *p = peer;
        if (peer == NULL)
            return VFW_E_NOT_CONNECTED;
        peer->AddRef();
        return S_OK;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *mt)
    {
        ZeroMemory(mt, sizeof *mt);
        mt->majortype = major;
        return peer ? S_OK : VFW_E_NOT_CONNECTED;
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO *pi)
    {
        pi->pFilter = owner;
        if (owner)
            owner->AddRef();
        pi->dir = PINDIR_INPUT;
        wcscpy(pi->achName, L"In");
        return S_OK;
    }
    STDMETHODIMP QueryDirection(PIN_DIRECTION *d) { *d = PINDIR_INPUT; return S_OK; }
    STDMETHODIMP QueryId(LPWSTR *id)
    {
        *id = (LPWSTR) CoTaskMemAlloc(3 * sizeof(WCHAR));
        if (*id == NULL)
            return E_OUTOFMEMORY;
        wcscpy(*id, L"In");
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *mt)
    {
        return mt->majortype == major ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **) { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin **, ULONG *) { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() { SetEvent(eos); return S_OK; }
    STDMETHODIMP BeginFlush() { flushing = 1; flush_mark = sample_count; return S_OK; }
    STDMETHODIMP EndFlush() { flushing = 0; return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

    STDMETHODIMP GetAllocator(IMemAllocator **a)
    {
        *a = failing;
        return failing ? S_OK : VFW_E_NO_ALLOCATOR;
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator *, BOOL) { return S_OK; }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES *p)
    {
        ZeroMemory(p, sizeof *p);
        p->cBuffers = 1;
        p->cbAlign = align;
        return S_OK;
    }
    STDMETHODIMP Receive(IMediaSample *s)
    {
        BYTE   *p = NULL;
        long    n = s->GetActualDataLength();

        deliveries++;
        if (flushing) {
            /* A pin that is flushing rejects samples */
            while_flushing++;
            return S_FALSE;
        }
        if (reject_from > 0 && deliveries >= reject_from) {
            rejected++;
            return E_FAIL;
        }
        if (FAILED(s->GetPointer(&p)) || n <= 0)
            return S_OK;
        if (length + n > capacity) {
            long    want = (length + n) * 2;
            BYTE   *grown = (BYTE *) realloc(stream, want);

            if (grown == NULL)
                return E_OUTOFMEMORY;
            stream = grown;
            capacity = want;
        }
        if (sample_count == sample_capacity) {
            int     want = sample_capacity ? 2 * sample_capacity : 64;
            received_sample *grown =
                (received_sample *) realloc(samples, want * sizeof(received_sample));

            if (grown == NULL)
                return E_OUTOFMEMORY;
            samples = grown;
            sample_capacity = want;
        }
        {
            received_sample *r = &samples[sample_count++];
            HRESULT t = s->GetTime(&r->start, &r->stop);

            r->offset = length;
            r->length = n;
            r->timed = t == S_OK ? 2 : (t == VFW_S_NO_STOP_TIME ? 1 : 0);
            r->sync = s->IsSyncPoint() == S_OK;
            r->preroll = s->IsPreroll() == S_OK;
        }
        memcpy(stream + length, p, n);
        length += n;
        return S_OK;
    }
    STDMETHODIMP ReceiveMultiple(IMediaSample **s, long n, long *done)
    {
        for (*done = 0; *done < n; ++*done)
            Receive(s[*done]);
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() { return S_FALSE; }
};

/**
 * @brief The filter that owns an #AlignedSinkPin: one pin and a state.
 *
 * The graph manager asks every connected pin for its filter. So the pin
 * needs a filter to be part of a running graph.
 */
class AlignedSinkFilter : public IBaseFilter, public IEnumPins {
public:
    AlignedSinkPin &pin;    /**< the only pin */
    FILTER_STATE state;     /**< as set by Stop(), Pause() and Run() */
    IFilterGraph *graph;    /**< the graph the filter joined */
    IReferenceClock *clock; /**< the clock that the graph set */
    ULONG   next;           /**< the enumeration position */

    /** @brief Creates the filter that owns @p p. @param p the pin. */
    AlignedSinkFilter(AlignedSinkPin &p) : pin(p), state(State_Stopped), graph(NULL),
        clock(NULL), next(0)
    {
        p.owner = this;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter
            || riid == IID_IBaseFilter) {
            *ppv = static_cast<IBaseFilter *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    STDMETHODIMP GetClassID(CLSID *c) { *c = CLSID_NULL; return S_OK; }
    STDMETHODIMP Stop() { state = State_Stopped; return S_OK; }
    STDMETHODIMP Pause() { state = State_Paused; return S_OK; }
    STDMETHODIMP Run(REFERENCE_TIME) { state = State_Running; return S_OK; }
    STDMETHODIMP GetState(DWORD, FILTER_STATE *s) { *s = state; return S_OK; }
    STDMETHODIMP SetSyncSource(IReferenceClock *c) { clock = c; return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock **c)
    {
        *c = clock;
        if (clock)
            clock->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumPins(IEnumPins **e) { next = 0; *e = this; return S_OK; }
    STDMETHODIMP FindPin(LPCWSTR id, IPin **p)
    {
        if (wcscmp(id, L"In") == 0) {
            *p = &pin;
            return S_OK;
        }
        *p = NULL;
        return VFW_E_NOT_FOUND;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO *fi)
    {
        wcscpy(fi->achName, L"Aligned sink");
        fi->pGraph = graph;
        if (graph)
            graph->AddRef();
        return S_OK;
    }
    STDMETHODIMP JoinFilterGraph(IFilterGraph *g, LPCWSTR) { graph = g; return S_OK; }
    STDMETHODIMP QueryVendorInfo(LPWSTR *) { return E_NOTIMPL; }

    STDMETHODIMP Next(ULONG n, IPin **out, ULONG *got)
    {
        ULONG   k = 0;

        if (n > 0 && next == 0) {
            out[0] = &pin;
            k = 1;
            next = 1;
        }
        if (got)
            *got = k;
        return k == n ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG n) { next += n; return next <= 1 ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() { next = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins **) { return E_NOTIMPL; }
};

/**
 * @brief Encodes the test WAV into an #AlignedSinkPin.
 * @param cf    the filter DLL's class factory.
 * @param wav   the input file.
 * @param sink  the pin to deliver to. It asks for its own alignment.
 * @param connected receives whether the encoder accepted the sink.
 * @param timeout_ms  how long to wait for the end of the stream.
 * @param configure  when not NULL, called with the encoder's
 *                   IAudioEncoderProperties2 once its input is connected,
 *                   to change its settings.
 * @param setting    passed on to @p configure.
 * @return Non-zero when the stream ended within the timeout.
 */
static int
encode_into_aligned_sink(IClassFactory *cf, const WCHAR *wav, AlignedSinkPin &sink,
                         int *connected, DWORD timeout_ms = GRAPH_TIMEOUT_MS,
                         void (*configure)(IAudioEncoderProperties2 *, const void *) = NULL,
                         const void *setting = NULL)
{
    IGraphBuilder *graph = NULL;
    IBaseFilter *lame = NULL, *src = NULL;
    IPin   *src_out = NULL, *lame_in = NULL, *lame_out = NULL;
    IMediaControl *mc = NULL;
    AlignedSinkFilter owner(sink);
    DWORD   until;
    int     ended = 0;

    *connected = 0;
    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &lame)))
        return 0;
    if (FAILED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                                IID_IGraphBuilder, (void **) &graph)))
        goto out;
    graph->AddFilter(lame, L"LAME Audio Encoder");
    if (FAILED(graph->AddSourceFilter(wav, L"Source", &src)))
        goto out;
    src_out = find_pin(src, PINDIR_OUTPUT);
    lame_in = find_pin(lame, PINDIR_INPUT);
    lame_out = find_pin(lame, PINDIR_OUTPUT);
    if (src_out == NULL || lame_in == NULL || lame_out == NULL
        || FAILED(graph->Connect(src_out, lame_in)))
        goto out;
    if (configure != NULL) {
        IAudioEncoderProperties2 *props = NULL;

        if (FAILED(lame->QueryInterface(IID_IAudioEncoderProperties2_local, (void **) &props)))
            goto out;
        configure(props, setting);
        props->Release();
    }
    graph->AddFilter(&owner, L"Aligned sink");
    if (FAILED(graph->ConnectDirect(lame_out, &sink, NULL)))
        goto out;
    *connected = 1;
    if (FAILED(graph->QueryInterface(IID_IMediaControl, (void **) &mc)) || FAILED(mc->Run()))
        goto out;
    until = GetTickCount() + timeout_ms;
    while (!ended && (long) (until - GetTickCount()) > 0) {
        MSG     m;
        DWORD   r = MsgWaitForMultipleObjects(1, &sink.eos, FALSE, 100, QS_ALLINPUT);

        if (r == WAIT_OBJECT_0)
            ended = 1;
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    mc->Stop();
out:
    if (lame_out) {
        lame_out->Disconnect();
        lame_out->Release();
    }
    sink.Disconnect();
    if (mc) mc->Release();
    if (lame_in) lame_in->Release();
    if (src_out) src_out->Release();
    if (src) src->Release();
    if (graph) {
        graph->RemoveFilter(&owner);
        graph->Release();
    }
    if (lame) lame->Release();
    return ended;
}

/** @brief A setting of IAudioEncoderProperties2 to encode with. */
typedef struct {
    const char *what;   /**< the setting, for the check names */
    DWORD private_flag; /**< set_PrivateFlag() */
    DWORD reservoir;    /**< set_BitReservoir() */
    DWORD average_kbps; /**< 0 for CBR, else set_Average(TRUE) and this target */
    DWORD kbps;         /**< the CBR bitrate, 0 for 128 kbit/s */
    DWORD strict_iso;   /**< set_StrictISO() */
    int bitrate_mode;   /**< an #encoder_bitrate_mode */
} encoder_setting;

/** @brief How apply_encoder_setting() sets the bitrate mode. */
enum encoder_bitrate_mode {
    SETTING_CBR,        /**< set_Variable(FALSE) and the CBR bitrate */
    SETTING_VBR,        /**< set_Variable(TRUE) */
    SETTING_AS_READ     /**< the mode the filter read from the registry */
};

/**
 * @brief Applies an #encoder_setting to the encoder, for
 *        encode_into_aligned_sink().
 * @param props    the encoder's settings.
 * @param setting  the #encoder_setting.
 */
static void
apply_encoder_setting(IAudioEncoderProperties2 *props, const void *setting)
{
    const encoder_setting *s = (const encoder_setting *) setting;

    if (s->bitrate_mode != SETTING_AS_READ) {
        props->set_Variable(s->bitrate_mode == SETTING_VBR);
        props->set_Bitrate(s->kbps != 0 ? s->kbps : 128);
    }
    props->set_StrictISO(s->strict_iso);
    /* The LAME tag, which records the bitrate method, is written only with this */
    props->set_XingTag(TRUE);
    props->set_PrivateFlag(s->private_flag);
    props->set_BitReservoir(s->reservoir);
    if (s->average_kbps != 0) {
        props->set_AverageBitrate(s->average_kbps);
        props->set_Average(TRUE);
    }
}

/** @brief What scan_encoded() found in a stream. */
typedef struct {
    int frames;         /**< the frames */
    int private_set;    /**< the frames with the private bit set */
    int no_reservoir;   /**< the frames whose main_data_begin is 0 */
    int tag_method;     /**< the VBR method of the LAME tag, or MP3_TAG_ABSENT */
    int tag_abr_kbps;   /**< the ABR bitrate of the LAME tag, or MP3_TAG_ABSENT */
    int most_main_data_begin; /**< the largest main_data_begin, in bytes */
} encoded_scan;

/**
 * @brief Steps through the MPEG-1 frames of a stream, counts the frames with
 *        the private bit and the frames that use no bit reservoir, finds the
 *        largest main_data_begin, and reads the bitrate method and the ABR
 *        bitrate from the LAME tag.
 * @param buf   the stream.
 * @param len   its length in bytes.
 * @param rate  its sample rate in Hz.
 * @return the counts.
 */
static encoded_scan
scan_encoded(const unsigned char *buf, long len, unsigned long rate)
{
    encoded_scan s = { 0, 0, 0, MP3_TAG_ABSENT, MP3_TAG_ABSENT, 0 };
    long off = 0;

    while (off + MP3_HEADER_BYTES + MP3_CRC_BYTES + 2 <= len) {
        const unsigned char *h = buf + off;
        int index, framelen;

        if (!mp3_is_frame_sync(h)) {
            off++;
            continue;
        }
        index = mp3_bitrate_index(h);
        if (index == MP3_BITRATE_FREE_FORMAT || index == MP3_BITRATE_INVALID)
            break;
        framelen = mp3_frame_bytes(index, mp3_padding_bytes(h), rate);
        if (framelen <= 0)
            break;
        if (s.frames == 0 && off + framelen <= len) {
            s.tag_method = mp3_lame_tag_vbr_method(h, framelen);
            s.tag_abr_kbps = mp3_lame_tag_abr_kbps(h, framelen);
        }
        s.frames++;
        if (h[2] & 0x01)
            s.private_set++;
        if (mp3_main_data_begin(h) == 0)
            s.no_reservoir++;
        if (mp3_main_data_begin(h) > s.most_main_data_begin)
            s.most_main_data_begin = mp3_main_data_begin(h);
        off += framelen;
    }
    return s;
}

/**
 * @brief Encodes the test WAV with one #encoder_setting into a byte stream.
 * @param cf       the filter DLL's class factory.
 * @param wav      the input file.
 * @param rate     its sample rate in Hz.
 * @param setting  the setting.
 * @return what scan_encoded() found; no frames when the encode failed.
 */
static encoded_scan
encode_with(IClassFactory *cf, const WCHAR *wav, DWORD rate, const encoder_setting *setting)
{
    AlignedSinkPin bytes(1);
    int connected = 0;
    encoded_scan none = { 0, 0, 0, MP3_TAG_ABSENT, MP3_TAG_ABSENT, 0 };

    if (!encode_into_aligned_sink(cf, wav, bytes, &connected, GRAPH_TIMEOUT_MS,
                                  apply_encoder_setting, setting))
        return none;
    return scan_encoded(bytes.stream, bytes.length, rate);
}

/**
 * @brief Encodes the test WAV through the File Writer, as a program that saves
 *        a file does, with one #encoder_setting, and reads the LAME tag of
 *        the file.
 *
 * Only a sink that can seek gets the LAME tag: the encoder writes it last,
 * into the first frame.
 *
 * @param cf       the filter DLL's class factory.
 * @param wav      the input file.
 * @param path     the output file, as a wide string; it is deleted afterwards.
 * @param rate     the sample rate of the input, in Hz.
 * @param setting  the setting.
 * @return the frames, the bitrate method and the ABR bitrate of the file;
 *         no frames when the encode failed.
 */
static encoded_scan
encode_to_file(IClassFactory *cf, const WCHAR *wav, const WCHAR *path, DWORD rate,
               const encoder_setting *setting)
{
    encoded_scan result = { 0, 0, 0, MP3_TAG_ABSENT, MP3_TAG_ABSENT, 0 };
    IGraphBuilder *graph = NULL;
    IBaseFilter *lame = NULL, *src = NULL, *writer = NULL;
    IFileSinkFilter *sink = NULL;
    IAudioEncoderProperties2 *props = NULL;
    IPin *src_out = NULL, *lame_in = NULL, *lame_out = NULL, *wr_in = NULL;
    IMediaControl *mc = NULL;
    IMediaEvent *me = NULL;
    long ev = 0;
    std::vector<unsigned char> bytes;
    FILE *f;

    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &lame))
        || FAILED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
                                   (void **) &graph))
        || FAILED(graph->AddFilter(lame, L"LAME Audio Encoder"))
        || FAILED(graph->AddSourceFilter(wav, L"Source", &src))
        || (src_out = find_pin(src, PINDIR_OUTPUT)) == NULL
        || (lame_in = find_pin(lame, PINDIR_INPUT)) == NULL
        || FAILED(graph->Connect(src_out, lame_in))
        || FAILED(lame->QueryInterface(IID_IAudioEncoderProperties2_local, (void **) &props)))
        goto out;
    apply_encoder_setting(props, setting);
    if (FAILED(CoCreateInstance(CLSID_FileWriter, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                                (void **) &writer))
        || FAILED(writer->QueryInterface(IID_IFileSinkFilter, (void **) &sink))
        || FAILED(sink->SetFileName(path, NULL))
        || FAILED(graph->AddFilter(writer, L"File Writer"))
        || (lame_out = find_pin(lame, PINDIR_OUTPUT)) == NULL
        || (wr_in = find_pin(writer, PINDIR_INPUT)) == NULL
        || FAILED(graph->Connect(lame_out, wr_in))
        || FAILED(graph->QueryInterface(IID_IMediaControl, (void **) &mc))
        || FAILED(graph->QueryInterface(IID_IMediaEvent, (void **) &me))
        || FAILED(mc->Run()))
        goto out;
    me->WaitForCompletion(GRAPH_TIMEOUT_MS, &ev);
    mc->Stop();
out:
    if (me) me->Release();
    if (mc) mc->Release();
    if (wr_in) wr_in->Release();
    if (lame_out) lame_out->Release();
    if (lame_in) lame_in->Release();
    if (src_out) src_out->Release();
    if (props) props->Release();
    if (sink) sink->Release();
    if (writer) writer->Release();
    if (src) src->Release();
    if (graph) graph->Release();
    if (lame) lame->Release();
    f = _wfopen(path, L"rb");
    if (f != NULL) {
        unsigned char chunk[4096];
        size_t n;

        while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
            bytes.insert(bytes.end(), chunk, chunk + n);
        fclose(f);
        if (!bytes.empty())
            result = scan_encoded(&bytes[0], (long) bytes.size(), rate);
    }
    DeleteFileW(path);
    return result;
}

/**
 * @brief Checks the settings that IAudioEncoderProperties2 adds, in the
 *        encoded stream.
 *
 * - the private bit: set in every frame with it, in none without it;
 * - the bit reservoir: off, every frame holds all of its own data
 *   (main_data_begin 0); on, some frames use bytes of earlier ones;
 * - ABR: the LAME tag of a file records ABR and the target bitrate, for two
 *   targets; the file of the default settings does not record ABR. On the
 *   test tone ABR stays at the VBR minimum, so its bitrates do not show the
 *   target; the tag is only in a file, encode_to_file().
 *
 * @param cf    the filter DLL's class factory.
 * @param wav   the input file.
 * @param out   a file name for the ABR encodes, as a wide string.
 * @param rate  its sample rate in Hz.
 */
static void
test_interface2_in_the_stream(IClassFactory *cf, const WCHAR *wav, const WCHAR *out, DWORD rate)
{
    static const encoder_setting plain = { "default", 0, 1, 0 };
    static const encoder_setting with_private = { "private", 1, 1, 0 };
    static const encoder_setting no_reservoir = { "no reservoir", 0, 0, 0 };
    static const encoder_setting abr_low = { "ABR 96", 0, 1, 96 };
    static const encoder_setting abr_high = { "ABR 192", 0, 1, 192 };
    encoded_scan p, w, n, lo, hi, pf;
    char detail[CTEST_DETAIL_CHARS];
    WCHAR file[MAX_PATH];

    printf("the settings of IAudioEncoderProperties2 in the stream\n");
    p = encode_with(cf, wav, rate, &plain);
    w = encode_with(cf, wav, rate, &with_private);
    n = encode_with(cf, wav, rate, &no_reservoir);
    /* A file of its own: the main graph still holds the one of out. */
    _snwprintf(file, MAX_PATH, L"%s.abr.mp3", out);
    file[MAX_PATH - 1] = L'\0';
    lo = encode_to_file(cf, wav, file, rate, &abr_low);
    hi = encode_to_file(cf, wav, file, rate, &abr_high);
    pf = encode_to_file(cf, wav, file, rate, &plain);
    sprintf(detail, "%d of %d frames with it, %d of %d without", w.private_set, w.frames,
            p.private_set, p.frames);
    ctest_record(w.frames > 0 && p.frames > 0 && w.private_set == w.frames && p.private_set == 0,
                 "the private bit is in every frame with the setting and in none without", detail);
    sprintf(detail, "%d of %d frames use no reservoir with it off, %d of %d with it on",
            n.no_reservoir, n.frames, p.no_reservoir, p.frames);
    ctest_record(n.frames > 0 && n.no_reservoir == n.frames && p.no_reservoir < p.frames,
                 "without the bit reservoir every frame holds all of its own data", detail);
    sprintf(detail, "methods %d, %d and %d (ABR is %d), ABR bitrates %d and %d, frames %d, %d and %d",
            lo.tag_method, hi.tag_method, pf.tag_method, MP3_TAG_METHOD_ABR, lo.tag_abr_kbps,
            hi.tag_abr_kbps, lo.frames, hi.frames, pf.frames);
    ctest_record(lo.tag_method == MP3_TAG_METHOD_ABR && hi.tag_method == MP3_TAG_METHOD_ABR
                 && lo.tag_abr_kbps == 96 && hi.tag_abr_kbps == 192 && pf.tag_method != MP3_TAG_METHOD_ABR,
                 "an ABR stream records ABR and its target in the LAME tag", detail);
}

/** @brief The VBR settings of one announced-bitrate case. */
typedef struct {
    DWORD level;    /**< set_VariableQ() */
    DWORD lowest;   /**< set_VariableMin(), VBR_BITRATE_NO_LIMIT for none */
    DWORD highest;  /**< set_VariableMax(), VBR_BITRATE_NO_LIMIT for none */
} vbr_announce_case;

/**
 * @brief Sets VBR with the level and limits of a #vbr_announce_case, for
 *        encode_into_aligned_sink().
 * @param props    the encoder's settings.
 * @param setting  the #vbr_announce_case.
 */
static void
apply_vbr_announce_case(IAudioEncoderProperties2 *props, const void *setting)
{
    const vbr_announce_case *c = (const vbr_announce_case *) setting;

    props->set_Variable(TRUE);
    props->set_VariableQ(c->level);
    props->set_VariableMin(c->lowest);
    props->set_VariableMax(c->highest);
}

/**
 * @brief Checks the bitrate that the format of a VBR stream announces.
 *
 * It is the typical bitrate of the VBR level at the stream's rate and
 * channels, held within the VBR limits: level 4 without limits, level 0 under
 * a 128 kbit/s maximum, level 9 over a 128 kbit/s minimum.
 *
 * @param cf    the filter DLL's class factory.
 * @param wav   the input file, stereo.
 * @param rate  its sample rate in Hz.
 */
static void
test_vbr_announced_bitrate(IClassFactory *cf, const WCHAR *wav, DWORD rate)
{
    enum { LIMIT_KBPS = 128, CHANNELS = 2, CASES = 3 };
    static const vbr_announce_case cases[CASES] = {
        { 4, VBR_BITRATE_NO_LIMIT, VBR_BITRATE_NO_LIMIT },
        { 0, VBR_BITRATE_NO_LIMIT, LIMIT_KBPS },
        { 9, LIMIT_KBPS, VBR_BITRATE_NO_LIMIT },
    };
    DWORD const want[CASES] = { VbrTypicalBitrate(rate, CHANNELS, 4), LIMIT_KBPS, LIMIT_KBPS };
    DWORD got[CASES];
    char detail[CTEST_DETAIL_CHARS];
    int i, connected;

    printf("the bitrate that the format of a VBR stream announces\n");
    for (i = 0; i < CASES; i++) {
        AlignedSinkPin audio(1, MEDIATYPE_Audio);

        connected = 0;
        (void) encode_into_aligned_sink(cf, wav, audio, &connected, GRAPH_TIMEOUT_MS, apply_vbr_announce_case,
                                        &cases[i]);
        got[i] = audio.avg_bytes_per_sec * 8 / 1000;
    }
    sprintf(detail, "%lu, %lu and %lu kbps; wanted %lu, %lu and %lu", (unsigned long) got[0], (unsigned long) got[1],
            (unsigned long) got[2], (unsigned long) want[0], (unsigned long) want[1], (unsigned long) want[2]);
    ctest_record(want[0] != 0 && got[0] == want[0] && got[1] == want[1] && got[2] == want[2],
                 "a VBR format announces the typical bitrate of its level, within the limits", detail);
}

/**
 * @brief Checks "Strict ISO compliance" in the stream, on noise at 320 kbit/s.
 *
 * With it no frame uses the bit reservoir. Without it the encoder has LAME's
 * own limit, as the lame program does, and main_data_begin reaches past 396
 * bytes: the most that the limit MDB_DEFAULT (lame_set_strict_ISO(0)) leaves
 * at this bitrate, measured.
 *
 * @param cf     the filter DLL's class factory.
 * @param noise  the noise WAV.
 * @param rate   its sample rate in Hz.
 */
static void
test_strict_iso_in_the_stream(IClassFactory *cf, const WCHAR *noise, DWORD rate)
{
    /* The highest MPEG-1 bitrate, where the two limits differ; and the largest
       main_data_begin of MDB_DEFAULT there. */
    enum { HIGHEST_KBPS = 320, MDB_DEFAULT_MOST_BYTES = 396 };
    static const encoder_setting lame_limit = { "LAME's limit", 0, 1, 0, HIGHEST_KBPS, 0 };
    static const encoder_setting strict = { "strict ISO", 0, 1, 0, HIGHEST_KBPS, 1 };
    encoded_scan own, iso;
    char detail[CTEST_DETAIL_CHARS];

    printf("strict ISO compliance in the stream\n");
    own = encode_with(cf, noise, rate, &lame_limit);
    iso = encode_with(cf, noise, rate, &strict);
    sprintf(detail, "main_data_begin up to %d bytes without, %d of %d frames use no reservoir with",
            own.most_main_data_begin, iso.no_reservoir, iso.frames);
    ctest_record(own.frames > 0 && own.most_main_data_begin > MDB_DEFAULT_MOST_BYTES,
                 "without strict ISO compliance the bit reservoir has LAME's own limit", detail);
    ctest_record(iso.frames > 0 && iso.no_reservoir == iso.frames,
                 "with strict ISO compliance no frame uses the bit reservoir at 320 kbps", detail);
}

/**
 * @brief Checks that the filter's VBR is LAME's default VBR mode, from
 *        set_Variable() and from the registry.
 *
 * The LAME tag of a file records the VBR method. A file encoded after
 * set_Variable(TRUE) records LAME's default one. So does a file of a filter
 * that reads VBR from the registry, where a released filter stored its older
 * mode, vbr_rh. The user's own settings key is kept before and put back after.
 *
 * @param cf    the filter DLL's class factory.
 * @param wav   the input file.
 * @param out   a file name for the encodes, as a wide string.
 * @param rate  its sample rate in Hz.
 */
static void
test_vbr_is_lames_default(IClassFactory *cf, const WCHAR *wav, const WCHAR *out, DWORD rate)
{
    /* What a released filter stored in the registry for VBR: vbr_rh. */
    enum { RELEASED_VBR_VALUE = 2 };
    static const encoder_setting vbr = { "VBR", 0, 1, 0, 0, 0, SETTING_VBR };
    static const encoder_setting as_read = { "as read", 0, 1, 0, 0, 0, SETTING_AS_READ };
    static saved_key saved;
    encoded_scan set, read;
    char detail[CTEST_DETAIL_CHARS];
    WCHAR file[MAX_PATH];
    DWORD value = RELEASED_VBR_VALUE;
    HKEY key;

    printf("the filter's VBR is LAME's default VBR mode\n");
    _snwprintf(file, MAX_PATH, L"%s.vbr.mp3", out);
    file[MAX_PATH - 1] = L'\0';
    set = encode_to_file(cf, wav, file, rate, &vbr);
    sprintf(detail, "LAME tag method %d of %d frames, LAME's default is %d", set.tag_method, set.frames,
            MP3_TAG_METHOD_VBR_MTRH);
    ctest_record(set.frames > 0 && set.tag_method == MP3_TAG_METHOD_VBR_MTRH,
                 "set_Variable(TRUE) encodes LAME's default VBR mode", detail);

    if (!save_key(&saved)) {
        CHECK(0, "the settings key can be kept before the test writes it");
        return;
    }
    memset(&read, 0, sizeof read);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, FILTER_SETTINGS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL)
        == ERROR_SUCCESS) {
        LONG const rc = RegSetValueExA(key, "Variable", 0, REG_DWORD, (const BYTE *) &value, sizeof value);

        RegCloseKey(key);
        if (rc == ERROR_SUCCESS)
            read = encode_to_file(cf, wav, file, rate, &as_read);
    }
    CHECK(restore_key(&saved), "the settings key is put back as it was");
    sprintf(detail, "LAME tag method %d of %d frames, LAME's default is %d", read.tag_method, read.frames,
            MP3_TAG_METHOD_VBR_MTRH);
    ctest_record(read.frames > 0 && read.tag_method == MP3_TAG_METHOD_VBR_MTRH,
                 "VBR that a released filter stored is read as LAME's default VBR mode", detail);
}

/**
 * @brief Saves a filter's settings as a filter graph does, into a stream in
 *        memory, and returns its bytes.
 * @param filter  the filter.
 * @param bytes   receives the bytes of the stream.
 * @return 1 on success, else 0.
 */
static int
save_settings(IUnknown *filter, std::vector<BYTE> &bytes)
{
    IPersistStream *persist = NULL;
    IStream *stream = NULL;
    HGLOBAL memory = NULL;
    STATSTG stat;
    int ok = 0;

    if (SUCCEEDED(filter->QueryInterface(IID_IPersistStream, (void **) &persist))
        && SUCCEEDED(CreateStreamOnHGlobal(NULL, FALSE, &stream))
        && SUCCEEDED(persist->Save(stream, TRUE))
        && SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))
        && SUCCEEDED(GetHGlobalFromStream(stream, &memory))) {
        const BYTE *p = (const BYTE *) GlobalLock(memory);

        if (p != NULL) {
            bytes.assign(p, p + (size_t) stat.cbSize.QuadPart);
            GlobalUnlock(memory);
            ok = 1;
        }
    }
    if (stream != NULL)
        stream->Release();
    if (memory != NULL)
        GlobalFree(memory);
    if (persist != NULL)
        persist->Release();
    return ok;
}

/**
 * @brief Loads saved settings into a filter as a filter graph does.
 * @param filter  the filter.
 * @param bytes   the saved stream.
 * @param left    receives the bytes of the stream that the load left unread.
 * @return the result of IPersistStream::Load().
 */
static HRESULT
load_settings(IUnknown *filter, const std::vector<BYTE> &bytes, ULONGLONG *left)
{
    IPersistStream *persist = NULL;
    IStream *stream = NULL;
    HRESULT hr = E_FAIL;
    LARGE_INTEGER zero;
    ULARGE_INTEGER at;

    zero.QuadPart = 0;
    *left = 0;
    if (SUCCEEDED(filter->QueryInterface(IID_IPersistStream, (void **) &persist))
        && SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &stream))
        && SUCCEEDED(stream->Write(&bytes[0], (ULONG) bytes.size(), NULL))
        && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, NULL))) {
        hr = persist->Load(stream);
        if (SUCCEEDED(stream->Seek(zero, STREAM_SEEK_CUR, &at)))
            *left = bytes.size() - at.QuadPart;
    }
    if (stream != NULL)
        stream->Release();
    if (persist != NULL)
        persist->Release();
    return hr;
}

/**
 * @brief Checks the settings in a saved filter graph across the change of
 *        their size.
 *
 * A filter writes its settings after a version: 1 since the settings of
 * IAudioEncoderProperties2, 0 before, with three DWORDs fewer. The test saves
 * a filter with the private bit and the CRC set, and makes a version 0 stream
 * from it by hand, followed by bytes of the next data, as in a saved graph.
 * Loaded into a fresh filter, the version 0 stream sets the CRC, leaves the
 * private bit as it was, and leaves the following bytes unread; the version 1
 * stream sets both.
 *
 * @param cf  the filter DLL's class factory.
 */
static void
test_saved_graphs(IClassFactory *cf)
{
    /* The version that CPersistStream writes first: 12 wide characters. */
    const size_t VERSION_BYTES = 12 * sizeof(WCHAR);
    /* Bytes after the version 0 settings, as another filter's data follows them. */
    const size_t FOLLOWING_BYTES = 3 * sizeof(DWORD);
    IAudioEncoderProperties2 *saver = NULL, *older = NULL, *newer = NULL;
    std::vector<BYTE> saved, version0;
    DWORD full = 0, crc = 0, private_flag = 1;
    ULONGLONG left = 1;
    HRESULT hr;

    printf("the settings in a saved filter graph\n");
    if (FAILED(cf->CreateInstance(NULL, IID_IAudioEncoderProperties2_local, (void **) &saver))
        || FAILED(cf->CreateInstance(NULL, IID_IAudioEncoderProperties2_local, (void **) &older))
        || FAILED(cf->CreateInstance(NULL, IID_IAudioEncoderProperties2_local, (void **) &newer))) {
        CHECK(0, "three filters with IAudioEncoderProperties2");
        goto out;
    }
    saver->set_PrivateFlag(1);
    saver->set_CRCFlag(1);
    saver->get_ParameterBlockSize(NULL, &full);
    CHECK(save_settings(saver, saved) && saved.size() == VERSION_BYTES + full,
          "a filter saves its version and its settings");
    if (saved.size() != VERSION_BYTES + full)
        goto out;
    CHECK(memcmp(&saved[0], L"00000000001 ", VERSION_BYTES) == 0, "the saved settings are version 1");

    version0.assign(saved.begin(), saved.end() - 3 * sizeof(DWORD));
    memcpy(&version0[0], L"00000000000 ", VERSION_BYTES);
    /* What follows in a real stream, which a load that reads too far takes in. */
    version0.insert(version0.end(), FOLLOWING_BYTES, 0xA5);
    older->set_PrivateFlag(0);
    older->set_CRCFlag(0);
    hr = load_settings(older, version0, &left);
    older->get_CRCFlag(&crc);
    older->get_PrivateFlag(&private_flag);
    CHECK(SUCCEEDED(hr) && crc == 1 && private_flag == 0 && left == FOLLOWING_BYTES,
          "a version 0 graph sets the earlier settings, leaves the new ones, and reads its own bytes");

    newer->set_PrivateFlag(0);
    hr = load_settings(newer, saved, &left);
    newer->get_PrivateFlag(&private_flag);
    CHECK(SUCCEEDED(hr) && private_flag == 1 && left == 0, "a version 1 graph sets the new settings too");
out:
    if (saver != NULL)
        saver->Release();
    if (older != NULL)
        older->Release();
    if (newer != NULL)
        newer->Release();
}

/**
 * @brief Checks IAudioEncoderProperties2: each setting reads back what was
 *        written, ABR and VBR exclude each other, and the parameter block
 *        works at its full size and at the size before this interface.
 *
 * The settings are put back afterwards.
 *
 * @param lame  the filter.
 */
static void
test_interface2(IBaseFilter *lame)
{
    IAudioEncoderProperties2 *props = NULL;
    DWORD private_flag = 0, reservoir = 0, average = 0, average_kbps = 0, variable = 0;
    DWORD got_private = 0, got_reservoir = 0, got_average = 0, got_kbps = 0, got_variable = 0;
    DWORD full = 0, after = 0;
    BYTE block[1024], first[1024];
    HRESULT hr;

    printf("IAudioEncoderProperties2\n");
    hr = lame->QueryInterface(IID_IAudioEncoderProperties2_local, (void **) &props);
    REQUIRE_HR(hr, "the filter offers IAudioEncoderProperties2");
    if (FAILED(hr))
        return;
    props->get_PrivateFlag(&private_flag);
    props->get_BitReservoir(&reservoir);
    props->get_Average(&average);
    props->get_AverageBitrate(&average_kbps);
    props->get_Variable(&variable);

    props->set_PrivateFlag(1);
    props->set_BitReservoir(0);
    props->set_AverageBitrate(160);
    props->set_Average(TRUE);
    props->get_PrivateFlag(&got_private);
    props->get_BitReservoir(&got_reservoir);
    props->get_AverageBitrate(&got_kbps);
    props->get_Average(&got_average);
    props->get_Variable(&got_variable);
    CHECK(got_private == 1 && got_reservoir == 0 && got_kbps == 160 && got_average == 1 && got_variable == 0,
          "the new settings read back what was written, and ABR is not VBR");
    props->set_Variable(TRUE);
    props->get_Average(&got_average);
    props->get_Variable(&got_variable);
    CHECK(got_average == 0 && got_variable == 1, "set_Variable() ends ABR");

    /* The parameter block: its size, and a block of the earlier size. */
    REQUIRE_HR(props->get_ParameterBlockSize(NULL, &full), "the parameter block has a size");
    CHECK(full > 3 * sizeof(DWORD) && full <= sizeof block, "the parameter block fits the test's buffer");
    if (full > 3 * sizeof(DWORD) && full <= sizeof block) {
        DWORD const earlier = full - 3 * sizeof(DWORD);
        DWORD size = earlier;

        props->set_PrivateFlag(1);
        hr = props->get_ParameterBlockSize(first, &size);
        CHECK(hr == S_OK && size == earlier, "a program asking for the earlier block size gets that block");
        size = full;
        props->get_ParameterBlockSize(block, &size);
        props->set_PrivateFlag(0);
        CHECK(props->set_ParameterBlockSize(first, earlier) == S_OK,
              "a block of the earlier size is accepted");
        props->get_PrivateFlag(&after);
        CHECK(after == 0, "a block of the earlier size leaves the new settings as they are");
        CHECK(props->set_ParameterBlockSize(block, full) == S_OK, "a block of the full size is accepted");
        props->get_PrivateFlag(&after);
        CHECK(after == 1, "a block of the full size sets the new settings too");
        CHECK(props->set_ParameterBlockSize(block, earlier + 1) == E_FAIL, "a block of another size is refused");
    }

    props->set_PrivateFlag(private_flag);
    props->set_BitReservoir(reservoir);
    props->set_AverageBitrate(average_kbps);
    props->set_Variable(variable);
    props->set_Average(average);
    props->Release();
}

/** @brief The sample rate of the pushed PCM, in Hz. */
#define PUSH_RATE 44100
/** @brief The channels of the pushed PCM. */
#define PUSH_CHANNELS 2
/** @brief The sample frames in each pushed media sample: 100 ms. */
#define PUSH_FRAMES_PER_SAMPLE (PUSH_RATE / 10)
/** @brief The bytes of each pushed media sample. */
#define PUSH_BYTES_PER_SAMPLE (PUSH_FRAMES_PER_SAMPLE * PUSH_CHANNELS * (long) sizeof(short))
/** @brief The stop time of a segment without an end, in 100 ns units. */
#define SEGMENT_END_UNITS ((REFERENCE_TIME) 0x7FFFFFFFFFFFFFFF)
/** @brief The duration of each pushed media sample, in 100 ns units. */
#define PUSH_SAMPLE_UNITS ((REFERENCE_TIME) PUSH_FRAMES_PER_SAMPLE * 10000000 / PUSH_RATE)

/**
 * @brief An output pin that pushes PCM media samples with the times the test
 *        chooses.
 *
 * A file source stamps its samples without gaps. This pin is how the test
 * gives the encoder a gap in the input times. It connects with 16-bit stereo
 * PCM, takes the allocator of the pin it connects to, and pushes from the
 * test's thread. It lives on the stack of the test that uses it. It does not
 * need reference counting and does not do it.
 */
class PushSourcePin : public IPin {
public:
    IPin   *peer;           /**< the connected input pin */
    IMemInputPin *mem;      /**< its transport interface */
    IMemAllocator *alloc;   /**< the allocator it gave */
    IBaseFilter *owner;     /**< the filter the pin reports as its own */
    WAVEFORMATEX wfx;       /**< the format of the pushed PCM */
    AM_MEDIA_TYPE mt;       /**< the media type it connects with */

    /** @brief Creates an unconnected pin for 16-bit stereo PCM at #PUSH_RATE. */
    PushSourcePin() : peer(NULL), mem(NULL), alloc(NULL), owner(NULL)
    {
        ZeroMemory(&wfx, sizeof wfx);
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = PUSH_CHANNELS;
        wfx.nSamplesPerSec = PUSH_RATE;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = PUSH_CHANNELS * sizeof(short);
        wfx.nAvgBytesPerSec = PUSH_RATE * wfx.nBlockAlign;
        ZeroMemory(&mt, sizeof mt);
        mt.majortype = MEDIATYPE_Audio;
        mt.subtype = MEDIASUBTYPE_PCM;
        mt.bFixedSizeSamples = TRUE;
        mt.lSampleSize = wfx.nBlockAlign;
        mt.formattype = FORMAT_WaveFormatEx;
        mt.cbFormat = sizeof wfx;
        mt.pbFormat = (BYTE *) &wfx;
    }
    /** @brief Releases what the connection holds. */
    ~PushSourcePin() { Disconnect(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IPin) {
            *ppv = static_cast<IPin *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    STDMETHODIMP Connect(IPin *r, const AM_MEDIA_TYPE *)
    {
        ALLOCATOR_PROPERTIES want = { 4, PUSH_BYTES_PER_SAMPLE, 1, 0 }, got;
        HRESULT hr = r->ReceiveConnection(this, &mt);

        if (FAILED(hr))
            return hr;
        peer = r;
        peer->AddRef();
        if (FAILED(hr = r->QueryInterface(IID_IMemInputPin, (void **) &mem))
            || FAILED(hr = mem->GetAllocator(&alloc))
            || FAILED(hr = alloc->SetProperties(&want, &got))
            || FAILED(hr = mem->NotifyAllocator(alloc, FALSE))) {
            Disconnect();
            return hr;
        }
        return S_OK;
    }
    STDMETHODIMP ReceiveConnection(IPin *, const AM_MEDIA_TYPE *) { return E_UNEXPECTED; }
    STDMETHODIMP Disconnect()
    {
        if (alloc) {
            alloc->Decommit();
            alloc->Release();
            alloc = NULL;
        }
        if (mem) {
            mem->Release();
            mem = NULL;
        }
        if (peer == NULL)
            return S_FALSE;
        peer->Release();
        peer = NULL;
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin **p)
    {
        *p = peer;
        if (peer == NULL)
            return VFW_E_NOT_CONNECTED;
        peer->AddRef();
        return S_OK;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *m)
    {
        if (peer == NULL)
            return VFW_E_NOT_CONNECTED;
        *m = mt;
        m->pbFormat = (BYTE *) CoTaskMemAlloc(sizeof wfx);
        if (m->pbFormat == NULL)
            return E_OUTOFMEMORY;
        memcpy(m->pbFormat, &wfx, sizeof wfx);
        return S_OK;
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO *pi)
    {
        pi->pFilter = owner;
        if (owner)
            owner->AddRef();
        pi->dir = PINDIR_OUTPUT;
        wcscpy(pi->achName, L"Out");
        return S_OK;
    }
    STDMETHODIMP QueryDirection(PIN_DIRECTION *d) { *d = PINDIR_OUTPUT; return S_OK; }
    STDMETHODIMP QueryId(LPWSTR *id)
    {
        *id = (LPWSTR) CoTaskMemAlloc(4 * sizeof(WCHAR));
        if (*id == NULL)
            return E_OUTOFMEMORY;
        wcscpy(*id, L"Out");
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *m)
    {
        return m->majortype == MEDIATYPE_Audio && m->subtype == MEDIASUBTYPE_PCM ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **) { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin **, ULONG *) { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() { return E_UNEXPECTED; }
    STDMETHODIMP BeginFlush() { return E_UNEXPECTED; }
    STDMETHODIMP EndFlush() { return E_UNEXPECTED; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

    /**
     * @brief Pushes one media sample of the test tone.
     * @param first  the first sample frame of the tone in it.
     * @param start  its start time. Its stop time follows from its length.
     * @return what the connected pin's Receive() returned, or the error of
     *         getting the buffer.
     */
    HRESULT Push(unsigned long first, REFERENCE_TIME start)
    {
        IMediaSample *s = NULL;
        BYTE   *p = NULL;
        REFERENCE_TIME stop = start + PUSH_SAMPLE_UNITS;
        HRESULT hr = alloc->GetBuffer(&s, NULL, NULL, 0);
        short  *pcm;
        long    i;

        if (FAILED(hr))
            return hr;
        if (FAILED(hr = s->GetPointer(&p))) {
            s->Release();
            return hr;
        }
        pcm = (short *) p;
        for (i = 0; i < PUSH_FRAMES_PER_SAMPLE; i++) {
            short v = ctest_tone(first + i, PUSH_RATE, TONE_HZ, TONE_AMPLITUDE);

            pcm[PUSH_CHANNELS * i] = v;
            pcm[PUSH_CHANNELS * i + 1] = v;
        }
        s->SetActualDataLength(PUSH_BYTES_PER_SAMPLE);
        s->SetTime(&start, &stop);
        s->SetSyncPoint(TRUE);
        hr = mem->Receive(s);
        s->Release();
        return hr;
    }
};

/**
 * @brief The filter that owns a #PushSourcePin: one pin and a state. It
 *        commits the pin's allocator when the graph pauses or runs.
 */
class PushSourceFilter : public IBaseFilter, public IEnumPins {
public:
    PushSourcePin &pin;     /**< the only pin */
    FILTER_STATE state;     /**< as set by Stop(), Pause() and Run() */
    IFilterGraph *graph;    /**< the graph the filter joined */
    IReferenceClock *clock; /**< the clock that the graph set */
    ULONG   next;           /**< the enumeration position */

    /** @brief Creates the filter that owns @p p. @param p the pin. */
    PushSourceFilter(PushSourcePin &p) : pin(p), state(State_Stopped), graph(NULL), clock(NULL),
        next(0)
    {
        p.owner = this;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter
            || riid == IID_IBaseFilter) {
            *ppv = static_cast<IBaseFilter *>(this);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    STDMETHODIMP GetClassID(CLSID *c) { *c = CLSID_NULL; return S_OK; }
    STDMETHODIMP Stop()
    {
        if (pin.alloc)
            pin.alloc->Decommit();
        state = State_Stopped;
        return S_OK;
    }
    STDMETHODIMP Pause()
    {
        if (pin.alloc)
            pin.alloc->Commit();
        state = State_Paused;
        return S_OK;
    }
    STDMETHODIMP Run(REFERENCE_TIME)
    {
        if (pin.alloc)
            pin.alloc->Commit();
        state = State_Running;
        return S_OK;
    }
    STDMETHODIMP GetState(DWORD, FILTER_STATE *s) { *s = state; return S_OK; }
    STDMETHODIMP SetSyncSource(IReferenceClock *c) { clock = c; return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock **c)
    {
        *c = clock;
        if (clock)
            clock->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumPins(IEnumPins **e) { next = 0; *e = this; return S_OK; }
    STDMETHODIMP FindPin(LPCWSTR id, IPin **p)
    {
        if (wcscmp(id, L"Out") == 0) {
            *p = &pin;
            return S_OK;
        }
        *p = NULL;
        return VFW_E_NOT_FOUND;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO *fi)
    {
        wcscpy(fi->achName, L"Push source");
        fi->pGraph = graph;
        if (graph)
            graph->AddRef();
        return S_OK;
    }
    STDMETHODIMP JoinFilterGraph(IFilterGraph *g, LPCWSTR) { graph = g; return S_OK; }
    STDMETHODIMP QueryVendorInfo(LPWSTR *) { return E_NOTIMPL; }

    STDMETHODIMP Next(ULONG n, IPin **out, ULONG *got)
    {
        ULONG   k = 0;

        if (n > 0 && next == 0) {
            out[0] = &pin;
            k = 1;
            next = 1;
        }
        if (got)
            *got = k;
        return k == n ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG n) { next += n; return next <= 1 ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() { next = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins **) { return E_NOTIMPL; }
};

/**
 * @brief Encodes pushed PCM into an #AlignedSinkPin.
 *
 * Pushes @p count media samples of 100 ms of the test tone. Their times follow
 * each other without a gap, except that the times from sample @p gap_at on are
 * @p gap_units later. With @p flush_at, the encoder is flushed before that
 * sample, as for a seek, and the times and the tone start again at 0.
 *
 * @param cf         the filter DLL's class factory.
 * @param sink       the pin to deliver to.
 * @param count      the media samples to push.
 * @param gap_at     the first sample after the gap. 0 for no gap.
 * @param gap_units  the gap, in 100 ns units.
 * @param connected  receives whether both connections were made.
 * @param flush_at   the first sample after the flush. 0 for no flush.
 * @return Non-zero when every sample was taken and the stream ended within
 *         the timeout.
 */
static int
encode_pushed(IClassFactory *cf, AlignedSinkPin &sink, int count, int gap_at,
              REFERENCE_TIME gap_units, int *connected, int flush_at = 0)
{
    IGraphBuilder *graph = NULL;
    IBaseFilter *lame = NULL;
    IPin   *lame_in = NULL, *lame_out = NULL;
    IMediaControl *mc = NULL;
    PushSourcePin src;
    PushSourceFilter src_owner(src);
    AlignedSinkFilter sink_owner(sink);
    DWORD   until;
    int     ended = 0, i, pushed = 1;

    *connected = 0;
    if (FAILED(cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &lame)))
        return 0;
    if (FAILED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                                IID_IGraphBuilder, (void **) &graph)))
        goto out;
    graph->AddFilter(&src_owner, L"Push source");
    graph->AddFilter(lame, L"LAME Audio Encoder");
    graph->AddFilter(&sink_owner, L"Aligned sink");
    lame_in = find_pin(lame, PINDIR_INPUT);
    lame_out = find_pin(lame, PINDIR_OUTPUT);
    if (lame_in == NULL || lame_out == NULL
        || FAILED(graph->ConnectDirect(&src, lame_in, &src.mt))
        || FAILED(graph->ConnectDirect(lame_out, &sink, NULL)))
        goto out;
    *connected = 1;
    if (FAILED(graph->QueryInterface(IID_IMediaControl, (void **) &mc)) || FAILED(mc->Run()))
        goto out;
    for (i = 0; i < count && pushed; i++) {
        int const n = flush_at > 0 && i >= flush_at ? i - flush_at : i;
        REFERENCE_TIME start = n * PUSH_SAMPLE_UNITS + (gap_at > 0 && i >= gap_at ? gap_units : 0);

        if (flush_at > 0 && i == flush_at) {
            lame_in->BeginFlush();
            lame_in->EndFlush();
            lame_in->NewSegment(0, SEGMENT_END_UNITS, 1.0);
        }
        pushed = src.Push((unsigned long) n * PUSH_FRAMES_PER_SAMPLE, start) == S_OK;
    }
    lame_in->EndOfStream();
    until = GetTickCount() + GRAPH_TIMEOUT_MS;
    while (!ended && (long) (until - GetTickCount()) > 0) {
        MSG     m;
        DWORD   r = MsgWaitForMultipleObjects(1, &sink.eos, FALSE, 100, QS_ALLINPUT);

        if (r == WAIT_OBJECT_0)
            ended = 1;
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    mc->Stop();
out:
    if (lame_out) {
        lame_out->Disconnect();
        lame_out->Release();
    }
    if (lame_in) {
        lame_in->Disconnect();
        lame_in->Release();
    }
    sink.Disconnect();
    src.Disconnect();
    if (mc) mc->Release();
    if (graph) {
        graph->RemoveFilter(&sink_owner);
        graph->RemoveFilter(&src_owner);
        graph->Release();
    }
    if (lame) lame->Release();
    return ended && pushed;
}

/**
 * @brief Checks that a sink that asks for an alignment gets the stream,
 *        padded with zeros.
 *
 * The stream encoded without alignment is the reference. With an alignment,
 * the encoder rounds the last block up. The bytes after the reference must be
 * zero. They must not be old data from the reused sample buffer. An alignment
 * larger than the encoder buffer can never fill a block. The encoder must
 * reject it at connection, so that the stream does not end up empty.
 *
 * @param cf   the filter DLL's class factory.
 * @param wav  the input file.
 */
static void
test_aligned_stream_end(IClassFactory *cf, const WCHAR *wav)
{
    static const long aligns[] = { 512, 4096, 8192 };
    AlignedSinkPin ref(1), big(12288);
    char    detail[CTEST_DETAIL_CHARS];
    int     connected, i;
    long    k;

    CHECK(encode_into_aligned_sink(cf, wav, ref, &connected) && ref.length > 0,
          "the encoder streams into a sink without alignment");
    if (ref.length <= 0)
        return;
    for (i = 0; i < (int) (sizeof aligns / sizeof aligns[0]); i++) {
        AlignedSinkPin s(aligns[i]);
        long    zero = 0, tail;
        char    what[96];

        encode_into_aligned_sink(cf, wav, s, &connected);
        tail = s.length - ref.length;
        for (k = ref.length; k < s.length; k++)
            if (s.stream[k] == 0)
                zero++;
        sprintf(detail, "%ld bytes, reference %ld, %ld of %ld after it zero",
                s.length, ref.length, zero, tail > 0 ? tail : 0);
        sprintf(what, "an alignment of %ld delivers the whole stream", aligns[i]);
        ctest_record(s.length >= ref.length && memcmp(s.stream, ref.stream, ref.length) == 0,
                     what, detail);
        sprintf(what, "an alignment of %ld pads the stream's end with zeros", aligns[i]);
        ctest_record(tail >= 0 && zero == tail, what, detail);
    }
    encode_into_aligned_sink(cf, wav, big, &connected);
    sprintf(detail, "connected %d, %ld bytes", connected, big.length);
    ctest_record(!connected, "an alignment the encoder cannot fill is refused at connection",
                 detail);
}

/** @brief How long test_rejected_delivery() waits for the end of a stream that upstream stopped. */
#define REJECTED_TIMEOUT_MS 3000

/**
 * @brief Checks that the encoder stops delivering when the downstream filter
 *        rejects a sample.
 *
 * The sink rejects its second sample with E_FAIL. The encoder returns the
 * failure from Receive(), so the source stops sending. At most the flush at
 * the end of the stream reaches the sink after the rejected sample. The same
 * encode where the sink takes every sample is the control: it delivers more
 * than three samples, so an encoder that goes on would have more than two
 * rejected.
 *
 * @param cf   the filter DLL's class factory.
 * @param wav  the input file.
 */
static void
test_rejected_delivery(IClassFactory *cf, const WCHAR *wav)
{
    AlignedSinkPin all(1), rejecting(1);
    char    detail[CTEST_DETAIL_CHARS];
    int     connected;

    encode_into_aligned_sink(cf, wav, all, &connected);
    rejecting.reject_from = 2;
    encode_into_aligned_sink(cf, wav, rejecting, &connected, REJECTED_TIMEOUT_MS);
    sprintf(detail, "%d samples to a sink that takes all; %d to one that rejects, %d rejected",
            all.deliveries, rejecting.deliveries, rejecting.rejected);
    ctest_record(all.deliveries > rejecting.reject_from + 2,
                 "the control encode delivers more than three samples", detail);
    ctest_record(connected && rejecting.rejected >= 1 && rejecting.rejected <= 2,
                 "after a rejected sample the encoder stops delivering", detail);
}

/**
 * @brief Checks that a flush discards what the encoder holds and that the
 *        data after it is encoded as by a new encoder.
 *
 * One second of pushed PCM, a flush as for a seek, and one second more whose
 * times and tone start again at 0. The same one second alone is the
 * reference. In frame output:
 * - nothing reaches the sink while it is flushing;
 * - the frames after the flush are the frames of the reference.
 * In byte stream output, nothing reaches the sink while it is flushing either.
 *
 * @param cf  the filter DLL's class factory.
 */
static void
test_flush_restarts_the_encoder(IClassFactory *cf)
{
    const int half = 10;
    AlignedSinkPin reference(1, MEDIATYPE_Audio), flushed(1, MEDIATYPE_Audio), bytes(1);
    char    detail[CTEST_DETAIL_CHARS];
    int     connected = 0, ok;
    long    after = -1;

    ok = encode_pushed(cf, reference, half, 0, 0, &connected);
    ctest_record(ok && reference.sample_count > 0, "one second of pushed PCM is encoded alone", NULL);
    ok = encode_pushed(cf, flushed, 2 * half, 0, 0, &connected, half);
    sprintf(detail, "%d frames before the flush, %d after; %d refused while flushing",
            flushed.flush_mark, flushed.sample_count - flushed.flush_mark, flushed.while_flushing);
    ctest_record(ok && flushed.flush_mark > 0, "a flush in the middle of the encode is passed downstream",
                 detail);
    ctest_record(flushed.while_flushing == 0, "nothing reaches the sink while it is flushing", detail);
    if (flushed.flush_mark >= 0 && flushed.flush_mark < flushed.sample_count)
        after = flushed.samples[flushed.flush_mark].offset;
    sprintf(detail, "%ld bytes after the flush, %ld encoded alone",
            after >= 0 ? flushed.length - after : -1L, reference.length);
    ctest_record(after >= 0 && flushed.length - after == reference.length
                 && memcmp(flushed.stream + after, reference.stream, reference.length) == 0,
                 "after a flush the frames are those of a new encoder", detail);

    ok = encode_pushed(cf, bytes, 2 * half, 0, 0, &connected, half);
    sprintf(detail, "%d refused while flushing", bytes.while_flushing);
    ctest_record(ok && bytes.flush_mark > 0 && bytes.while_flushing == 0,
                 "in byte stream output too, nothing reaches the sink while it is flushing", detail);
}

/**
 * @brief Checks that the encoder stops when it gets no buffer for its output.
 *
 * The sink hands out an allocator whose second request for a buffer fails.
 * The encoder returns the failure from Receive(), so the source stops
 * sending, in byte stream and in frame output. At most the flush at the end of
 * the stream reaches the sink after the failure. The same encode with the
 * system allocator is the control: it delivers more than three samples, so an
 * encoder that goes on without the lost buffer delivers more than two.
 *
 * @param cf   the filter DLL's class factory.
 * @param wav  the input file.
 */
static void
test_failed_output_buffer(IClassFactory *cf, const WCHAR *wav)
{
    static const GUID *const majors[] = { &MEDIATYPE_Stream, &MEDIATYPE_Audio };
    static const char *const outputs[] = { "byte stream", "frame" };
    char    detail[CTEST_DETAIL_CHARS], what[96];
    int     connected, i;

    for (i = 0; i < 2; i++) {
        AlignedSinkPin all(1, *majors[i]), failing(1, *majors[i]);
        FailingAllocator alloc(2);

        failing.failing = &alloc;
        encode_into_aligned_sink(cf, wav, all, &connected);
        encode_into_aligned_sink(cf, wav, failing, &connected, REJECTED_TIMEOUT_MS);
        sprintf(detail, "%d samples with the system allocator; %d after request %d of %d failed",
                all.deliveries, failing.deliveries, alloc.fail_at, alloc.requests);
        sprintf(what, "the %s output delivers more than three samples with the system allocator",
                outputs[i]);
        ctest_record(all.deliveries > 3, what, detail);
        sprintf(what, "after a failed output buffer the %s output stops delivering", outputs[i]);
        ctest_record(connected && alloc.requests >= alloc.fail_at && failing.deliveries <= 2, what,
                     detail);
    }
}

/**
 * @brief Checks the output for a downstream pin that takes audio, as a muxer
 *        or a player does: one MP3 frame per media sample, stamped with its
 *        time.
 *
 * - Each sample holds one whole frame: it starts with a frame header, and its
 *   length is the length that the header gives.
 * - Each sample is a sync point.
 * - The first frame is a preroll sample, which ends at 0; no other is one.
 *   The frames after it start at 0, each a frame duration after the one
 *   before.
 * - The frames joined are the bytes that a byte stream sink gets: every frame
 *   is delivered, the last one too.
 *
 * @param cf    the filter DLL's class factory.
 * @param wav   the input file.
 * @param rate  its sample rate, in Hz, which the encoder keeps.
 */
static void
test_frame_mode(IClassFactory *cf, const WCHAR *wav, DWORD rate)
{
    AlignedSinkPin bytes(1), frames(1, MEDIATYPE_Audio);
    REFERENCE_TIME const frame_units = MulDiv(10000000, MP3_SAMPLES_PER_FRAME, (int) rate);
    char    detail[CTEST_DETAIL_CHARS];
    int     connected = 0, ended, i, whole = 0, sync = 0, on_time = 0, preroll = 0;

    encode_into_aligned_sink(cf, wav, bytes, &connected);
    ended = encode_into_aligned_sink(cf, wav, frames, &connected);
    sprintf(detail, "connected %d, ended %d, %d samples", connected, ended, frames.sample_count);
    ctest_record(connected && ended && frames.sample_count > 0,
                 "the encoder delivers to a sink that takes audio", detail);
    for (i = 0; i < frames.sample_count; i++) {
        const received_sample *r = &frames.samples[i];
        const unsigned char *h = frames.stream + r->offset;

        if (r->length >= MP3_HEADER_BYTES && mp3_is_frame_sync(h)
            && r->length == mp3_frame_bytes(mp3_bitrate_index(h), mp3_padding_bytes(h), rate))
            whole++;
        if (r->sync)
            sync++;
        if (r->preroll == (i == 0))
            preroll++;
        if (r->timed > 0 && r->start == (i - 1) * frame_units)
            on_time++;
    }
    sprintf(detail, "%d of %d samples", whole, frames.sample_count);
    ctest_record(whole == frames.sample_count, "each sample is one whole MP3 frame", detail);
    sprintf(detail, "%d of %d samples", sync, frames.sample_count);
    ctest_record(sync == frames.sample_count, "each sample is a sync point", detail);
    sprintf(detail, "%d of %d samples as expected", preroll, frames.sample_count);
    ctest_record(frames.sample_count > 0 && preroll == frames.sample_count,
                 "the first frame is a preroll sample, and no other is", detail);
    sprintf(detail, "%d of %d samples at (n - 1) x %ld units", on_time, frames.sample_count,
            (long) frame_units);
    ctest_record(on_time == frames.sample_count,
                 "the preroll frame ends at 0, and the frames after it start one frame duration apart",
                 detail);
    sprintf(detail, "%ld bytes of frames, %ld of the byte stream", frames.length, bytes.length);
    ctest_record(bytes.length > 0 && frames.length == bytes.length
                 && memcmp(frames.stream, bytes.stream, bytes.length) == 0,
                 "the frames joined are the byte stream, its last frame included", detail);
}

/**
 * @brief Checks that a gap in the input times moves the times of the frames
 *        after it.
 *
 * Two seconds of pushed PCM, once with times that follow each other and once
 * with a gap of half a second before the second half. Without the gap, each
 * frame starts one frame duration after the one before, the preroll frame
 * first, which ends at 0. With it, the step
 * between two frames is longer by the gap once, at the first frame that
 * starts at or after the input sample after the gap, and one frame duration
 * everywhere else.
 *
 * @param cf  the filter DLL's class factory.
 */
static void
test_frame_times_follow_a_gap(IClassFactory *cf)
{
    const int count = 20, gap_at = 10;
    const REFERENCE_TIME gap = 5000000;
    REFERENCE_TIME const frame_units = MulDiv(10000000, MP3_SAMPLES_PER_FRAME, PUSH_RATE);
    /* In whole frames of output samples, as the filter counts them, after
       the preroll frame */
    const int gap_frame = 1 + (gap_at * PUSH_FRAMES_PER_SAMPLE + MP3_SAMPLES_PER_FRAME - 1)
        / MP3_SAMPLES_PER_FRAME;
    AlignedSinkPin plain(1, MEDIATYPE_Audio), gapped(1, MEDIATYPE_Audio);
    char    detail[CTEST_DETAIL_CHARS];
    int     connected = 0, ok, i, steps = 0, jumps = 0, jump_at = -1, others = 0;

    ok = encode_pushed(cf, plain, count, 0, 0, &connected);
    sprintf(detail, "connected %d, %d frames", connected, plain.sample_count);
    ctest_record(ok && plain.sample_count > 2, "two seconds of pushed PCM are encoded into frames",
                 detail);
    for (i = 1; i < plain.sample_count; i++)
        if (plain.samples[i].timed > 0 && plain.samples[i].start - plain.samples[i - 1].start == frame_units)
            steps++;
    sprintf(detail, "%d of %d steps are %ld units", steps, plain.sample_count - 1, (long) frame_units);
    ctest_record(steps == plain.sample_count - 1 && plain.samples[0].preroll
                 && plain.samples[1].start == 0,
                 "without a gap, the frames after the preroll frame start at 0, one frame duration apart",
                 detail);

    ok = encode_pushed(cf, gapped, count, gap_at, gap, &connected);
    ctest_record(ok && gapped.sample_count == plain.sample_count,
                 "the same PCM with a gap in its times gives as many frames", detail);
    for (i = 1; i < gapped.sample_count; i++) {
        REFERENCE_TIME step = gapped.samples[i].start - gapped.samples[i - 1].start;

        if (step == frame_units + gap) {
            jumps++;
            jump_at = i;
        } else if (step != frame_units) {
            others++;
        }
    }
    sprintf(detail, "%d step(s) longer by the gap, at frame %d of %d; %d other step(s)", jumps,
            jump_at, gapped.sample_count, others);
    ctest_record(jumps == 1 && others == 0, "a gap in the input moves the frame times on by the gap, once",
                 detail);
    sprintf(detail, "at frame %d, the first frame from the gap on is %d", jump_at, gap_frame);
    ctest_record(jump_at == gap_frame, "the frame times move at the first frame from the gap on",
                 detail);
}

/*
 * The CLSIDs of the filter's three property pages and the resource IDs of
 * their dialogs, spelled locally for the same reason as the filter's CLSID
 * above. The tabs of a property frame come in the order of the CLSIDs.
 */
static const GUID CLSID_page_local[] = {
    { 0xb8d27089, 0xff5f, 0x4b7c, { 0x98, 0xdc, 0x0e, 0x91, 0xa1, 0x69, 0x62, 0x86 } },
    { 0xb8d2708a, 0xff5f, 0x4b7c, { 0x98, 0xdc, 0x0e, 0x91, 0xa1, 0x69, 0x62, 0x86 } },
    { 0xfe69edd1, 0xf4cb, 0x11d5, { 0x99, 0x4a, 0x00, 0x00, 0x21, 0xd1, 0xfe, 0x2f } },
};
/** @brief The number of property pages of the filter. */
enum { PAGES = sizeof CLSID_page_local / sizeof CLSID_page_local[0] };
static const WORD page_dialog[PAGES] = { 100, 102, 105 };
static const char *const page_name[PAGES] = { "the main page", "the Advanced page", "the About page" };
/** @brief The controls of each page with an access key. */
static const size_t page_access_keys[PAGES] = { 16, 11, 0 };
/** @brief The encoding-quality slider of the main page and the text beside it. */
enum { SLIDER_QUALITY_ID = 1021, TEXT_QUALITY_ID = 1023 };
/** @brief The VBR quality box of the main page, and its VBR minimum and maximum. */
enum { COMBO_VBR_QUALITY_ID = 1026, COMBO_VBR_MIN_ID = 1013, COMBO_VBR_MAX_ID = 1024 };
/** @brief The sample rate box of the main page. */
enum { COMBO_SAMPLE_RATE_ID = 1025 };
/** @brief The title and the licence box of the About page. */
enum { ABOUT_TITLE_ID = 1046, ABOUT_LICENSE_ID = 1044, ABOUT_URL_ID = 1047, ABOUT_CREDITS_ID = 1100,
       ABOUT_ICON_ID = 1101 };

/** @brief What inspect_property_frame() found, for test_property_pages(). */
static struct {
    int frame_found;
    int pages_seen;
    int fonts_match;
    int quality_range_ok;
    int quality_texts_fit;
    int vbr_levels_listed;
    int vbr_no_limit_shown;
    int same_rate_shown;
    int about_ok;
} frame_result;

/**
 * @brief Finds the property frame among the windows of this thread: a visible
 *        dialog with a tab control.
 * @param window  a top-level window of this thread.
 * @param found   the HWND that receives the frame.
 * @return FALSE once the frame is found, to stop the enumeration.
 */
static BOOL CALLBACK
find_property_frame(HWND window, LPARAM found)
{
    char name[16];

    if (IsWindowVisible(window) && GetClassNameA(window, name, sizeof name) > 0
        && strcmp(name, "#32770") == 0
        && FindWindowExA(window, NULL, "SysTabControl32", NULL) != NULL) {
        *(HWND *) found = window;
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Finds the visible page among the descendants of the frame.
 *
 * The frame puts each page into a dialog of its own, which has no controls
 * of its own. The page is the visible dialog inside that one.
 *
 * @param window  a descendant window of the frame.
 * @param found   the HWND that receives the page.
 * @return FALSE once the page is found, to stop the enumeration.
 */
static BOOL CALLBACK
find_visible_page(HWND window, LPARAM found)
{
    char name[16], parent[16];

    if (IsWindowVisible(window) && GetClassNameA(window, name, sizeof name) > 0
        && strcmp(name, "#32770") == 0
        && GetClassNameA(GetParent(window), parent, sizeof parent) > 0
        && strcmp(parent, "#32770") == 0 && GetParent(window) != GetAncestor(window, GA_ROOT)) {
        *(HWND *) found = window;
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Checks each page of the property frame of this thread, then closes
 *        the frame.
 *
 * A timer calls it from the message loop of the modal frame. Once it finds the
 * frame, it stops the timer, selects each tab, and checks the page that shows:
 * its font is the font of the desktop (the typeface, and the height to the
 * nearest point), and check_dialog_layout() passes. On the main page it notes
 * the range of the encoding-quality slider, whether the text of each level
 * fits beside it, the levels that the VBR quality box lists, and what the
 * VBR minimum and maximum show; on the
 * About page, the title and the licence notice.
 *
 * @param window   NULL, the timer has no window.
 * @param message  WM_TIMER.
 * @param id       the timer.
 * @param time     the tick count.
 */
static void CALLBACK
inspect_property_frame(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    HWND frame = NULL, tabs;
    LOGFONTW desktop;
    int p;

    EnumThreadWindows(GetCurrentThreadId(), find_property_frame, (LPARAM) &frame);
    if (frame == NULL)
        return;
    KillTimer(NULL, id);
    frame_result.frame_found = 1;
    tabs = FindWindowExA(frame, NULL, "SysTabControl32", NULL);
    if (!desktop_dialog_font(&desktop))
        memset(&desktop, 0, sizeof desktop);
    for (p = 0; p < PAGES; p++) {
        HWND page = NULL;
        HFONT font;
        LOGFONTW used;

        if (p > 0)
            SendMessageA(tabs, TCM_SETCURFOCUS, (WPARAM) p, 0);
        EnumChildWindows(frame, find_visible_page, (LPARAM) &page);
        if (page == NULL)
            continue;
        frame_result.pages_seen++;
        font = (HFONT) SendMessageW(page, WM_GETFONT, 0, 0);
        memset(&used, 0, sizeof used);
        if (font != NULL && GetObjectW(font, sizeof used, &used) != 0
            && wcsncmp(used.lfFaceName, desktop.lfFaceName, LF_FACESIZE) == 0
            && used.lfHeight - desktop.lfHeight >= -1 && used.lfHeight - desktop.lfHeight <= 1) {
            frame_result.fonts_match++;
        }
        printf("        %s: \"%ls\" %ld, desktop \"%ls\" %ld\n", page_name[p], used.lfFaceName,
               (long) used.lfHeight, desktop.lfFaceName, (long) desktop.lfHeight);
        check_dialog_layout(page, page_access_keys[p], page_name[p]);
        if (p == 2) {
            char title[CONTROL_TEXT_CHARS], license[2048], credits[CONTROL_TEXT_CHARS], url[CONTROL_TEXT_CHARS];
            char icon[CONTROL_TEXT_CHARS];

            title[0] = license[0] = credits[0] = url[0] = icon[0] = '\0';
            GetDlgItemTextA(page, ABOUT_TITLE_ID, title, sizeof title);
            GetDlgItemTextA(page, ABOUT_LICENSE_ID, license, sizeof license);
            GetDlgItemTextA(page, ABOUT_CREDITS_ID, credits, sizeof credits);
            GetDlgItemTextA(page, ABOUT_URL_ID, url, sizeof url);
            GetDlgItemTextA(page, ABOUT_ICON_ID, icon, sizeof icon);
            frame_result.about_ok = strncmp(title, "LAME Audio Encoder v", strlen("LAME Audio Encoder v")) == 0
                && strcmp(license, LICENSE_NOTICE) == 0 && strcmp(credits, ABOUT_CREDITS) == 0
                && strcmp(url, LAME_URL) == 0 && strcmp(icon, ABOUT_ICON_CREDIT) == 0;
            printf("        \"%s\"\n", title);
        }
        if (p == 0) {
            HWND const slider = GetDlgItem(page, SLIDER_QUALITY_ID);
            HWND const text = GetDlgItem(page, TEXT_QUALITY_ID);
            WCHAR shown[CONTROL_TEXT_CHARS];
            unsigned int level;

            frame_result.quality_range_ok = slider != NULL
                && SendMessageA(slider, TBM_GETRANGEMIN, 0, 0) == 0
                && SendMessageA(slider, TBM_GETRANGEMAX, 0, 0) == (LRESULT) (ENCODING_QUALITY_LEVELS - 1);
            if (text != NULL) {
                GetWindowTextW(text, shown, CONTROL_TEXT_CHARS);
                for (level = 0; level < ENCODING_QUALITY_LEVELS; level++) {
                    SetWindowTextA(text, EncodingQualityText(level));
                    frame_result.quality_texts_fit += static_text_fits(text);
                }
                SetWindowTextW(text, shown);
            }
            {
                HWND const vbr = GetDlgItem(page, COMBO_VBR_QUALITY_ID);
                char item[CONTROL_TEXT_CHARS], want[CONTROL_TEXT_CHARS];

                for (level = 0; vbr != NULL && level < VBR_QUALITY_LEVELS; level++) {
                    item[0] = '\0';
                    if (SendMessageA(vbr, CB_GETLBTEXTLEN, level, 0) < CONTROL_TEXT_CHARS)
                        SendMessageA(vbr, CB_GETLBTEXT, level, (LPARAM) item);
                    VbrQualityText(level, want, sizeof want);
                    if (strcmp(item, want) == 0)
                        frame_result.vbr_levels_listed++;
                }
                if (vbr != NULL && SendMessageA(vbr, CB_GETCOUNT, 0, 0) != (LRESULT) VBR_QUALITY_LEVELS)
                    frame_result.vbr_levels_listed = 0;
            }
            {
                char lowest[CONTROL_TEXT_CHARS], highest[CONTROL_TEXT_CHARS];

                lowest[0] = highest[0] = '\0';
                GetDlgItemTextA(page, COMBO_VBR_MIN_ID, lowest, sizeof lowest);
                GetDlgItemTextA(page, COMBO_VBR_MAX_ID, highest, sizeof highest);
                frame_result.vbr_no_limit_shown = strcmp(lowest, "No limit") == 0 && strcmp(highest, "No limit") == 0;
            }
            {
                char rate[CONTROL_TEXT_CHARS];

                rate[0] = '\0';
                GetDlgItemTextA(page, COMBO_SAMPLE_RATE_ID, rate, sizeof rate);
                frame_result.same_rate_shown = strcmp(rate, "Same as input") == 0;
            }
        }
    }
    PostMessageA(frame, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
}

/**
 * @brief Opens the filter's property pages in a real property frame, as a
 *        program that shows the settings of a filter does.
 *
 * OleCreatePropertyFrame() creates the pages from their CLSIDs. The test
 * registers the class factories of the pages for this process only
 * (CoRegisterClassObject()), so nothing goes into the registry. A timer
 * checks every page (inspect_property_frame()) and closes the frame.
 *
 * @param get_class  DllGetClassObject() of the filter.
 * @param cf         the class factory of the filter.
 */
static void
test_property_pages(PFN_DllGetClassObject get_class, IClassFactory *cf)
{
    /* Often enough that the frame closes soon after it opens. */
    const UINT TIMER_MS = 200;
    DWORD cookies[PAGES] = { 0 };
    CLSID pages[PAGES];
    IUnknown *filter = NULL;
    UINT_PTR timer;
    HRESULT hr;
    int i, registered = 0;

    printf("the property pages of the filter\n");
    for (i = 0; i < PAGES; i++) {
        IUnknown *factory = NULL;

        pages[i] = CLSID_page_local[i];
        hr = get_class(CLSID_page_local[i], IID_IClassFactory, (void **) &factory);
        if (SUCCEEDED(hr)) {
            hr = CoRegisterClassObject(CLSID_page_local[i], factory, CLSCTX_INPROC_SERVER,
                                       REGCLS_MULTIPLEUSE, &cookies[i]);
            factory->Release();
        }
        if (SUCCEEDED(hr))
            registered++;
    }
    CHECK_EQ_U(registered, PAGES, "the class factory of every page registers for this process");
    hr = cf->CreateInstance(NULL, IID_IUnknown, (void **) &filter);
    REQUIRE_HR(hr, "a filter instance for the pages");
    if (SUCCEEDED(hr) && registered == PAGES) {
        IAudioEncoderProperties *props = NULL;
        DWORD lowest = 1, highest = 1;

        /* The VBR limits and the sample rate as LAME has them by default: no
           limits, the rate of the input. */
        if (SUCCEEDED(filter->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props))) {
            props->set_VariableMin(VBR_BITRATE_NO_LIMIT);
            props->set_VariableMax(VBR_BITRATE_NO_LIMIT);
            props->set_SampleRate(0);
        }
        memset(&frame_result, 0, sizeof frame_result);
        timer = SetTimer(NULL, 0, TIMER_MS, inspect_property_frame);
        hr = OleCreatePropertyFrame(NULL, 0, 0, L"LAME Audio Encoder", 1, &filter, PAGES, pages,
                                    0, 0, NULL);
        KillTimer(NULL, timer);
        if (props != NULL) {
            props->get_VariableMin(&lowest);
            props->get_VariableMax(&highest);
            props->Release();
        }
        CHECK(frame_result.vbr_no_limit_shown && lowest == VBR_BITRATE_NO_LIMIT && highest == VBR_BITRATE_NO_LIMIT,
              "without VBR limits the page shows \"No limit\" and leaves them as they are");
        CHECK(frame_result.same_rate_shown, "with the sample rate of the input the page shows \"Same as input\"");
        REQUIRE_HR(hr, "OleCreatePropertyFrame() shows the pages");
        CHECK(frame_result.frame_found, "the property frame opens");
        CHECK_EQ_U(frame_result.pages_seen, PAGES, "every page shows in the frame");
        CHECK_EQ_U(frame_result.fonts_match, PAGES, "every page uses the font of the desktop");
        CHECK(frame_result.quality_range_ok, "the encoding-quality slider covers the levels 0 to 9");
        CHECK_EQ_U(frame_result.quality_texts_fit, ENCODING_QUALITY_LEVELS,
                   "the text of every quality level fits beside the slider");
        CHECK_EQ_U(frame_result.vbr_levels_listed, VBR_QUALITY_LEVELS,
                   "the VBR quality box lists each level with its typical bitrate");
        CHECK(frame_result.about_ok, "the About page names the filter and its version, and shows the codec's credits, address, icon credit and licence");
    }
    if (filter != NULL)
        filter->Release();
    for (i = 0; i < PAGES; i++) {
        if (cookies[i] != 0)
            CoRevokeClassObject(cookies[i]);
    }
}

/**
 * @brief Leaves every message of a page to the default handling.
 * @param dialog   the page.
 * @param message  the message.
 * @param wparam   the first message parameter.
 * @param lparam   the second message parameter.
 * @return FALSE, for "not handled".
 */
static INT_PTR CALLBACK
quiet_page_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    return FALSE;
}

/**
 * @brief Checks the layout of the page templates in a font one and a half
 *        times as high as the font of the desktop, as a larger text size of
 *        the desktop gives it.
 *
 * The pages are created from the filter's resources through
 * DialogTemplateWithFont(), as children of a hidden window. They show the
 * texts of the templates; the frame test covers the texts the filter sets.
 *
 * @param filter  the module of the filter.
 */
static void
test_page_templates(HMODULE filter)
{
    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    LOGFONTW larger;
    HWND host;
    int p;

    printf("the property pages of the filter in a larger font\n");
    InitCommonControlsEx(&controls);
    if (!desktop_dialog_font(&larger)) {
        CHECK(0, "the desktop reports its dialog font");
        return;
    }
    larger.lfHeight = larger.lfHeight * 3 / 2;
    host = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    CHECK(host != NULL, "a hidden window holds the pages");
    if (host == NULL)
        return;
    for (p = 0; p < PAGES; p++) {
        std::vector<BYTE> dialog_template;
        char what[80];
        HWND page;

        if (!DialogTemplateWithFont(filter, page_dialog[p], larger, dialog_template)) {
            CHECK(0, "the page template takes another font");
            continue;
        }
        page = CreateDialogIndirectParamA(filter, (LPCDLGTEMPLATEA) &dialog_template[0], host,
                                          quiet_page_proc, 0);
        CHECK(page != NULL, "the page is created from its template");
        if (page == NULL)
            continue;
        snprintf(what, sizeof what, "%s in a larger desktop font", page_name[p]);
        check_dialog_layout(page, page_access_keys[p], what);
        DestroyWindow(page);
    }
    DestroyWindow(host);
}

int
main(int argc, char **argv)
{
    const DWORD rate = 44100;
    const double seconds = 2.0;

    HMODULE mod = NULL;
    ctest_stderr filter_stderr;
    PFN_DllGetClassObject get_class = NULL;
    IClassFactory *cf = NULL;
    IGraphBuilder *graph = NULL;
    IBaseFilter *lame = NULL, *src = NULL, *writer = NULL;
    IFileSinkFilter *sink = NULL;
    IMediaControl *mc = NULL;
    IMediaEvent *me = NULL;
    IPin *src_out = NULL, *lame_in = NULL, *lame_out = NULL, *wr_in = NULL;
    HRESULT hr;
    long ev = 0;
    char filter[MAX_PATH];
    char wav[MAX_PATH], mp3[MAX_PATH], noise[MAX_PATH];
    WCHAR wavw[MAX_PATH], mp3w[MAX_PATH], noisew[MAX_PATH];
    int require;
    ctest_component found;

    ctest_start("dshow_test: the LAME DirectShow filter in a real filter graph");

    /* --require mirrors the smoke test's parameter of the same name, and for
       the same reason: the filter is built only where the base class sources
       are laid out, so its absence is a configuration in one caller and a
       failure in another. The caller says which. */
    found = ctest_component_path(argc, argv, "lame.ax", filter, sizeof(filter), &require);
    if (found == CTEST_NO_PATH) {
        CHECK(0, "the path of the filter could be formed");
        return ctest_summary("dshow_test");
    }
    printf("        filter: %s\n", filter);

    if (found == CTEST_ABSENT) {
        if (require) {
            CHECK(0, "the filter was built and is where it was looked for");
            return ctest_summary("dshow_test");
        }
        printf("dshow_test: SKIPPED - no filter at that path, and it was not "
               "required.\n");
        printf("            The filter builds only where the DirectShow base "
               "class sources are.\n");
        return 0;
    }

    GetTempPathA(MAX_PATH, wav);
    strcat(wav, "lame_dshow_test_in.wav");
    GetTempPathA(MAX_PATH, mp3);
    strcat(mp3, "lame_dshow_test_out.mp3");
    DeleteFileA(mp3);
    CHECK(write_wav(wav, rate, 2, (DWORD) (rate * seconds), WAV_TONE) != 0,
          "the input WAV was written");
    GetTempPathA(MAX_PATH, noise);
    strcat(noise, "lame_dshow_test_noise.wav");
    CHECK(write_wav(noise, rate, 2, (DWORD) (rate * seconds), WAV_NOISE) != 0,
          "the noise WAV was written");
    MultiByteToWideChar(CP_ACP, 0, noise, -1, noisew, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, wav, -1, wavw, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, mp3, -1, mp3w, MAX_PATH);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    REQUIRE_HR(hr, "COM starts up");
    if (FAILED(hr)) {
        return ctest_summary("dshow_test");
    }

    if (!ctest_load_with_stderr_file(filter, &filter_stderr)) {
        char detail[CTEST_DETAIL_CHARS];

        sprintf(detail, "Win32 error %lu", GetLastError());
        ctest_record(0, "the filter image loads, with its stderr going to a file", detail);
        goto out;
    }
    mod = filter_stderr.module;
    CHECK(mod != NULL, "the filter image loads, with its stderr going to a file");

    get_class = (PFN_DllGetClassObject) GetProcAddress(mod, "DllGetClassObject");
    CHECK(get_class != NULL, "DllGetClassObject resolves");
    if (get_class == NULL) {
        goto out;
    }

    hr = get_class(CLSID_LAMEDShowFilter_local, IID_IClassFactory, (void **) &cf);
    REQUIRE_HR(hr, "the DLL's class factory answers for the encoder CLSID");
    if (FAILED(hr)) {
        goto out;
    }

    hr = cf->CreateInstance(NULL, IID_IBaseFilter, (void **) &lame);
    REQUIRE_HR(hr, "the filter instantiates as an IBaseFilter");
    if (FAILED(hr)) {
        goto out;
    }

    hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                          IID_IGraphBuilder, (void **) &graph);
    REQUIRE_HR(hr, "a filter graph could be created");
    if (FAILED(hr)) {
        goto out;
    }

    hr = graph->AddFilter(lame, L"LAME Audio Encoder");
    REQUIRE_HR(hr, "the graph accepts the encoder");
    if (FAILED(hr)) {
        goto out;
    }

    hr = graph->AddSourceFilter(wavw, L"Source", &src);
    REQUIRE_HR(hr, "the WAV is added as a source");
    if (FAILED(hr)) {
        goto out;
    }

    hr = CoCreateInstance(CLSID_FileWriter, NULL, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, (void **) &writer);
    REQUIRE_HR(hr, "the File Writer could be created");
    if (FAILED(hr)) {
        goto out;
    }
    hr = writer->QueryInterface(IID_IFileSinkFilter, (void **) &sink);
    REQUIRE_HR(hr, "the writer offers IFileSinkFilter");
    if (FAILED(hr)) {
        goto out;
    }
    REQUIRE_HR(sink->SetFileName(mp3w, NULL), "the output file name is set");
    REQUIRE_HR(graph->AddFilter(writer, L"File Writer"),
               "the graph accepts the writer");

    src_out = find_pin(src, PINDIR_OUTPUT);
    lame_in = find_pin(lame, PINDIR_INPUT);
    CHECK(src_out != NULL && lame_in != NULL,
          "the source's output pin and the encoder's input pin are there");
    if (src_out == NULL || lame_in == NULL) {
        goto out;
    }

    /* Intelligent connect: the graph manager inserts the WAV parser itself and
       negotiates a media type both sides accept. This is the step a filter that
       merely links can still fail. */
    hr = graph->Connect(src_out, lame_in);
    REQUIRE_HR(hr, "source to encoder connects, media type negotiated");
    if (FAILED(hr)) {
        goto out;
    }

    test_property_round_trip(lame);
    test_settings_survive_a_save(cf);
    test_save_keeps_no_handle(cf);
    test_encoder_properties(lame);

    lame_out = find_pin(lame, PINDIR_OUTPUT);
    wr_in = find_pin(writer, PINDIR_INPUT);
    CHECK(lame_out != NULL && wr_in != NULL,
          "the encoder's output pin and the writer's input pin are there");
    if (lame_out == NULL || wr_in == NULL) {
        goto out;
    }

    /* Before the output pin is connected to anything, so that what is being
       asked about is the pass-through to the input side and not something the
       downstream connection supplied. */
    test_seeking_passes_through(lame_out);
    test_stream_caps(lame_out, "with the output pin unconnected");

    REQUIRE_HR(graph->Connect(lame_out, wr_in),
               "encoder to file writer connects");

    /* And again with the graph complete: the pin is now connected as a stream,
       which carries no format block either, so this asks the same question
       about the other state a caller can find the pin in. */
    test_stream_caps(lame_out, "with the whole graph connected");
    test_stream_caps_list(lame_out);

    hr = graph->QueryInterface(IID_IMediaControl, (void **) &mc);
    REQUIRE_HR(hr, "the graph offers IMediaControl");
    if (FAILED(hr)) {
        goto out;
    }
    hr = graph->QueryInterface(IID_IMediaEvent, (void **) &me);
    REQUIRE_HR(hr, "the graph offers IMediaEvent");
    if (FAILED(hr)) {
        goto out;
    }

    hr = mc->Run();
    REQUIRE_HR(hr, "the graph runs");
    if (FAILED(hr)) {
        goto out;
    }

    hr = me->WaitForCompletion(GRAPH_TIMEOUT_MS, &ev);
    REQUIRE_HR(hr, "the graph reaches completion within a minute");
    CHECK_EQ_U(ev, EC_COMPLETE, "it finished because the stream ended");
    mc->Stop();

    inspect_mp3(mp3, seconds, rate, SECOND_BITRATE_KBPS);
    test_zero_output_rate(lame, lame_out);
    test_refused_setting_fails_run(lame, mc);
    test_aligned_stream_end(cf, wavw);
    test_rejected_delivery(cf, wavw);
    test_frame_mode(cf, wavw, rate);
    test_frame_times_follow_a_gap(cf);
    test_failed_output_buffer(cf, wavw);
    test_flush_restarts_the_encoder(cf);
    test_default_settings(lame);
    test_interface2(lame);
    test_saved_graphs(cf);
    test_interface2_in_the_stream(cf, wavw, mp3w, rate);
    test_strict_iso_in_the_stream(cf, noisew, rate);
    test_vbr_announced_bitrate(cf, wavw, rate);
    test_vbr_is_lames_default(cf, wavw, mp3w, rate);
    test_property_pages(get_class, cf);
    test_page_templates(mod);
    /* LAME reports why it rejects the VBR range of
       test_refused_setting_fails_run(). */
    ctest_stderr_empty(&filter_stderr, "the filter writes nothing to the stderr of its host");

out:
    if (wr_in) wr_in->Release();
    if (lame_out) lame_out->Release();
    if (lame_in) lame_in->Release();
    if (src_out) src_out->Release();
    if (me) me->Release();
    if (mc) mc->Release();
    if (sink) sink->Release();
    if (writer) writer->Release();
    if (src) src->Release();
    if (lame) lame->Release();
    if (graph) graph->Release();
    if (cf) cf->Release();
    CoUninitialize();
    DeleteFileA(wav);
    DeleteFileA(mp3);

    return ctest_summary("dshow_test");
}
