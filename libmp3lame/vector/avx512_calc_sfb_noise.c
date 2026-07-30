/*
 *      MP3 VBR scalefactor-band noise, AVX-512 intrinsics function
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
 *  This routine is two things joined together, and they behave very
 *  differently when made wider.
 *
 *  The first is the quantization - multiply, truncate, look up, add, truncate,
 *  then the error against the reconstructed value.  It is elementwise, so
 *  sixteen at a time computes exactly what four at a time computes, and the
 *  gathers are the same lane-for-lane shape as the ones in the quantization
 *  loop next door.  All of the width is usable here.
 *
 *  The second is a sum of squares, and a sum has an order.  The narrower tier
 *  pins gcc's and clang's (x0^2+x2^2)+(x1^2+x3^2) association per four-element
 *  block and accumulates those block sums one after another, so that every CPU
 *  of an architecture agrees to the last bit.  Widening a sum means
 *  re-associating it, and this value chooses a scalefactor, so re-associating
 *  it moves the bitstream.
 *
 *  Hence two forms, and the difference between them is the point:
 *
 *    default            The block decomposition and the accumulation order of
 *                       the narrower tier, exactly.  Four blocks are quantized
 *                       at once - that part is free - and then their four sums
 *                       are folded in one at a time, in order, in the pinned
 *                       association.  Output is bit-for-bit what the SSE2 tier
 *                       and the C code produce.  The width pays for the
 *                       quantization and not for the reduction.
 *
 *    LAME_AVX512_UNSAFE_REDUCTION
 *                       One sixteen-wide accumulator and a single reduction at
 *                       the end.  Faster, and *deliberately not* bit-exact: it
 *                       exists to measure what the pinned order costs, so that
 *                       the question "is the difference worth a quality
 *                       argument" has a number attached rather than an
 *                       opinion.  Not for a shipped build.
 *
 *  The masked tail matters more here than in the other kernels: these bands
 *  are short - a scalefactor band is often a handful of values - so what the
 *  narrower tier spends on its remainder is a large fraction of the work
 *  rather than a rounding error at the end of it.  The dead lanes are zeroed
 *  after the error is formed rather than simply left out, because a padded
 *  index would otherwise contribute -sfpow*pow43[0], which is not what the
 *  scalar loop puts there.
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

/* |x| for sixteen lanes: clear the sign bit.  Spelled with the integer AND so
   it needs nothing above the AVX-512 foundation. */
static AVX512_FUNCTION __m512
abs_ps_avx512(__m512 v)
{
    return _mm512_castsi512_ps(_mm512_and_si512(_mm512_castps_si512(v),
                                                _mm512_set1_epi32(0x7fffffff)));
}

/* One pass' quantization: x = xr34*sfpow34 already applied, then the
   two-truncate adj43[] step, leaving the integer indices l3.  Identical in
   effect to k_34_4()'s default path. */
static AVX512_FUNCTION __m512i
quant_block_avx512(__m512 x, const FLOAT * const adj, __mmask16 k)
{
    __m512i const r = _mm512_cvttps_epi32(x);
    __m512 const a = _mm512_mask_i32gather_ps(_mm512_setzero_ps(), k, r, adj, 4);

    return _mm512_cvttps_epi32(_mm512_add_ps(x, a));
}

/* d = |xr| - sfpow*pow43[l3], with the lanes past the end of the band forced
   to zero so they contribute nothing to the sum. */
static AVX512_FUNCTION __m512
noise_block_avx512(__m512i l3, __m512 vsfpow, __m512 absxr,
                   const FLOAT * const pw43, __mmask16 k)
{
    __m512 const p = _mm512_mask_i32gather_ps(_mm512_setzero_ps(), k, l3, pw43, 4);

    return _mm512_maskz_mov_ps(k, _mm512_sub_ps(absxr, _mm512_mul_ps(vsfpow, p)));
}

#ifndef LAME_AVX512_UNSAFE_REDUCTION
/* (s0+s2)+(s1+s3) within each four-element block, left in the block's first
   element.  The shuffles work inside each 128-bit lane, so this is the SSE2
   routine's reduction performed on four blocks at once - the same two
   operations in the same order, not a wider sum. */
static AVX512_FUNCTION __m512
hsum_pinned_avx512(__m512 sq)
{
    __m512 const lo = _mm512_add_ps(sq, _mm512_shuffle_ps(sq, sq, _MM_SHUFFLE(1, 0, 3, 2)));

    return _mm512_add_ps(lo, _mm512_shuffle_ps(lo, lo, _MM_SHUFFLE(1, 1, 1, 1)));
}
#endif

AVX512_FUNCTION FLOAT
calc_sfb_noise_x34_avx512(const FLOAT * xr, const FLOAT * xr34, unsigned int bw,
                          FLOAT sfpow, FLOAT sfpow34, const FLOAT * const adj,
                          const FLOAT * const pw43)
{
    __m512 const vsfpow34 = _mm512_set1_ps(sfpow34);
    __m512 const vsfpow = _mm512_set1_ps(sfpow);
#ifdef LAME_AVX512_UNSAFE_REDUCTION
    __m512  acc = _mm512_setzero_ps();
#else
    __m128  acc = _mm_setzero_ps();
#endif
    unsigned int i;

    for (i = 0; i < bw; i += 16) {
        unsigned int const have = bw - i;
        __mmask16 const k = have >= 16u ? (__mmask16) 0xffffu
                                        : (__mmask16) ((1u << have) - 1u);
        __m512 const x34 = _mm512_maskz_loadu_ps(k, xr34 + i);
        __m512i const l3 = quant_block_avx512(_mm512_mul_ps(x34, vsfpow34), adj, k);
        __m512 const absxr = abs_ps_avx512(_mm512_maskz_loadu_ps(k, xr + i));
        __m512 const d = noise_block_avx512(l3, vsfpow, absxr, pw43, k);
        __m512 const sq = _mm512_mul_ps(d, d);

#ifdef LAME_AVX512_UNSAFE_REDUCTION
        acc = _mm512_add_ps(acc, sq);
#else
        {
            /* Fold the four block sums in one at a time, lowest block first -
               the order the narrower tier accumulates them in.  A block past
               the end of the band sums to zero, and adding zero to a running
               total of squares leaves it unchanged, so the short case needs no
               separate arm. */
            __m512 const v = hsum_pinned_avx512(sq);

            acc = _mm_add_ss(acc, _mm512_castps512_ps128(v));
            acc = _mm_add_ss(acc, _mm512_extractf32x4_ps(v, 1));
            acc = _mm_add_ss(acc, _mm512_extractf32x4_ps(v, 2));
            acc = _mm_add_ss(acc, _mm512_extractf32x4_ps(v, 3));
        }
#endif
    }
#ifdef LAME_AVX512_UNSAFE_REDUCTION
    return _mm512_reduce_add_ps(acc);
#else
    return _mm_cvtss_f32(acc);
#endif
}

#endif /* HAVE_AVX512_INTRINSICS */
