/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the file-time helpers behind @c --preserve-modtime.
 *
 * This program compiles @c frontend/lametime.c into itself. The tests call the
 * two helpers in the same order as the frontend:
 * - @c lame_read_file_times() runs before anything opens the input.
 * - @c lame_write_file_times() runs after the output file exists.
 *
 * On a platform without @c utime(), both calls return failure. They do not
 * return success after doing nothing. The tests also pass on that build.
 * For this reason, each test checks two things: the return value and the
 * visible effect on the file.
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
 * @brief 1 if this build can set file times, 0 if it cannot.
 *
 * It uses the same three configure results as lametime.c: @c HAVE_UTIME,
 * @c HAVE_UTIME_H and @c HAVE_SYS_UTIME_H.
 */
#if defined(HAVE_UTIME) && (defined(HAVE_UTIME_H) || defined(HAVE_SYS_UTIME_H))
# define TEST_CAN_SET_TIMES 1
#else
# define TEST_CAN_SET_TIMES 0
#endif

/** @brief The file that the tests read the times from. */
#define SRC_NAME "lame_test_file_times_src.tmp"
/** @brief The file that the tests write the times to. */
#define DST_NAME "lame_test_file_times_dst.tmp"
/** @brief A file name that no test creates. The failure tests use it. */
#define GONE_NAME "lame_test_file_times_no_such_file.tmp"

/**
 * @brief 2001-02-03 04:05:06 UTC.
 *
 * This time is far from any clock that this test can run against. So a pass
 * cannot come from two files that have the same time by chance.
 */
#define KNOWN_TIME ((time_t) 981173106L)

/**
 * @brief 2002-03-04 05:06:07 UTC.
 *
 * A second known time. One test moves the times of the source to it.
 */
#define DISTURBED_TIME ((time_t) 1015218367L)


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
 * @brief Creates the source and destination files, and sets the times of the
 *        source to @c KNOWN_TIME.
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
 * @brief Returns the modification time of a file, as stat() reports it.
 * @param name  the file to check.
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
 * @brief Checks that the times read from one file are the times written to
 *        another file.
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
 * @brief Checks that the write uses the times that the read captured, not the
 *        current times of the source.
 * @param state cmocka fixture state (unused).
 *
 * This is why the copy uses two calls. The encode reads the source between
 * the two calls, and this changes the access time of the source. A single
 * call at the end would copy the time of the encode. The test moves both
 * times of the source to @c DISTURBED_TIME between the two calls. It checks
 * that they moved, and that the destination gets @c KNOWN_TIME.
 */
static void
test_read_then_disturb_then_write(LAME_UNUSED void **state)
{
    lame_file_times times;
    lame_file_times disturbed;
    struct stat before, moved, after;

    assert_int_equal(0, stat(SRC_NAME, &before));
    if (lame_read_file_times(SRC_NAME, &times) != 0) {
        skip();         /* nothing to preserve on this build */
    }

    disturbed.valid = 1;
    disturbed.actime = DISTURBED_TIME;
    disturbed.modtime = DISTURBED_TIME;
    assert_int_equal(0, lame_write_file_times(SRC_NAME, &disturbed));
    assert_int_equal(0, stat(SRC_NAME, &moved));
    assert_true(moved.st_mtime == DISTURBED_TIME);
    assert_true(moved.st_atime == DISTURBED_TIME);

    assert_int_equal(0, lame_write_file_times(DST_NAME, &times));
    assert_int_equal(0, stat(DST_NAME, &after));

    /* The captured value, not whatever the source carries now. */
    assert_true(after.st_mtime == KNOWN_TIME);
    assert_true(after.st_atime == KNOWN_TIME);
    assert_true(before.st_mtime == KNOWN_TIME);
}


/**
 * @brief Checks that reading a missing source fails and leaves no usable
 *        times.
 * @param state cmocka fixture state (unused).
 *
 * A later write would apply a half-filled structure as if its values were
 * real. So both calls must show the failure. The read returns -1 and clears
 * @c valid. The write returns -1 and does not change the destination.
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
 * @brief Checks that writing to a missing destination fails and does not
 *        create the file.
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
 * @brief Checks that both functions return -1 for a null argument and do not
 *        dereference it.
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
