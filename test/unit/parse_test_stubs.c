/**
 * @file
 * @ingroup unit_tests
 * @brief Link-time stubs for the parse.c unit tests.
 *
 * Minimal stand-ins for the console and file helpers that @c frontend/parse.c
 * references but that normally live in @c console.c / @c lame_main.c. They only
 * need to satisfy the linker; none is exercised on the @c set_path_arg() /
 * @c merge_argv() paths the unit tests drive. @c parse.c itself defines the
 * frontend global-config blocks (@c global_reader / @c global_writer / ...),
 * so - unlike the get_audio test - those are @e not stubbed here.
 *
 * The @c utf8To* / @c toLatin1 helpers @c parse.c uses are compiled in only
 * under @c _WIN32 && !__MINGW32__, so they are not referenced on the platforms
 * these tests build on and need no stubs.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>

#include "lame.h"
#include "console.h"

/* console reporting state + helpers (defined in console.c normally) */
Console_IO_t Console_IO;

int   console_printf(const char *format, ...) { (void) format; return 0; }
int   error_printf  (const char *format, ...) { (void) format; return 0; }
int   report_printf (const char *format, ...) { (void) format; return 0; }
void  console_flush(void) {}
void  error_flush(void) {}
void  report_flush(void) {}

/* used by parse.c's album-art reader (defined in lame_main.c normally) */
FILE *lame_fopen(char const *file, char const *mode) { return fopen(file, mode); }

/* LAMEOPT environment lookup, used by parse_args() (defined in main.c normally) */
char *lame_getenv(char const *var) { (void) var; return NULL; }
