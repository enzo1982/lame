/**
 * @file
 * @ingroup unit_tests
 * @brief Link-time stubs for the get_audio.c unit tests.
 *
 * @c get_audio.c uses frontend globals and helpers that are defined in other
 * files: @c parse.c, @c main.c, @c console.c and @c lametime.c. This file
 * defines minimal stand-ins for them. The AIFF, WAVE and floating point reader
 * tests link this file. The console stand-ins print nothing.
 *
 * The file stays small because the tests compile @c get_audio.c with
 * @c HAVE_MPG123 undefined. The large mpg123 and mpglib code paths are then not
 * compiled.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "lame.h"
#include "main.h"
#include "console.h"

/* frontend global configuration blocks (defined in parse.c/main.c normally) */
ReaderConfig  global_reader    = { sf_unknown, 0, 0, 0, 0 };
WriterConfig  global_writer    = { 0 };
UiConfig      global_ui_config = { 0, 0, 0, 0 };
DecoderConfig global_decoder;
RawPCMConfig  global_raw_pcm   = { 16, 1, ByteOrderLittleEndian };

/* console output helpers (defined in console.c normally) */
int   console_printf(const char *format, ...) { (void) format; return 0; }
int   error_printf  (const char *format, ...) { (void) format; return 0; }
int   report_printf (const char *format, ...) { (void) format; return 0; }
void  console_flush(void) {}
void  error_flush(void) {}
void  report_flush(void) {}

/* filesystem / encoding helpers (defined in main.c normally) */
FILE *lame_fopen(char const *file, char const *mode) { return fopen(file, mode); }
char *utf8ToConsole8Bit(const char *str) { return (char *) str; }
char *utf8ToLatin1(const char *str)      { return (char *) str; }

/* defined in frontend/lametime.c normally */
int   lame_set_stream_binary_mode(FILE *const fp) { (void) fp; return 0; }
