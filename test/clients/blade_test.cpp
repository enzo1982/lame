/**
 * @file
 * @brief Tests for the Blade-compatible encoder DLL, lame_enc.dll.
 *
 * The smoke test checks that the DLL loads and exports the Blade entry
 * points. This test checks that they encode. It opens a stream, feeds it a
 * known sine, drains it and closes it. It uses the same calls as a Blade
 * host. Then it walks the output frame by frame and compares each frame with
 * the bitrate that the configuration asks for.
 *
 * The test loads the DLL at run time from the directory of this executable.
 * The DirectShow test loads lame.ax the same way. No import library is
 * involved. The test gets the exports by the undecorated names that the .def
 * file publishes. A Blade host binds to the DLL through the same names. The
 * main solution builds the DLL, and a different solution builds this test.
 * So a missing lame_enc.dll is a skip here, and a failure under --require.
 * lame_dshow_test follows the same rule.
 *
 * Two things are deliberately not covered:
 * - The behavior of the entry points on null arguments. It is not part of
 *   the Blade contract, and the DLL does not promise it.
 * - The failure paths of beWriteInfoTag. They show a message box, and no
 *   unattended test can click through one.
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
 * @brief Entry-point type for beFlushNoGap, which the header does not declare.
 *
 * BladeMP3EncDLL.h declares a pointer typedef for every other export. For
 * this export it defines only the name, TEXT_BEFLUSHNOGAP. It has no
 * BEFLUSHNOGAP. This is an upstream omission. This file declares the type
 * itself and does not edit the interface header, because Blade hosts keep
 * their own copies of that header. The signature is the signature of the
 * export.
 */
typedef BE_ERR (*BLADE_TEST_BEFLUSHNOGAP) (HBE_STREAM, PBYTE, PDWORD);

/** @brief Sample rate of every stream in this file. */
#define RATE        44100
/** @brief The constant bitrate that the tests ask for, in kbit/s. */
#define KBPS        128
/** @brief Channel count. Every stream here is stereo. */
#define CHANNELS    2
/** @brief Frequency of the test tone, in Hz. */
#define TONE_HZ     1000.0
/** @brief Amplitude of the test tone. It is loud, but far from clipping. */
#define TONE_PEAK   12000.0
/** @brief Seconds of audio each stream encodes. */
#define SECONDS     1
/** @brief Buffer size for one returned chunk. beInitStream reports a smaller size. */
#define OUT_ROOM    65536
/** @brief Buffer size for a whole encoded stream at these settings. */
#define STREAM_ROOM (KBPS * MP3_BITS_PER_KBIT / MP3_BITS_PER_BYTE * (SECONDS + 1))

/**
 * @brief Every Blade entry point, resolved from the loaded DLL.
 *
 * The typedefs and the export names come from the header of the DLL. The
 * one exception is the typedef for beFlushNoGap, which this file declares.
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
 * @brief Resolves the nine exports and checks each one by name.
 * @param mod  the loaded lame_enc.dll.
 * @param be   receives the entry points.
 * @return 1 when all nine entry points resolve, else 0.
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
 * @brief Fills in the Blade configuration that every stream here starts from.
 * @param cfg              receives the configuration.
 * @param write_vbr_header whether the stream reserves a frame for the LAME tag.
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
 * @brief Returns the value of one stereo sample pair of the test tone.
 * @param n  index of the sample pair from the start of the stream.
 * @return the sample value, identical on both channels.
 */
static SHORT
tone_sample(DWORD n)
{
    return ctest_tone(n, RATE, TONE_HZ, TONE_PEAK);
}

/**
 * @brief Encodes the test tone through the given chunk entry point.
 *
 * Opens a stream and feeds it exactly the chunk size that beInitStream
 * reports. Then drains the encoder and closes the stream. A Blade host
 * performs the same sequence. Both chunk entry points take samples on the
 * same 16-bit scale. So either one can produce the same stream.
 *
 * @param be         the resolved entry points.
 * @param use_float  feed beEncodeChunkFloatS16NI instead of beEncodeChunk.
 * @param out        receives the encoded stream.
 * @param out_room   the size of @a out, in bytes.
 * @param what       names the case in the description of each check.
 * @return bytes of encoded stream, or 0 when beInitStream fails.
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
 * @brief Returns the bitrate index that a frame header uses for a bitrate in
 *        kbit/s.
 * @param kbps  the bitrate to look up.
 * @return its index, or #MP3_BITRATE_INVALID when the table does not have it.
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
 * Every frame must begin with a sync and have the expected bitrate index.
 * The header of each frame sets where the next frame starts. So one wrong
 * bitrate, or a sample rate other than the one the walk assumes, loses the
 * sync and fails the check. The walk checks the sync, the bitrate and the
 * sample rate together.
 *
 * @param buf   the stream.
 * @param len   its length in bytes.
 * @param what  names the stream in the description of each check.
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

/** @brief Bytes in the header of the synthetic ID3v2 tag. */
#define ID3V2_HEADER_BYTES 10
/** @brief Padding inside the synthetic tag. It fits in one seven-bit size byte. */
#define ID3V2_PADDING      0x20

/**
 * @brief Writes @a len stream bytes to @a path, optionally after an ID3v2 tag.
 *
 * The tag is the smallest shape that the reader in the DLL accepts. It has
 * the identifier, the version and the flags, then the size in four seven-bit
 * bytes, then the padding.
 *
 * @param path        the file to create.
 * @param buf         the encoded stream.
 * @param len         its length in bytes.
 * @param with_id3v2  put the synthetic tag in front of the audio.
 * @return 1 when the whole file was written, else 0.
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
 * @brief Encodes with a reserved frame for the LAME tag, and has the DLL fill
 *        it in.
 *
 * This tests the beWriteInfoTag path. The test writes the stream to a file.
 * The tag write opens the file again and skips any ID3v2 tag in front of the
 * audio. Then it overwrites the reserved first frame in place. The test runs
 * two cases: a bare file, and a file with an ID3v2 tag in front. A file
 * without a tag takes the other branch of the skip. Only the pair shows that
 * the skip works.
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
 * @brief Checks that upsampled output stays inside the buffer size that
 *        beInitStream() returns.
 *
 * A 4 kHz stream resampled to 48 kHz returns twelve times as many frames per
 * chunk as a stream that is not resampled. The output buffer is exactly the
 * size that beInitStream() reports. Guard bytes follow it, and the DLL does
 * not know about them. After every chunk and after the final flush, each call
 * must succeed within the buffer. Every guard byte must be unchanged.
 *
 * @param be the resolved entry points.
 */
static void
test_upsampled_chunks_fit(const blade_exports *be)
{
    enum { GUARD = 65536, SOURCE_RATE = 4000, OUTPUT_RATE = 48000, CHUNKS = 20 };
    BE_CONFIG cfg;
    HBE_STREAM hbe = 0;
    DWORD   samples = 0, room = 0, written = 0, i, n, largest = 0;
    unsigned char *buf;
    SHORT  *pcm;
    unsigned s = 4711;
    int     calls_ok = 1, untouched = 1;
    char    detail[CTEST_DETAIL_CHARS];

    make_config(&cfg, 0);
    cfg.format.LHV1.dwSampleRate = SOURCE_RATE;
    cfg.format.LHV1.dwReSampleRate = OUTPUT_RATE;
    cfg.format.LHV1.dwBitrate = 320;
    if (be->init(&cfg, &samples, &room, &hbe) != BE_ERR_SUCCESSFUL) {
        CHECK(0, "a 4 kHz stream resampled to 48 kHz is accepted");
        return;
    }
    buf = (unsigned char *) malloc(room + GUARD);
    pcm = (SHORT *) malloc(samples * sizeof(SHORT));
    if (buf == NULL || pcm == NULL) {
        CHECK(0, "the upsampling test's buffers could be allocated");
        free(buf);
        free(pcm);
        be->close(hbe);
        return;
    }
    memset(buf + room, 0xA5, GUARD);
    for (n = 0; n < CHUNKS && calls_ok && untouched; n++) {
        for (i = 0; i < samples; i++) {
            s = s * 1103515245u + 12345u;
            pcm[i] = (SHORT) ((int) ((s >> 16) & 0xffff) - 32768);
        }
        written = 0;
        calls_ok = be->chunk(hbe, samples, pcm, buf, &written) == BE_ERR_SUCCESSFUL && written <= room;
        if (written > largest) {
            largest = written;
        }
        for (i = 0; i < GUARD; i++) {
            if (buf[room + i] != 0xA5) {
                untouched = 0;
                break;
            }
        }
    }
    written = 0;
    if (calls_ok && untouched) {
        calls_ok = be->deinit(hbe, buf, &written) == BE_ERR_SUCCESSFUL && written <= room;
        for (i = 0; i < GUARD; i++) {
            if (buf[room + i] != 0xA5) {
                untouched = 0;
                break;
            }
        }
    }
    printf("        upsampled 12x: advised %lu bytes, largest chunk %lu\n",
           (unsigned long) room, (unsigned long) largest);
    sprintf(detail, "advised %lu, largest %lu", (unsigned long) room, (unsigned long) largest);
    ctest_record(calls_ok, "every upsampled chunk and the flush succeed within the advised buffer", detail);
    CHECK(untouched, "nothing is written past the advised buffer");
    be->close(hbe);
    free(buf);
    free(pcm);
}

/**
 * @brief Encodes a short tone into a file with the given configuration, and
 *        leaves the stream open.
 * @param be          the resolved entry points.
 * @param cfg         the configuration of the stream.
 * @param path        the file to write.
 * @param hbe         receives the open stream.
 * @return 1 when the stream was opened and the file written, else 0.
 */
static int
encode_config_file(const blade_exports *be, BE_CONFIG *cfg, const char *path, HBE_STREAM *hbe)
{
    enum { CHUNKS = 8 };
    DWORD   samples = 0, room = 0, written = 0, total = 0, n, i;
    unsigned char *out;
    SHORT  *pcm;
    int     ok;

    if (be->init(cfg, &samples, &room, hbe) != BE_ERR_SUCCESSFUL) {
        return 0;
    }
    out = (unsigned char *) malloc(room * (CHUNKS + 1));
    pcm = (SHORT *) malloc(samples * sizeof(SHORT));
    ok = out != NULL && pcm != NULL;
    for (n = 0; ok && n < CHUNKS; n++) {
        for (i = 0; i < samples; i++) {
            pcm[i] = tone_sample((n * samples + i) / CHANNELS);
        }
        ok = be->chunk(*hbe, samples, pcm, out + total, &written) == BE_ERR_SUCCESSFUL;
        total += written;
    }
    ok = ok && be->deinit(*hbe, out + total, &written) == BE_ERR_SUCCESSFUL;
    total += written;
    ok = ok && write_stream(path, out, total, 0);
    free(out);
    free(pcm);
    return ok;
}

/**
 * @brief As encode_config_file(), with the configuration of make_config().
 * @param be          the resolved entry points.
 * @param with_tag    whether the stream reserves a frame for the LAME tag.
 * @param path        the file to write.
 * @param hbe         receives the open stream.
 * @return 1 when the stream was opened and the file written, else 0.
 */
static int
encode_short_file(const blade_exports *be, int with_tag, const char *path, HBE_STREAM *hbe)
{
    BE_CONFIG cfg;

    make_config(&cfg, with_tag);
    return encode_config_file(be, &cfg, path, hbe);
}

/**
 * @brief Checks the calls on a stream that the DLL has released. The DLL
 *        returns an error and never uses the stream.
 *
 * beWriteInfoTag() releases the stream that it writes the tag for.
 * beCloseStream() releases a stream without a tag. After the tag write, the
 * legacy beWriteVBRHeader() has no stream left to write.
 * beWriteInfoTag() on a stream that beCloseStream() released returns
 * BE_ERR_INVALID_HANDLE.
 *
 * @param be    the resolved entry points.
 * @param dir   directory for the scratch file, with a trailing separator.
 */
static void
test_released_stream(const blade_exports *be, const char *dir)
{
    HBE_STREAM hbe = 0;
    char    path[MAX_PATH];

    sprintf(path, "%slame_blade_test_released.mp3", dir);
    if (!encode_short_file(be, 1, path, &hbe)) {
        CHECK(0, "a short stream with a tag frame is encoded");
        return;
    }
    CHECK_EQ_U(be->close(hbe), BE_ERR_SUCCESSFUL, "a stream with a tag frame closes");
    CHECK_EQ_U(be->info_tag(hbe, path), BE_ERR_SUCCESSFUL, "its tag is written after the close");
    CHECK_EQ_U(be->vbr_header(path), BE_ERR_INVALID_FORMAT_PARAMETERS,
               "beWriteVBRHeader() then has no stream left to write");
    remove(path);

    if (!encode_short_file(be, 0, path, &hbe)) {
        CHECK(0, "a short stream without a tag frame is encoded");
        return;
    }
    CHECK_EQ_U(be->close(hbe), BE_ERR_SUCCESSFUL, "a stream without a tag frame closes");
    CHECK_EQ_U(be->info_tag(hbe, path), BE_ERR_INVALID_HANDLE,
               "beWriteInfoTag() on the closed stream answers BE_ERR_INVALID_HANDLE");
    remove(path);
}

/**
 * @brief Checks that the DLL rejects a VBR stream with a VBR method that it
 *        does not have. A stream with a method that it has is accepted.
 *
 * @param be the resolved entry points.
 */
static void
test_unknown_vbr_method_refused(const blade_exports *be)
{
    static const int methods[] = { VBR_METHOD_ABR + 1, VBR_METHOD_NONE - 1 };
    BE_CONFIG cfg;
    HBE_STREAM hbe = 0;
    DWORD   samples = 0, room = 0;
    size_t  i;
    char    detail[CTEST_DETAIL_CHARS];

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        make_config(&cfg, 0);
        cfg.format.LHV1.bEnableVBR = TRUE;
        cfg.format.LHV1.nVbrMethod = (VBRMETHOD) methods[i];
        sprintf(detail, "VBR method %d is refused", methods[i]);
        CHECK_EQ_U(be->init(&cfg, &samples, &room, &hbe), BE_ERR_INVALID_FORMAT_PARAMETERS, detail);
        be->close(hbe);
    }
    make_config(&cfg, 0);
    cfg.format.LHV1.bEnableVBR = TRUE;
    cfg.format.LHV1.nVbrMethod = VBR_METHOD_MTRH;
    CHECK_EQ_U(be->init(&cfg, &samples, &room, &hbe), BE_ERR_SUCCESSFUL,
               "VBR method MTRH is accepted");
    be->close(hbe);
}

/**
 * @brief Checks what an encode call reports when the encoder rejects its
 *        input: a floating point sample that is not finite.
 *
 * The encoder returns a negative error. The DLL reports every negative result
 * of an encode or flush call as BE_ERR_BUFFER_TOO_SMALL, with no bytes
 * written.
 *
 * @param be the resolved entry points.
 */
static void
test_rejected_input_reported(const blade_exports *be)
{
    BE_CONFIG cfg;
    HBE_STREAM hbe = 0;
    DWORD   samples = 0, room = 0, written = 1;
    FLOAT   pcm_l[MP3_SAMPLES_PER_FRAME], pcm_r[MP3_SAMPLES_PER_FRAME];
    unsigned char *out;
    int     i;

    make_config(&cfg, 0);
    if (be->init(&cfg, &samples, &room, &hbe) != BE_ERR_SUCCESSFUL
        || (out = (unsigned char *) malloc(room)) == NULL) {
        CHECK(0, "a stream for the rejected input opens");
        be->close(hbe);
        return;
    }
    for (i = 0; i < MP3_SAMPLES_PER_FRAME; i++) {
        pcm_l[i] = 0;
        pcm_r[i] = 0;
    }
    pcm_l[0] = (FLOAT) HUGE_VAL;
    CHECK_EQ_U(be->chunk_float(hbe, MP3_SAMPLES_PER_FRAME, pcm_l, pcm_r, out, &written),
               BE_ERR_BUFFER_TOO_SMALL,
               "a chunk with an infinite sample is reported as BE_ERR_BUFFER_TOO_SMALL");
    CHECK_EQ_U(written, 0, "and no bytes are reported written");
    free(out);
    be->close(hbe);
}

/**
 * @brief Reads a whole file into memory.
 * @param path  the file.
 * @param size  receives its length in bytes.
 * @return the contents, to be released with free(), or NULL when the file
 *         cannot be read.
 */
static unsigned char *
read_whole_file(const char *path, long *size)
{
    FILE   *fp = fopen(path, "rb");
    unsigned char *buf = NULL;

    *size = 0;
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) == 0 && (*size = ftell(fp)) > 0 && fseek(fp, 0, SEEK_SET) == 0
        && (buf = (unsigned char *) malloc((size_t) *size)) != NULL
        && fread(buf, 1, (size_t) *size, fp) != (size_t) *size) {
        free(buf);
        buf = NULL;
    }
    fclose(fp);
    return buf;
}

/**
 * @brief Checks the ABR preset at a bitrate above the highest one: the DLL
 *        encodes ABR at 320 kbit/s.
 *
 * The DLL passes the bitrate of the ABR preset to lame_set_preset(), where
 * values from 1000 up name other presets. A request for 1001 kbit/s must not
 * select the VBR preset that 1001 names. The LAME tag records the method.
 *
 * @param be    the resolved entry points.
 * @param dir   directory for the scratch file, with a trailing separator.
 */
static void
test_abr_preset_above_range(const blade_exports *be, const char *dir)
{
    BE_CONFIG cfg;
    HBE_STREAM hbe = 0;
    char    path[MAX_PATH];
    unsigned char *buf;
    long    size = 0;

    sprintf(path, "%slame_blade_test_abr.mp3", dir);
    make_config(&cfg, 1);
    cfg.format.LHV1.nPreset = LQP_ABR;
    cfg.format.LHV1.dwVbrAbr_bps = 1001000;
    if (!encode_config_file(be, &cfg, path, &hbe)) {
        CHECK(0, "a stream with the ABR preset at 1001 kbit/s is encoded");
        be->close(hbe);
        return;
    }
    CHECK_EQ_U(be->info_tag(hbe, path), BE_ERR_SUCCESSFUL, "its LAME tag is written");
    buf = read_whole_file(path, &size);
    if (buf != NULL && size > MP3_HEADER_BYTES && mp3_is_frame_sync(buf)) {
        long const first = mp3_frame_bytes(mp3_bitrate_index(buf), mp3_padding_bytes(buf), RATE);

        CHECK_EQ_U(mp3_lame_tag_vbr_method(buf, first < size ? first : size), MP3_TAG_METHOD_ABR,
                   "the ABR preset at 1001 kbit/s encodes ABR, not the preset that 1001 names");
    } else {
        CHECK(0, "the file with the LAME tag can be read");
    }
    free(buf);
    remove(path);
}

/**
 * @brief Checks that both ways of asking for ABR round the bitrate to the
 *        nearest kbit/s.
 *
 * 128600 bit/s is 128.6 kbit/s. The LAME tag records the ABR bitrate, and it
 * must be 129 through the ABR preset and through the ABR setting of the
 * configuration.
 *
 * @param be    the resolved entry points.
 * @param dir   directory for the scratch file, with a trailing separator.
 */
static void
test_abr_bitrate_rounds(const blade_exports *be, const char *dir)
{
    static const struct {
        int     preset;
        const char *what;
    } paths[] = {
        { LQP_ABR, "the ABR preset at 128600 bit/s records 129 kbit/s" },
        { LQP_NOPRESET, "the ABR setting at 128600 bit/s records 129 kbit/s" },
    };
    size_t  i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        BE_CONFIG cfg;
        HBE_STREAM hbe = 0;
        char    path[MAX_PATH];
        unsigned char *buf;
        long    size = 0;

        sprintf(path, "%slame_blade_test_abr_round.mp3", dir);
        make_config(&cfg, 1);
        cfg.format.LHV1.nPreset = paths[i].preset;
        cfg.format.LHV1.dwVbrAbr_bps = 128600;
        if (!encode_config_file(be, &cfg, path, &hbe)) {
            CHECK(0, "a stream at 128600 bit/s ABR is encoded");
            be->close(hbe);
            continue;
        }
        CHECK_EQ_U(be->info_tag(hbe, path), BE_ERR_SUCCESSFUL, "its LAME tag is written");
        buf = read_whole_file(path, &size);
        if (buf != NULL && size > MP3_HEADER_BYTES && mp3_is_frame_sync(buf)) {
            long const first = mp3_frame_bytes(mp3_bitrate_index(buf), mp3_padding_bytes(buf), RATE);

            CHECK_EQ_U(mp3_lame_tag_abr_kbps(buf, first < size ? first : size), 129, paths[i].what);
        } else {
            CHECK(0, "the file with the LAME tag can be read");
        }
        free(buf);
        remove(path);
    }
}

/**
 * @brief Runs the Blade encoder DLL tests.
 * @param argc  argument count.
 * @param argv  an optional path to lame_enc.dll, and an optional --require.
 *              With --require, a missing DLL is a failure, not a skip.
 * @return 0 on success or skip, non-zero when any check failed.
 */
int
main(int argc, char **argv)
{
    char    dll[MAX_PATH], dir[MAX_PATH];
    int     require;
    int     frames;
    ctest_component found;
    HMODULE mod;
    blade_exports be;
    BE_VERSION ver;
    unsigned char *out;
    DWORD   len;
    char    detail[CTEST_DETAIL_CHARS];

    ctest_start("blade_test: the Blade-compatible encoder DLL");

    found = ctest_component_path(argc, argv, "lame_enc.dll", dll, sizeof(dll), &require);
    if (found == CTEST_NO_PATH) {
        CHECK(0, "the path of the DLL could be formed");
        return ctest_summary("blade_test");
    }
    printf("        dll: %s\n", dll);

    if (found == CTEST_ABSENT) {
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

    test_upsampled_chunks_fit(&be);
    test_unknown_vbr_method_refused(&be);
    test_rejected_input_reported(&be);
    test_abr_preset_above_range(&be, dir);
    test_abr_bitrate_rounds(&be, dir);
    test_released_stream(&be, dir);

    FreeLibrary(mod);
    return ctest_summary("blade_test");
}
