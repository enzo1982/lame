/**
 * @file
 * @brief Reading MPEG audio frame headers, for the Windows client tests.
 *
 * Both client tests ask the same question about what their component produced:
 * is this a run of MPEG frames, how many are there, and at how many distinct
 * bitrates. A byte total cannot answer it - a total that comes out low may be a
 * tail that was never flushed, a bitrate that was silently substituted, or a
 * variable rate, and the three are indistinguishable by size. The frame headers
 * tell them apart.
 *
 * The fields are the same fields for both tests, so they are named once here
 * rather than written twice as shifts and masks over a byte index.
 */

#ifndef LAME_TEST_CLIENTS_MP3FRAME_H
#define LAME_TEST_CLIENTS_MP3FRAME_H

/** @brief Bytes of header the fields below are read from. */
#define MP3_HEADER_BYTES            4

/** @brief Header byte carrying the low sync bits, the version and the layer. */
#define MP3_HEADER_SYNC_BYTE        1
/** @brief Header byte carrying the bitrate index and the padding bit. */
#define MP3_HEADER_BITRATE_BYTE     2

/** @brief A frame sync is eleven set bits: all eight of byte 0 ... */
#define MP3_SYNC_BYTE0              0xFF
/** @brief ... and the top three of byte 1. */
#define MP3_SYNC_MASK1              0xE0

/** @brief The bitrate index occupies the top four bits of its byte. */
#define MP3_BITRATE_INDEX_SHIFT     4
/** @brief Mask for the four-bit bitrate index, once shifted down. */
#define MP3_BITRATE_INDEX_MASK      0x0F
/** @brief The padding bit sits one place above the private bit. */
#define MP3_PADDING_SHIFT           1
/** @brief Mask for the one-bit padding flag, once shifted down. */
#define MP3_PADDING_MASK            1

/** @brief Bitrate index 0 means the rate is not in the table (free format). */
#define MP3_BITRATE_FREE_FORMAT     0
/** @brief Bitrate index 15 is reserved and never valid in a frame. */
#define MP3_BITRATE_INVALID         15
/** @brief One entry per value the four-bit index can take. */
#define MP3_BITRATE_INDEX_COUNT     16

/** @brief Samples one MPEG-1 Layer III frame carries. */
#define MP3_SAMPLES_PER_FRAME       1152
/** @brief The frame length is bytes; the bitrate is bits. */
#define MP3_BITS_PER_BYTE           8
/** @brief The bitrate table is in kbit/s. */
#define MP3_BITS_PER_KBIT           1000

/**
 * @brief Bitrates in kbit/s by index, MPEG-1 Layer III.
 *
 * Index 0 is free format and carries no rate, which is why the entry is zero
 * rather than absent; index 15 is reserved and has no entry at all.
 */
static const int mp3_bitrate_kbps[MP3_BITRATE_INVALID] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
};

/** @brief Whether @a h begins with a frame sync. */
static int
mp3_is_frame_sync(const unsigned char *h)
{
    return h[0] == MP3_SYNC_BYTE0
        && (h[MP3_HEADER_SYNC_BYTE] & MP3_SYNC_MASK1) == MP3_SYNC_MASK1;
}

/** @brief The bitrate index of the frame at @a h. */
static int
mp3_bitrate_index(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_BITRATE_INDEX_SHIFT)
        & MP3_BITRATE_INDEX_MASK;
}

/** @brief The padding bit of the frame at @a h, in bytes. */
static int
mp3_padding_bytes(const unsigned char *h)
{
    return (h[MP3_HEADER_BITRATE_BYTE] >> MP3_PADDING_SHIFT) & MP3_PADDING_MASK;
}

/**
 * @brief Length in bytes of a frame at @a index and @a rate, padding included.
 *
 * The usual form of this writes the leading coefficient as 144, which is the
 * frame's sample count divided by the bits in a byte; spelling it that way
 * leaves nothing to look up.
 */
static int
mp3_frame_bytes(int index, int padding, unsigned long rate)
{
    return (MP3_SAMPLES_PER_FRAME / MP3_BITS_PER_BYTE)
        * mp3_bitrate_kbps[index] * MP3_BITS_PER_KBIT / (int) rate + padding;
}

/** @brief Frames one second of audio at @a rate is carried in. */
static double
mp3_frames_per_second(unsigned long rate)
{
    return (double) rate / (double) MP3_SAMPLES_PER_FRAME;
}

#endif /* LAME_TEST_CLIENTS_MP3FRAME_H */
