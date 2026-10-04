/**
 * @file
 * @brief Reads MPEG audio frame headers for the Windows client tests.
 *
 * Each client test asks the same questions about the output of its
 * component. Is it a run of MPEG frames? How many frames are there? How many
 * distinct bitrates do they use? A byte total cannot tell. A low total can
 * mean a tail that was never flushed, a bitrate that was silently replaced,
 * or a variable bitrate. All three look the same by size. The frame headers
 * tell them apart.
 *
 * Every test reads the same fields. This file names them once, so the tests
 * do not repeat the shifts and masks over a byte index.
 */

#ifndef LAME_TEST_CLIENTS_MP3FRAME_H
#define LAME_TEST_CLIENTS_MP3FRAME_H

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

/** @brief Returns non-zero when @a h begins with a frame sync. */
static inline int
mp3_is_frame_sync(const unsigned char *h)
{
    return h[0] == MP3_SYNC_BYTE0
        && (h[MP3_HEADER_SYNC_BYTE] & MP3_SYNC_MASK1) == MP3_SYNC_MASK1;
}

/** @brief Returns the bitrate index of the frame at @a h. */
static inline int
mp3_bitrate_index(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_BITRATE_INDEX_SHIFT)
        & MP3_BITRATE_INDEX_MASK;
}

/** @brief Returns the padding of the frame at @a h, in bytes. */
static inline int
mp3_padding_bytes(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_PADDING_SHIFT) & MP3_PADDING_MASK;
}

/**
 * @brief Returns the length in bytes of a frame, padding included.
 *
 * @a index is the bitrate index, and @a rate is the sample rate in Hz. The
 * usual form of this formula writes the leading coefficient as 144. That is
 * the sample count of a frame divided by the bits in a byte. The code writes
 * it in that form, so the reader has nothing to look up.
 */
static inline int
mp3_frame_bytes(int index, int padding, unsigned long rate)
{
    return (MP3_SAMPLES_PER_FRAME / MP3_BITS_PER_BYTE)
        * mp3_bitrate_kbps[index] * MP3_BITS_PER_KBIT / (int) rate + padding;
}

/** @brief Returns the number of frames in one second of audio at @a rate. */
static inline double
mp3_frames_per_second(unsigned long rate)
{
    return (double) rate / (double) MP3_SAMPLES_PER_FRAME;
}

/** @brief The Xing or Info marker at the start of the Xing frame is four bytes. */
#define MP3_TAG_MARKER_BYTES        4
/**
 * @brief Offset of the LAME tag from the marker. The offset skips the marker,
 * the flags, the frame count, the byte count, the 100-entry TOC and the
 * quality word.
 */
#define MP3_TAG_LAME_OFFSET         (MP3_TAG_MARKER_BYTES + 4 + 4 + 4 + 100 + 4)
/** @brief The LAME tag starts with a nine-byte encoder string. */
#define MP3_TAG_ENCODER_BYTES       9
/** @brief The encoder string is followed by one byte: tag revision and VBR method. */
#define MP3_TAG_REVISION_BYTES      1
/** @brief Offset of the lowpass byte from the marker. */
#define MP3_TAG_LOWPASS_OFFSET \
    (MP3_TAG_LAME_OFFSET + MP3_TAG_ENCODER_BYTES + MP3_TAG_REVISION_BYTES)
/** @brief The lowpass byte counts in units of 100 Hz. Zero means no filter. */
#define MP3_TAG_LOWPASS_UNIT_HZ     100
/** @brief What mp3_lame_tag_lowpass_hz() returns when there is no tag. */
#define MP3_TAG_ABSENT              (-1)

/**
 * @brief Returns the lowpass frequency that the LAME tag of a stream records, in Hz.
 *
 * The LAME tag is in the first frame of the stream, the Xing frame. An Xing
 * or Info marker follows the header and the side information. The LAME tag
 * follows the TOC. The encoder writes the lowpass it applied into the LAME
 * tag, in units of 100 Hz, with zero for no filter. This function searches
 * the first frame for the marker. So it does not need to know the size of the
 * side information.
 *
 * @param buf    the stream.
 * @param frame  the length of its first frame, in bytes.
 * @return the lowpass in Hz, 0 for no filter, or @c MP3_TAG_ABSENT when the
 *         first frame has no LAME tag.
 */
static inline int
mp3_lame_tag_lowpass_hz(const unsigned char *buf, long frame)
{
    long off;

    for (off = MP3_HEADER_BYTES; off + MP3_TAG_LOWPASS_OFFSET < frame; off++) {
        if ((memcmp(buf + off, "Xing", MP3_TAG_MARKER_BYTES) == 0
             || memcmp(buf + off, "Info", MP3_TAG_MARKER_BYTES) == 0)
            && memcmp(buf + off + MP3_TAG_LAME_OFFSET, "LAME",
                      MP3_TAG_MARKER_BYTES) == 0) {
            return buf[off + MP3_TAG_LOWPASS_OFFSET] * MP3_TAG_LOWPASS_UNIT_HZ;
        }
    }
    return MP3_TAG_ABSENT;
}

#endif /* LAME_TEST_CLIENTS_MP3FRAME_H */
