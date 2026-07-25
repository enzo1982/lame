/*
 *      MP3 VBR scalefactor-band noise, SSE2 intrinsics function
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
 *  calc_sfb_noise_x34() quantizes a band and sums the squared quantization
 *  error.  Two parts: the quantization itself - multiply, truncate, look up
 *  adj43[], add, truncate - is the same elementwise kernel already written for
 *  quantize_lines, and computes value-for-value what the C loop does.  The
 *  second part, the sum of squares, is a reduction, and a reduction has an
 *  order.
 *
 *  gcc and clang, under the -ffast-math this library builds with, already
 *  reassociate the source's ((a+b)+(c+d)) into (x0^2+x2^2)+(x1^2+x3^2) - the
 *  natural SSE2 horizontal add - and agree with each other; MSVC at /fp:precise
 *  keeps the source order.  So no single implementation matches all three, and
 *  the project pins gcc and clang's association here (they build most shipped
 *  binaries and every Autotools platform).  This routine reduces exactly that
 *  way, per four-element block, accumulating into a running scalar - the same
 *  order the two compilers emit.  MSVC's output moves and is checked by the
 *  quality harness, not by a bitstream identity gate.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "lame_intrin.h"


#ifdef HAVE_SSE2_INTRINSICS

#include <emmintrin.h>

/* |x| for four lanes: clear the sign bit. */
#define ABS_MASK_PS (_mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)))

/* One block's quantization: x = xr34*sfpow34, then the two-truncate adj43[]
   gather, leaving the integer indices l3 as a vector.  Identical in effect to
   k_34_4()'s default path and to quantize_lines_xrpow_sse2's core. */
static SSE_FUNCTION __m128i
quant_block_sse2(__m128 x, const FLOAT * const adj)
{
    int     r0, r1, r2, r3;

    /* first truncate -> indices in general registers to subscript adj43[] */
    r0 = _mm_cvttss_si32(x);
    r1 = _mm_cvttss_si32(_mm_shuffle_ps(x, x, _MM_SHUFFLE(1, 1, 1, 1)));
    r2 = _mm_cvttss_si32(_mm_unpackhi_ps(x, x));
    r3 = _mm_cvttss_si32(_mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 3, 3, 3)));

    x = _mm_add_ps(x, _mm_set_ps(adj[r3], adj[r2], adj[r1], adj[r0]));
    return _mm_cvttps_epi32(x);       /* second truncate = l3 */
}

/* d = |xr| - sfpow*pow43[l3], four lanes. */
static SSE_FUNCTION __m128
noise_block_sse2(__m128i l3, __m128 vsfpow, const FLOAT * xr, const FLOAT * const pw43)
{
    int const i0 = _mm_cvtsi128_si32(l3);
    int const i1 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(1, 1, 1, 1)));
    int const i2 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(2, 2, 2, 2)));
    int const i3 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(3, 3, 3, 3)));
    __m128 const p = _mm_set_ps(pw43[i3], pw43[i2], pw43[i1], pw43[i0]);
    __m128 const absxr = _mm_and_ps(_mm_loadu_ps(xr), ABS_MASK_PS);

    return _mm_sub_ps(absxr, _mm_mul_ps(vsfpow, p));
}

/* (s0+s2)+(s1+s3), the pinned association, in lane 0.  Kept in a register and
   accumulated with _mm_add_ss rather than reduced to a scalar and added with C's
   +=, because -ffast-math reassociates a scalar float accumulator - and does it
   differently in the SSE2 and AVX2 translation units, which would make the two
   tiers' output differ per CPU.  An add_ss chain it leaves in the order written,
   so the association is pinned by the source, not by a compiler flag (which
   would also touch the FFT butterflies sharing this library). */
static SSE_FUNCTION __m128
hsum_pinned_sse2(__m128 sq)
{
    __m128 const lo = _mm_add_ps(sq, _mm_movehl_ps(sq, sq));  /* {s0+s2, s1+s3, ..} */

    return _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(1, 1, 1, 1)));
}

SSE_FUNCTION FLOAT
calc_sfb_noise_x34_sse2(const FLOAT * xr, const FLOAT * xr34, unsigned int bw,
                        FLOAT sfpow, FLOAT sfpow34, const FLOAT * const adj,
                        const FLOAT * const pw43)
{
    __m128 const vsfpow34 = _mm_set1_ps(sfpow34);
    __m128 const vsfpow = _mm_set1_ps(sfpow);
    unsigned int const full = bw >> 2u;
    unsigned int const rem = bw & 0x03u;
    __m128  acc = _mm_setzero_ps();
    unsigned int i;

    for (i = 0; i < full; ++i) {
        __m128i const l3 = quant_block_sse2(_mm_mul_ps(_mm_loadu_ps(xr34), vsfpow34), adj);
        __m128 const d = noise_block_sse2(l3, vsfpow, xr, pw43);

        acc = _mm_add_ss(acc, hsum_pinned_sse2(_mm_mul_ps(d, d)));
        xr += 4;
        xr34 += 4;
    }
    if (rem) {
        /* The scalar loop zero-fills the block, quantizes all four, then keeps
           only the first rem error terms and squares zeros for the rest.  Build
           the same: load rem lanes (others 0), quantize, then mask the error
           vector so the padding lanes contribute exactly 0 - the padded index
           would otherwise pick up -sfpow*pow43[0], which is not what the scalar
           loop stores there. */
        __m128 x34, x;
        __m128i l3;
        __m128 d;
        __m128i const lanes = _mm_set_epi32(3, 2, 1, 0);
        __m128 const keep = _mm_castsi128_ps(_mm_cmplt_epi32(lanes, _mm_set1_epi32((int) rem)));

        switch (rem) {
        case 3:  x34 = _mm_set_ps(0.f, xr34[2], xr34[1], xr34[0]);
                 x = _mm_set_ps(0.f, xr[2], xr[1], xr[0]); break;
        case 2:  x34 = _mm_set_ps(0.f, 0.f, xr34[1], xr34[0]);
                 x = _mm_set_ps(0.f, 0.f, xr[1], xr[0]); break;
        default: x34 = _mm_set_ps(0.f, 0.f, 0.f, xr34[0]);
                 x = _mm_set_ps(0.f, 0.f, 0.f, xr[0]); break;
        }
        l3 = quant_block_sse2(_mm_mul_ps(x34, vsfpow34), adj);
        {
            int const i0 = _mm_cvtsi128_si32(l3);
            int const i1 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(1, 1, 1, 1)));
            int const i2 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(2, 2, 2, 2)));
            int const i3 = _mm_cvtsi128_si32(_mm_shuffle_epi32(l3, _MM_SHUFFLE(3, 3, 3, 3)));
            __m128 const p = _mm_set_ps(pw43[i3], pw43[i2], pw43[i1], pw43[i0]);
            __m128 const absxr = _mm_and_ps(x, ABS_MASK_PS);

            d = _mm_and_ps(_mm_sub_ps(absxr, _mm_mul_ps(vsfpow, p)), keep);
        }
        acc = _mm_add_ss(acc, hsum_pinned_sse2(_mm_mul_ps(d, d)));
    }
    return _mm_cvtss_f32(acc);
}

#endif /* HAVE_SSE2_INTRINSICS */
