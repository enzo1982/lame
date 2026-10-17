/*
 *    Get Audio routines include file
 *
 *    Copyright (c) 1999 Albert L Faber
 *                  2010 Robert Hegemann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef LAME_GET_AUDIO_H
#define LAME_GET_AUDIO_H
#include "lame.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sound_file_format_e {
    sf_unknown,
    sf_raw,
    sf_wave,
    sf_aiff,
    sf_mp1,                  /* MPEG Layer 1, aka mpg */
    sf_mp2,                  /* MPEG Layer 2 */
    sf_mp3,                  /* MPEG Layer 3 */
    sf_mp123,                /* MPEG Layer 1,2 or 3; whatever .mp3, .mp2, .mp1 or .mpg contains */
    sf_ogg
} sound_file_format;

/** The number of samples per channel in the frame buffers of the readers. */
#define FRAME_BUFFER_SAMPLES 1152
/** The number of samples of an input whose length is not known. */
#define NUM_SAMPLES_UNKNOWN 0xFFFFFFFF

int     is_mpeg_file_format( int input_file_format );

/** @internal @brief An open input file, from init_infile(). */
typedef struct audio_reader audio_reader;

audio_reader *init_infile(lame_t gfp, char const * inPath);
int     samples_to_skip_at_start(audio_reader const * reader);
int     samples_to_skip_at_end(audio_reader const * reader);
unsigned long samples_above_full_scale(audio_reader const * reader);
int     input_ends_inside_a_frame(audio_reader const * reader);
int     input_shorter_than_its_tag(audio_reader const * reader);
int     input_is_float(audio_reader const * reader);
void    close_infile(audio_reader * reader);
int     get_audio(audio_reader * reader, lame_t gfp, int buffer[2][FRAME_BUFFER_SAMPLES]);
int     get_audio16(audio_reader * reader, lame_t gfp, short buffer[2][FRAME_BUFFER_SAMPLES]);
int     get_audio_float(audio_reader * reader, lame_t gfp, float buffer[2][FRAME_BUFFER_SAMPLES]);
hip_t   get_hip(audio_reader const * reader);
size_t  sizeOfOldTag(audio_reader const * reader);
unsigned char *getOldTag(audio_reader const * reader);

/** The data size a WAV header gets while the length of the data is not known. */
#define WAV_DATA_SIZE_UNKNOWN 0x7FFFFFFFu
/** The largest data size a WAV header gets. Larger data keeps this size. */
#define WAV_DATA_SIZE_MAX 0xFFFFFFD0u

FILE   *init_outfile(char const *outPath, int decode);
unsigned int wav_data_size(double frames, int bytes_per_frame);
int     WriteWaveHeader(FILE * const fp, unsigned int pcmbytes, int freq, int channels, int bits);
int     put_audio16(FILE* outf, short Buffer[2][FRAME_BUFFER_SAMPLES], int iread, int nch);



#ifdef __cplusplus
}
#endif

#endif /* ifndef LAME_GET_AUDIO_H */
