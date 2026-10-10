/**
 * @file
 * @ingroup unit_tests
 * @brief Link-time stubs for the get_audio.c unit tests.
 *
 * @c get_audio.c uses frontend globals and helpers that are defined in other
 * files: @c parse.c, @c main.c, @c console.c and @c lametime.c. This file
 * defines minimal stand-ins for them. The AIFF, WAVE, floating point and cut
 * input reader tests link this file. The console stand-ins print nothing.
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
#include "test_unused.h"

/* frontend configuration (defined in parse.c normally) */
const FrontendConfig frontend_config_defaults = FRONTEND_CONFIG_DEFAULTS;
FrontendConfig frontend_config = FRONTEND_CONFIG_DEFAULTS;

/* console output helpers (defined in console.c normally) */
int   console_printf(const char *format, ...) { (void) format; return 0; }
int   error_printf  (const char *format, ...) { (void) format; return 0; }
int   report_printf (const char *format, ...) { (void) format; return 0; }
void  frontend_errorf(LAME_UNUSED const char *format, LAME_UNUSED va_list ap) { return; }
void  console_flush(void) {}
void  error_flush(void) {}
void  report_flush(void) {}

/* filesystem / encoding helpers (defined in main.c normally) */
FILE *lame_fopen(char const *file, char const *mode) { return fopen(file, mode); }
char *utf8ToConsole8Bit(const char *str) { return (char *) str; }
char *utf8ToLatin1(const char *str)      { return (char *) str; }

/* defined in frontend/lametime.c normally */
int   lame_set_stream_binary_mode(FILE *const fp) { (void) fp; return 0; }
