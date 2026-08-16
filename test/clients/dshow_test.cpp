/**
 * @file
 * @brief Tests for the LAME DirectShow filter.
 *
 * The smoke test asks whether @c lame.ax loads and exports the COM entry
 * points. This asks whether it works as a filter: the graph manager builds the
 * graph, inserts whatever parser the source needs, negotiates media types
 * across both pin connections and streams a WAV file through the encoder into
 * a file - all of it DirectShow's code driving the filter's.
 *
 * No registry and no administrator are involved. The only thing a registration
 * would add is the CLSID lookup, so instead of @c CoCreateInstance the filter
 * comes from the DLL's own class factory through @c DllGetClassObject - the
 * same object by the same code path inside the filter, minus the registry
 * step. What this therefore cannot cover is that @c DllRegisterServer writes
 * correct entries and that the filter is discoverable by category and merit;
 * both need a machine-wide registration and are out of scope.
 *
 * The filter is built only where the DirectShow base class sources are laid
 * out, so this test is built under the same condition.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

/**
 * @brief The part of the filter's property interface this test uses.
 *
 * Declared here for the same reason the GUIDs are: so the test does not depend
 * on the filter's private headers. The vtable order is the interface's, and a
 * mismatch would show up as the wrong method being called rather than as a
 * compile error - which is why only the first few entries are declared, and
 * why the bitrate accessors are the pair that gets exercised.
 */
struct IAudioEncoderPropertiesSubset : public IUnknown
{
    STDMETHOD(get_PESOutputEnabled)(DWORD *enabled) PURE;
    STDMETHOD(set_PESOutputEnabled)(DWORD enabled) PURE;
    STDMETHOD(get_Bitrate)(DWORD *bitrate) PURE;
    STDMETHOD(set_Bitrate)(DWORD bitrate) PURE;
};

/** @brief The first pin of the given direction, or NULL. */
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

/** @brief Bytes of the canonical WAV header this test writes. */
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

/** @brief The RIFF size counts the file past its own field - id and size. */
#define WAV_RIFF_SIZE_EXCLUDES  8
/** @brief Size of a PCM format chunk, which carries no extension. */
#define WAV_PCM_FMT_BYTES       16
/** @brief The format tag for uncompressed PCM. */
#define WAV_FORMAT_PCM          1
/** @brief The sample width this test writes. */
#define WAV_BITS_PER_SAMPLE     16
/** @brief The same width in bytes, which several fields are counted in. */
#define WAV_BYTES_PER_SAMPLE    (WAV_BITS_PER_SAMPLE / 8)

/**
 * @brief How long the graph is given to run a second of audio through.
 *
 * Generous rather than tight: this is the bound that turns a filter which never
 * completes into a failed check instead of a test that never returns.
 */
#define GRAPH_TIMEOUT_MS        60000

/** @brief Concert A, the test tone the file is filled with. */
#define TONE_HZ                 440.0
/** @brief Its amplitude, comfortably below full scale, so nothing clips. */
#define TONE_AMPLITUDE          16000.0
/** @brief One turn of the circle, for the sine's argument. */
#define TWO_PI                  6.283185307179586

/**
 * @brief Writes a WAV file holding a sine, which is what the graph reads.
 *
 * The header is the canonical 44-byte one: a RIFF chunk, a PCM format chunk of
 * the minimum size, and a data chunk. Every field is written at the offset the
 * format fixes it at, so the offsets are named rather than counted.
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
        double t = (double) i / (double) rate;
        short v = (short) (TONE_AMPLITUDE * sin(TWO_PI * TONE_HZ * t));
        WORD c;

        for (c = 0; c < channels; c++) {
            fwrite(&v, WAV_BYTES_PER_SAMPLE, 1, f);
        }
    }
    fclose(f);
    return 1;
}

/**
 * @brief Reads the produced file back as MPEG frames.
 *
 * Same reasoning as the ACM test: a byte count cannot tell a truncated stream
 * from a variable-rate one, and the frame headers can.
 */
static void
inspect_mp3(const char *path, double seconds, DWORD rate)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;
    long off = 0;
    int seen = 0;
    int distinct = 0;
    int rates[MP3_BITRATE_INDEX_COUNT];
    int want;

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

    memset(rates, 0, sizeof(rates));
    while (off + MP3_HEADER_BYTES <= size) {
        unsigned char *h = buf + off;
        int index, padding, len;

        if (!mp3_is_frame_sync(h)) {
            ++off;
            continue;
        }
        index = mp3_bitrate_index(h);
        padding = mp3_padding_bytes(h);
        if (index == MP3_BITRATE_FREE_FORMAT || index == MP3_BITRATE_INVALID) {
            break;
        }
        len = mp3_frame_bytes(index, padding, rate);
        if (len <= 0) {
            break;
        }
        if (!rates[index]) {
            rates[index] = 1;
            ++distinct;
        }
        ++seen;
        off += len;
    }
    printf("        %ld bytes, %d frame(s), %d distinct bitrate(s)\n",
           size, seen, distinct);
    CHECK(size >= MP3_HEADER_BYTES && mp3_is_frame_sync(buf),
          "the file begins with an MPEG frame sync");
    /* Allow the first frame and the last: the encoder may hold one back, and
       the tail is only as long as what is left of the input. */
    want = (int) (seconds * mp3_frames_per_second(rate)) - 2;
    CHECK(seen >= want, "the whole input is present as MPEG frames");
    free(buf);
}

/**
 * @brief The filter's own property interface answers and holds what it is told.
 *
 * @c iaudioprops.h says to configure the encoder with its input pin already
 * connected, since before that the parameters are overridden by the defaults
 * for the input media type. This is therefore called between the two
 * connections rather than before either.
 */
static void
test_encoder_properties(IBaseFilter *lame)
{
    IAudioEncoderPropertiesSubset *props = NULL;
    HRESULT hr;
    DWORD first = 0;
    DWORD second = 0;

    hr = lame->QueryInterface(IID_IAudioEncoderProperties_local, (void **) &props);
    REQUIRE_HR(hr, "the filter offers its audio encoder properties");
    if (FAILED(hr)) {
        return;
    }

    REQUIRE_HR(props->set_Bitrate(128), "a bitrate of 128 kbps is accepted");
    REQUIRE_HR(props->get_Bitrate(&first), "the bitrate reads back");
    CHECK_EQ_U(first, 128, "it reads back as the value that was set");

    REQUIRE_HR(props->set_Bitrate(192), "a bitrate of 192 kbps is accepted");
    REQUIRE_HR(props->get_Bitrate(&second), "the second bitrate reads back");
    CHECK_EQ_U(second, 192, "it reads back as the second value");
    CHECK_NE_U(first, second, "the interface is not returning a fixed number");

    props->Release();
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
    const char *given = NULL;
    int require = 0;
    int i;

    printf("dshow_test: the LAME DirectShow filter in a real filter graph\n");

    /* --require mirrors the smoke test's parameter of the same name, and for
       the same reason: the filter is built only where the base class sources
       are laid out, so its absence is a configuration in one caller and a
       failure in another. The caller says which. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--require") == 0) {
            require = 1;
        } else {
            given = argv[i];
        }
    }

    if (given != NULL) {
        strncpy(filter, given, sizeof(filter) - 1);
        filter[sizeof(filter) - 1] = '\0';
    } else {
        char *slash;

        if (::GetModuleFileNameA(NULL, filter, MAX_PATH) == 0 ||
            (slash = strrchr(filter, '\\')) == NULL) {
            CHECK(0, "this executable's own directory could be determined");
            return ctest_summary("dshow_test");
        }
        *(slash + 1) = '\0';
        strcat(filter, "lame.ax");
    }
    printf("        filter: %s\n", filter);

    if (::GetFileAttributesA(filter) == INVALID_FILE_ATTRIBUTES) {
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

    test_encoder_properties(lame);

    lame_out = find_pin(lame, PINDIR_OUTPUT);
    wr_in = find_pin(writer, PINDIR_INPUT);
    CHECK(lame_out != NULL && wr_in != NULL,
          "the encoder's output pin and the writer's input pin are there");
    if (lame_out == NULL || wr_in == NULL) {
        goto out;
    }
    REQUIRE_HR(graph->Connect(lame_out, wr_in),
               "encoder to file writer connects");

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

    inspect_mp3(mp3, seconds, rate);

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
