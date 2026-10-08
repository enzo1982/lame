/*
 *      Finding the LAME tag frame of a written MP3 stream
 *
 *      Copyright (c) 2026 The LAME project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.     See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * \file
 * \internal
 * \brief The byte tests that lame_enc.dll and the DirectShow filter use to find
 *        the LAME tag frame again before they rewrite it.
 *
 * The LAME tag frame is the first frame of the stream, after an ID3v2 tag if
 * there is one. Each caller reads the bytes from its own kind of stream and
 * passes them here.
 */

#ifndef LAME_LAMETAG_SCAN_H
#define LAME_LAMETAG_SCAN_H

#include <string.h>

/** The length of an ID3v2 tag header, in bytes. */
#define ID3V2_HEADER_BYTES 10

/**
 * \internal
 * \brief Returns non-zero when two bytes begin an MPEG audio frame: the 11
 *        bits of the frame sync are set.
 * \param b  the first two bytes of the frame.
 * \return non-zero for a frame sync, 0 otherwise.
 */
static int
lametag_is_frame_sync(const unsigned char b[2])
{
    return b[0] == 0xffu && (b[1] & 0xE0u) == 0xE0u;
}

/**
 * \internal
 * \brief Returns where the audio of a stream begins, from its first 10 bytes.
 *
 * An ID3v2 tag stores its size, without the 10-byte header, in four bytes of
 * 7 bits each. So the result fits in 28 bits plus the header.
 *
 * \param hdr  the first 10 bytes of the stream.
 * \return the size of the ID3v2 tag, header included. 0 when the stream does
 *         not begin with an ID3v2 tag.
 */
static long
lametag_audio_offset(const unsigned char hdr[ID3V2_HEADER_BYTES])
{
    if (memcmp(hdr, "ID3", 3) != 0) {
        return 0;
    }
    return (((long) (hdr[6] & 0x7f) << 21)
            | ((long) (hdr[7] & 0x7f) << 14)
            | ((long) (hdr[8] & 0x7f) << 7)
            | (long) (hdr[9] & 0x7f))
        + ID3V2_HEADER_BYTES;
}

#endif /* LAME_LAMETAG_SCAN_H */
