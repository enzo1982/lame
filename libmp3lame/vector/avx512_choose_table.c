/*
 *      MP3 Huffman table selection, AVX-512 intrinsics functions
 *
 *      Copyright (c) 2026 The LAME project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.     See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 *  Two of the three narrower routines are here.  The third, the one that reads
 *  three code-length tables at once, is not: those tables are bytes, the
 *  subscripts are wider than bytes, and a gather that fetches one useful byte
 *  per lane is slower than the loop it replaces.  A compiler asked to
 *  vectorise it produces exactly that, at some length, which is a reasonable
 *  way to find out.  It stays scalar here.
 *
 *  The two that are here are written against what the narrower tiers do, and
 *  differ from them in three places where the wider registers change the
 *  answer rather than the size:
 *
 *  - The region maximum compares full thirty-two-bit values.  The narrower
 *    tiers first pack to sixteen bits, which is what makes them quick and also
 *    what makes them saturate; with sixteen lanes of thirty-two bits there is
 *    nothing to gain by narrowing, so the result is the exact maximum and the
 *    caveat in the header does not apply to it.
 *
 *  - The escape-table search subscripts a table of thirty-two-bit values with
 *    thirty-two-bit indices.  Fetching sixteen of them takes one gather; a
 *    gather built on sixty-four-bit indices would fetch eight.
 *
 *  - The count of values clamped to fifteen stays in a mask register and is
 *    read with a population count.  The narrower tiers turn the comparison
 *    back into a vector, subtract it into per-lane counters, and reduce those
 *    at the end, because that is all sixteen-bit lanes can do.  Here the
 *    comparison already is the count.
 *
 *  Both routines reach the end of their region under a mask rather than in a
 *  scalar loop, so a short region is the same code as a long one.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_intrin.h"


#ifdef HAVE_AVX512_INTRINSICS

#include <immintrin.h>

AVX512_FUNCTION int
ix_max_avx512(const int *ix, const int *const end)
{
    __m512i acc = _mm512_setzero_si512();

    while (end - ix >= 16) {
        acc = _mm512_max_epi32(acc, _mm512_loadu_si512((const void *) ix));
        ix += 16;
    }
    if (ix < end) {
        /* The inactive lanes read as zero, and the accumulator started at zero
           over values that are magnitudes, so they cannot raise the maximum. */
        __mmask16 const k = (__mmask16) ((1u << (unsigned int) (end - ix)) - 1u);

        acc = _mm512_max_epi32(acc, _mm512_maskz_loadu_epi32(k, ix));
    }
    return _mm512_reduce_max_epi32(acc);
}


AVX512_FUNCTION unsigned int
count_bit_esc_avx512(const int *ix, const int *const end,
                     const uint32_t * const largetbl, unsigned int *const nclamped)
{
    /* The region is a sequence of pairs, so the two halves of each pair are
       gathered into vectors of their own: sixteen pairs, thirty-two values. */
    __m512i const evens = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14,
                                            16, 18, 20, 22, 24, 26, 28, 30);
    __m512i const odds = _mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15,
                                           17, 19, 21, 23, 25, 27, 29, 31);
    __m512i const v14 = _mm512_set1_epi32(14);
    __m512i const v15 = _mm512_set1_epi32(15);
    __m512i vsum = _mm512_setzero_si512();
    unsigned int n = 0;

    while (ix < end) {
        unsigned int const pairs = (unsigned int) (end - ix) >> 1;
        unsigned int const np = pairs < 16u ? pairs : 16u;
        unsigned int const nv = np * 2u;
        __mmask16 const ka = nv >= 16u ? (__mmask16) 0xffffu
                                       : (__mmask16) ((1u << nv) - 1u);
        __mmask16 const kb = nv <= 16u ? (__mmask16) 0u
                                       : (__mmask16) ((1u << (nv - 16u)) - 1u);
        __mmask16 const kp = np >= 16u ? (__mmask16) 0xffffu
                                       : (__mmask16) ((1u << np) - 1u);
        __m512i const a = _mm512_maskz_loadu_epi32(ka, ix);
        __m512i const b = _mm512_maskz_loadu_epi32(kb, ix + 16);
        __m512i x = _mm512_permutex2var_epi32(a, evens, b);
        __m512i y = _mm512_permutex2var_epi32(a, odds, b);
        __m512i idx;

        /* A comparison is already a count; only live pairs may contribute. */
        n += (unsigned int) _mm_popcnt_u32(_mm512_mask_cmpgt_epu32_mask(kp, x, v14));
        n += (unsigned int) _mm_popcnt_u32(_mm512_mask_cmpgt_epu32_mask(kp, y, v14));

        x = _mm512_min_epu32(x, v15);
        y = _mm512_min_epu32(y, v15);
        idx = _mm512_add_epi32(_mm512_slli_epi32(x, 4), y);

        vsum = _mm512_mask_add_epi32(vsum, kp, vsum,
                                     _mm512_mask_i32gather_epi32(_mm512_setzero_si512(),
                                                                 kp, idx, largetbl, 4));
        ix += nv;
    }
    /* Integer addition, so the order the lanes are folded in does not change
       the total. */
    *nclamped = n;
    return (unsigned int) _mm512_reduce_add_epi32(vsum);
}

#endif /* HAVE_AVX512_INTRINSICS */
