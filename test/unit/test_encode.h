/**
 * @file
 * @ingroup unit_tests
 * @brief Encodes a stereo signal in equal calls, flushes, and collects the
 *        stream, for the cmocka tests that need a whole stream.
 *
 * It uses no cmocka call. Each test decides what a failure means for it.
 */
#ifndef LAME_TEST_ENCODE_H
#define LAME_TEST_ENCODE_H

#include "lame.h"

/**
 * @brief Encodes @p calls blocks of @p per_call samples per channel, then
 *        flushes, and collects everything in @p out.
 * @param gfp       the encoder instance, after lame_init_params().
 * @param left      the left channel.
 * @param right     the right channel.
 * @param per_call  the samples per channel of each call.
 * @param calls     the number of calls.
 * @param stride    the samples between the starts of two calls; 0 encodes the
 *                  same block each time.
 * @param out       receives the stream.
 * @param cap       the size of @p out in bytes.
 * @return the bytes of the stream, or the first negative result of an encode
 *         or flush call.
 */
static inline int
encode_collect(lame_t gfp, const short *left, const short *right, int per_call, int calls,
               int stride, unsigned char *out, int cap)
{
    int     total = 0, c, n;

    for (c = 0; c < calls; c++) {
        n = lame_encode_buffer(gfp, left + c * stride, right + c * stride, per_call,
                               out + total, cap - total);
        if (n < 0)
            return n;
        total += n;
    }
    n = lame_encode_flush(gfp, out + total, cap - total);
    if (n < 0)
        return n;
    return total + n;
}

#endif /* LAME_TEST_ENCODE_H */
