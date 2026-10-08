/**
 * @file
 * @ingroup unit_tests
 * @brief Writes the fields of WAVE and AIFF test inputs, and hands the bytes
 *        to the reader as a stream.
 *
 * Each field is stored byte by byte in the order of its format, so an input
 * has the same bytes on big-endian and little-endian hosts.
 */
#ifndef LAME_TEST_BYTES_H
#define LAME_TEST_BYTES_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <cmocka.h>

/**
 * @brief Stores a field, least significant byte first.
 * @param p  where to store the field.
 * @param v  the value of the field.
 * @param n  the width of the field in bytes.
 */
static inline void
put_le(unsigned char *p, uint32_t v, int n)
{
    int     i;

    for (i = 0; i < n; ++i)
        p[i] = (unsigned char) (v >> (8 * i));
}

/**
 * @brief Stores a field, most significant byte first.
 * @param p  where to store the field.
 * @param v  the value of the field.
 * @param n  the width of the field in bytes.
 */
static inline void
put_be(unsigned char *p, uint32_t v, int n)
{
    int     i;

    for (i = 0; i < n; ++i)
        p[i] = (unsigned char) (v >> (8 * (n - 1 - i)));
}

/**
 * @brief Writes @p bytes to a temporary stream and rewinds it.
 * @param bytes  the bytes.
 * @param n      the number of bytes.
 * @return an open temporary stream, positioned at its start.
 */
static inline FILE *
bytes_stream(const unsigned char *bytes, size_t n)
{
    FILE   *f = tmpfile();

    assert_non_null(f);
    if (n > 0)
        assert_int_equal(fwrite(bytes, 1, n, f), n);
    rewind(f);
    return f;
}

#endif /* LAME_TEST_BYTES_H */
