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
 *
 * Several of the stubs below look superfluous on any one platform, and are not:
 * @c parse.c reaches for them under configurations other than the one being
 * built - a decoderless build, a debug or unoptimized build, Windows - and the
 * tests link that one translation unit without the rest of the frontend. Deleting
 * a stub because nothing here calls it breaks @c make @c check somewhere else.
 * The real prototypes are included rather than re-declared so a signature that
 * drifts fails to compile instead of failing to link.
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

/* used by parse.c's album-art reader (defined in lame_main.c normally) */
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
