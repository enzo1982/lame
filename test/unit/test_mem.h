/**
 * @file
 * @brief Searches in byte buffers for the cmocka tests.
 *
 * memmem() is not standard C, and this suite builds on several platforms.
 * An empty needle fails the calling test: it occurs everywhere, so an
 * assertion about it would hold whatever the buffer contains.
 */
#ifndef LAME_TEST_MEM_H
#define LAME_TEST_MEM_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

/**
 * @brief Returns 1 if the byte string @p needle occurs in @p hay.
 * @param hay     the bytes to search.
 * @param hn      the number of bytes in @p hay.
 * @param needle  the bytes to look for, without their terminating NUL.
 * @return 1 if @p needle occurs, 0 if it does not.
 */
static inline int
mem_contains(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t const nn = strlen(needle);
    size_t i;

    if (nn == 0)
        fail_msg("mem_contains: an empty needle");
    for (i = 0; i + nn <= hn; ++i) {
        if (memcmp(hay + i, needle, nn) == 0)
            return 1;
    }
    return 0;
}

/**
 * @brief Counts the places in @p hay where @p needle starts.
 * @param hay     the bytes to search.
 * @param hn      the number of bytes in @p hay.
 * @param needle  the bytes to look for, without their terminating NUL.
 * @return the number of places where @p needle starts, overlaps counted.
 */
static inline size_t
mem_count(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t const nn = strlen(needle);
    size_t i, n = 0;

    if (nn == 0)
        fail_msg("mem_count: an empty needle");
    for (i = 0; i + nn <= hn; ++i) {
        if (memcmp(hay + i, needle, nn) == 0)
            ++n;
    }
    return n;
}

/**
 * @brief Returns 1 if the ASCII @p needle occurs in @p hay as UTF-16 code
 *        units.
 *
 * UTF-16 text uses two bytes per character. So an ASCII needle never appears
 * as one run of bytes. The function searches for its wide form instead, in
 * little-endian or big-endian order: each character together with a zero
 * byte.
 *
 * @param hay     the bytes to search.
 * @param hn      the number of bytes in @p hay.
 * @param needle  the ASCII text to look for.
 * @return 1 if @p needle occurs in either byte order, 0 if it does not.
 */
static inline int
mem_contains_wide(const unsigned char *hay, size_t hn, const char *needle)
{
    size_t const nn = strlen(needle), wn = nn * 2;
    size_t i, j;

    if (nn == 0)
        fail_msg("mem_contains_wide: an empty needle");
    for (i = 0; i + wn <= hn; ++i) {
        int le = 1, be = 1;
        for (j = 0; j < nn; ++j) {
            unsigned char c = (unsigned char) needle[j];
            if (hay[i + 2 * j] != c || hay[i + 2 * j + 1] != 0)
                le = 0;
            if (hay[i + 2 * j] != 0 || hay[i + 2 * j + 1] != c)
                be = 0;
        }
        if (le || be)
            return 1;
    }
    return 0;
}

#endif /* LAME_TEST_MEM_H */
