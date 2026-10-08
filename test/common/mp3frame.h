/**
 * @file
 * @brief Reads MPEG audio frame headers for the unit tests and the Windows
 *        client tests.
 *
 * Each test asks the same questions about the output of its
 * component or call. Is it a run of MPEG frames? How many frames are there? How many
 * distinct bitrates do they use? A byte total cannot tell. A low total can
 * mean a tail that was never flushed, a bitrate that was silently replaced,
 * or a variable bitrate. All three look the same by size. The frame headers
 * tell them apart.
 *
 * Every test reads the same fields. This file names them once, so the tests
 * do not repeat the shifts and masks over a byte index.
 */

#ifndef LAME_TEST_COMMON_MP3FRAME_H
#define LAME_TEST_COMMON_MP3FRAME_H

#include <string.h>

/** @brief Number of header bytes that the fields below are read from. */
#define MP3_HEADER_BYTES            4

/** @brief Header byte with the low sync bits, the version and the layer. */
#define MP3_HEADER_SYNC_BYTE        1
/** @brief Header byte with the bitrate index and the padding bit. */
#define MP3_HEADER_BITRATE_BYTE     2

/** @brief A frame sync is eleven set bits: all eight bits of byte 0 ... */
#define MP3_SYNC_BYTE0              0xFF
/** @brief ... and the top three bits of byte 1. */
#define MP3_SYNC_MASK1              0xE0

/** @brief The bitrate index is in the top four bits of its byte. */
#define MP3_BITRATE_INDEX_SHIFT     4
/** @brief Mask for the four-bit bitrate index, after the shift. */
#define MP3_BITRATE_INDEX_MASK      0x0F
/** @brief The padding bit is one place above the private bit. */
#define MP3_PADDING_SHIFT           1
/** @brief Mask for the one-bit padding flag, after the shift. */
#define MP3_PADDING_MASK            1

/** @brief Bitrate index 0 means free format. The bitrate is not in the table. */
#define MP3_BITRATE_FREE_FORMAT     0
/** @brief Bitrate index 15 is reserved. It is never valid in a frame. */
#define MP3_BITRATE_INVALID         15
/** @brief One entry for each value of the four-bit index. */
#define MP3_BITRATE_INDEX_COUNT     16

/** @brief Samples per channel in one MPEG-1 Layer III frame. */
#define MP3_SAMPLES_PER_FRAME       1152
/** @brief The frame length is in bytes, and the bitrate is in bits. */
#define MP3_BITS_PER_BYTE           8
/** @brief The bitrate table is in kbit/s. */
#define MP3_BITS_PER_KBIT           1000

/**
 * @brief Bitrates in kbit/s by index, MPEG-1 Layer III.
 *
 * Index 0 is free format and has no bitrate. Its entry is zero, so that each
 * index is also the position of its entry. Index 15 is reserved and has no
 * entry.
 */
static const int mp3_bitrate_kbps[MP3_BITRATE_INVALID] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
};

/**
 * @brief Returns whether the bytes at @a h begin with a frame sync.
 * @param h the bytes, at least two of them.
 * @return non-zero for a frame sync, 0 otherwise.
 */
static inline int
mp3_is_frame_sync(const unsigned char *h)
{
    return h[0] == MP3_SYNC_BYTE0
        && (h[MP3_HEADER_SYNC_BYTE] & MP3_SYNC_MASK1) == MP3_SYNC_MASK1;
}

/**
 * @brief Returns the bitrate index of the frame at @a h.
 * @param h the frame header.
 * @return the four-bit index, 0 to 15.
 */
static inline int
mp3_bitrate_index(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_BITRATE_INDEX_SHIFT)
        & MP3_BITRATE_INDEX_MASK;
}

/**
 * @brief Returns the padding of the frame at @a h, in bytes.
 * @param h the frame header.
 * @return 1 when the frame has a padding byte, 0 otherwise.
 */
static inline int
mp3_padding_bytes(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_PADDING_SHIFT) & MP3_PADDING_MASK;
}

/** @brief The version bits of an MPEG-1 frame, in the second header byte. */
#define MP3_VERSION_MPEG1           0x18
/** @brief Mask for the two version bits in the second header byte. */
#define MP3_VERSION_MASK            0x18
/** @brief A clear protection bit means that a 2-byte CRC follows the header. */
#define MP3_PROTECTION_MASK         0x01
/** @brief Length of the CRC after the header, when the frame has one. */
#define MP3_CRC_BYTES               2

/** @brief Byte of the header that holds the channel mode. */
#define MP3_HEADER_MODE_BYTE        3
/** @brief The channel mode is in the top two bits of that byte. */
#define MP3_MODE_SHIFT              6
/** @brief Mask for the two channel mode bits, after the shift. */
#define MP3_MODE_MASK               3
/** @brief Channel mode values in the header: stereo, joint stereo, dual channel, mono. */
#define MP3_MODE_STEREO             0
#define MP3_MODE_JOINT_STEREO       1
#define MP3_MODE_DUAL_CHANNEL       2
#define MP3_MODE_MONO               3

/**
 * @brief Returns the channel mode of the frame at @a h.
 * @param h the frame header.
 * @return one of ::MP3_MODE_STEREO, ::MP3_MODE_JOINT_STEREO,
 *         ::MP3_MODE_DUAL_CHANNEL and ::MP3_MODE_MONO.
 */
static inline int
mp3_channel_mode(const unsigned char *h)
{
    return (h[MP3_HEADER_MODE_BYTE] >> MP3_MODE_SHIFT) & MP3_MODE_MASK;
}

/**
 * @brief Returns the main_data_begin field of the Layer III frame at @a h.
 *
 * The field is the first field of the side information, after the header and
 * the CRC, if the frame has one. It is 9 bits long in an MPEG-1 frame and 8
 * bits long in an MPEG-2 or MPEG-2.5 frame.
 *
 * @param h the frame. The header, the CRC if there is one, and the first two
 *          bytes of the side information must be readable.
 * @return the number of bytes of this frame's data that are in earlier
 *         frames. 0 when the frame uses no bit reservoir.
 */
static inline int
mp3_main_data_begin(const unsigned char *h)
{
    const unsigned char *s = h + MP3_HEADER_BYTES
        + ((h[MP3_HEADER_SYNC_BYTE] & MP3_PROTECTION_MASK) ? 0 : MP3_CRC_BYTES);

    if ((h[MP3_HEADER_SYNC_BYTE] & MP3_VERSION_MASK) == MP3_VERSION_MPEG1)
        return (s[0] << 1) | (s[1] >> 7);
    return s[0];
}

/**
 * @brief Returns the length in bytes of an MPEG-1 frame, padding included.
 *
 * The usual form of this formula writes the leading coefficient as 144. That
 * is the sample count of a frame divided by the bits in a byte. The code
 * writes it in that form, so the reader has nothing to look up.
 *
 * @param index    the bitrate index, neither free format nor reserved.
 * @param padding  the padding in bytes.
 * @param rate     the sample rate in Hz.
 * @return the length in bytes.
 */
static inline int
mp3_frame_bytes(int index, int padding, unsigned long rate)
{
    return (MP3_SAMPLES_PER_FRAME / MP3_BITS_PER_BYTE)
        * mp3_bitrate_kbps[index] * MP3_BITS_PER_KBIT / (int) rate + padding;
}

/**
 * @brief Returns the number of MPEG-1 frames in one second of audio.
 * @param rate the sample rate in Hz.
 * @return the frames per second.
 */
static inline double
mp3_frames_per_second(unsigned long rate)
{
    return (double) rate / (double) MP3_SAMPLES_PER_FRAME;
}

/** @brief What mp3_scan_frames() found in a stream. */
typedef struct {
    int frames;         /**< the MPEG-1 frames in the run */
    int distinct;       /**< the distinct bitrates among them */
    int sole_kbps;      /**< the bitrate when there is one, in kbit/s, else 0 */
    long first_off;     /**< the offset of the first frame */
    long first_len;     /**< the length of the first frame, 0 when there is none */
} mp3_scan;

/**
 * @brief Counts the MPEG-1 frames in a stream, and the distinct bitrates.
 *
 * The scan skips bytes up to the first frame sync. From there it steps from
 * frame to frame by the length that each header gives. It stops at the end of
 * the stream, at a free format or reserved bitrate, or at a frame with no
 * length.
 *
 * @param buf   the stream.
 * @param len   its length, in bytes.
 * @param rate  the sample rate of the stream, in Hz.
 * @param s     receives the counts.
 * @return the number of frames, as in @c s->frames.
 */
static inline int
mp3_scan_frames(const unsigned char *buf, long len, unsigned long rate, mp3_scan *s)
{
    int seen_rate[MP3_BITRATE_INDEX_COUNT];
    long off = 0;

    memset(seen_rate, 0, sizeof(seen_rate));
    memset(s, 0, sizeof(*s));
    while (off + MP3_HEADER_BYTES <= len) {
        const unsigned char *h = buf + off;
        int index, framelen;

        if (!mp3_is_frame_sync(h)) {
            ++off;
            continue;
        }
        index = mp3_bitrate_index(h);
        if (index == MP3_BITRATE_FREE_FORMAT || index == MP3_BITRATE_INVALID) {
            break;
        }
        framelen = mp3_frame_bytes(index, mp3_padding_bytes(h), rate);
        if (framelen <= 0) {
            break;
        }
        if (!seen_rate[index]) {
            seen_rate[index] = 1;
            ++s->distinct;
            s->sole_kbps = mp3_bitrate_kbps[index];
        }
        if (s->frames == 0) {
            s->first_off = off;
            s->first_len = framelen;
        }
        ++s->frames;
        off += framelen;
    }
    if (s->distinct != 1) {
        s->sole_kbps = 0;
    }
    return s->frames;
}

/** @brief The version bits of an MPEG-2 frame, in the second header byte. */
#define MP3_VERSION_MPEG2           0x10
/** @brief The version bits of an MPEG-2.5 frame, in the second header byte. */
#define MP3_VERSION_MPEG25          0x00
/** @brief An MPEG-2 or MPEG-2.5 Layer III frame holds 576 samples. */
#define MP3_SAMPLES_PER_FRAME_LSF   576
/** @brief The sample rate bits are bits 2 and 3 of the third header byte. */
#define MP3_SAMPLE_RATE_SHIFT       2
/** @brief Mask for the two sample rate bits, after the shift. */
#define MP3_SAMPLE_RATE_MASK        3
/** @brief The sample rate value that is reserved in every version. */
#define MP3_SAMPLE_RATE_RESERVED    3

/** @brief Bitrates in kbit/s by index, MPEG-2 and MPEG-2.5 Layer III. */
static const int mp3_lsf_bitrate_kbps[MP3_BITRATE_INVALID] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
};

/**
 * @brief Returns the sample rate of the frame at @a h.
 * @param h the frame header.
 * @return the sample rate in Hz. 0 when the version or the sample rate bits
 *         hold a reserved value.
 */
static inline unsigned long
mp3_sample_rate(const unsigned char *h)
{
    static const unsigned long mpeg1[] = { 44100, 48000, 32000 };
    static const unsigned long mpeg2[] = { 22050, 24000, 16000 };
    static const unsigned long mpeg25[] = { 11025, 12000, 8000 };
    const int index = (h[MP3_HEADER_BITRATE_BYTE] >> MP3_SAMPLE_RATE_SHIFT) & MP3_SAMPLE_RATE_MASK;

    if (index == MP3_SAMPLE_RATE_RESERVED)
        return 0;
    switch (h[MP3_HEADER_SYNC_BYTE] & MP3_VERSION_MASK) {
    case MP3_VERSION_MPEG1:
        return mpeg1[index];
    case MP3_VERSION_MPEG2:
        return mpeg2[index];
    case MP3_VERSION_MPEG25:
        return mpeg25[index];
    default:
        return 0;
    }
}

/**
 * @brief Returns the number of samples in the frame at @a h.
 * @param h the frame header.
 * @return 1152 for an MPEG-1 frame, 576 for an MPEG-2 or MPEG-2.5 frame.
 */
static inline int
mp3_frame_samples(const unsigned char *h)
{
    if ((h[MP3_HEADER_SYNC_BYTE] & MP3_VERSION_MASK) == MP3_VERSION_MPEG1)
        return MP3_SAMPLES_PER_FRAME;
    return MP3_SAMPLES_PER_FRAME_LSF;
}

/**
 * @brief Returns the bitrate of the frame at @a h, from the table of its MPEG
 *        version.
 * @param h the frame header.
 * @return the bitrate in kbit/s. 0 for a free format frame and for the
 *         reserved bitrate index.
 */
static inline int
mp3_frame_kbps(const unsigned char *h)
{
    const int index = mp3_bitrate_index(h);

    if (index == MP3_BITRATE_INVALID)
        return 0;
    if ((h[MP3_HEADER_SYNC_BYTE] & MP3_VERSION_MASK) == MP3_VERSION_MPEG1)
        return mp3_bitrate_kbps[index];
    return mp3_lsf_bitrate_kbps[index];
}

/**
 * @brief Returns the length in bytes of the frame at @a h, padding included,
 *        for any MPEG version.
 * @param h the frame header.
 * @return the length. 0 when the header has free format, a reserved bitrate
 *         or a reserved sample rate.
 */
static inline int
mp3_frame_length(const unsigned char *h)
{
    const unsigned long rate = mp3_sample_rate(h);
    const int kbps = mp3_frame_kbps(h);

    if (rate == 0 || kbps == 0)
        return 0;
    return (mp3_frame_samples(h) / MP3_BITS_PER_BYTE) * kbps * MP3_BITS_PER_KBIT / (int) rate
        + mp3_padding_bytes(h);
}

/** @brief The layer bits of the header byte with the version, after the mask. */
#define MP3_LAYER_MASK              0x06
/** @brief The layer bits of a Layer III frame. */
#define MP3_LAYER_III               0x02

/**
 * @brief Returns whether the frame at @a h is a Layer III frame.
 * @param h the frame header.
 * @return non-zero for Layer III, 0 for the other layers.
 */
static inline int
mp3_is_layer3(const unsigned char *h)
{
    return (h[MP3_HEADER_SYNC_BYTE] & MP3_LAYER_MASK) == MP3_LAYER_III;
}

/** @brief The Xing or Info marker at the start of the Xing frame is four bytes. */
#define MP3_TAG_MARKER_BYTES        4
/** @brief The flags word after the marker says which of four fields follow. */
#define MP3_XING_FLAGS_BYTES        4
/** @brief Flag: a 4-byte frame count follows. */
#define MP3_XING_FRAMES             0x1
/** @brief Flag: a 4-byte byte count follows. */
#define MP3_XING_BYTES              0x2
/** @brief Flag: a 100-entry seek table follows. */
#define MP3_XING_TOC                0x4
/** @brief Flag: a 4-byte quality word follows. */
#define MP3_XING_QUALITY            0x8
/** @brief The size of the frame count, the byte count and the quality word. */
#define MP3_XING_FIELD_BYTES        4
/** @brief The size of the seek table. */
#define MP3_XING_TOC_BYTES          100

/** @brief Offset of the revision and VBR method byte in the LAME tag, after the
    nine-byte encoder string. The method is in its low 4 bits. */
#define MP3_TAG_METHOD_OFFSET       9
#define MP3_TAG_METHOD_MASK         0x0F
/** @brief Offset of the lowpass byte in the LAME tag. */
#define MP3_TAG_LOWPASS_OFFSET      10
/** @brief The lowpass byte counts in units of 100 Hz. Zero means no filter. */
#define MP3_TAG_LOWPASS_UNIT_HZ     100
/** @brief Offset of the ABR bitrate byte in the LAME tag. It follows the
    lowpass byte, the peak amplitude (4 bytes), the two replay gains (2 bytes
    each) and the encoding flags (1 byte). */
#define MP3_TAG_ABR_OFFSET          (MP3_TAG_LOWPASS_OFFSET + 1 + 4 + 2 + 2 + 1)
/** @brief Offset of the three bytes with the encoder delay and the padding. */
#define MP3_TAG_DELAY_OFFSET        (MP3_TAG_ABR_OFFSET + 1)
/** @brief The delay and the padding take 12 bits each, in 3 bytes. */
#define MP3_TAG_DELAY_BYTES         3
/** @brief What the LAME tag readers return when there is no tag. */
#define MP3_TAG_ABSENT              (-1)
/** @brief The VBR method values of the LAME tag for ABR and for the default
    VBR mode. */
#define MP3_TAG_METHOD_ABR          2
#define MP3_TAG_METHOD_VBR_MTRH     4

/**
 * @brief Returns where the LAME tag is in the first frame of a stream.
 *
 * The LAME tag is in the first frame of the stream, the Xing frame. An Xing
 * or Info marker follows the header and the side information. The flags after
 * the marker say which of the frame count, the byte count, the seek table and
 * the quality word follow; the LAME tag comes after them. This function
 * searches the first frame for the marker, so it does not need to know the
 * size of the side information.
 *
 * @param buf    the stream.
 * @param frame  the length of its first frame, in bytes.
 * @return the offset of the LAME tag, or -1 when the first frame has no LAME
 *         tag whose fields up to the delay and the padding fit into it.
 */
static inline long
mp3_lame_tag_at(const unsigned char *buf, long frame)
{
    long off;

    for (off = MP3_HEADER_BYTES; off + MP3_TAG_MARKER_BYTES + MP3_XING_FLAGS_BYTES <= frame; off++) {
        if (memcmp(buf + off, "Xing", MP3_TAG_MARKER_BYTES) == 0
            || memcmp(buf + off, "Info", MP3_TAG_MARKER_BYTES) == 0) {
            const unsigned char *f = buf + off + MP3_TAG_MARKER_BYTES;
            unsigned long const flags = ((unsigned long) f[0] << 24) | ((unsigned long) f[1] << 16)
                | ((unsigned long) f[2] << 8) | f[3];
            long at = off + MP3_TAG_MARKER_BYTES + MP3_XING_FLAGS_BYTES;

            if (flags & MP3_XING_FRAMES)
                at += MP3_XING_FIELD_BYTES;
            if (flags & MP3_XING_BYTES)
                at += MP3_XING_FIELD_BYTES;
            if (flags & MP3_XING_TOC)
                at += MP3_XING_TOC_BYTES;
            if (flags & MP3_XING_QUALITY)
                at += MP3_XING_FIELD_BYTES;
            if (at + MP3_TAG_DELAY_OFFSET + MP3_TAG_DELAY_BYTES <= frame
                && memcmp(buf + at, "LAME", MP3_TAG_MARKER_BYTES) == 0)
                return at;
        }
    }
    return -1;
}

/**
 * @brief Returns the lowpass frequency that the LAME tag of a stream records, in Hz.
 *
 * The encoder writes the lowpass it applied into the LAME tag, in units of
 * 100 Hz, with zero for no filter.
 *
 * @param buf    the stream.
 * @param frame  the length of its first frame, in bytes.
 * @return the lowpass in Hz, 0 for no filter, or @c MP3_TAG_ABSENT when the
 *         first frame has no LAME tag.
 */
static inline int
mp3_lame_tag_lowpass_hz(const unsigned char *buf, long frame)
{
    long const off = mp3_lame_tag_at(buf, frame);

    if (off < 0)
        return MP3_TAG_ABSENT;
    return buf[off + MP3_TAG_LOWPASS_OFFSET] * MP3_TAG_LOWPASS_UNIT_HZ;
}

/**
 * @brief Returns the VBR method from a stream's LAME tag.
 * @param buf    the stream.
 * @param frame  the length of its first frame, in bytes.
 * @return the method, for example ::MP3_TAG_METHOD_ABR, or @c MP3_TAG_ABSENT
 *         when the first frame has no LAME tag.
 */
static inline int
mp3_lame_tag_vbr_method(const unsigned char *buf, long frame)
{
    long const off = mp3_lame_tag_at(buf, frame);

    if (off < 0)
        return MP3_TAG_ABSENT;
    return buf[off + MP3_TAG_METHOD_OFFSET] & MP3_TAG_METHOD_MASK;
}

/**
 * @brief Returns the ABR bitrate from a stream's LAME tag, in kbit/s.
 * @param buf    the stream.
 * @param frame  the length of its first frame, in bytes.
 * @return the bitrate, 255 for 255 kbit/s and above, or @c MP3_TAG_ABSENT
 *         when the first frame has no LAME tag.
 */
static inline int
mp3_lame_tag_abr_kbps(const unsigned char *buf, long frame)
{
    long const off = mp3_lame_tag_at(buf, frame);

    if (off < 0)
        return MP3_TAG_ABSENT;
    return buf[off + MP3_TAG_ABR_OFFSET];
}

/**
 * @brief Reads the encoder delay and the padding from a stream's LAME tag.
 * @param buf      the stream.
 * @param frame    the length of its first frame, in bytes.
 * @param delay    receives the samples that the encoder added at the start.
 * @param padding  receives the samples that it added at the end.
 * @return 0, or @c MP3_TAG_ABSENT when the first frame has no LAME tag.
 */
static inline int
mp3_lame_tag_delay_padding(const unsigned char *buf, long frame, int *delay, int *padding)
{
    long const off = mp3_lame_tag_at(buf, frame);
    const unsigned char *d;

    if (off < 0)
        return MP3_TAG_ABSENT;
    d = buf + off + MP3_TAG_DELAY_OFFSET;
    *delay = (d[0] << 4) | (d[1] >> 4);
    *padding = ((d[1] & 0x0f) << 8) | d[2];
    return 0;
}

#endif /* LAME_TEST_COMMON_MP3FRAME_H */
