/**
 * @file
 * @ingroup unit_tests
 * @brief Unit test for @c frontend_classify_suffix() and the suffixes that
 *        @c generateOutPath() replaces, in @c frontend/parse.c.
 *
 * Both read one table of file name suffixes. The tests check the input format
 * that a suffix hints at, the cases where a file name has no suffix, the
 * output name that a known and an unknown suffix give, and that the output
 * name stays inside its buffer.
 *
 * The test includes the whole translation unit with @c \#include, as the
 * other tests of @c parse.c do. @c parse_test_stubs.c supplies the console and
 * file helpers that @c parse.c uses. libmp3lame supplies the @c lame_* API.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"

#include "parse.c"

/**
 * @brief Checks the input format of each kind of suffix, in either case.
 * @param state cmocka fixture state (unused).
 */
static void
test_suffix_formats(LAME_UNUSED void **state)
{
    static const struct {
        const char *path;
        sound_file_format format;
    } cases[] = {
        { "a.wav", sf_wave }, { "A.WAVE", sf_wave },
        { "a.aif", sf_aiff }, { "a.aiff", sf_aiff }, { "a.AIFC", sf_aiff },
        { "a.raw", sf_raw },
        { "a.mp1", sf_mp123 }, { "a.mp2", sf_mp123 }, { "a.MP3", sf_mp123 }, { "a.mpg", sf_mp123 },
        { "a.ogg", sf_ogg },
        { "a.flac", sf_unknown },
        { "a.xyz", sf_unknown },
        { "song.wav.mp3", sf_mp123 },
    };
    size_t  i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        sound_file_format const got = frontend_classify_suffix(cases[i].path);
        if (got != cases[i].format)
            fail_msg("%s: format %d, wanted %d", cases[i].path, (int) got, (int) cases[i].format);
    }
}

/**
 * @brief Checks that a name without a suffix hints at no format, also when a
 *        directory name has a dot.
 * @param state cmocka fixture state (unused).
 */
static void
test_no_suffix(LAME_UNUSED void **state)
{
    char    dir[32];

    snprintf(dir, sizeof dir, "dir.wav%cfile", SLASH);
    assert_int_equal(frontend_classify_suffix("file"), sf_unknown);
    assert_int_equal(frontend_classify_suffix(dir), sf_unknown);
    assert_int_equal(frontend_classify_suffix(NULL), sf_unknown);
}

/**
 * @brief Checks that the output name replaces a known suffix and keeps an
 *        unknown one.
 * @param state cmocka fixture state (unused).
 */
static void
test_output_name(LAME_UNUSED void **state)
{
    static char out[PATH_MAX + 1];

    assert_int_equal(generateOutPath("song.wave", NULL, ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "song.mp3");
    assert_int_equal(generateOutPath("song.flac", NULL, ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "song.mp3");
    assert_int_equal(generateOutPath("song.xyz", NULL, ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "song.xyz.mp3");
}

/**
 * @brief Checks which characters separate the directories of a path.
 *
 * On Windows both slashes do, so an input path written with @c / gives the
 * same output name as one written with @c \\, and an output directory may end
 * in either. The colon after a drive letter is kept as it was. Elsewhere only
 * @c / separates, and a backslash is part of a name.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_output_name_separators(LAME_UNUSED void **state)
{
    static char out[PATH_MAX + 1];

#if defined(_WIN32)
    assert_int_equal(generateOutPath("E:/d/song.wav", "out", ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "out\\song.mp3");
    assert_int_equal(generateOutPath("E:\\d/song.wav", "out", ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "out\\song.mp3");
    assert_int_equal(generateOutPath("E:/d/song.wav", NULL, ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "E:/d\\song.mp3");
    assert_int_equal(generateOutPath("E:\\d\\song.wav", "out/", ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "out/song.mp3");
    assert_int_equal(generateOutPath("E:song.wav", "E:", ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "E:\\song.mp3");
    assert_int_equal(frontend_classify_suffix("E:/d.wav/song"), sf_unknown);
#else
    assert_int_equal(generateOutPath("d/x\\y.wav", NULL, ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "d/x\\y.mp3");
    assert_int_equal(generateOutPath("d/song.wav", "out\\", ".mp3", out, sizeof out), 0);
    assert_string_equal(out, "out\\/song.mp3");
#endif
}

/** @brief The size of the output buffer of test_output_name_size(). */
#define NAME_BYTES 16
/** @brief The bytes after that buffer that generateOutPath() must not write. */
#define GUARD_BYTES 8
/** @brief The value of each guard byte. */
#define GUARD_VALUE 0x7f

/**
 * @brief Checks that the output name fails when the output suffix does not fit
 *        the buffer, and is built when it just fits.
 *
 * "song.xyz" keeps its unknown suffix, and the output suffix has seven
 * characters. So the name is 15 characters, and needs all 16 bytes of the
 * buffer with its NUL.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_output_name_size(LAME_UNUSED void **state)
{
    char    out[NAME_BYTES + GUARD_BYTES];
    size_t  i;

    memset(out, GUARD_VALUE, sizeof out);
    assert_int_equal(generateOutPath("song.xyz", NULL, ".abcdef", out, NAME_BYTES - 1), 1);
    for (i = NAME_BYTES - 1; i < sizeof out; ++i)
        assert_int_equal(out[i], GUARD_VALUE);

    memset(out, GUARD_VALUE, sizeof out);
    assert_int_equal(generateOutPath("song.xyz", NULL, ".abcdef", out, NAME_BYTES), 0);
    assert_string_equal(out, "song.xyz.abcdef");
    for (i = NAME_BYTES; i < sizeof out; ++i)
        assert_int_equal(out[i], GUARD_VALUE);
}

/** @brief Registers and runs the suffix tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_suffix_formats),
        cmocka_unit_test(test_no_suffix),
        cmocka_unit_test(test_output_name),
        cmocka_unit_test(test_output_name_size),
        cmocka_unit_test(test_output_name_separators),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
