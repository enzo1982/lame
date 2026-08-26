/**
 * @file
 * @brief Tests for the Blade-compatible encoder DLL, lame_enc.dll.
 *
 * The smoke test asks whether the DLL loads and exports the Blade entry
 * points. This asks whether they encode: a stream is opened, fed a known
 * sine, drained and closed through the same calls a Blade host makes, and
 * the bytes that come out are walked frame by frame against the rate the
 * configuration asked for.
 *
 * The DLL is loaded at run time from beside this executable, the way the
 * DirectShow test loads lame.ax, so no import library is involved and the
 * exports are reached by the undecorated names the .def file publishes -
 * which is the surface a Blade host actually binds to. The DLL is built by
 * the main solution rather than this one, so a missing lame_enc.dll is a
 * skip here and a failure under --require, mirroring lame_dshow_test.
 *
 * What stays uncovered, deliberately: the entry points' behaviour on null
 * arguments is not part of the Blade contract and the DLL does not promise
 * it, and beWriteInfoTag's failure arms raise a message box, which no
 * unattended test can walk through.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ctest.h"
#include "mp3frame.h"

#include "BladeMP3EncDLL.h"

/**
 * @brief Entry-point type for beFlushNoGap, which the header does not name.
 *
 * BladeMP3EncDLL.h declares a pointer typedef for every other export and
 * TEXT_BEFLUSHNOGAP for this one's name, but no BEFLUSHNOGAP - an upstream
 * omission this file works around rather than edits into an interface header
 * that Blade hosts carry their own copies of. The signature is the export's.
 */
typedef BE_ERR (*BLADE_TEST_BEFLUSHNOGAP) (HBE_STREAM, PBYTE, PDWORD);

/** @brief Sample rate every stream in this file encodes at. */
#define RATE        44100
/** @brief The constant bit rate asked for, in kbit/s. */
#define KBPS        128
/** @brief Channels; every stream here is stereo. */
#define CHANNELS    2
/** @brief Frequency of the test tone, in Hz. */
#define TONE_HZ     1000.0
/** @brief Amplitude of the test tone - loud, but nowhere near clipping. */
#define TONE_PEAK   12000.0
/** @brief Seconds of audio each stream encodes. */
#define SECONDS     1
/** @brief Room for one returned chunk; beInitStream reports less. */
#define OUT_ROOM    65536
/** @brief Room for a whole encoded stream at these settings. */
#define STREAM_ROOM (KBPS * MP3_BITS_PER_KBIT / MP3_BITS_PER_BYTE * (SECONDS + 1))

/**
 * @brief Every Blade entry point, resolved from the loaded DLL.
 *
 * The typedefs and the export names are the DLL's own header's.
 */
typedef struct {
    BEINITSTREAM             init;
    BEENCODECHUNK            chunk;
    BEENCODECHUNKFLOATS16NI  chunk_float;
    BEDEINITSTREAM           deinit;
    BECLOSESTREAM            close;
    BEVERSION                version;
    BEWRITEVBRHEADER         vbr_header;
    BLADE_TEST_BEFLUSHNOGAP  flush_nogap;
    BEWRITEINFOTAG           info_tag;
} blade_exports;

/**
 * @brief Resolves the nine exports, checking each one by name.
 * @param mod  the loaded lame_enc.dll.
 * @param be   receives the entry points.
 * @return 1 when everything the tests below call resolved, else 0.
 */
static int
load_exports(HMODULE mod, blade_exports *be)
{
    be->init = (BEINITSTREAM) GetProcAddress(mod, TEXT_BEINITSTREAM);
    be->chunk = (BEENCODECHUNK) GetProcAddress(mod, TEXT_BEENCODECHUNK);
    be->chunk_float = (BEENCODECHUNKFLOATS16NI)
        GetProcAddress(mod, TEXT_BEENCODECHUNKFLOATS16NI);
    be->deinit = (BEDEINITSTREAM) GetProcAddress(mod, TEXT_BEDEINITSTREAM);
    be->close = (BECLOSESTREAM) GetProcAddress(mod, TEXT_BECLOSESTREAM);
    be->version = (BEVERSION) GetProcAddress(mod, TEXT_BEVERSION);
    be->vbr_header = (BEWRITEVBRHEADER) GetProcAddress(mod, TEXT_BEWRITEVBRHEADER);
    be->flush_nogap = (BLADE_TEST_BEFLUSHNOGAP)
        GetProcAddress(mod, TEXT_BEFLUSHNOGAP);
    be->info_tag = (BEWRITEINFOTAG) GetProcAddress(mod, TEXT_BEWRITEINFOTAG);
    CHECK(be->init != NULL, "beInitStream resolves");
    CHECK(be->chunk != NULL, "beEncodeChunk resolves");
    CHECK(be->chunk_float != NULL, "beEncodeChunkFloatS16NI resolves");
    CHECK(be->deinit != NULL, "beDeinitStream resolves");
    CHECK(be->close != NULL, "beCloseStream resolves");
    CHECK(be->version != NULL, "beVersion resolves");
    CHECK(be->vbr_header != NULL, "beWriteVBRHeader resolves");
    CHECK(be->flush_nogap != NULL, "beFlushNoGap resolves");
    CHECK(be->info_tag != NULL, "beWriteInfoTag resolves");
    return be->init && be->chunk && be->chunk_float && be->deinit
        && be->close && be->version && be->vbr_header && be->flush_nogap
        && be->info_tag;
}

/**
 * @brief The Blade configuration every stream here starts from.
 * @param cfg              filled in.
 * @param write_vbr_header whether the stream reserves a LAME-tag frame.
 */
static void
make_config(BE_CONFIG *cfg, int write_vbr_header)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->dwConfig = BE_CONFIG_LAME;
    cfg->format.LHV1.dwStructVersion = 1;
    cfg->format.LHV1.dwStructSize = sizeof(*cfg);
    cfg->format.LHV1.dwSampleRate = RATE;
    cfg->format.LHV1.nMode = BE_MP3_MODE_STEREO;
    cfg->format.LHV1.dwBitrate = KBPS;
    cfg->format.LHV1.dwMpegVersion = MPEG1;
    cfg->format.LHV1.bWriteVBRHeader = write_vbr_header ? TRUE : FALSE;
}

/**
 * @brief One interleaved stereo sample pair of the test tone.
 * @param n  index of the sample pair from the start of the stream.
 * @return the sample value, identical on both channels.
 */
static SHORT
tone_sample(DWORD n)
{
    return (SHORT) (TONE_PEAK
                    * sin(2.0 * 3.14159265358979 * TONE_HZ
                          * (double) n / (double) RATE));
}

/**
 * @brief Encodes the test tone through the given chunk entry point.
 *
 * Opens a stream, feeds it exactly the chunk size beInitStream reported,
 * drains the encoder and closes the stream - the sequence a Blade host
 * performs. Both chunk entry points carry samples valued alike, so either
 * one can drive the same stream shape.
 *
 * @param be         the resolved entry points.
 * @param use_float  feed beEncodeChunkFloatS16NI instead of beEncodeChunk.
 * @param out        receives the encoded stream.
 * @param out_room   how much @a out can hold.
 * @param what       names the arm in each check's description.
 * @return bytes of encoded stream, or 0 when a call failed.
 */
static DWORD
encode_tone(const blade_exports *be, int use_float,
            unsigned char *out, DWORD out_room, const char *what)
{
    BE_CONFIG cfg;
    HBE_STREAM hbe = 0;
    DWORD   samples = 0, chunk_room = 0, written = 0, total = 0, fed = 0;
    DWORD   want = (DWORD) RATE * CHANNELS * SECONDS;
    char    detail[CTEST_DETAIL_CHARS];
    BE_ERR  err;

    make_config(&cfg, 0);
    err = be->init(&cfg, &samples, &chunk_room, &hbe);
    sprintf(detail, "%s: beInitStream", what);
    CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);
    if (err != BE_ERR_SUCCESSFUL) {
        return 0;
    }
    /* An MPEG-1 stream is fed a frame of samples per channel at a time. */
    sprintf(detail, "%s: chunk size is one frame per channel", what);
    CHECK_EQ_U(samples, (DWORD) MP3_SAMPLES_PER_FRAME * CHANNELS, detail);

    while (fed < want && total + chunk_room < out_room) {
        SHORT   pcm[MP3_SAMPLES_PER_FRAME * CHANNELS];
        FLOAT   pcm_l[MP3_SAMPLES_PER_FRAME], pcm_r[MP3_SAMPLES_PER_FRAME];
        DWORD   i;

        for (i = 0; i < samples; i++) {
            pcm[i] = tone_sample((fed + i) / CHANNELS);
        }
        if (use_float) {
            for (i = 0; i < samples / CHANNELS; i++) {
                pcm_l[i] = (FLOAT) pcm[i * CHANNELS];
                pcm_r[i] = (FLOAT) pcm[i * CHANNELS + 1];
            }
            /* The non-interleaved entry point counts samples per channel,
               where the interleaved one counts them across both. */
            err = be->chunk_float(hbe, samples / CHANNELS, pcm_l, pcm_r,
                                  out + total, &written);
        } else {
            err = be->chunk(hbe, samples, pcm, out + total, &written);
        }
        if (err != BE_ERR_SUCCESSFUL) {
            break;
        }
        total += written;
        fed += samples;
    }
    sprintf(detail, "%s: every chunk encoded", what);
    CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);

    written = 0;
    err = be->deinit(hbe, out + total, &written);
    sprintf(detail, "%s: beDeinitStream", what);
    CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);
    if (err == BE_ERR_SUCCESSFUL) {
        total += written;
    }
    err = be->close(hbe);
    sprintf(detail, "%s: beCloseStream", what);
    CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);
    return total;
}

/**
 * @brief The bitrate index the frame header carries for a rate in kbit/s.
 * @param kbps  the rate to look up.
 * @return its index, or #MP3_BITRATE_INVALID when the table lacks it.
 */
static int
bitrate_index_of(int kbps)
{
    int     i;

    for (i = 1; i < MP3_BITRATE_INVALID; i++) {
        if (mp3_bitrate_kbps[i] == kbps) {
            return i;
        }
    }
    return MP3_BITRATE_INVALID;
}

/**
 * @brief Walks an encoded stream frame by frame and counts the frames.
 *
 * Every frame must begin with a sync and carry the expected bitrate index,
 * and each frame's own header decides where the next one starts - so one
 * wrong rate, or a sample rate other than the one the walk assumes,
 * desynchronises the walk and fails the count. That makes the walk a joint
 * check on the sync, the bitrate and the sample rate at once.
 *
 * @param buf   the stream.
 * @param len   its length in bytes.
 * @param what  names the stream in each check's description.
 * @return frames walked before the first mismatch or the end of the data.
 */
static int
walk_frames(const unsigned char *buf, DWORD len, const char *what)
{
    DWORD   pos = 0;
    int     frames = 0, in_sync = 1;
    int     want_index = bitrate_index_of(KBPS);
    char    detail[CTEST_DETAIL_CHARS];

    while (pos + MP3_HEADER_BYTES <= len) {
        int     idx;

        if (!mp3_is_frame_sync(buf + pos)) {
            in_sync = 0;
            break;
        }
        idx = mp3_bitrate_index(buf + pos);
        if (idx != want_index) {
            in_sync = 0;
            break;
        }
        pos += mp3_frame_bytes(idx, mp3_padding_bytes(buf + pos), RATE);
        frames++;
    }
    sprintf(detail, "%s: every frame in sync at %d kbit/s", what, KBPS);
    ctest_record(in_sync && frames > 0, detail, "");
    return frames;
}

/** @brief Bytes of ID3v2 tag the tagged arm writes: header plus padding. */
#define ID3V2_HEADER_BYTES 10
/** @brief Padding inside the synthetic tag; fits in one seven-bit size byte. */
#define ID3V2_PADDING      0x20

/**
 * @brief Writes @a len stream bytes to @a path, optionally behind an ID3v2 tag.
 *
 * The tag is the smallest shape the DLL's reader understands: the identifier
 * and version, then the size in four seven-bit bytes, then padding.
 *
 * @param path        the file to create.
 * @param buf         the encoded stream.
 * @param len         its length in bytes.
 * @param with_id3v2  put the synthetic tag in front of the audio.
 * @return 1 when the file was written whole, else 0.
 */
static int
write_stream(const char *path, const unsigned char *buf, DWORD len,
             int with_id3v2)
{
    FILE   *fp = fopen(path, "wb");
    size_t  put = 0;

    if (fp == NULL) {
        return 0;
    }
    if (with_id3v2) {
        static const unsigned char tag[ID3V2_HEADER_BYTES] =
            { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, ID3V2_PADDING };
        unsigned char body[ID3V2_PADDING];

        memset(body, 0, sizeof(body));
        put += fwrite(tag, 1, sizeof(tag), fp);
        put += fwrite(body, 1, sizeof(body), fp);
    }
    put += fwrite(buf, 1, len, fp);
    fclose(fp);
    return put == len + (with_id3v2 ? ID3V2_HEADER_BYTES + ID3V2_PADDING : 0);
}

/**
 * @brief Encodes with a reserved LAME-tag frame and has the DLL fill it in.
 *
 * This is the beWriteInfoTag path: the stream is written to a file, the tag
 * write reopens it, skips whatever ID3v2 tag sits in front of the audio and
 * overwrites the reserved first frame in place. Both arms are run - a bare
 * file, and one with a tag in front - because a file with no tag takes the
 * other branch of the skip, and only the pair shows the skip skips.
 *
 * @param be    the resolved entry points.
 * @param dir   directory for the scratch files, with a trailing separator.
 */
static void
test_info_tag(const blade_exports *be, const char *dir)
{
    static const char *arm_name[2] = { "bare file", "file behind an ID3v2 tag" };
    long    size[2] = { 0, 0 };
    int     arm;

    for (arm = 0; arm < 2; arm++) {
        BE_CONFIG cfg;
        HBE_STREAM hbe = 0;
        DWORD   samples = 0, chunk_room = 0, written = 0, total = 0, fed = 0;
        DWORD   want = (DWORD) RATE * CHANNELS * SECONDS;
        unsigned char *out = (unsigned char *) malloc(STREAM_ROOM);
        char    path[MAX_PATH], detail[CTEST_DETAIL_CHARS];
        FILE   *fp;
        BE_ERR  err;

        CHECK(out != NULL, "stream buffer allocated");
        if (out == NULL) {
            return;
        }
        sprintf(path, "%slame_blade_test_%d.mp3", dir, arm);

        make_config(&cfg, 1);
        err = be->init(&cfg, &samples, &chunk_room, &hbe);
        sprintf(detail, "%s: beInitStream with a reserved tag frame", arm_name[arm]);
        CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);
        if (err != BE_ERR_SUCCESSFUL) {
            free(out);
            return;
        }
        while (fed < want && total + chunk_room < STREAM_ROOM) {
            SHORT   pcm[MP3_SAMPLES_PER_FRAME * CHANNELS];
            DWORD   i;

            for (i = 0; i < samples; i++) {
                pcm[i] = tone_sample((fed + i) / CHANNELS);
            }
            if (be->chunk(hbe, samples, pcm, out + total, &written)
                != BE_ERR_SUCCESSFUL) {
                break;
            }
            total += written;
            fed += samples;
        }
        written = 0;
        if (be->deinit(hbe, out + total, &written) == BE_ERR_SUCCESSFUL) {
            total += written;
        }
        sprintf(detail, "%s: the stream was written", arm_name[arm]);
        ctest_record(write_stream(path, out, total, arm), detail, "");

        err = be->info_tag(hbe, path);
        sprintf(detail, "%s: beWriteInfoTag succeeds", arm_name[arm]);
        CHECK_EQ_U(err, BE_ERR_SUCCESSFUL, detail);
        be->close(hbe);

        /* The rewritten file: the tag - when there is one - is intact, and
           the audio behind it still walks from its first byte. */
        fp = fopen(path, "rb");
        sprintf(detail, "%s: the finished file opens", arm_name[arm]);
        ctest_record(fp != NULL, detail, "");
        if (fp != NULL) {
            unsigned char head[ID3V2_HEADER_BYTES + ID3V2_PADDING];
            long    audio_at = arm ? (long) sizeof(head) : 0;
            long    audio_bytes;
            DWORD   got;

            fseek(fp, 0, SEEK_END);
            size[arm] = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (arm) {
                got = (DWORD) fread(head, 1, sizeof(head), fp);
                sprintf(detail, "%s: the ID3v2 tag survived the rewrite",
                        arm_name[arm]);
                ctest_record(got == sizeof(head)
                             && memcmp(head, "ID3", 3) == 0, detail, "");
            }
            /* Bounded by what the buffer holds, which the encode fit into -
               so a rewritten file that no longer fits is itself a failure. */
            audio_bytes = size[arm] - audio_at;
            sprintf(detail, "%s: the audio fits the room it came from",
                    arm_name[arm]);
            ctest_record(audio_bytes > 0 && audio_bytes <= STREAM_ROOM,
                         detail, "");
            if (audio_bytes > 0 && audio_bytes <= STREAM_ROOM) {
                got = (DWORD) fread(out, 1, (size_t) audio_bytes, fp);
                sprintf(detail, "%s: audio after the tag", arm_name[arm]);
                walk_frames(out, got, detail);
            }
            fclose(fp);
        }
        remove(path);
        free(out);
    }

    /* The two files carry the same audio and the same rewritten tag frame;
       the synthetic ID3v2 tag is the only difference there is. */
    CHECK_EQ_U((unsigned long) (size[1] - size[0]),
               ID3V2_HEADER_BYTES + ID3V2_PADDING,
               "the ID3v2 tag accounts for the whole size difference");
}

/**
 * @brief Runs the Blade encoder DLL tests.
 * @param argc  argument count.
 * @param argv  an optional path to lame_enc.dll, and --require to turn a
 *              missing DLL from a skip into a failure.
 * @return 0 on success or skip, non-zero when any check failed.
 */
int
main(int argc, char **argv)
{
    char    dll[MAX_PATH], dir[MAX_PATH];
    const char *given = NULL;
    int     require = 0;
    int     i, frames;
    HMODULE mod;
    blade_exports be;
    BE_VERSION ver;
    unsigned char *out;
    DWORD   len;
    char    detail[CTEST_DETAIL_CHARS];

    ctest_start("blade_test: the Blade-compatible encoder DLL");

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--require") == 0) {
            require = 1;
        } else {
            given = argv[i];
        }
    }

    if (given != NULL) {
        strncpy(dll, given, sizeof(dll) - 1);
        dll[sizeof(dll) - 1] = '\0';
    } else {
        char   *slash;

        if (GetModuleFileNameA(NULL, dll, MAX_PATH) == 0 ||
            (slash = strrchr(dll, '\\')) == NULL) {
            CHECK(0, "this executable's own directory could be determined");
            return ctest_summary("blade_test");
        }
        *(slash + 1) = '\0';
        strcat(dll, "lame_enc.dll");
    }
    printf("        dll: %s\n", dll);

    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        if (require) {
            CHECK(0, "the DLL was built and is where it was looked for");
            return ctest_summary("blade_test");
        }
        printf("blade_test: SKIPPED - no lame_enc.dll at that path, and it "
               "was not required.\n");
        printf("            The DLL is built by the vs_lame.slnx solution.\n");
        return 0;
    }

    mod = LoadLibraryA(dll);
    if (mod == NULL) {
        sprintf(detail, "Win32 error %lu", GetLastError());
        ctest_record(0, "the DLL image loads", detail);
        return ctest_summary("blade_test");
    }
    CHECK(mod != NULL, "the DLL image loads");
    if (!load_exports(mod, &be)) {
        return ctest_summary("blade_test");
    }

    /* The version report: the interface version is the Blade DLL's own, the
       engine version is the library's, and both are filled in. */
    memset(&ver, 0, sizeof(ver));
    be.version(&ver);
    printf("        lame_enc interface %u.%u, engine %u.%u\n",
           ver.byDLLMajorVersion, ver.byDLLMinorVersion,
           ver.byMajorVersion, ver.byMinorVersion);
    CHECK(ver.byDLLMajorVersion != 0, "the interface version is filled in");
    CHECK(ver.byMajorVersion >= 3, "the engine version is the library's");

    out = (unsigned char *) malloc(STREAM_ROOM);
    CHECK(out != NULL, "stream buffer allocated");
    if (out == NULL) {
        return ctest_summary("blade_test");
    }

    /* One second of CBR audio through the SHORT entry point, walked. The
       frame count follows from the rate and the duration, padded upward by
       the encoder's own delay and the final flush - together worth up to
       two extra frames on top of the partial last one. */
    len = encode_tone(&be, 0, out, STREAM_ROOM, "short samples");
    CHECK(len > 0, "the short-sample stream produced bytes");
    frames = walk_frames(out, len, "short-sample stream");
    sprintf(detail, "got %d frames for %.1f of audio",
            frames, mp3_frames_per_second(RATE) * SECONDS);
    ctest_record((double) frames >= mp3_frames_per_second(RATE) * SECONDS
                 && (double) frames <= mp3_frames_per_second(RATE) * SECONDS + 3.0,
                 "the stream is a second of frames", detail);

    /* The same second through the non-interleaved float entry point. */
    len = encode_tone(&be, 1, out, STREAM_ROOM, "float samples");
    CHECK(len > 0, "the float-sample stream produced bytes");
    walk_frames(out, len, "float-sample stream");
    free(out);

    /* The tag rewrite, over a bare file and over an ID3v2 tag. */
    GetTempPathA(MAX_PATH, dir);
    test_info_tag(&be, dir);

    FreeLibrary(mod);
    return ctest_summary("blade_test");
}
