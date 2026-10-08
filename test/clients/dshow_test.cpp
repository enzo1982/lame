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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ctest.h"
#include "mp3frame.h"

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

/**
 * @brief Writes a WAV file with a sine. The graph reads this file.
 *
 * The header is the canonical 44-byte header: a RIFF chunk, a PCM format
 * chunk of the minimum size, and a data chunk. Each field is at an offset
 * that the format fixes. So the offsets have names, and the code does not
 * count them.
 */
static int
write_wav(const char *path, DWORD rate, WORD channels, DWORD frames)
{
    FILE *f = fopen(path, "wb");
    DWORD data_bytes = frames * channels * WAV_BYTES_PER_SAMPLE;
    unsigned char h[WAV_HEADER_BYTES];
    DWORD i;

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
            CoTaskMemFree(mt->pbFormat);
            if (mt->pUnk != NULL) {
                mt->pUnk->Release();
            }
            CoTaskMemFree(mt);
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
    DWORD   variable = 0, vmin = 0, vmax = 0;
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
    static const struct {
        dword_getter get;
        dword_setter set;
        const char *name;
    } settings[] = {
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
    enum { N = sizeof(settings) / sizeof(settings[0]), FIRST_VALUE = 1000 };
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
        (props->*settings[i].get)(&saved[i]);
    }
    for (i = 0; i < N; i++) {
        (props->*settings[i].set)((DWORD) (FIRST_VALUE + i));
    }
    for (i = 0; i < N; i++) {
        got = 0;
        if ((props->*settings[i].get)(&got) == S_OK && got == (DWORD) (FIRST_VALUE + i)) {
            ++same;
        } else {
            sprintf(detail, "%s reads back %lu, not %d", settings[i].name, (unsigned long) got,
                    FIRST_VALUE + i);
            CHECK(0, detail);
        }
    }
    CHECK_EQ_U(same, N, "each of the 20 stored settings reads back its own value");
    for (i = 0; i < N; i++) {
        (props->*settings[i].set)(saved[i]);
    }
    props->Release();
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
 * @brief A stream sink whose input pin asks for an allocator alignment.
 *
 * The stock File Writer asks for no alignment. So a graph of stock filters
 * never shows how the encoder pads a stream to an alignment. This pin accepts
 * a byte stream, asks for @c cbAlign bytes, and appends what it receives to
 * one buffer. It lives on the stack of the test that uses it. It does not
 * need reference counting and does not do it.
 */
class AlignedSinkPin : public IPin, public IMemInputPin {
public:
    long    align;          /**< the alignment the pin asks for */
    IPin   *peer;           /**< the connected output pin */
    IBaseFilter *owner;     /**< the filter the pin reports as its own */
    HANDLE  eos;            /**< signaled by EndOfStream() */
    BYTE   *stream;         /**< everything received, in order */
    long    length;         /**< bytes in #stream */
    long    capacity;       /**< bytes allocated for #stream */
    int     deliveries;     /**< number of samples received */

    /**
     * @brief Creates a pin that asks for @p a bytes of alignment.
     * @param a the alignment.
     */
    AlignedSinkPin(long a) : align(a), peer(NULL), owner(NULL), stream(NULL),
        length(0), capacity(0), deliveries(0)
    {
        eos = CreateEvent(NULL, TRUE, FALSE, NULL);
    }
    /** @brief Releases the peer pin, and frees the received stream and the event. */
    ~AlignedSinkPin()
    {
        if (peer)
            peer->Release();
        free(stream);
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
        if (mt == NULL || mt->majortype != MEDIATYPE_Stream)
            return VFW_E_TYPE_NOT_ACCEPTED;
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
        mt->majortype = MEDIATYPE_Stream;
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
        return mt->majortype == MEDIATYPE_Stream ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **) { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin **, ULONG *) { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() { SetEvent(eos); return S_OK; }
    STDMETHODIMP BeginFlush() { return S_OK; }
    STDMETHODIMP EndFlush() { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

    STDMETHODIMP GetAllocator(IMemAllocator **a) { *a = NULL; return VFW_E_NO_ALLOCATOR; }
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
 * @return Non-zero when the stream ended within the graph timeout.
 */
static int
encode_into_aligned_sink(IClassFactory *cf, const WCHAR *wav, AlignedSinkPin &sink,
                         int *connected)
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
    graph->AddFilter(&owner, L"Aligned sink");
    if (FAILED(graph->ConnectDirect(lame_out, &sink, NULL)))
        goto out;
    *connected = 1;
    if (FAILED(graph->QueryInterface(IID_IMediaControl, (void **) &mc)) || FAILED(mc->Run()))
        goto out;
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

int
main(int argc, char **argv)
{
    const DWORD rate = 44100;
    const double seconds = 2.0;

    HMODULE mod = NULL;
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
    char wav[MAX_PATH], mp3[MAX_PATH];
    WCHAR wavw[MAX_PATH], mp3w[MAX_PATH];
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
    CHECK(write_wav(wav, rate, 2, (DWORD) (rate * seconds)) != 0,
          "the input WAV was written");
    MultiByteToWideChar(CP_ACP, 0, wav, -1, wavw, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, mp3, -1, mp3w, MAX_PATH);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    REQUIRE_HR(hr, "COM starts up");
    if (FAILED(hr)) {
        return ctest_summary("dshow_test");
    }

    mod = LoadLibraryA(filter);
    if (mod == NULL) {
        char detail[CTEST_DETAIL_CHARS];

        sprintf(detail, "Win32 error %lu", GetLastError());
        ctest_record(0, "the filter image loads", detail);
        goto out;
    }
    CHECK(mod != NULL, "the filter image loads");

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
