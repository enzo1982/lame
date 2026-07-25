/*
 *      MP3 quantization of xr^(3/4), intrinsics functions
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
 *  This loop is elementwise - a multiply, a truncation, a table lookup, an
 *  add and a second truncation, with no accumulator anywhere - so the vector
 *  form computes each value exactly as the scalar one does and the output
 *  cannot move.  That is the whole reason it is written out here rather than
 *  left to the compiler.
 *
 *  Left to the compiler is in fact what happens on some toolchains: gcc and
 *  clang already emit this sequence, down to reassembling the four looked-up
 *  values with unpcklps.  MSVC does not, and leaves the loop essentially
 *  scalar.  Writing it once means every toolchain runs the same code, which
 *  is the same rule this library already applies to CPU dispatch.
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

SSE_FUNCTION void
quantize_lines_xrpow_sse2(unsigned int l, FLOAT istep, const FLOAT * xr, int *ix,
                          const FLOAT * const adj)
{
    __m128 const vistep = _mm_set1_ps(istep);
    unsigned int remaining;

    /* The caller's element accounting, reproduced exactly: four at a time,
       then an optional pair - and an odd l therefore leaves its last value
       untouched, as it always has. */
    l = l >> 1;
    remaining = l % 2;
    l = l >> 1;

    while (l--) {
        __m128 const x = _mm_mul_ps(_mm_loadu_ps(xr), vistep);
        int     r0, r1, r2, r3;

        /* The indices have to reach general registers to subscript the table.
           Each lane is shuffled down and converted on its own, which looks
           like more work than converting all four at once and reading them
           back from memory - but four narrow loads cannot all forward from
           one wide store, and that stall costs more than the shuffles do.
           Measured: the memory form was slower than the scalar loop it
           replaced. It is also the sequence gcc and clang emit unaided. */
        r0 = _mm_cvttss_si32(x);
        r1 = _mm_cvttss_si32(_mm_shuffle_ps(x, x, _MM_SHUFFLE(1, 1, 1, 1)));
        r2 = _mm_cvttss_si32(_mm_unpackhi_ps(x, x));
        r3 = _mm_cvttss_si32(_mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 3, 3, 3)));

        _mm_storeu_si128((__m128i *) ix,
                         _mm_cvttps_epi32(_mm_add_ps(x,
                                                     _mm_set_ps(adj[r3], adj[r2],
                                                                adj[r1], adj[r0]))));
        xr += 4;
        ix += 4;
    }
    if (remaining) {
        FLOAT   x0, x1;
        int     rx0, rx1;

        x0 = *xr++ * istep;
        x1 = *xr++ * istep;
        rx0 = (int) x0;
        rx1 = (int) x1;
        x0 += adj[rx0];
        x1 += adj[rx1];
        *ix++ = (int) x0;
        *ix++ = (int) x1;
    }
}

#endif /* HAVE_SSE2_INTRINSICS */
