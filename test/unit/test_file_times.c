/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the file-time helpers behind @c --preserve-modtime.
 *
 * @c frontend/lametime.c is compiled into this program, so the two halves are
 * exercised as the frontend uses them: @c lame_read_file_times() before
 * anything opens the input, @c lame_write_file_times() once the output exists.
 *
 * Where the platform has no @c utime(), both calls report failure rather than
 * returning success having done nothing; the arms below hold for that build
 * too, which is why they assert on the pair (return value, observable effect)
 * rather than on the return value alone.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cmocka.h>

#include "lametime.h"
#include "test_unused.h"

/**
 * @brief Whether this build can set file times at all.
 *
 * Decided from the same three configure answers as lametime.c.
 */
#if defined(HAVE_UTIME) && (defined(HAVE_UTIME_H) || defined(HAVE_SYS_UTIME_H))
# define TEST_CAN_SET_TIMES 1
#else
# define TEST_CAN_SET_TIMES 0
#endif

/** @brief The file whose times are captured. */
#define SRC_NAME "lame_test_file_times_src.tmp"
/** @brief The file the captured times are applied to. */
#define DST_NAME "lame_test_file_times_dst.tmp"
/** @brief A name no test creates, for the failure arms. */
#define GONE_NAME "lame_test_file_times_no_such_file.tmp"

/**
 * @brief 2001-02-03 04:05:06 UTC.
 *
 * Far from any clock this test could run against, so a pass cannot come
 * from the two files happening to share a timestamp.
 */
#define KNOWN_TIME ((time_t) 981173106L)


/**
 * @brief Creates a file with known contents.
 * @param name  the file to create.
 * @param text  what to put in it.
 */
static void
write_file(char const *name, char const *text)
{
    FILE   *fp = fopen(name, "wb");
    assert_non_null(fp);
    assert_true(fputs(text, fp) >= 0);
    assert_int_equal(0, fclose(fp));
}


/**
 * @brief Reads a file, for the sake of what reading does to its access time.
 * @param name  the file to read.
 */
static void
read_file(char const *name)
{
    FILE   *fp = fopen(name, "rb");
    char    buf[16];
    assert_non_null(fp);
    (void) fread(buf, 1, sizeof(buf), fp);
    assert_int_equal(0, fclose(fp));
}


/**
 * @brief Creates the source and destination files and stamps the source.
 * @param state cmocka fixture state (unused).
 * @return 0.
 */
static int
setup_files(LAME_UNUSED void **state)
{
    lame_file_times times;

    write_file(SRC_NAME, "source");
    write_file(DST_NAME, "destination");

    /* Stamp through the call under test: a build without utime() has no other
     * portable way, and the arms below hold either way. */
    times.valid = 1;
    times.actime = KNOWN_TIME;
    times.modtime = KNOWN_TIME;
    (void) lame_write_file_times(SRC_NAME, &times);
    return 0;
}


/**
 * @brief Removes what setup_files() created.
 * @param state cmocka fixture state (unused).
 * @return 0.
 */
static int
teardown_files(LAME_UNUSED void **state)
{
    remove(SRC_NAME);
    remove(DST_NAME);
    return 0;
}


/**
 * @brief The modification time a file carries, read back from the system.
 * @param name  the file to ask about.
 * @return its modification time.
 */
static time_t
mtime_of(char const *name)
{
    struct stat st;
    assert_int_equal(0, stat(name, &st));
    return st.st_mtime;
}


/**
 * @brief What is read from one file is what is written to another.
 * @param state cmocka fixture state (unused).
 */
static void
test_read_then_write(LAME_UNUSED void **state)
{
    lame_file_times times;
    int     rd, wr;

    rd = lame_read_file_times(SRC_NAME, &times);
    wr = lame_write_file_times(DST_NAME, &times);

#if TEST_CAN_SET_TIMES
    assert_int_equal(0, rd);
    assert_int_equal(1, times.valid);
    assert_int_equal(0, wr);
    assert_true(mtime_of(DST_NAME) == mtime_of(SRC_NAME));
    assert_true(mtime_of(DST_NAME) == KNOWN_TIME);
#else
    /* No way to set them: both halves say so, and the destination is left
     * alone rather than being reported as stamped. */
    assert_int_equal(-1, rd);
    assert_int_equal(0, times.valid);
    assert_int_equal(-1, wr);
    assert_true(mtime_of(DST_NAME) != KNOWN_TIME);
#endif
}


/**
 * @brief The times applied are the ones captured, not the ones on disk.
 * @param state cmocka fixture state (unused).
 *
 * The reason the copy is two calls rather than one. Between capturing the
 * source's times and applying them the source is read, which moves its access
 * time; a single call at the end would preserve the moment of the encode. A
 * test that never disturbs the source passes either way.
 */
static void
test_read_then_disturb_then_write(LAME_UNUSED void **state)
{
    lame_file_times times;
    struct stat before, after;

    assert_int_equal(0, stat(SRC_NAME, &before));
    if (lame_read_file_times(SRC_NAME, &times) != 0) {
        skip();         /* nothing to preserve on this build */
    }

    read_file(SRC_NAME);

    assert_int_equal(0, lame_write_file_times(DST_NAME, &times));
    assert_int_equal(0, stat(DST_NAME, &after));

    /* The captured value, not whatever the source carries now. */
    assert_true(after.st_mtime == KNOWN_TIME);
    assert_true(after.st_atime == KNOWN_TIME);
    assert_true(before.st_mtime == KNOWN_TIME);
}


/**
 * @brief A source that is not there fails, and leaves nothing usable behind.
 * @param state cmocka fixture state (unused).
 *
 * A half-filled structure would be applied by a later write as if it were
 * real, so the failure has to be visible in both halves.
 */
static void
test_read_missing_source(LAME_UNUSED void **state)
{
    lame_file_times times;
    time_t  before;

    memset(&times, 0xff, sizeof(times));
    assert_int_equal(-1, lame_read_file_times(GONE_NAME, &times));
    assert_int_equal(0, times.valid);

    before = mtime_of(DST_NAME);
    assert_int_equal(-1, lame_write_file_times(DST_NAME, &times));
    assert_true(mtime_of(DST_NAME) == before);
}


/**
 * @brief A destination that is not there fails, and is not created.
 * @param state cmocka fixture state (unused).
 */
static void
test_write_missing_destination(LAME_UNUSED void **state)
{
    lame_file_times times;
    struct stat st;

    (void) lame_read_file_times(SRC_NAME, &times);
    assert_int_equal(-1, lame_write_file_times(GONE_NAME, &times));
    assert_int_not_equal(0, stat(GONE_NAME, &st));
}


/**
 * @brief Null arguments are refused rather than dereferenced.
 * @param state cmocka fixture state (unused).
 */
static void
test_null_arguments(LAME_UNUSED void **state)
{
    lame_file_times times;

    assert_int_equal(-1, lame_read_file_times(SRC_NAME, NULL));
    assert_int_equal(-1, lame_read_file_times(NULL, &times));
    assert_int_equal(0, times.valid);
    assert_int_equal(-1, lame_write_file_times(NULL, &times));
    assert_int_equal(-1, lame_write_file_times(DST_NAME, NULL));
}


/** @brief Runs the file-time tests. */
int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_read_then_write,
                                        setup_files, teardown_files),
        cmocka_unit_test_setup_teardown(test_read_then_disturb_then_write,
                                        setup_files, teardown_files),
        cmocka_unit_test_setup_teardown(test_read_missing_source,
                                        setup_files, teardown_files),
        cmocka_unit_test_setup_teardown(test_write_missing_destination,
                                        setup_files, teardown_files),
        cmocka_unit_test_setup_teardown(test_null_arguments,
                                        setup_files, teardown_files),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
