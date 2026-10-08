/**
 * @file
 * @ingroup unit_tests
 * @brief Link-time stubs for the parse.c unit tests.
 *
 * @c frontend/parse.c uses console and file helpers that are defined in other
 * frontend files, mostly @c console.c and @c main.c. This file defines minimal
 * stand-ins for them. The parse.c tests link this file. The console stand-ins
 * print nothing. @c lame_fopen() opens the file with @c fopen().
 *
 * @c parse.c itself defines the frontend configuration @c frontend_config.
 * So this file does @e not stub it. The get_audio tests are different: their
 * stub file defines it.
 *
 * @c parse.c uses the @c utf8To* and @c toLatin1 helpers only under
 * @c _WIN32 && !__MINGW32__. These tests do not build on such a platform. So
 * nothing references the helpers, and they need no stubs.
 *
 * Some stubs below look unnecessary on any one platform. They are necessary.
 * @c parse.c calls them in other configurations:
 * - a build without the decoder,
 * - a debug or unoptimized build,
 * - Windows.
 *
 * The tests link this one translation unit without the rest of the frontend.
 * If you delete a stub because nothing calls it here, @c make @c check fails
 * in another configuration.
 *
 * The file includes the real prototypes and does not declare its own. So a
 * signature that changes fails at compile time, not at link time.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>

#include "lame.h"
#include "main.h"
#include "console.h"

#include "test_unused.h"

/* console reporting state + helpers (defined in console.c normally) */
Console_IO_t Console_IO;

int   console_printf(const char *format, ...) { (void) format; return 0; }
int   error_printf  (const char *format, ...) { (void) format; return 0; }
int   report_printf (const char *format, ...) { (void) format; return 0; }
void  console_flush(void) {}
void  error_flush(void) {}
void  report_flush(void) {}

/* used by parse.c's album-art reader (defined in main.c normally) */
FILE *lame_fopen(char const *file, char const *mode) { return fopen(file, mode); }

/* LAMEOPT environment lookup, used by parse_args() (defined in main.c normally) */
char *lame_getenv(char const *var) { (void) var; return NULL; }

/* --debug-file, a developer switch (defined in console.c normally). Referenced
   whenever the compiler does not fold away the internal-options branch, which
   an unoptimized or debug build does not. */
void  set_debug_file(LAME_UNUSED const char *fn) { return; }

/* input-format probe (defined in get_audio.c normally). parse.c calls it only
   when the decoder is configured out, which is when get_audio.c stops being
   linked into anything the tests can reach. */
int   is_mpeg_file_format(LAME_UNUSED int input_file_format) { return 0; }

/* Windows/OS2-only frontend helpers (defined in main.c normally) */
void  dosToLongFileName(LAME_UNUSED char *filename) { return; }
void  setProcessPriority(LAME_UNUSED int priority) { return; }
