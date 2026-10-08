/*
 *      Command line frontend program
 *
 *      Copyright (c) 1999 Mark Taylor
 *                    2000 Takehiro TOMIANGA
 *                    2010-2011 Robert Hegemann
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

#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#include "get_audio.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif


/* GLOBAL VARIABLES used by parse.c and main.c.  
   instantiated in parce.c.  ugly, ugly */

typedef struct ReaderConfig
{
    sound_file_format input_format;
    int   swapbytes;                /* force byte swapping   default=0 */
    int   swap_channel;             /* 0: no-op, 1: swaps input channels */
    int   input_samplerate;
    int   ignorewavlength;
} ReaderConfig;

typedef struct WriterConfig
{
    int   flush_write;
    int   preserve_modtime;        /* give the output file the input file's times */
    int   replaygain_id3v2;        /* also write ReplayGain to ID3v2 TXXX frames */
    int   id3v2_padding;           /* bytes of ID3v2 padding the user asked for */
} WriterConfig;

typedef struct UiConfig
{
    int   silent;                   /* Verbosity */
    int   brhist;
    int   print_clipping_info;      /* print info whether waveform clips */
    float update_interval;          /* to use Frank's time status display */
} UiConfig;

typedef struct DecoderConfig
{
    int   mp3_delay;                /* to adjust the number of samples truncated during decode */
    int   mp3_delay_set;            /* user specified the value of the mp3 encoder delay to assume for decoding */
    int   disable_wav_header;
    mp3data_struct mp3input_data;
} DecoderConfig;

typedef enum ByteOrder { ByteOrderLittleEndian, ByteOrderBigEndian } ByteOrder;

typedef struct RawPCMConfig
{
    int     in_bitwidth;
    int     in_signed;
    ByteOrder in_endian;
} RawPCMConfig;

/**
 * @internal
 * @brief The configuration of the frontend programs. parse_args() sets it
 *        from the command line.
 *
 * All members are plain values without pointers, so a struct copy saves and
 * restores the whole configuration.
 */
typedef struct FrontendConfig
{
    ReaderConfig  reader;           /**< how the input file is read */
    WriterConfig  writer;           /**< how the output file is written */
    UiConfig      ui_config;        /**< what is printed while encoding */
    DecoderConfig decoder;          /**< the settings of --decode */
    RawPCMConfig  raw_pcm;          /**< the format of raw PCM input */
} FrontendConfig;

/**
 * @internal
 * @brief The initializer of the default configuration: the settings without
 *        any option.
 */
#define FRONTEND_CONFIG_DEFAULTS                                        \
{ /* reader    */ { sf_unknown, 0, 0, 0, 0 }                            \
, /* writer    */ { 0, 0, 0, 0 }                                        \
, /* ui_config */ { 0, 1, 0, 0 }                                        \
, /* decoder   */ { 0, 0, 0, { 0 } }                                    \
, /* raw_pcm   */ { 16, -1, ByteOrderLittleEndian }                     \
}

/** @internal @brief The default configuration, ::FRONTEND_CONFIG_DEFAULTS. */
extern const FrontendConfig frontend_config_defaults;

/** @internal @brief The configuration of this run. */
extern FrontendConfig frontend_config;


/*  strnlen is C11 and POSIX.1-2008; older systems do not have it.  The calls
    that use it are bounded on purpose - the buffer need not contain a NUL at
    all - so strlen is not a substitute for it. */
#ifdef HAVE_STRNLEN
# define lame_strnlen strnlen
#else
extern size_t lame_strnlen(char const* s, size_t n);
#endif

extern FILE* lame_fopen(char const* file, char const* mode);
extern char* utf8ToConsole8Bit(const char* str);
extern char* utf8ToLocal8Bit(const char* str);
extern unsigned short* utf8ToUtf16(char const* mbstr);
extern char* utf8ToLatin1(char const* str);
extern char* local8BitToUtf8(const char* str);
#ifdef _WIN32
extern wchar_t* utf8ToUnicode(char const* mbstr);
extern char *unicodeToUtf8(const wchar_t *wstr);
#endif

extern void dosToLongFileName(char* filename);
extern void setProcessPriority(int priority);

extern int lame_main(lame_t gf, int argc, char** argv);
extern char* lame_getenv(char const* var);

#if defined(__cplusplus)
}
#endif

#endif
