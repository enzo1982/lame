#ifndef PARSE_H_INCLUDED
#define PARSE_H_INCLUDED

#include <stdio.h>

#include "lame.h"

#if defined(__cplusplus)
extern "C" {
#endif

int     usage(FILE * const fp, const char *ProgramName);
int     short_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName);
int     long_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName,
                  int lessmode);
int     display_bitrates(FILE * const fp);
int     frontend_init_params(lame_global_flags * gfp);
const char *frontend_encode_error_text(int code);

int     parse_args(lame_global_flags * gfp, int argc, char **argv, char *const inPath,
                   char *const outPath, char *const outDir, char **nogap_inPath, int *num_nogap);

int     generateOutPath(char const* inPath, char const* outDir, char const* s_ext, char* outPath);

#if defined(__cplusplus)
}
#endif

#endif
/* end of parse.h */
