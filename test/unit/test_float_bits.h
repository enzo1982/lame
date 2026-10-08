/**
 * @file
 * @ingroup unit_tests
 * @brief Floating point values from their IEEE-754 bit patterns, for the
 *        cmocka tests that pass NaN and the infinities.
 *
 * These tests are built with fast floating point math. Under it, the compiler
 * removes a NaN or an infinity that it can see at compile time. So each value
 * is built from its bit pattern through a volatile copy, which the compiler
 * cannot fold.
 */
#ifndef LAME_TEST_FLOAT_BITS_H
#define LAME_TEST_FLOAT_BITS_H

#include <stdint.h>
#include <string.h>

/**
 * @brief Returns the float with bit pattern @p bits.
 * @param bits  the bit pattern.
 * @return the float with that pattern.
 */
static inline float
float_from_bits(uint32_t bits)
{
    uint32_t volatile opaque = bits;
    uint32_t pattern;
    float   f;

    pattern = opaque;
    memcpy(&f, &pattern, sizeof f);
    return f;
}

/**
 * @brief Returns the double with bit pattern @p bits.
 * @param bits  the bit pattern.
 * @return the double with that pattern.
 */
static inline double
double_from_bits(uint64_t bits)
{
    uint64_t volatile opaque = bits;
    uint64_t pattern;
    double  d;

    pattern = opaque;
    memcpy(&d, &pattern, sizeof d);
    return d;
}

/**
 * @brief Returns a quiet NaN as a float.
 * @return the NaN.
 */
static inline float
float_nan(void)
{
    return float_from_bits(0x7FC00000u);
}

/**
 * @brief Returns an infinity as a float.
 * @param negative  nonzero for minus infinity.
 * @return the infinity.
 */
static inline float
float_inf(int negative)
{
    return float_from_bits(negative ? 0xFF800000u : 0x7F800000u);
}

/**
 * @brief Returns a quiet NaN as a double.
 * @return the NaN.
 */
static inline double
double_nan(void)
{
    return double_from_bits(0x7FF8000000000000ull);
}

#endif /* LAME_TEST_FLOAT_BITS_H */
