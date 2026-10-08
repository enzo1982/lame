/*
 *      Get Audio routines source file
 *
 *      Copyright (c) 1999 Albert L Faber
 *                    2008-2017 Robert Hegemann
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

/* $Id$ */


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#include <stdarg.h>
#include <stdio.h>

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <string.h>
#else
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char   *strchr(), *strrchr();
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#else
# ifdef HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#ifdef HAVE_MPG123
/**
 * @internal
 * The reader uses the decoder struct of libmp3lame (mpstr_tag from
 * mpglib/mpglib.h) as the hip_t of lame.h. That struct is not part of the
 * interface of libmp3lame. The lame program therefore needs the libmp3lame
 * of the same build.
 */
#define hip_global_struct mpstr_tag
#endif
#ifdef HAVE_MPG123
#include <mpg123.h>
/* for mpstr_tag */
#include "mpglib/mpglib.h"

#endif


#include <math.h>

#if defined(__riscos__)
# include <kernel.h>
# include <sys/swis.h>
#elif defined(_WIN32)
# include <io.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <windows.h>
#else
# include <sys/stat.h>
#endif

#ifdef __sun__
/* woraround for SunOS 4.x, it has SEEK_* defined here */
#include <unistd.h>
#endif

#include "lame.h"
#include "main.h"
#include "get_audio.h"
#include "lametime.h"
#include "console.h"
#include "machine.h"
#include "encoder.h"
#include "lame-analysis.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/** The delay of the layer III decoder of libmpg123, in samples. */
#define MP3_DECODER_DELAY 529

#define UNSIGNED_TO_FLOAT(u) (((double)((long)((u) - 2147483647L - 1))) + 2147483648.0)

static void reader_error(const char *format, ...) CONSOLE_PRINTF(1, 2);

/**
 * @internal
 * @brief Prints an error message of the input reader, unless the user asked
 *        for no messages.
 *
 * @param format  the printf() format of the message.
 * @param ...     the values for @p format.
 */
static void
reader_error(const char *format, ...)
{
    va_list args;

    if (global_ui_config.silent < 10) {
        va_start(args, format);
        frontend_errorf(format, args);
        va_end(args);
    }
}

static uint32_t uint32_high_low(unsigned char const *bytes)
{
    uint32_t const hh = bytes[0];
    uint32_t const hl = bytes[1];
    uint32_t const lh = bytes[2];
    uint32_t const ll = bytes[3];
    return (hh << 24) | (hl << 16) | (lh << 8) | ll;
}

/**
 * @internal
 * @brief Returns the 32-bit value of 4 bytes, the least significant first.
 * @param bytes  the 4 bytes.
 * @return the value.
 */
static uint32_t uint32_low_high(unsigned char const *bytes)
{
    uint32_t const ll = bytes[0];
    uint32_t const lh = bytes[1];
    uint32_t const hl = bytes[2];
    uint32_t const hh = bytes[3];
    return (hh << 24) | (hl << 16) | (lh << 8) | ll;
}

/**
 * @internal
 * @brief Returns the 16-bit value of 2 bytes, the most significant first.
 * @param bytes  the 2 bytes.
 * @return the value.
 */
static uint16_t uint16_high_low(unsigned char const *bytes)
{
    uint16_t const h = bytes[0];
    uint16_t const l = bytes[1];
    return (uint16_t) ((h << 8) | l);
}

/**
 * @internal
 * @brief Returns the 16-bit value of 2 bytes, the least significant first.
 * @param bytes  the 2 bytes.
 * @return the value.
 */
static uint16_t uint16_low_high(unsigned char const *bytes)
{
    uint16_t const l = bytes[0];
    uint16_t const h = bytes[1];
    return (uint16_t) ((h << 8) | l);
}

/**
 * @internal
 * @brief Returns non-zero for an integer sample width that the readers unpack.
 * @param bits  the width in bits.
 * @return non-zero for 8, 16, 24 and 32, 0 otherwise.
 */
static int
pcm_int_width_supported(int bits)
{
    return bits == 8 || bits == 16 || bits == 24 || bits == 32;
}

/**
 * @internal
 * @brief Returns the bytes in one sample frame: one sample of each channel.
 * @param channels  the number of channels.
 * @param bits      the sample width in bits. A width that is not a whole
 *                  number of bytes takes up the next whole byte.
 * @return the frame size in bytes.
 */
static uint32_t
pcm_bytes_per_frame(unsigned int channels, unsigned int bits)
{
    return channels * ((bits + 7u) / 8u);
}

/* The header readers below return 0 on success and -1 once the input runs
   short, leaving their result untouched. Callers chain them so that the
   first short read stops the rest and rejects the file. */

static int
read_ieee_extended_high_low(FILE * fp, double *out)
{
    unsigned char bytes[10];
    memset(bytes, 0, 10);
    if (fread(bytes, 1, 10, fp) != 10)
        return -1;
    {
        int32_t const s = (bytes[0] & 0x80);
        int32_t const e_h = (bytes[0] & 0x7F);
        int32_t const e_l = bytes[1];
        int32_t e = (e_h << 8) | e_l;
        uint32_t const hm = uint32_high_low(bytes + 2);
        uint32_t const lm = uint32_high_low(bytes + 6);
        double  result = 0;
        if (e != 0 || hm != 0 || lm != 0) {
            if (e == 0x7fff) {
                result = HUGE_VAL;
            }
            else {
                double  mantissa_h = UNSIGNED_TO_FLOAT(hm);
                double  mantissa_l = UNSIGNED_TO_FLOAT(lm);
                e -= 0x3fff;
                e -= 31;
                result = ldexp(mantissa_h, e);
                e -= 32;
                result += ldexp(mantissa_l, e);
            }
        }
        *out = s ? -result : result;
        return 0;
    }
}


static int
read_16_bits_low_high(FILE * fp, uint16_t * out)
{
    unsigned char bytes[2] = { 0, 0 };
    if (fread(bytes, 1, 2, fp) != 2)
        return -1;
    *out = uint16_low_high(bytes);
    return 0;
}


static int
read_32_bits_low_high(FILE * fp, uint32_t * out)
{
    unsigned char bytes[4] = { 0, 0, 0, 0 };
    if (fread(bytes, 1, 4, fp) != 4)
        return -1;
    *out = uint32_low_high(bytes);
    return 0;
}

static int
read_16_bits_high_low(FILE * fp, uint16_t * out)
{
    unsigned char bytes[2] = { 0, 0 };
    if (fread(bytes, 1, 2, fp) != 2)
        return -1;
    *out = uint16_high_low(bytes);
    return 0;
}

static int
read_32_bits_high_low(FILE * fp, uint32_t * out)
{
    unsigned char bytes[4] = { 0, 0, 0, 0 };
    if (fread(bytes, 1, 4, fp) != 4)
        return -1;
    *out = uint32_high_low(bytes);
    return 0;
}

/* The WAV header fields are unsigned; taking an unsigned value keeps the
   byte extraction free of the signed-overflow and right-shift-of-negative that
   a size at/above INT_MAX would otherwise incur (e.g. the 0x7FFFFFFF streaming
   placeholder plus the 44-byte header). */
static void
write_16_bits_low_high(FILE * fp, unsigned int val)
{
    unsigned char bytes[2];
    bytes[0] = (val & 0xff);
    bytes[1] = ((val >> 8) & 0xff);
    fwrite(bytes, 1, 2, fp);
}

static void
write_32_bits_low_high(FILE * fp, unsigned int val)
{
    unsigned char bytes[4];
    bytes[0] = (val & 0xff);
    bytes[1] = ((val >> 8) & 0xff);
    bytes[2] = ((val >> 16) & 0xff);
    bytes[3] = ((val >> 24) & 0xff);
    fwrite(bytes, 1, 4, fp);
}

#ifdef LIBSNDFILE

#include <sndfile.h>


#else

typedef void SNDFILE;

#endif /* ifdef LIBSNDFILE */



typedef struct blockAlign_struct {
    uint32_t offset;
    uint32_t blockSize;
} blockAlign;

typedef struct IFF_AIFF_struct {
    short   numChannels;
    unsigned long numSampleFrames;
    short   sampleSize;
    double  sampleRate;
    uint32_t sampleType;
    uint32_t sampleFormat;
    blockAlign blkAlgn;
} IFF_AIFF;



struct PcmBuffer {
    void   *ch[2];           /* buffer for each channel */
    int     w;               /* sample width */
    int     n;               /* number samples allocated */
    int     u;               /* number samples used */
    int     skip_start;      /* number samples to ignore at the beginning */
    int     skip_end;        /* number samples to ignore at the end */
};

typedef struct PcmBuffer PcmBuffer;

static void
initPcmBuffer(PcmBuffer * b, int w)
{
    b->ch[0] = 0;
    b->ch[1] = 0;
    b->w = w;
    b->n = 0;
    b->u = 0;
    b->skip_start = 0;
    b->skip_end = 0;
}

static void
freePcmBuffer(PcmBuffer * b)
{
    if (b != 0) {
        free(b->ch[0]);
        free(b->ch[1]);
        b->ch[0] = 0;
        b->ch[1] = 0;
        b->n = 0;
        b->u = 0;
    }
}

/**
 * @internal
 * @brief Grows both channels of a sample buffer.
 *
 * @param b      the buffer.
 * @param n      the number of samples per channel it must hold.
 * @param bytes  the size of @p n samples of one channel, in bytes.
 * @return 0, or -1 when memory runs out. The buffer then keeps its samples.
 */
static int
growPcmBuffer(PcmBuffer * b, int n, int bytes)
{
    void   *ch;

    ch = realloc(b->ch[0], bytes);
    if (ch == NULL)
        return -1;
    b->ch[0] = ch;
    ch = realloc(b->ch[1], bytes);
    if (ch == NULL)
        return -1;
    b->ch[1] = ch;
    b->n = n;
    return 0;
}

/**
 * @internal
 * @brief Adds the samples of one read to a sample buffer.
 *
 * The samples to skip at the start are dropped first.
 *
 * @param b     the buffer, or NULL.
 * @param a0    the first channel of the read, or NULL.
 * @param a1    the second channel of the read, or NULL.
 * @param read  the number of samples per channel that were read. Negative
 *              after a read error.
 * @param used  receives the number of samples that the buffer can hand out.
 * @return 0, or -1 when memory runs out.
 */
static int
addPcmBuffer(PcmBuffer * b, void *a0, void *a1, int read, int *used)
{
    int     a_n;

    *used = 0;
    if (b == 0) {
        return 0;
    }
    if (read < 0) {
        *used = b->u - b->skip_end;
        return 0;
    }
    if (b->skip_start >= read) {
        b->skip_start -= read;
        *used = b->u - b->skip_end;
        return 0;
    }
    a_n = read - b->skip_start;

    if (a_n > 0) {
        int const a_skip = b->w * b->skip_start;
        int const a_want = b->w * a_n;
        int const b_used = b->w * b->u;
        int const b_have = b->w * b->n;
        int const b_need = b->w * (b->u + a_n);
        if (b_have < b_need) {
            if (growPcmBuffer(b, b->u + a_n, b_need) < 0)
                return -1;
        }
        b->u += a_n;
        if (b->ch[0] != 0 && a0 != 0) {
            char   *src = a0;
            char   *dst = b->ch[0];
            memcpy(dst + b_used, src + a_skip, a_want);
        }
        if (b->ch[1] != 0 && a1 != 0) {
            char   *src = a1;
            char   *dst = b->ch[1];
            memcpy(dst + b_used, src + a_skip, a_want);
        }
    }
    b->skip_start = 0;
    *used = b->u - b->skip_end;
    return 0;
}

static int
takePcmBuffer(PcmBuffer * b, void *a0, void *a1, int a_n, int mm)
{
    if (a_n > mm) {
        a_n = mm;
    }
    if (b != 0 && a_n > 0) {
        int const a_take = b->w * a_n;
        if (a0 != 0 && b->ch[0] != 0) {
            memcpy(a0, b->ch[0], a_take);
        }
        if (a1 != 0 && b->ch[1] != 0) {
            memcpy(a1, b->ch[1], a_take);
        }
        b->u -= a_n;
        if (b->u < 0) {
            b->u = 0;
            return a_n;
        }
        if (b->ch[0] != 0) {
            memmove(b->ch[0], (char *) b->ch[0] + a_take, b->w * b->u);
        }
        if (b->ch[1] != 0) {
            memmove(b->ch[1], (char *) b->ch[1] + a_take, b->w * b->u);
        }
    }
    return a_n;
}

/* global data for get_audio.c. */
typedef struct get_audio_global_data_struct {
    int     count_samples_carefully;
    int     pcmbitwidth;
    int     pcmswapbytes;
    int     pcm_is_unsigned_8bit;
    int     pcm_is_ieee_float;
    unsigned long num_samples_read;
    unsigned long num_samples_above_full_scale;
    FILE   *music_in;
    SNDFILE *snd_file;
    hip_t     hip;
    PcmBuffer pcm32;
    PcmBuffer pcm16;
    PcmBuffer pcmf;
    size_t  in_id3v2_size;
    unsigned char* in_id3v2_tag;
} get_audio_global_data;

static get_audio_global_data global;



#ifdef HAVE_MPG123
int     lame123_decode_initfile(FILE * fd, mp3data_struct * mp3data, int *enc_delay, int *enc_padding);
#endif


static int read_samples_pcm(FILE * musicin, int sample_buffer[2 * FRAME_BUFFER_SAMPLES],
                            int samples_to_read);
static int read_samples_float(FILE * musicin, float sample_buffer[2 * FRAME_BUFFER_SAMPLES],
                              int samples_to_read);
static int read_samples_mp3(lame_t gfp, FILE * musicin,
                            short int mpg123pcm[2][FRAME_BUFFER_SAMPLES]);
#ifdef LIBSNDFILE
static SNDFILE *open_snd_file(lame_t gfp, char const *inPath);
#endif
static FILE *open_mpeg_file(lame_t gfp, char const *inPath, int *enc_delay, int *enc_padding);
static FILE *open_wave_file(lame_t gfp, char const *inPath, int *enc_delay, int *enc_padding);
static int close_input_file(FILE * musicin);


static  size_t
min_size_t(size_t a, size_t b)
{
    if (a < b) {
        return a;
    }
    return b;
}

/**
 * @internal
 * @brief Returns the byte order of this machine.
 * @return ::ByteOrderBigEndian or ::ByteOrderLittleEndian.
 */
static enum ByteOrder
machine_byte_order(void)
{
    long    one = 1;
    return !(*((char *) (&one))) ? ByteOrderBigEndian : ByteOrderLittleEndian;
}



/* Replacement for forward fseek(,,SEEK_CUR), because fseek() fails on pipes */


static int
fskip_long(FILE * fp, long offset, int whence)
{
#ifndef PIPE_BUF
    char    buffer[4096];
#else
    char    buffer[PIPE_BUF];
#endif

/* S_ISFIFO macro is defined on newer Linuxes */
#ifndef S_ISFIFO
# ifdef _S_IFIFO
    /* _S_IFIFO is defined on Win32 and Cygwin */
#  define S_ISFIFO(m) (((m)&_S_IFIFO) == _S_IFIFO)
# endif
#endif

#ifdef S_ISFIFO
    /* fseek is known to fail on pipes with several C-Library implementations
       workaround: 1) test for pipe
       2) for pipes, only relatvie seeking is possible
       3)            and only in forward direction!
       else fallback to old code
     */
    {
        int const fd = fileno(fp);
        struct stat file_stat;

        if (fstat(fd, &file_stat) == 0) {
            if (S_ISFIFO(file_stat.st_mode)) {
                if (whence != SEEK_CUR || offset < 0) {
                    return -1;
                }
                while (offset > 0) {
                    size_t const bytes_to_skip = min_size_t(sizeof(buffer), offset);
                    size_t const read = fread(buffer, 1, bytes_to_skip, fp);
                    if (read < 1) {
                        return -1;
                    }
                    assert( read <= LONG_MAX );
                    offset -= (long) read;
                }
                return 0;
            }
        }
    }
#endif
    if (0 == fseek(fp, offset, whence)) {
        return 0;
    }

    if (whence != SEEK_CUR || offset < 0) {
        reader_error("fskip problem: Mostly the return status of functions is not evaluated, "
                     "so it is more secure to pollute <stderr>.\n");
        return -1;
    }

    while (offset > 0) {
        size_t const bytes_to_skip = min_size_t(sizeof(buffer), offset);
        size_t const read = fread(buffer, 1, bytes_to_skip, fp);
        if (read < 1) {
            return -1;
        }
        assert( read <= LONG_MAX );
        offset -= (long) read;
    }

    return 0;
}

static int
fskip_uint32(FILE * fp, uint32_t offset)
{
    int   ret = 0;
    while (offset > INT_MAX && ret == 0) {
        offset -= INT_MAX;
        ret = fskip_long(fp, INT_MAX, SEEK_CUR);
    }
    if (offset > 0 && ret == 0) {
        ret = fskip_long(fp, offset, SEEK_CUR);
    }
    return ret;
}

static  off_t
lame_get_file_size(FILE * fp)
{
    struct stat sb;
    int     fd = fileno(fp);

    if (0 == fstat(fd, &sb))
        return sb.st_size;
    return (off_t) - 1;
}


FILE   *
init_outfile(char const *outPath, LAME_UNUSED int decode)
{
    FILE   *outf;

    /* open the output file */
    if (0 == strcmp(outPath, "-")) {
        outf = stdout;
        lame_set_stream_binary_mode(outf);
    }
    else {
        outf = lame_fopen(outPath, "w+b");
#ifdef __riscos__
        /* Assign correct file type */
        if (outf != NULL) {
            char   *p, *out_path = strdup(outPath);
            if (out_path != NULL) {
                for (p = out_path; *p; p++) { /* ugly, ugly to modify a string */
                    switch (*p) {
                    case '.':
                        *p = '/';
                        break;
                    case '/':
                        *p = '.';
                        break;
                    }
                }
                SetFiletype(out_path, decode ? 0xFB1 /*WAV*/ : 0x1AD /*AMPEG*/);
                free(out_path);
            }
        }
#endif
    }
    return outf;
}


static void
setSkipStartAndEnd(lame_t gfp, int enc_delay, int enc_padding)
{
    int     skip_start = 0, skip_end = 0;

    if (global_decoder.mp3_delay_set)
        skip_start = global_decoder.mp3_delay;

    switch (global_reader.input_format) {
    case sf_mp123:
        break;

    case sf_mp3:
        if (skip_start == 0) {
            if (enc_delay > -1 || enc_padding > -1) {
                if (enc_delay > -1)
                    skip_start = enc_delay + MP3_DECODER_DELAY;
                if (enc_padding > -1)
                    skip_end = enc_padding - MP3_DECODER_DELAY;
            }
            else
                skip_start = lame_get_encoder_delay(gfp) + MP3_DECODER_DELAY;
        }
        else {
            /* user specified a value of skip. just add for decoder */
            skip_start += MP3_DECODER_DELAY;
        }
        break;
    case sf_mp2:
        skip_start += 240 + 1;
        break;
    case sf_mp1:
        skip_start += 240 + 1;
        break;
    default:
        break;
    }
    skip_start = skip_start < 0 ? 0 : skip_start;
    skip_end = skip_end < 0 ? 0 : skip_end;
    global. pcmf.skip_start = global.pcm16.skip_start = global.pcm32.skip_start = skip_start;
    global. pcmf.skip_end = global.pcm16.skip_end = global.pcm32.skip_end = skip_end;
}



int
init_infile(lame_t gfp, char const *inPath)
{
    int     enc_delay = 0, enc_padding = 0;
    /* open the input file */
    global. count_samples_carefully = 0;
    global. num_samples_read = 0;
    global. num_samples_above_full_scale = 0;
    global. pcmbitwidth = global_raw_pcm.in_bitwidth;
    global. pcmswapbytes = global_reader.swapbytes;
    global. pcm_is_unsigned_8bit = global_raw_pcm.in_signed == 1 ? 0 : 1;
    global. pcm_is_ieee_float = 0;
    global. hip = 0;
    global. music_in = 0;
    global. snd_file = 0;
    global. in_id3v2_size = 0;
    global. in_id3v2_tag = 0;
    if (is_mpeg_file_format(global_reader.input_format)) {
        global. music_in = open_mpeg_file(gfp, inPath, &enc_delay, &enc_padding);
    }
    else {
#ifdef LIBSNDFILE
        if (strcmp(inPath, "-") != 0) { /* not for stdin */
            global. snd_file = open_snd_file(gfp, inPath);
        }
#endif
        if (global.snd_file == 0) {
            global. music_in = open_wave_file(gfp, inPath, &enc_delay, &enc_padding);
        }
    }
    initPcmBuffer(&global.pcm32, sizeof(int));
    initPcmBuffer(&global.pcm16, sizeof(short));
    initPcmBuffer(&global.pcmf, sizeof(float));
    setSkipStartAndEnd(gfp, enc_delay, enc_padding);
    {
        unsigned long n = lame_get_num_samples(gfp);
        if (n != NUM_SAMPLES_UNKNOWN) {
            unsigned long const discard = global.pcm32.skip_start + global.pcm32.skip_end;
            lame_set_num_samples(gfp, n > discard ? n - discard : 0);
        }
    }
    if (global.snd_file == NULL && global.music_in == NULL) {
        return -1;
    }
    if (global_reader.swap_channel && lame_get_num_channels(gfp) != 2) {
        reader_error("Error: --swap-channel needs an input with two channels\n");
        close_infile();
        return -1;
    }
    return 1;
}

int
samples_to_skip_at_start(void)
{
    return global.pcm32.skip_start;
}

int
samples_to_skip_at_end(void)
{
    return global.pcm32.skip_end;
}

/**
 * @internal
 * @brief Returns how many floating point input samples are above full scale
 *        after the scaling of the encoder.
 *
 * @c get_audio_float() counts them while it reads. So the count covers the
 * part of the file that was read so far. It starts at 0 for each input file. A
 * sample at exactly full scale is not counted.
 *
 * @return the number of samples. 0 if no sample was above full scale, or if
 *         the input has no floating point samples.
 */
unsigned long
samples_above_full_scale(void)
{
    return global.num_samples_above_full_scale;
}

/**
 * @internal
 * @brief Tells whether the open input file contains floating point samples.
 *
 * The frontend reads such a file with @c get_audio_float() and encodes it with
 * @c lame_encode_buffer_ieee_float(). @c get_audio() returns -1 for such a
 * file.
 *
 * @return nonzero for floating point samples, 0 otherwise.
 */
int
input_is_float(void)
{
    return global.pcm_is_ieee_float;
}

void
close_infile(void)
{
#ifdef HAVE_MPG123
    if (global.hip != 0) {
        hip_decode_exit(global.hip); /* release mp3decoder memory */
        global. hip = 0;
    }
#endif
    close_input_file(global.music_in);
#ifdef LIBSNDFILE
    if (global.snd_file) {
        if (sf_close(global.snd_file) != 0) {
            reader_error("Could not close sound file \n");
        }
        global. snd_file = 0;
    }
#endif
    freePcmBuffer(&global.pcm32);
    freePcmBuffer(&global.pcm16);
    freePcmBuffer(&global.pcmf);
    global. music_in = 0;
    free(global.in_id3v2_tag);
    global.in_id3v2_tag = 0;
    global.in_id3v2_size = 0;
}


static int
        get_audio_common(lame_t gfp, int buffer[2][FRAME_BUFFER_SAMPLES],
                         short buffer16[2][FRAME_BUFFER_SAMPLES],
                         float bufferf[2][FRAME_BUFFER_SAMPLES]);


/**
 * @internal
 * @brief Converts a floating point sample to a 16 bit one, for the 16 bit
 *        PCM that @c --decode writes.
 *
 * The function rounds the sample to 32 bits and keeps the top 16 bits.
 * Integer samples are reduced to 16 bits in the same way. So both readers in
 * this file return the same value, whichever one @c --with-fileio selected.
 *
 * @param u a finite sample, where 1.0 is full scale.
 * @return the 16 bit sample. A sample at or above full scale gives the largest
 *         or the smallest 16 bit value.
 */
static short
float_sample_to_16bit(float u)
{
    /* Magnitude that a sample of 1.0 would map to at 32 bit. The two
       full-scale magnitudes differ by one, but neither is representable in a
       float: both round to 2^31, so a single factor scales both signs. Samples
       of magnitude 1 or above are taken before scaling, so the product always
       stays below 2^31 and the conversion cannot overflow. */
    float const full_scale = -(float) INT_MIN;
    int     wide;
    if (u >= 1)
        return SHRT_MAX;
    if (u <= -1)
        return SHRT_MIN;
    if (u >= 0)
        wide = (int) (u * full_scale + 0.5f);
    else
        wide = (int) (u * full_scale - 0.5f);
    return (short) (wide >> (8 * sizeof(int) - 16));
}

/**
 * @internal
 * @brief Counts the samples that the encoder gets above full scale.
 *
 * Before counting, the function applies the same scaling as the encoder: the
 * overall scale, the left and right scales, and the mix of two input channels
 * into one mono output channel. It uses the values that @c lame_init_params()
 * set. A sample that this scaling brings back within full scale is not
 * counted.
 *
 * @param gfp  the encoder instance, after @c lame_init_params().
 * @param l    the left channel, where 1.0 is full scale.
 * @param r    the right channel. Not read for mono input.
 * @param n    the number of samples per channel.
 * @return the number of samples above full scale.
 */
static unsigned long
count_above_full_scale(lame_t gfp, float const *l, float const *r, int n)
{
    float const gain = lame_get_scale(gfp);
    float const gain_l = gain * lame_get_scale_left(gfp);
    float const gain_r = gain * lame_get_scale_right(gfp);
    unsigned long count = 0;
    int     i;

    if (lame_get_num_channels(gfp) == 2 && lame_get_mode(gfp) == MONO) {
        float const mix_l = 0.5f * gain_l, mix_r = 0.5f * gain_r;
        for (i = 0; i < n; ++i) {
            float const u = mix_l * l[i] + mix_r * r[i];
            count += (u > 1.0f) | (u < -1.0f);
        }
        return count;
    }
    for (i = 0; i < n; ++i) {
        float const u = gain_l * l[i];
        count += (u > 1.0f) | (u < -1.0f);
    }
    if (lame_get_num_channels(gfp) == 2) {
        for (i = 0; i < n; ++i) {
            float const v = gain_r * r[i];
            count += (v > 1.0f) | (v < -1.0f);
        }
    }
    return count;
}

/**
 * @internal
 * @brief Reads one frame of samples through a sample buffer, and swaps the
 *        channels when the user asked for it.
 *
 * Exactly one of @p buffer, @p buffer16 and @p bufferf is given, as for
 * @c get_audio_common(). @p left and @p right are its two channels.
 *
 * @param gfp       the encoder instance, after @c lame_init_params().
 * @param pcm       the sample buffer for the type of @p left and @p right.
 * @param buffer    the int output, or NULL.
 * @param buffer16  the 16 bit output, or NULL.
 * @param bufferf   the float output, or NULL.
 * @param left      the first channel of the output.
 * @param right     the second channel of the output.
 * @return the number of samples per channel. 0 at the end of the input, and a
 *         negative value on an error.
 */
static int
read_frame(lame_t gfp, PcmBuffer * pcm, int buffer[2][FRAME_BUFFER_SAMPLES],
           short buffer16[2][FRAME_BUFFER_SAMPLES], float bufferf[2][FRAME_BUFFER_SAMPLES],
           void *left, void *right)
{
    int     used = 0, read = 0;
    do {
        read = get_audio_common(gfp, buffer, buffer16, bufferf);
        if (addPcmBuffer(pcm, left, right, read, &used) < 0)
            return -1;
    } while (used <= 0 && read > 0);
    if (read < 0) {
        return read;
    }
    if (global_reader.swap_channel == 0)
        return takePcmBuffer(pcm, left, right, used, FRAME_BUFFER_SAMPLES);
    else
        return takePcmBuffer(pcm, right, left, used, FRAME_BUFFER_SAMPLES);
}

/************************************************************************
*
* get_audio()
*
* PURPOSE:  reads a frame of audio data from a file to the buffer,
*   aligns the data for future processing, and separates the
*   left and right channels
*
************************************************************************/
int
get_audio(lame_t gfp, int buffer[2][FRAME_BUFFER_SAMPLES])
{
    return read_frame(gfp, &global.pcm32, buffer, NULL, NULL, buffer[0], buffer[1]);
}

/*
  get_audio16 - behave as the original get_audio function, with a limited
                16 bit per sample output
*/
int
get_audio16(lame_t gfp, short buffer[2][FRAME_BUFFER_SAMPLES])
{
    return read_frame(gfp, &global.pcm16, NULL, buffer, NULL, buffer[0], buffer[1]);
}

/**
 * @internal
 * @brief Reads one frame of floating point samples.
 *
 * It works like @c get_audio(), which reads integer samples. The samples are
 * not converted: 1.0 is full scale, as @c lame_encode_buffer_ieee_float()
 * expects. The function counts the samples that are above full scale after
 * scaling. @c samples_above_full_scale() returns this count.
 *
 * @param gfp     the encoder instance, after @c lame_init_params().
 * @param buffer  receives up to 1152 samples per channel.
 * @return the number of samples per channel. 0 at the end of the input, and a
 *         negative value on an error.
 */
int
get_audio_float(lame_t gfp, float buffer[2][FRAME_BUFFER_SAMPLES])
{
    int const n = read_frame(gfp, &global.pcmf, NULL, NULL, buffer, buffer[0], buffer[1]);

    if (n < 0) {
        return n;
    }
    global.num_samples_above_full_scale += count_above_full_scale(gfp, buffer[0], buffer[1], n);
    return n;
}

/************************************************************************
  get_audio_common - central functionality of get_audio*
    in: gfp
   out: buffer    int output    (if buffer != NULL)
        buffer16  16-bit output (if buffer16 != NULL)
        bufferf   float output  (if bufferf != NULL)
returns: samples read
note: exactly one of the three is given; a floating point file is read into
      bufferf, or into buffer16 for --decode, never into buffer, and only a
      floating point file into bufferf
*/

/**
 * @internal
 * @brief Splits interleaved samples into the two channels of @p dst.
 *
 * The @p n samples per channel end at @p src_end, which the loop moves back to
 * their start. Each sample passes through @p CONV. With one channel, the second
 * channel of @p dst is set to zero.
 */
#define DEINTERLEAVE(dst, src_end, n, nch, CONV)                      \
    do {                                                              \
        int     k_;                                                   \
        if ((nch) == 2) {                                             \
            for (k_ = (n); --k_ >= 0;) {                              \
                (dst)[1][k_] = CONV(*--(src_end));                    \
                (dst)[0][k_] = CONV(*--(src_end));                    \
            }                                                         \
        }                                                             \
        else {                                                        \
            memset((dst)[1], 0, (n) * sizeof((dst)[1][0]));           \
            for (k_ = (n); --k_ >= 0;)                                \
                (dst)[0][k_] = CONV(*--(src_end));                    \
        }                                                             \
    } while (0)
/** @internal @brief Keeps a sample as it is, for DEINTERLEAVE(). */
#define SAMPLE_AS_IS(x)     (x)
/** @internal @brief Keeps the top 16 bits of an int sample, for DEINTERLEAVE(). */
#define INT_TO_16BIT(x)     ((x) >> (8 * sizeof(int) - 16))

static int
get_audio_common(lame_t gfp, int buffer[2][FRAME_BUFFER_SAMPLES],
                 short buffer16[2][FRAME_BUFFER_SAMPLES], float bufferf[2][FRAME_BUFFER_SAMPLES])
{
    const int num_channels = lame_get_num_channels(gfp);
    const int framesize = lame_get_framesize(gfp);
    /* The input format cannot change while we are reading from it, so ask once:
       the same answer decides whether buf_tmp16 gets filled and whether it is
       read back, and saying so lets the compiler see that too. */
    const int input_is_mpeg = is_mpeg_file_format(global_reader.input_format);
    int     insamp[2 * FRAME_BUFFER_SAMPLES];
    short   buf_tmp16[2][FRAME_BUFFER_SAMPLES];
    int     samples_read;
    int     samples_to_read;
    int     i;

    /* sanity checks, that's what we expect to be true */
    if ((num_channels < 1 || 2 < num_channels)
      ||(framesize < 1 || FRAME_BUFFER_SAMPLES < framesize)
      ||(bufferf != NULL && !global.pcm_is_ieee_float)
      ||(buffer != NULL && global.pcm_is_ieee_float)) {
        reader_error("Error: internal problem!\n");
        return -1;
    }

    /* 
     * NOTE: LAME can now handle arbritray size input data packets,
     * so there is no reason to read the input data in chuncks of
     * size "framesize".  EXCEPT:  the LAME graphical frame analyzer 
     * will get out of sync if we read more than framesize worth of data.
     */

    samples_to_read = framesize;

    /* if this flag has been set, then we are carefull to read
     * exactly num_samples and no more.  This is useful for .wav and .aiff
     * files which have id3 or other tags at the end.  Note that if you
     * are using LIBSNDFILE, this is not necessary 
     */
    if (global.count_samples_carefully) {
        unsigned long tmp_num_samples, remaining;
        /* get num_samples */
        if (input_is_mpeg) {
            tmp_num_samples = global_decoder.mp3input_data.nsamp;
        }
        else {
            tmp_num_samples = lame_get_num_samples(gfp);
        }
        if (global.num_samples_read < tmp_num_samples) {
            remaining = tmp_num_samples - global.num_samples_read;
        }
        else {
            remaining = 0;
        }
        if (remaining < (unsigned long) framesize && 0 != tmp_num_samples)
            /* in case the input is a FIFO (at least it's reproducible with
               a FIFO) tmp_num_samples may be 0 and therefore remaining
               would be 0, but we need to read some samples, so don't
               change samples_to_read to the wrong value in this case */
            samples_to_read = (int) remaining; /* bounded by framesize above */
    }

    if (input_is_mpeg) {
        if (buffer != NULL)
            samples_read = read_samples_mp3(gfp, global.music_in, buf_tmp16);
        else
            samples_read = read_samples_mp3(gfp, global.music_in, buffer16);
        if (samples_read < 0) {
            return samples_read;
        }
    }
    else if (global.pcm_is_ieee_float) {
        /* The samples as stored, 1.0 being full scale. fsamp[] is bounded as
           insamp[] is: the sanity check above holds the frame to 1152 samples
           of at most two channels. */
        float   fsamp[2 * FRAME_BUFFER_SAMPLES];
        float const *q;
        if (global.snd_file) {
#ifdef LIBSNDFILE
            samples_read = (int) sf_read_float(global.snd_file, fsamp,
                                               num_channels * samples_to_read);
#else
            samples_read = 0;
#endif
        }
        else {
            samples_read =
                read_samples_float(global.music_in, fsamp, num_channels * samples_to_read);
        }
        if (samples_read < 0) {
            return samples_read;
        }
        q = fsamp + samples_read;
        if (bufferf == NULL) {
            /* --decode writes 16 bit PCM, and a sample that is not a finite
               number has no 16 bit value */
            for (i = 0; i < samples_read; ++i) {
                if (!float_is_finite(fsamp[i])) {
                    reader_error("Error: the input holds a sample that is not a finite number\n");
                    return -1;
                }
            }
        }
        samples_read /= num_channels;
        if (bufferf != NULL)
            DEINTERLEAVE(bufferf, q, samples_read, num_channels, SAMPLE_AS_IS);
        else
            DEINTERLEAVE(buffer16, q, samples_read, num_channels, float_sample_to_16bit);
    }
    else {
        int    *p;
        if (global.snd_file) {
#ifdef LIBSNDFILE
            int const items = num_channels * samples_to_read;
            samples_read = sf_read_int(global.snd_file, insamp, items);
#else
            samples_read = 0;
#endif
        }
        else {
            samples_read =
                read_samples_pcm(global.music_in, insamp, num_channels * samples_to_read);
        }
        if (samples_read < 0) {
            return samples_read;
        }
        p = insamp + samples_read;
        samples_read /= num_channels;
        if (buffer != NULL)
            DEINTERLEAVE(buffer, p, samples_read, num_channels, SAMPLE_AS_IS);
        else
            DEINTERLEAVE(buffer16, p, samples_read, num_channels, INT_TO_16BIT);
    }

    /* LAME mp3 output 16bit -  convert to int, if necessary */
    if (input_is_mpeg) {
        if (buffer != NULL) {
            for (i = samples_read; --i >= 0;)
                buffer[0][i] = (int) ((unsigned int) buf_tmp16[0][i] << (8 * sizeof(int) - 16));
            if (num_channels == 2) {
                for (i = samples_read; --i >= 0;)
                    buffer[1][i] = (int) ((unsigned int) buf_tmp16[1][i] << (8 * sizeof(int) - 16));
            }
            else {
                memset(buffer[1], 0, samples_read * sizeof(int));
            }
        }
    }


    /* if ... then it is considered infinitely long.
       Don't count the samples */
    if (global.count_samples_carefully)
        global. num_samples_read += samples_read;

    return samples_read;
}

#undef DEINTERLEAVE
#undef SAMPLE_AS_IS
#undef INT_TO_16BIT



static int
read_samples_mp3(LAME_UNUSED lame_t gfp, LAME_UNUSED FILE * musicin,
                 LAME_UNUSED short int mpg123pcm[2][FRAME_BUFFER_SAMPLES])
{
    int     out;
#ifdef HAVE_MPG123
    short int *outbuf;
    size_t outbytes;
    static const char type_name[] = "MP3 file";

    /* Need to deinterleave so rather use mpg123_decode_frame() to decode the
       current frame and deinterleave from the internal buffer. */
    out = mpg123_decode_frame(global.hip->mh, NULL, (unsigned char**)&outbuf, &outbytes);
    if (out != MPG123_OK && out != MPG123_DONE)
    {
        if (out == MPG123_NEW_FORMAT)
        {
            reader_error("Error: format changed in %s - not supported\n", type_name);
        }
        return -1;
    }
    out = (int)(outbytes/(sizeof(short)*global_decoder.mp3input_data.stereo));
    if (global_decoder.mp3input_data.stereo == 2) {
        int i;
        for (i=0; i<out; ++i) {
            mpg123pcm[0][i] = *outbuf++;
            mpg123pcm[1][i] = *outbuf++;
        }
    }
    else if (out > 0)
        memcpy(mpg123pcm[0], outbuf, sizeof(short)*out);
    if(global.hip->pinfo)
        hip_finish_pinfo(global.hip);
#else
    out = -1;
#endif
    return out;
}

static
int set_input_num_channels(lame_t gfp, int num_channels)
{
    if (gfp) {
        if (-1 == lame_set_num_channels(gfp, num_channels)) {
            reader_error("Unsupported number of channels: %d\n", num_channels);
            return 0;
        }
    }
    return 1;
}

static
int set_input_samplerate(lame_t gfp, int input_samplerate)
{
    if (gfp) {
        int sr = global_reader.input_samplerate;
        if (sr == 0) sr = input_samplerate;
        if (-1 == lame_set_in_samplerate(gfp, sr)) {
            reader_error("Unsupported sample rate: %d\n", sr);
            return 0;
        }
    }
    return 1;
}

/**
 * @internal
 * @brief Returns the data size for the header of a decoded WAV file.
 *
 * Prints a message when there are no samples, and when the data is larger
 * than a WAV header can hold.
 *
 * @param frames           the number of samples per channel.
 * @param bytes_per_frame  the size of one sample of all channels, in bytes.
 * @return the size of the data in bytes. 0 when there are no samples, and
 *         @c WAV_DATA_SIZE_MAX when the data is larger.
 */
unsigned int
wav_data_size(double frames, int bytes_per_frame)
{
    if (frames <= 0) {
        reader_error("WAVE file contains 0 PCM samples\n");
        return 0;
    }
    if (frames > WAV_DATA_SIZE_MAX / bytes_per_frame) {
        reader_error("Very huge WAVE file, can't set filesize accordingly\n");
        return WAV_DATA_SIZE_MAX;
    }
    return (unsigned int) (frames * bytes_per_frame);
}

int
WriteWaveHeader(FILE * const fp, unsigned int pcmbytes, int freq, int channels, int bits)
{
    int     bytes = (bits + 7) / 8;

    /* quick and dirty, but documented */
    fwrite("RIFF", 1, 4, fp); /* label */
    write_32_bits_low_high(fp, pcmbytes + 44u - 8u); /* length in bytes without header */
    fwrite("WAVEfmt ", 2, 4, fp); /* 2 labels */
    write_32_bits_low_high(fp, 2 + 2 + 4 + 4 + 2 + 2); /* length of PCM format declaration area */
    write_16_bits_low_high(fp, 1); /* is PCM? */
    write_16_bits_low_high(fp, channels); /* number of channels */
    write_32_bits_low_high(fp, freq); /* sample frequency in [Hz] */
    write_32_bits_low_high(fp, freq * channels * bytes); /* bytes per second */
    write_16_bits_low_high(fp, channels * bytes); /* bytes per sample time */
    write_16_bits_low_high(fp, bits); /* bits per sample */
    fwrite("data", 1, 4, fp); /* label */
    write_32_bits_low_high(fp, pcmbytes); /* length in bytes of raw PCM data */

    return ferror(fp) ? -1 : 0;
}




#if defined(LIBSNDFILE)

extern SNDFILE *sf_wchar_open(wchar_t const *wpath, int mode, SF_INFO * sfinfo);

static SNDFILE *
open_snd_file(lame_t gfp, char const *inPath)
{
    char const *lpszFileName = inPath;
    SNDFILE *gs_pSndFileIn = NULL;
    SF_INFO gs_wfInfo;

    {
#if defined( _WIN32 ) && !defined(__MINGW32__)
        wchar_t *file_name = utf8ToUnicode(lpszFileName);
#endif
        /* Try to open the sound file */
        memset(&gs_wfInfo, 0, sizeof(gs_wfInfo));
#if defined( _WIN32 ) && !defined(__MINGW32__)
        gs_pSndFileIn = sf_wchar_open(file_name, SFM_READ, &gs_wfInfo);
#else
        gs_pSndFileIn = sf_open(lpszFileName, SFM_READ, &gs_wfInfo);
#endif

        /* Only a caller who asked for headerless PCM gets it. libsndfile
           cannot recognise raw input, so -r is the only thing that can say
           the file is meant to be read that way; without it an unrecognised
           file is an unrecognised file, and LAME's own reader - which
           refuses it - gets its turn instead. */
        if (gs_pSndFileIn == NULL && global_reader.input_format == sf_raw) {
            if (global_raw_pcm.in_signed == 0 && global_raw_pcm.in_bitwidth != 8) {
                error_printf("Unsigned input only supported with bitwidth 8\n");
#if defined( _WIN32 ) && !defined(__MINGW32__)
                free(file_name);
#endif
                return 0;
            }
            /* the defaults raw PCM is read with */
            gs_wfInfo.seekable = 0; /* raw input, so not seekable */
            gs_wfInfo.samplerate = lame_get_in_samplerate(gfp);
            gs_wfInfo.channels = lame_get_num_channels(gfp);
            gs_wfInfo.format = SF_FORMAT_RAW;
            if ((global_raw_pcm.in_endian == ByteOrderLittleEndian) ^ (global_reader.swapbytes !=
                                                                       0)) {
                gs_wfInfo.format |= SF_ENDIAN_LITTLE;
            }
            else {
                gs_wfInfo.format |= SF_ENDIAN_BIG;
            }
            switch (global_raw_pcm.in_bitwidth) {
            case 8:
                gs_wfInfo.format |=
                    global_raw_pcm.in_signed == 0 ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8;
                break;
            case 16:
                gs_wfInfo.format |= SF_FORMAT_PCM_16;
                break;
            case 24:
                gs_wfInfo.format |= SF_FORMAT_PCM_24;
                break;
            case 32:
                gs_wfInfo.format |= SF_FORMAT_PCM_32;
                break;
            default:
                break;
            }
#if defined( _WIN32 ) && !defined(__MINGW32__)
            gs_pSndFileIn = sf_wchar_open(file_name, SFM_READ, &gs_wfInfo);
#else
            gs_pSndFileIn = sf_open(lpszFileName, SFM_READ, &gs_wfInfo);
#endif
        }
#if defined( _WIN32 ) && !defined(__MINGW32__)
        free(file_name);
#endif

        /* Check result */
        if (gs_pSndFileIn == NULL) {
            sf_perror(gs_pSndFileIn);
            reader_error("Could not open sound file \"%s\".\n", lpszFileName);
            return 0;
        }
        switch (gs_wfInfo.format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_FLOAT:
        case SF_FORMAT_DOUBLE:
            global. pcm_is_ieee_float = 1;
            break;
        default:
            break;
        }

        if ((gs_wfInfo.format & SF_FORMAT_RAW) == SF_FORMAT_RAW) {
            global_reader.input_format = sf_raw;
        }

        if(gs_wfInfo.frames >= 0 && gs_wfInfo.frames < (sf_count_t)(unsigned)NUM_SAMPLES_UNKNOWN)
            (void) lame_set_num_samples(gfp, gs_wfInfo.frames);
        else
            (void) lame_set_num_samples(gfp, NUM_SAMPLES_UNKNOWN);
        if (!set_input_num_channels(gfp, gs_wfInfo.channels)) {
            sf_close(gs_pSndFileIn);
            return 0;
        }
        if (!set_input_samplerate(gfp, gs_wfInfo.samplerate)) {
            sf_close(gs_pSndFileIn);
            return 0;
        }
        global. pcmbitwidth = 32;
    }
    return gs_pSndFileIn;
}

#endif /* defined(LIBSNDFILE) */



/************************************************************************
unpack_read_samples - read and unpack signed low-to-high byte or unsigned
                      single byte input. (used for read_samples function)
                      Output integers are stored in the native byte order
                      (little or big endian).  -jd
  in: samples_to_read
      bytes_per_sample
      swap_order    - set for high-to-low byte order input stream
 i/o: pcm_in
 out: sample_buffer  (must be allocated up to samples_to_read upon call)
returns: number of samples read
*/
static int
unpack_read_samples(const int samples_to_read, const int bytes_per_sample,
                    const int swap_order, int *sample_buffer, FILE * pcm_in)
{
    int     samples_read;
    int     i;
    int    *op;              /* output pointer */
    unsigned char *ip = (unsigned char *) sample_buffer; /* input pointer */
    const int b = sizeof(int) * 8;

    {
        size_t  samples_read_ = fread(sample_buffer, bytes_per_sample, samples_to_read, pcm_in);
        assert( samples_read_ <= INT_MAX );
        samples_read = (int) samples_read_;
    }
    op = sample_buffer + samples_read;

#define GA_URS_IFLOOP( ga_urs_bps ) \
    if( bytes_per_sample == ga_urs_bps ) \
      for( i = samples_read * bytes_per_sample; (i -= bytes_per_sample) >=0;)

    /* Bytes are assembled into the high end of an int through unsigned shifts:
       a byte >= 0x80 shifted into the sign position would otherwise overflow the
       signed int. unsigned int has the same width as int, so every shift amount
       here stays below the width; the unsigned result converts back to the same
       two's-complement sample value on assignment. */
    if (swap_order == 0) {
        GA_URS_IFLOOP(1)
            * --op = (int) ((unsigned int) ip[i] << (b - 8));
        GA_URS_IFLOOP(2)
            * --op = (int) ((unsigned int) ip[i] << (b - 16) | (unsigned int) ip[i + 1] << (b - 8));
        GA_URS_IFLOOP(3)
            * --op = (int) ((unsigned int) ip[i] << (b - 24) | (unsigned int) ip[i + 1] << (b - 16) | (unsigned int) ip[i + 2] << (b - 8));
        GA_URS_IFLOOP(4)
            * --op = (int) ((unsigned int) ip[i] << (b - 32) | (unsigned int) ip[i + 1] << (b - 24) | (unsigned int) ip[i + 2] << (b - 16) | (unsigned int) ip[i + 3] << (b - 8));
    }
    else {
        GA_URS_IFLOOP(1)
            * --op = (int) ((unsigned int) (ip[i] ^ 0x80) << (b - 8) | (unsigned int) 0x7f << (b - 16)); /* convert from unsigned */
        GA_URS_IFLOOP(2)
            * --op = (int) ((unsigned int) ip[i] << (b - 8) | (unsigned int) ip[i + 1] << (b - 16));
        GA_URS_IFLOOP(3)
            * --op = (int) ((unsigned int) ip[i] << (b - 8) | (unsigned int) ip[i + 1] << (b - 16) | (unsigned int) ip[i + 2] << (b - 24));
        GA_URS_IFLOOP(4)
            * --op = (int) ((unsigned int) ip[i] << (b - 8) | (unsigned int) ip[i + 1] << (b - 16) | (unsigned int) ip[i + 2] << (b - 24) | (unsigned int) ip[i + 3] << (b - 32));
    }
#undef GA_URS_IFLOOP
    return (samples_read);
}



/************************************************************************
*
* read_samples()
*
* PURPOSE:  reads the PCM samples from a file to the buffer
*
*  SEMANTICS:
* Reads #samples_read# number of shorts from #musicin# filepointer
* into #sample_buffer[]#.  Returns the number of samples read.
*
************************************************************************/

static int
read_samples_pcm(FILE * musicin, int sample_buffer[2 * FRAME_BUFFER_SAMPLES], int samples_to_read)
{
    int     samples_read;
    int     bytes_per_sample = global.pcmbitwidth / 8;
    int     swap_byte_order; /* byte order of input stream */

    switch (global.pcmbitwidth) {
    case 32:
    case 24:
    case 16:
        if (global_raw_pcm.in_signed == 0) {
            reader_error("Unsigned input only supported with bitwidth 8\n");
            return -1;
        }
        swap_byte_order = (global_raw_pcm.in_endian != ByteOrderLittleEndian) ? 1 : 0;
        if (global.pcmswapbytes) {
            swap_byte_order = !swap_byte_order;
        }
        break;

    case 8:
        swap_byte_order = global.pcm_is_unsigned_8bit;
        break;

    default:
        reader_error("Only 8, 16, 24 and 32 bit input files supported \n");
        return -1;
    }
    if (samples_to_read < 0 || samples_to_read > 2 * FRAME_BUFFER_SAMPLES) {
        reader_error("Error: unexpected number of samples to read: %d\n", samples_to_read);
        return -1;
    }
    samples_read = unpack_read_samples(samples_to_read, bytes_per_sample, swap_byte_order,
                                       sample_buffer, musicin);
    if (ferror(musicin)) {
        reader_error("Error reading input file\n");
        return -1;
    }

    return samples_read;
}

/**
 * @internal
 * @brief Reads 32 bit floating point samples, in the byte order of the host.
 *
 * The byte order of the file is decided in the same way as for integer
 * samples: little endian, unless the format or @c --swap-bytes says otherwise.
 * If it differs from the byte order of the host, the four bytes of each sample
 * are reversed.
 *
 * @param musicin          the input file.
 * @param sample_buffer    receives the samples, interleaved.
 * @param samples_to_read  the number of samples to read, at most 2304.
 * @return the number of samples read. A negative value on an error.
 */
static int
read_samples_float(FILE * musicin, float sample_buffer[2 * FRAME_BUFFER_SAMPLES],
                   int samples_to_read)
{
    compiletime_assert(sizeof(float) == 4);
    int     file_is_big_endian = (global_raw_pcm.in_endian != ByteOrderLittleEndian) ? 1 : 0;
    int     samples_read, i;

    if (global.pcmswapbytes) {
        file_is_big_endian = !file_is_big_endian;
    }
    if (samples_to_read < 0 || samples_to_read > 2 * FRAME_BUFFER_SAMPLES) {
        reader_error("Error: unexpected number of samples to read: %d\n", samples_to_read);
        return -1;
    }
    samples_read = (int) fread(sample_buffer, 4, (size_t) samples_to_read, musicin);
    if (ferror(musicin)) {
        reader_error("Error reading input file\n");
        return -1;
    }
    if (file_is_big_endian != (machine_byte_order() == ByteOrderBigEndian)) {
        unsigned char *b = (unsigned char *) sample_buffer;
        for (i = 0; i < samples_read; ++i, b += 4) {
            unsigned char t = b[0];
            b[0] = b[3];
            b[3] = t;
            t = b[1];
            b[1] = b[2];
            b[2] = t;
        }
    }
    return samples_read;
}



/* AIFF Definitions */

static uint32_t const IFF_ID_FORM = 0x464f524d; /* "FORM" */
static uint32_t const IFF_ID_AIFF = 0x41494646; /* "AIFF" */
static uint32_t const IFF_ID_AIFC = 0x41494643; /* "AIFC" */
static uint32_t const IFF_ID_COMM = 0x434f4d4d; /* "COMM" */
static uint32_t const IFF_ID_SSND = 0x53534e44; /* "SSND" */

static uint32_t const IFF_ID_NONE = 0x4e4f4e45; /* "NONE" *//* AIFF-C data format */
static uint32_t const IFF_ID_2CBE = 0x74776f73; /* "twos" *//* AIFF-C data format */
static uint32_t const IFF_ID_2CLE = 0x736f7774; /* "sowt" *//* AIFF-C data format */
static uint32_t const IFF_ID_FL32 = 0x666C3332; /* "fl32" *//* AIFF-C data format */
static uint32_t const IFF_ID_FL64 = 0x666C3634; /* "fl64" *//* AIFF-C data format */

static uint32_t const WAV_ID_RIFF = 0x52494646; /* "RIFF" */
static uint32_t const WAV_ID_WAVE = 0x57415645; /* "WAVE" */
static uint32_t const WAV_ID_FMT = 0x666d7420; /* "fmt " */
static uint32_t const WAV_ID_DATA = 0x64617461; /* "data" */

#ifndef WAVE_FORMAT_PCM
static uint16_t const WAVE_FORMAT_PCM = 0x0001;
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
static uint16_t const WAVE_FORMAT_IEEE_FLOAT = 0x0003;
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
static uint16_t const WAVE_FORMAT_EXTENSIBLE = 0xFFFE;
#endif
#ifndef WAVE_FORMAT_MPEG
static uint16_t const WAVE_FORMAT_MPEG = 0x0050;
#endif
#ifndef WAVE_FORMAT_MPEGLAYER3
static uint16_t const WAVE_FORMAT_MPEGLAYER3 = 0x0055;
#endif

/** The number of chunks the WAV reader reads at most to find the data chunk. */
#define WAV_MAX_CHUNKS 20


static uint32_t
make_even_number_of_bytes_in_length(uint32_t x)
{
    return x + (x & 0x01);
}

/**
 * @internal
 * @brief Reads the size field of a chunk, and rounds the size up to an even
 *        number of bytes.
 *
 * @param sf          the input file, at the size field.
 * @param big_endian  1 for an AIFF file, 0 for a WAV file.
 * @param size        receives the size of the chunk body, padding included.
 * @return 0, or -1 when the field cannot be read.
 */
static int
read_chunk_size(FILE * sf, int big_endian, uint32_t * size)
{
    if (big_endian ? read_32_bits_high_low(sf, size) : read_32_bits_low_high(sf, size))
        return -1;
    *size = make_even_number_of_bytes_in_length(*size);
    return 0;
}

/**
 * @internal
 * @brief Takes the size field and the body of an AIFF chunk from the bytes of
 *        the FORM chunk that are left.
 *
 * @param remaining  the bytes left in the FORM chunk, at least 4.
 * @param size       the size of the chunk body, padding included.
 * @param min_size   the smallest body this chunk type can have.
 * @return 0, or -1 when the body is too small or does not fit.
 */
static int
take_chunk(uint32_t * remaining, uint32_t size, uint32_t min_size)
{
    *remaining -= 4;
    if (size < min_size || *remaining < size)
        return -1;
    *remaining -= size;
    return 0;
}


/*****************************************************************************
 *
 *	Read Microsoft Wave headers
 *
 *	By the time we get here the first 32-bits of the file have already been
 *	read, and we're pretty sure that we're looking at a WAV file.
 *
 *****************************************************************************/

static int
parse_wave_header(lame_global_flags * gfp, FILE * sf)
{
    uint32_t ui32_nSamplesPerSec = 0;
    uint32_t ui32_DataChunkSize = 0;
    uint16_t ui16_wFormatTag = 0;
    uint16_t ui16_nChannels = 0;
    uint16_t ui16_wBitsPerSample = 0;

    int     is_wav = 0;
    int     loop_sanity = 0;

    uint32_t ui32_chunkSize = 0; /* file_length */
    uint32_t ui32_WAVEID    = 0;
    if (read_32_bits_high_low(sf, &ui32_chunkSize)
        || read_32_bits_high_low(sf, &ui32_WAVEID))
        return -1;
    if (ui32_WAVEID != WAV_ID_WAVE || ui32_chunkSize < 1)
        return -1;

    for (loop_sanity = 0; loop_sanity < WAV_MAX_CHUNKS; ++loop_sanity) {
        uint32_t ui32_ckID = 0;
        if (read_32_bits_high_low(sf, &ui32_ckID))
            return -1;
        if (ui32_ckID == WAV_ID_FMT) {
            uint32_t ui32_nAvgBytesPerSec = 0;
            uint32_t ui32_cksize = 0;
            uint16_t ui16_nBlockAlign = 0;

            if (read_chunk_size(sf, 0, &ui32_cksize))
                return -1;
            if (ui32_cksize < 16u) {
                /*DEBUGF("'fmt' chunk too short (only %ld bytes)!", ui32_cksize);*/
                return -1;
            }
            if (read_16_bits_low_high(sf, &ui16_wFormatTag)
                || read_16_bits_low_high(sf, &ui16_nChannels)
                || read_32_bits_low_high(sf, &ui32_nSamplesPerSec)
                || read_32_bits_low_high(sf, &ui32_nAvgBytesPerSec)
                || read_16_bits_low_high(sf, &ui16_nBlockAlign)
                || read_16_bits_low_high(sf, &ui16_wBitsPerSample))
                return -1;
            /* The block alignment and the byte rate only restate what the
               other fields already say, so they are cross-checked rather
               than used: writers get them wrong often enough that a
               mismatch must not cost the user the file. */
            if (global_ui_config.silent < 0 && ui16_nChannels > 0 && ui16_wBitsPerSample > 0) {
                uint32_t const align = pcm_bytes_per_frame(ui16_nChannels, ui16_wBitsPerSample);
                if (ui16_nBlockAlign != align)
                    error_printf("Note: block alignment is %u, expected %u\n",
                                 (unsigned int) ui16_nBlockAlign, (unsigned int) align);
                if (ui32_nAvgBytesPerSec != ui32_nSamplesPerSec * align)
                    error_printf("Note: byte rate is %u, expected %u\n",
                                 (unsigned int) ui32_nAvgBytesPerSec,
                                 (unsigned int) (ui32_nSamplesPerSec * align));
            }
            ui32_cksize -= 16u;
            /* WAVE_FORMAT_EXTENSIBLE support */
            if ((ui32_cksize > 9u) && (ui16_wFormatTag == WAVE_FORMAT_EXTENSIBLE)) {
                uint16_t ui16_cbSize              = 0;
                uint16_t ui16_wValidBitsPerSample = 0;
                uint16_t ui16_SubFormat           = 0;
                if (read_16_bits_low_high(sf, &ui16_cbSize)
                    || read_16_bits_low_high(sf, &ui16_wValidBitsPerSample)
                    /* the channel mask only names the speakers; zero means
                       unspecified and nothing here depends on it */
                    || fskip_uint32(sf, 4u) != 0
                    || read_16_bits_low_high(sf, &ui16_SubFormat))
                    return -1;
                /* An extensible header describes a 22 byte extension and
                   cannot carry more valid bits than the sample holds. */
                if (ui16_cbSize < 22u)
                    return -1;
                if (ui16_wValidBitsPerSample == 0
                    || ui16_wValidBitsPerSample > ui16_wBitsPerSample)
                    return -1;
                ui32_cksize -= 10u;
                ui16_wFormatTag = ui16_SubFormat; /* SubType coincident with format_tag for PCM int or float */
            }
            /* DEBUGF("   skipping %d bytes\n", ui32_cksize); */
            if (ui32_cksize > 0) {
                if (fskip_uint32(sf, ui32_cksize) != 0)
                    return -1;
            };
        }
        else if (ui32_ckID == WAV_ID_DATA) {
            if (read_32_bits_low_high(sf, &ui32_DataChunkSize))
                return -1;
            is_wav = 1;
            /* We've found the audio data. Read no further! */
            break;
        }
        else {
            uint32_t ui32_cksize = 0;
            if (read_chunk_size(sf, 0, &ui32_cksize))
                return -1;
            if (fskip_uint32(sf, ui32_cksize) != 0) {
                return -1;
            }
        }
    }
    if (is_wav) {
        if (ui16_wFormatTag == WAVE_FORMAT_MPEG || ui16_wFormatTag == WAVE_FORMAT_MPEGLAYER3) {
            return sf_mp123;
        }
        if (ui16_wFormatTag != WAVE_FORMAT_PCM && ui16_wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
            reader_error("Unsupported data format: 0x%04X\n", ui16_wFormatTag);
            return 0;   /* oh no! non-supported format  */
        }

        /* make sure the header is sane */
        if (!set_input_num_channels(gfp, ui16_nChannels))
            return 0;
        /* The header carries the rate as 32 unsigned bits and the encoder
           takes it as an int, so the upper half of that range cannot be
           passed on at all. Refuse it here, while the declared value is
           still intact: converting first and letting the "not below 1"
           check catch the result reports a negative rate that appears
           nowhere in the file. */
        if (ui32_nSamplesPerSec > (uint32_t) INT_MAX) {
            reader_error("Unsupported sample rate: %u\n",
                         (unsigned int) ui32_nSamplesPerSec);
            return -1;
        }
        if (!set_input_samplerate(gfp, (int) ui32_nSamplesPerSec))
            return 0;
        /* The reader unpacks 8, 16, 24 and 32 bit integer samples, and reads
           floating point ones as 32 bit; no other width can be turned into
           audio. Deciding that here rather than at the first read names the
           field that is wrong while the header is still in view, and it keeps
           a floating point file of some other width from being unpacked as
           integers and then reinterpreted as floats, which produces noise
           rather than an error. The sibling AIFF reader has enumerated its
           accepted widths all along. */
        {
            int     width_ok;

            if (ui16_wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                width_ok = (ui16_wBitsPerSample == 32);
            else
                width_ok = pcm_int_width_supported(ui16_wBitsPerSample);
            if (!width_ok) {
                reader_error("Unsupported bits per sample: %d\n", ui16_wBitsPerSample);
                return -1;
            }
        }
        global. pcmbitwidth = ui16_wBitsPerSample;
        global. pcm_is_unsigned_8bit = 1;
        global. pcm_is_ieee_float = (ui16_wFormatTag == WAVE_FORMAT_IEEE_FLOAT ? 1 : 0);
        if (ui32_DataChunkSize == NUM_SAMPLES_UNKNOWN)
            (void) lame_set_num_samples(gfp, NUM_SAMPLES_UNKNOWN);
        else
            (void) lame_set_num_samples(gfp, ui32_DataChunkSize / pcm_bytes_per_frame(ui16_nChannels, ui16_wBitsPerSample));
        return 1;
    }
    return -1;
}



/************************************************************************
* aiff_check2
*
* PURPOSE:	Checks AIFF header information to make sure it is valid.
*	        returns 0 on success, 1 on errors
************************************************************************/

static int
aiff_check2(IFF_AIFF * const pcm_aiff_data)
{
    if (pcm_aiff_data->sampleType != IFF_ID_SSND) {
        reader_error("ERROR: input sound data is not PCM\n");
        return 1;
    }
    if (!pcm_int_width_supported(pcm_aiff_data->sampleSize)) {
        reader_error("ERROR: input sound data is not 8, 16, 24 or 32 bits\n");
        return 1;
    }
    if (pcm_aiff_data->numChannels != 1 && pcm_aiff_data->numChannels != 2) {
        reader_error("ERROR: input sound data is not mono or stereo\n");
        return 1;
    }
    if (pcm_aiff_data->blkAlgn.blockSize != 0) {
        reader_error("ERROR: block size of input sound data is not 0 bytes\n");
        return 1;
    }
    /* A bug, since we correctly skip the offset earlier in the code.
       if (pcm_aiff_data->blkAlgn.offset != 0) {
       error_printf("Block offset is not 0 bytes in '%s'\n", file_name);
       return 1;
       } */

    return 0;
}


/*****************************************************************************
 *
 *	Read Audio Interchange File Format (AIFF) headers.
 *
 *	By the time we get here the first 32 bits of the file have already been
 *	read, and we're pretty sure that we're looking at an AIFF file.
 *
 *****************************************************************************/

static int
parse_aiff_header(lame_global_flags * gfp, FILE * sf)
{
    uint32_t  ui32_ChunkSize = 0;
    uint32_t  ui32_TypeID = 0;
    IFF_AIFF aiff_info;
    int     seen_comm_chunk = 0, seen_ssnd_chunk = 0;
    long    pcm_data_pos = -1;

    memset(&aiff_info, 0, sizeof(aiff_info));
    aiff_info.sampleFormat = IFF_ID_NONE;
    if (read_32_bits_high_low(sf, &ui32_ChunkSize)
        || read_32_bits_high_low(sf, &ui32_TypeID))
        return -1;
    if (ui32_ChunkSize < 4)     /* malformed FORM size: guard the -= 4 underflow */
        return -1;
    ui32_ChunkSize -= 4;
    if ((ui32_TypeID != IFF_ID_AIFF) && (ui32_TypeID != IFF_ID_AIFC))
        return -1;

    while (ui32_ChunkSize >= 8) {
        uint32_t ui32_type = 0;
        if (read_32_bits_high_low(sf, &ui32_type))
            return -1;
        ui32_ChunkSize -= 4;

        /* DEBUGF(
           "found chunk type %08x '%4.4s'\n", ui32_type, (char*)&ui32_type); */

        /* don't use a switch here to make it easier to use 'break' for SSND */
        if (ui32_type == IFF_ID_COMM) {
            uint32_t ui32_cksize = 0;
            uint16_t ui16_numChannels = 0, ui16_sampleSize = 0;
            uint32_t ui32_numSampleFrames = 0;

            if (read_chunk_size(sf, 1, &ui32_cksize)
                || take_chunk(&ui32_ChunkSize, ui32_cksize, 18))
                return -1;
            seen_comm_chunk = seen_ssnd_chunk + 1;

            if (read_16_bits_high_low(sf, &ui16_numChannels)
                || read_32_bits_high_low(sf, &ui32_numSampleFrames)
                || read_16_bits_high_low(sf, &ui16_sampleSize)
                || read_ieee_extended_high_low(sf, &aiff_info.sampleRate))
                return -1;
            /* The rate reaches lame as an int below, and a value outside that
               range has no defined conversion - including the infinity an
               all-ones exponent decodes to. INT_MAX is exact as a double on
               any target with an int of 53 bits or fewer. */
            if (!(aiff_info.sampleRate >= 1
                  && aiff_info.sampleRate <= (double) INT_MAX))
                return -1;
            aiff_info.numChannels = (short) ui16_numChannels;
            aiff_info.numSampleFrames = ui32_numSampleFrames;
            aiff_info.sampleSize = (short) ui16_sampleSize;
            ui32_cksize -= 18;
            if (ui32_TypeID == IFF_ID_AIFC) {
                if (ui32_cksize < 4)
                    return -1;
                if (read_32_bits_high_low(sf, &aiff_info.sampleFormat))
                    return -1;
                ui32_cksize -= 4;
            }
            if (fskip_uint32(sf, ui32_cksize) != 0)
                return -1;
        }
        else if (ui32_type == IFF_ID_SSND) {
            uint32_t ui32_cksize = 0;
            if (read_chunk_size(sf, 1, &ui32_cksize)
                || take_chunk(&ui32_ChunkSize, ui32_cksize, 8))
                return -1;
            seen_ssnd_chunk = 1;

            aiff_info.sampleType = IFF_ID_SSND;
            if (read_32_bits_high_low(sf, &aiff_info.blkAlgn.offset)
                || read_32_bits_high_low(sf, &aiff_info.blkAlgn.blockSize))
                return -1;
            ui32_cksize -= 8;
            if (seen_comm_chunk > 0) {
                if (fskip_uint32(sf, aiff_info.blkAlgn.offset) != 0)
                    return -1;
                /* We've found the audio data. Read no further! */
                break;
            }
            pcm_data_pos = ftell(sf);
            if (pcm_data_pos >= 0) {
                pcm_data_pos += aiff_info.blkAlgn.offset;
            }
            if (fskip_uint32(sf, ui32_cksize) != 0)
                return -1;
        }
        else {
            uint32_t ui32_cksize = 0;
            if (read_chunk_size(sf, 1, &ui32_cksize)
                || take_chunk(&ui32_ChunkSize, ui32_cksize, 0))
                return -1;
            if (fskip_uint32(sf, ui32_cksize) != 0)
                return -1;
        }
    }
    if (aiff_info.sampleFormat == IFF_ID_2CLE) {
        global. pcm_is_ieee_float = 0;
        global. pcmswapbytes = global_reader.swapbytes;
    }
    else if (aiff_info.sampleFormat == IFF_ID_2CBE) {
        global. pcm_is_ieee_float = 0;
        global. pcmswapbytes = !global_reader.swapbytes;
    }
    else if (aiff_info.sampleFormat == IFF_ID_NONE) {
        global. pcm_is_ieee_float = 0;
        global. pcmswapbytes = !global_reader.swapbytes;
    }
    else if (aiff_info.sampleFormat == IFF_ID_FL32) {
        global. pcm_is_ieee_float = 1;
        global. pcmswapbytes = !global_reader.swapbytes;
    }
    else if (aiff_info.sampleFormat == IFF_ID_FL64) {
        /* reading 64 bit floating point samples is not implemented */
        reader_error("Unsupported data format: 64 bit floating point\n");
        return -1;
    }
    else {
        return -1;
    }

    /* DEBUGF("Parsed AIFF %d\n", is_aiff); */
    if (seen_comm_chunk && (seen_ssnd_chunk > 0 || aiff_info.numSampleFrames == 0)) {
        /* make sure the header is sane */
        if (0 != aiff_check2(&aiff_info))
            return 0;
        if (!set_input_num_channels(gfp, aiff_info.numChannels))
            return 0;
        if (!set_input_samplerate(gfp, (int) aiff_info.sampleRate))
            return 0;
        (void) lame_set_num_samples(gfp, aiff_info.numSampleFrames);
        global. pcmbitwidth = aiff_info.sampleSize;
        global. pcm_is_unsigned_8bit = 0;
        if (pcm_data_pos >= 0) {
            if (fseek(sf, pcm_data_pos, SEEK_SET) != 0) {
                reader_error("Can't rewind stream to audio data position\n");
                return 0;
            }
        }

        return 1;
    }
    return -1;
}



/************************************************************************
*
* parse_file_header
*
* PURPOSE: Read the header from a bytestream.  Try to determine whether
*          it's a WAV file or AIFF without rewinding, since rewind
*          doesn't work on pipes and there's a good chance we're reading
*          from stdin (otherwise we'd probably be using libsndfile).
*
* When this function returns, the file offset will be positioned at the
* beginning of the sound data.
*
************************************************************************/

static int
parse_file_header(lame_global_flags * gfp, FILE * sf)
{
    uint32_t ui32_type = 0;
    /*
       DEBUGF(
       "First word of input stream: %08x '%4.4s'\n", ui32_type, (char*) &type);
     */
    global. count_samples_carefully = 0;
    global. pcm_is_unsigned_8bit = global_raw_pcm.in_signed == 1 ? 0 : 1;
    /*global_reader.input_format = sf_raw; commented out, because it is better to fail
       here as to encode some hundreds of input files not supported by LAME
       If you know you have RAW PCM data, use the -r switch
     */

    if (read_32_bits_high_low(sf, &ui32_type) != 0) {
        /* not even the four identifying bytes are present */
        reader_error("Warning: unsupported audio format\n");
        return sf_unknown;
    }

    if (ui32_type == WAV_ID_RIFF) {
        /* It's probably a WAV file */
        int const ret = parse_wave_header(gfp, sf);
        if (ret == sf_mp123) {
            global. count_samples_carefully = 1;
            return sf_mp123;
        }
        if (ret > 0) {
            if (lame_get_num_samples(gfp) == NUM_SAMPLES_UNKNOWN
                || global_reader.ignorewavlength == 1)
            {
                global. count_samples_carefully = 0;
                lame_set_num_samples(gfp, NUM_SAMPLES_UNKNOWN);
            }
            else
                global. count_samples_carefully = 1;
            return sf_wave;
        }
        if (ret < 0) {
            reader_error("Warning: corrupt or unsupported WAVE format\n");
        }
    }
    else if (ui32_type == IFF_ID_FORM) {
        /* It's probably an AIFF file */
        int const ret = parse_aiff_header(gfp, sf);
        if (ret > 0) {
            global. count_samples_carefully = 1;
            return sf_aiff;
        }
        if (ret < 0) {
            reader_error("Warning: corrupt or unsupported AIFF format\n");
        }
    }
    else {
        reader_error("Warning: unsupported audio format\n");
    }
    return sf_unknown;
}


static int
open_mpeg_file_part2(lame_t gfp, LAME_UNUSED FILE * musicin, LAME_UNUSED char const *inPath,
                     LAME_UNUSED int *enc_delay, LAME_UNUSED int *enc_padding)
{
#ifdef HAVE_MPG123
    if (-1 == lame123_decode_initfile(musicin, &global_decoder.mp3input_data, enc_delay, enc_padding)) {
        reader_error("Error opening MPEG input file %s.\n", inPath);
        return 0;
    }
#endif
    if (!set_input_num_channels(gfp, global_decoder.mp3input_data.stereo)) {
        return 0;
    }
    if (!set_input_samplerate(gfp, global_decoder.mp3input_data.samplerate)) {
        return 0;
    }
    (void) lame_set_num_samples(gfp, global_decoder.mp3input_data.nsamp);
    return 1;
}


static FILE *
open_wave_file(lame_t gfp, char const *inPath, int *enc_delay, int *enc_padding)
{
    FILE   *musicin;

    /* set the defaults from info incase we cannot determine them from file */
    lame_set_num_samples(gfp, NUM_SAMPLES_UNKNOWN);

    if (!strcmp(inPath, "-")) {
        lame_set_stream_binary_mode(musicin = stdin); /* Read from standard input. */
    }
    else {
        if ((musicin = lame_fopen(inPath, "rb")) == NULL) {
            reader_error("Could not find \"%s\".\n", inPath);
            return 0;
        }
    }

    if (global_reader.input_format == sf_ogg) {
        reader_error("sorry, vorbis support in LAME is deprecated.\n");
        close_input_file(musicin);
        return 0;
    }
    else if (global_reader.input_format == sf_raw) {
        /* assume raw PCM */
        if (global_ui_config.silent < 9) {
            console_printf("Assuming raw pcm input file");
            if (global_reader.swapbytes)
                console_printf(" : Forcing byte-swapping\n");
            else
                console_printf("\n");
        }
        global. pcmswapbytes = global_reader.swapbytes;
    }
    else {
        global_reader.input_format = parse_file_header(gfp, musicin);
    }
    if (global_reader.input_format == sf_mp123) {
        if (open_mpeg_file_part2(gfp, musicin, inPath, enc_delay, enc_padding))
            return musicin;
        close_input_file(musicin);
        return 0;
    }
    if (global_reader.input_format == sf_unknown) {
        close_input_file(musicin);
        return 0;
    }

    if (lame_get_num_samples(gfp) == NUM_SAMPLES_UNKNOWN && musicin != stdin) {
        int const tmp_num_channels = lame_get_num_channels(gfp);
        double const flen = lame_get_file_size(musicin); /* try to figure out num_samples */
        if (flen >= 0 && tmp_num_channels > 0 ) {
            /* try file size, assume 2 bytes per sample */
            unsigned long fsize = (unsigned long) (flen / (2 * tmp_num_channels));
            (void) lame_set_num_samples(gfp, fsize);
            global. count_samples_carefully = 0;
        }
    }
    return musicin;
}



static FILE *
open_mpeg_file(lame_t gfp, char const *inPath, int *enc_delay, int *enc_padding)
{
    FILE   *musicin;

    /* set the defaults from info incase we cannot determine them from file */
    lame_set_num_samples(gfp, NUM_SAMPLES_UNKNOWN);

    if (strcmp(inPath, "-") == 0) {
        musicin = stdin;
        lame_set_stream_binary_mode(musicin); /* Read from standard input. */
    }
    else {
        musicin = lame_fopen(inPath, "rb");
        if (musicin == NULL) {
            reader_error("Could not find \"%s\".\n", inPath);
            return 0;
        }
    }
    if ( 0 == open_mpeg_file_part2(gfp, musicin, inPath, enc_delay, enc_padding) ) {
        close_input_file(musicin);
        return 0;
    }
    if (lame_get_num_samples(gfp) == NUM_SAMPLES_UNKNOWN && musicin != stdin) {
        double  flen = lame_get_file_size(musicin); /* try to figure out num_samples */
        if (flen >= 0) {
            /* try file size, assume 2 bytes per sample */
            if (global_decoder.mp3input_data.bitrate > 0) {
                double  totalseconds =
                    (flen * 8.0 / (1000.0 * global_decoder.mp3input_data.bitrate));
                unsigned long tmp_num_samples =
                    (unsigned long) (totalseconds * lame_get_in_samplerate(gfp));

                (void) lame_set_num_samples(gfp, tmp_num_samples);
                global_decoder.mp3input_data.nsamp = tmp_num_samples;
                global. count_samples_carefully = 0;
            }
        }
    }
    return musicin;
}


static int
close_input_file(FILE * musicin)
{
    int     ret = 0;

    if (musicin != stdin && musicin != 0) {
        ret = fclose(musicin);
    }
    if (ret != 0) {
        reader_error("Could not close audio input file\n");
    }
    return ret;
}

#ifdef HAVE_MPG123
#define CHECK123(code) if(MPG123_OK != (code)) return -1

#ifdef _WIN32
static mpg123_ssize_t lame123_read_from_file(void* handle, void* buffer, size_t size)
{
   return fread(buffer, 1, size, (FILE*)handle);
}

static off_t lame123_seek_in_file(void* handle, off_t offset, int direction)
{
   HANDLE const os_handle =
       (HANDLE) _get_osfhandle(_fileno((FILE*) handle));

   /* fseek() has undefined behavior for non-seeking Windows devices. */
   if (os_handle == INVALID_HANDLE_VALUE ||
       GetFileType(os_handle) != FILE_TYPE_DISK)
      return (off_t)-1;
   if (fseek((FILE*)handle, offset, direction) != 0)
      return (off_t)-1;
   return ftell((FILE*)handle);
}

static void lame123_cleanup_file(LAME_UNUSED void* handle)
{
   /* don't call fclose(); close_input_file() will do that */
}
#endif

int lame123_decode_initfile(FILE *fd, mp3data_struct *mp3data, int *enc_delay, int *enc_padding)
{
    off_t len;
    unsigned char *id3buf;
    size_t id3size;
    struct mpg123_frameinfo fi;
    long rate, val;
    int channels;

    mpg123_init();
    memset(mp3data, 0, sizeof(mp3data_struct));
    if (global.hip) {
        hip_decode_exit(global.hip);
    }
    global. hip = hip_decode_init();
    if(!global.hip || !global.hip->mh)
        return -1;
    /* TODO: enforce float format ... optionally be careful for builds
       that only know 16 bit output. */
    mpg123_param(global.hip->mh, MPG123_ADD_FLAGS, MPG123_STORE_RAW_ID3, 0.);
    mpg123_param(global.hip->mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.);
    mpg123_format_none(global.hip->mh);
    /* TODO: switch to MPG123_ENC_FLOAT_32, always! */
    CHECK123(mpg123_format2(global.hip->mh,
        0, MPG123_MONO|MPG123_STEREO, MPG123_ENC_SIGNED_16));
    /* TODO: verboseness / silence set up */
#ifdef _WIN32
    /* On Win32 compiles it can happen that lame.exe and libmp3lame.dll use
       different C++ runtimes, which maintail different FILE* lists, and
       fileno() would produce invalid file numbers for those msvcrt instances.
       So use mpg123_replace_reader_handle() / mpg123_open_handle() here
       instead of mpg123_open_fd(). */
    CHECK123(mpg123_replace_reader_handle(global.hip->mh, lame123_read_from_file, lame123_seek_in_file, lame123_cleanup_file));
    CHECK123(mpg123_open_handle(global.hip->mh, fd));
#else
    CHECK123(mpg123_open_fd(global.hip->mh, fileno(fd)));
#endif
    /* Seek to get past Info frame and ID3v2. */
    CHECK123(mpg123_seek(global.hip->mh, SEEK_SET, 0));
    /* TODO: Figure out if MPG123_GAPLESS is desired or not. */
    /* Guessing seems to be OK, so we do not have to insist on knowing
       if libmpg123 got that info from Info tag or not. */
    /* I am paranoid about off_t being larger than long or int. */
    len = mpg123_framelength(global.hip->mh);
    if(len <= ((unsigned int)-1)/2)     /* totalframes is int, bound to INT_MAX */
        mp3data->totalframes = (int)len;
    else
        return -1;
    len = mpg123_length(global.hip->mh);
    if(len <= ((unsigned int)-1)/2)
        mp3data->nsamp = len;
    else
        return -1;
    /* Encoder delay and padding are not needed when libmpg123 handles gapless
       decoding itself. So let's see if we get away with that. */
    mpg123_getstate(global.hip->mh, MPG123_ENC_DELAY, &val, NULL);
    *enc_delay = (int)val;
    mpg123_getstate(global.hip->mh, MPG123_ENC_PADDING, &val, NULL);
    *enc_padding = (int)val;
    if(global.in_id3v2_tag)
        free(global.in_id3v2_tag);
    global.in_id3v2_size = 0;
    if( MPG123_OK == mpg123_id3_raw(global.hip->mh, NULL, NULL,
        &id3buf, &id3size) && id3buf && id3size ) {
        global.in_id3v2_tag = malloc(id3size);
        if(global.in_id3v2_tag) {
            memcpy(global.in_id3v2_tag, id3buf, id3size);
            global.in_id3v2_size = id3size;
        }
    }
    CHECK123(mpg123_info(global.hip->mh, &fi));
    CHECK123(mpg123_getformat(global.hip->mh, &rate, &channels, NULL));
    /* How much of this is actually needed for the frontend? */
    mp3data->header_parsed = 1;
    mp3data->stereo = channels; /* Channel count correct? Or is dual mono different? */
    mp3data->samplerate = (int)rate;
    mp3data->mode = fi.mode;
    mp3data->mode_ext = fi.mode_ext;
    mp3data->framesize = mpg123_spf(global.hip->mh);
    mp3data->bitrate = fi.bitrate;
    if(global_reader.input_format == sf_mp123) switch(fi.layer) {
        case 1:
            global_reader.input_format = sf_mp1;
        break;
        case 2:
            global_reader.input_format = sf_mp2;
        break;
        case 3:
            global_reader.input_format = sf_mp3;
        break;
    }

    return 0;
}
#endif

int
is_mpeg_file_format(int input_file_format)
{
    switch (input_file_format) {
    case sf_mp1:
        return 1;
    case sf_mp2:
        return 2;
    case sf_mp3:
        return 3;
    case sf_mp123:
        return -1;
    default:
        break;
    }
    return 0;
}


#define LOW__BYTE(x) (x & 0x00ff)
#define HIGH_BYTE(x) ((x >> 8) & 0x00ff)

int
put_audio16(FILE * outf, short Buffer[2][FRAME_BUFFER_SAMPLES], int iread, int nch)
{
    char    data[2 * FRAME_BUFFER_SAMPLES * 2];
    int     i, m = 0;

    if (global_decoder.disable_wav_header && global_reader.swapbytes) {
        if (nch == 1) {
            for (i = 0; i < iread; i++) {
                short   x = Buffer[0][i];
                /* write 16 Bits High Low */
                data[m++] = HIGH_BYTE(x);
                data[m++] = LOW__BYTE(x);
            }
        }
        else {
            for (i = 0; i < iread; i++) {
                short   x = Buffer[0][i], y = Buffer[1][i];
                /* write 16 Bits High Low */
                data[m++] = HIGH_BYTE(x);
                data[m++] = LOW__BYTE(x);
                /* write 16 Bits High Low */
                data[m++] = HIGH_BYTE(y);
                data[m++] = LOW__BYTE(y);
            }
        }
    }
    else {
        if (nch == 1) {
            for (i = 0; i < iread; i++) {
                short   x = Buffer[0][i];
                /* write 16 Bits Low High */
                data[m++] = LOW__BYTE(x);
                data[m++] = HIGH_BYTE(x);
            }
        }
        else {
            for (i = 0; i < iread; i++) {
                short   x = Buffer[0][i], y = Buffer[1][i];
                /* write 16 Bits Low High */
                data[m++] = LOW__BYTE(x);
                data[m++] = HIGH_BYTE(x);
                /* write 16 Bits Low High */
                data[m++] = LOW__BYTE(y);
                data[m++] = HIGH_BYTE(y);
            }
        }
    }
    if (m > 0) {
        if (fwrite(data, 1, m, outf) != (size_t) m)
            return -1;
    }
    if (global_writer.flush_write == 1) {
        if (fflush(outf) != 0)
            return -1;
    }
    return 0;
}

hip_t
get_hip(void)
{
    return global.hip;
}

size_t
sizeOfOldTag(LAME_UNUSED lame_t gf)
{
    return global.in_id3v2_size;
}

unsigned char*
getOldTag(LAME_UNUSED lame_t gf)
{
    return global.in_id3v2_tag;
}

/* end of get_audio.c */
