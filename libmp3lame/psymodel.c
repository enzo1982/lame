/*
 *      psymodel.c
 *
 *      Copyright (c) 1999-2000 Mark Taylor
 *      Copyright (c) 2001-2002 Naoki Shibata
 *      Copyright (c) 2000-2003 Takehiro Tominaga
 *      Copyright (c) 2000-2012 Robert Hegemann
 *      Copyright (c) 2000-2005 Gabriel Bouvigne
 *      Copyright (c) 2000-2005 Alexander Leidinger
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */


/**
 * \file
 * \internal
 * \brief Psychoacoustic model: per-band masking thresholds, block type and
 *        perceptual entropy.
 *
 * The model answers one question per granule: how much quantisation noise may
 * each scalefactor band carry before a listener would hear it. Everything
 * downstream consumes that answer - the quantisation loops shape noise to the
 * thresholds, the bit reservoir is steered by the perceptual entropy, and the
 * stereo mode decision reads the per-channel thresholds.
 *
 * \par One entry point, every mode
 *
 * L3psycho_anal_vbr() serves CBR, ABR and VBR alike. The name records which
 * encoder the code was written for, not which ones reach it.
 *
 * \par Results lag the input by one granule
 *
 * A granule's block type cannot be settled until the following granule has been
 * analysed, because an attack decides retroactively that its predecessor has to
 * be a start block. The analysis therefore runs on the current granule and
 * returns the previous granule's results; callers carry that delay.
 *
 * \par Two frequency partitionings
 *
 * Analysis runs on a partition-band grid of at most CBANDS bands, each about
 * DELBARK bark wide on the Zwicker scale (freq2bark()); how many are actually
 * used depends on the sample rate and is decided by init_numline(). Results are
 * mapped at the end onto the scalefactor bands the bitstream quantises in -
 * SBMAX_l long, SBMAX_s short - by convert_partition2scalefac(), which splits a
 * partition straddling a scalefactor-band boundary between its two neighbours.
 *
 * \par The analysis filterbank is not the coding filterbank
 *
 * Thresholds are computed from a dedicated FFT rather than from the MDCT the
 * encoder quantises: 1024 points with a Blackman window for long blocks, 256
 * with a Hann window for each of the three short sub-blocks (init_fft()). A
 * transform that is not critically sampled retains the signal's full energy and
 * offers a magnitude spectrum to measure tonality on; the thresholds it yields
 * are scaled into the coder's domain where they are consumed.
 *
 * \par What the model computes, in order
 *
 * -# Attack detection. vbrpsy_attack_detection() high-passes the input at
 *    fs/4, splits the granule into nine sub-blocks and compares each against
 *    the one two positions earlier. A large enough ratio counts as an attack,
 *    and attacks select short blocks.
 * -# Energy per partition band, from the FFT (calc_energy()).
 * -# Tonality, as a peak-to-average energy ratio over a three-partition
 *    neighbourhood (calc_mask_index_l(), vbrpsy_calc_mask_index_s()). Tone-like
 *    content masks less well than noise-like content, so the index selects how
 *    far below the signal the band's threshold sits.
 * -# Spreading. Every band masks its neighbours through s3_func(), and the
 *    contributions are combined by vbrpsy_mask_add(), which is not a plain
 *    power sum.
 * -# Pre-echo control, holding thresholds down where raising them would let
 *    noise precede a transient: against the two preceding granules in the long
 *    path (vbrpsy_compute_masking_l()), and against the preceding sub-block,
 *    keyed on where in the granule the attack falls, in the short path - that
 *    one lives in L3psycho_anal_vbr() itself, after the scalefactor-band
 *    mapping, because only there is the attack position known.
 * -# Mid/side thresholds, in joint stereo only
 *    (vbrpsy_compute_MS_thresholds()).
 * -# Perceptual entropy, an estimate of how much the granule will cost to code
 *    (pecalc_l(), pecalc_s()).
 *
 * \par The absolute threshold is a separate mechanism
 *
 * Masking thresholds are relative to the signal; the absolute threshold of
 * hearing bounds them from below, and is computed elsewhere - ATHformula(),
 * together with the loudness-driven adjustment in adjust_ATH().
 */




#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <float.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "psymodel.h"
#include "lame_global_flags.h"
#include "fft.h"
#include "lame-analysis.h"


#define NSFIRLEN 21

#ifdef M_LN10
#define  LN_TO_LOG10  (M_LN10/10)
#else
#define  LN_TO_LOG10  0.2302585093
#endif


/*
   L3psycho_anal.  Compute psycho acoustics.

   Data returned to the calling program must be delayed by one 
   granule. 

   This is done in two places.  
   If we do not need to know the blocktype, the copying
   can be done here at the top of the program: we copy the data for
   the last granule (computed during the last call) before it is
   overwritten with the new data.  It looks like this:
  
   0. static psymodel_data 
   1. calling_program_data = psymodel_data
   2. compute psymodel_data
    
   For data which needs to know the blocktype, the copying must be
   done at the end of this loop, and the old values must be saved:
   
   0. static psymodel_data_old 
   1. compute psymodel_data
   2. compute possible block type of this granule
   3. compute final block type of previous granule based on #2.
   4. calling_program_data = psymodel_data_old
   5. psymodel_data_old = psymodel_data
*/





/* psycho_loudness_approx
   jd - 2001 mar 12
in:  energy   - BLKSIZE/2 elements of frequency magnitudes ^ 2
     gfp      - uses out_samplerate, ATHtype (also needed for ATHformula)
returns: loudness^2 approximation, a positive value roughly tuned for a value
         of 1.0 for signals near clipping.
notes:   When calibrated, feeding this function binary white noise at sample
         values +32767 or -32768 should return values that approach 3.
         ATHformula is used to approximate an equal loudness curve.
future:  Data indicates that the shape of the equal loudness curve varies
         with intensity.  This function might be improved by using an equal
         loudness curve shaped for typical playback levels (instead of the
         ATH, that is shaped for the threshold).  A flexible realization might
         simply bend the existing ATH curve to achieve the desired shape.
         However, the potential gain may not be enough to justify an effort.
*/
/**
 * \brief Approximate the loudness of a granule as an equal-loudness-weighted
 *        power sum.
 *
 * The weights in \a eql_w come from the absolute threshold of hearing, which
 * approximates an equal-loudness contour. Calibrated so that a signal near
 * clipping returns about 1.0, and full-scale binary white noise (samples at
 * +32767 / -32768) approaches 3.
 *
 * The result drives adjust_ATH(), and through it the only place the encoder
 * adapts to programme level.
 *
 * \todo The shape of an equal-loudness contour varies with intensity, and the
 *       ATH is the shape at threshold rather than at a typical playback level.
 *       Bending the curve towards a playback-level shape would be more
 *       faithful; whether the difference is worth the effort is unmeasured.
 */
static  FLOAT
psycho_loudness_approx(FLOAT const *energy, FLOAT const *eql_w)
{
    int     i;
    FLOAT   loudness_power;

    loudness_power = 0.0;
    /* apply weights to power in freq. bands */
    for (i = 0; i < BLKSIZE / 2; ++i)
        loudness_power += energy[i] * eql_w[i];
    loudness_power *= VO_SCALE;

    return loudness_power;
}

/* mask_add optimization */
/* init the limit values used to avoid computing log in mask_add when it is not necessary */

/* For example, with i = 10*log10(m2/m1)/10*16         (= log10(m2/m1)*16)
 *
 * abs(i)>8 is equivalent (as i is an integer) to
 * abs(i)>=9
 * i>=9 || i<=-9
 * equivalent to (as i is the biggest integer smaller than log10(m2/m1)*16 
 * or the smallest integer bigger than log10(m2/m1)*16 depending on the sign of log10(m2/m1)*16)
 * log10(m2/m1)>=9/16 || log10(m2/m1)<=-9/16
 * exp10 is strictly increasing thus this is equivalent to
 * m2/m1 >= 10^(9/16) || m2/m1<=10^(-9/16) which are comparisons to constants
 */


#define I1LIMIT 8       /* as in if(i>8)  */
#define I2LIMIT 23      /* as in if(i>24) -> changed 23 */
#define MLIMIT  15      /* as in if(m<15) */

/* pow(10, (I1LIMIT + 1) / 16.0); */
static const FLOAT ma_max_i1 = 3.6517412725483771;
/* pow(10, (I2LIMIT + 1) / 16.0); */
static const FLOAT ma_max_i2 = 31.622776601683793;
/* pow(10, (MLIMIT) / 10.0); */
static const FLOAT ma_max_m  = 31.622776601683793;

    /*This is the masking table:
       According to tonality, values are going from 0dB (TMN)
       to 9.3dB (NMT).
       After additive masking computation, 8dB are added, so
       final values are going from 8dB to 17.3dB
     */
static const FLOAT tab[] = {
    1.0 /*pow(10, -0) */ ,
    0.79433 /*pow(10, -0.1) */ ,
    0.63096 /*pow(10, -0.2) */ ,
    0.63096 /*pow(10, -0.2) */ ,
    0.63096 /*pow(10, -0.2) */ ,
    0.63096 /*pow(10, -0.2) */ ,
    0.63096 /*pow(10, -0.2) */ ,
    0.25119 /*pow(10, -0.6) */ ,
    0.11749             /*pow(10, -0.93) */
};

static const int tab_mask_add_delta[] = { 2, 2, 2, 1, 1, 1, 0, 0, -1 };
#define STATIC_ASSERT_EQUAL_DIMENSION(A,B) enum{static_assert_##A=1/((dimension_of(A) == dimension_of(B))?1:0)}

/**
 * \brief Bark distance, in partition bands, within which two maskers are
 *        combined non-linearly by vbrpsy_mask_add().
 *
 * Indexed by the tonality index: tone-like partitions get a narrower window
 * than noise-like ones. Roughly three partitions to the bark.
 */
inline static int
mask_add_delta(int i)
{
    STATIC_ASSERT_EQUAL_DIMENSION(tab_mask_add_delta,tab);
    assert(i < (int)dimension_of(tab));
    return tab_mask_add_delta[i];
}


/**
 * \brief Assert that vbrpsy_mask_add()'s precomputed comparison limits still
 *        equal the expressions they were derived from.
 *
 * Compiled to nothing under NDEBUG. The limits exist so the hot path can
 * compare energies directly instead of taking a logarithm.
 */
static void
init_mask_add_max_values(void)
{
#ifndef NDEBUG
    FLOAT const _ma_max_i1 = pow(10, (I1LIMIT + 1) / 16.0);
    FLOAT const _ma_max_i2 = pow(10, (I2LIMIT + 1) / 16.0);
    FLOAT const _ma_max_m = pow(10, (MLIMIT) / 10.0);
    assert(fabs(ma_max_i1 - _ma_max_i1) <= FLT_EPSILON);
    assert(fabs(ma_max_i2 - _ma_max_i2) <= FLT_EPSILON);
    assert(fabs(ma_max_m  - _ma_max_m ) <= FLT_EPSILON);
#endif
}




/* addition of simultaneous masking   Naoki Shibata 2000/7 */
/**
 * \brief Combine two masking contributions.
 *
 * Not a power sum. Within \a delta partitions and a moderate level ratio the
 * two add with a boost above unity; further apart, or with one masker well
 * above the other, the result falls back to a plain sum or to the larger of
 * the two alone.
 *
 * Modelling addition this way matters because a plain sum over many
 * neighbouring bands overestimates how much masking a spread-out signal
 * actually provides.
 *
 * Naoki Shibata, 2000.
 */
inline static FLOAT
vbrpsy_mask_add(FLOAT m1, FLOAT m2, int b, int delta)
{
    static const FLOAT table2[] = {
        1.33352 * 1.33352, 1.35879 * 1.35879, 1.38454 * 1.38454, 1.39497 * 1.39497,
        1.40548 * 1.40548, 1.3537 * 1.3537, 1.30382 * 1.30382, 1.22321 * 1.22321,
        1.14758 * 1.14758,
        1
    };

    FLOAT   ratio;

    if (m1 < 0) {
        m1 = 0;
    }
    if (m2 < 0) {
        m2 = 0;
    }
    if (m1 <= 0) {
        return m2;
    }
    if (m2 <= 0) {
        return m1;
    }
    if (m2 > m1) {
        ratio = m2 / m1;
    }
    else {
        ratio = m1 / m2;
    }
    if (abs(b) <= delta) {       /* approximately, 1 bark = 3 partitions */
        /* originally 'if(i > 8)' */
        if (ratio >= ma_max_i1) {
            return m1 + m2;
        }
        else {
            int     i = (int) (FAST_LOG10_X(ratio, 16.0f));
            return (m1 + m2) * table2[i];
        }
    }
    if (ratio < ma_max_i2) {
        return m1 + m2;
    }
    if (m1 < m2) {
        m1 = m2;
    }
    return m1;
}


/* short block threshold calculation (part 2)

    partition band bo_s[sfb] is at the transition from scalefactor
    band sfb to the next one sfb+1; enn and thmm have to be split
    between them
*/
/**
 * \brief Map partition-band energies and thresholds onto scalefactor bands.
 *
 * The two grids do not align: the analysis works in partition bands of roughly
 * constant bark width, the bitstream quantises in the scalefactor bands the
 * standard defines. A partition straddling a scalefactor-band boundary is
 * split between the two, weighted by how much of it falls on each side.
 */
static void
convert_partition2scalefac(PsyConst_CB2SB_t const *const gd, FLOAT const *eb, FLOAT const *thr,
                           FLOAT enn_out[], FLOAT thm_out[])
{
    FLOAT   enn, thmm;
    int     sb, b, n = gd->n_sb;
    enn = thmm = 0.0f;
    for (sb = b = 0; sb < n; ++b, ++sb) {
        int const bo_sb = gd->bo[sb];
        int const npart = gd->npart;
        int const b_lim = bo_sb < npart ? bo_sb : npart;
        while (b < b_lim) {
            assert(eb[b] >= 0); /* iff failed, it may indicate some index error elsewhere */
            assert(thr[b] >= 0);
            enn += eb[b];
            thmm += thr[b];
            b++;
        }
        if (b >= npart) {
            enn_out[sb] = enn;
            thm_out[sb] = thmm;
            ++sb;
            break;
        }
        assert(eb[b] >= 0); /* iff failed, it may indicate some index error elsewhere */
        assert(thr[b] >= 0);
        {
            /* at transition sfb -> sfb+1 */
            FLOAT const w_curr = gd->bo_weight[sb];
            FLOAT const w_next = 1.0f - w_curr;
            enn += w_curr * eb[b];
            thmm += w_curr * thr[b];
            enn_out[sb] = enn;
            thm_out[sb] = thmm;
            enn = w_next * eb[b];
            thmm = w_next * thr[b];
        }
    }
    /* zero initialize the rest */
    for (; sb < n; ++sb) {
        enn_out[sb] = 0;
        thm_out[sb] = 0;
    }
}

/**
 * \brief convert_partition2scalefac() for one short sub-block, storing into
 *        the per-channel short-block state.
 */
static void
convert_partition2scalefac_s(lame_internal_flags * gfc, FLOAT const *eb, FLOAT const *thr, int chn,
                             int sblock)
{
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gds = &gfc->cd_psy->s;
    FLOAT   enn[SBMAX_s], thm[SBMAX_s];
    int     sb;
    convert_partition2scalefac(gds, eb, thr, enn, thm);
    for (sb = 0; sb < SBMAX_s; ++sb) {
        psv->en[chn].s[sb][sblock] = enn[sb];
        psv->thm[chn].s[sb][sblock] = thm[sb];
    }
}

/* longblock threshold calculation (part 2) */
/**
 * \brief convert_partition2scalefac() for a long block, storing into the
 *        per-channel long-block state.
 */
static void
convert_partition2scalefac_l(lame_internal_flags * gfc, FLOAT const *eb, FLOAT const *thr, int chn)
{
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gdl = &gfc->cd_psy->l;
    FLOAT  *enn = &psv->en[chn].l[0];
    FLOAT  *thm = &psv->thm[chn].l[0];
    convert_partition2scalefac(gdl, eb, thr, enn, thm);
}

/**
 * \brief Derive short-block thresholds from a long-block analysis.
 *
 * Used where the short-block analysis was not run but short-block thresholds
 * are still required. The threshold is scaled down by a constant factor and
 * the same value is given to all three sub-blocks, so it is a placeholder
 * rather than an analysis: it cannot distinguish the sub-blocks from one
 * another.
 */
static void
convert_partition2scalefac_l_to_s(lame_internal_flags * gfc, FLOAT const *eb, FLOAT const *thr,
                                  int chn)
{
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gds = &gfc->cd_psy->l_to_s;
    FLOAT   enn[SBMAX_s], thm[SBMAX_s];
    int     sb, sblock;
    convert_partition2scalefac(gds, eb, thr, enn, thm);
    for (sb = 0; sb < SBMAX_s; ++sb) {
        FLOAT const scale = 1. / 64.f;
        FLOAT const tmp_enn = enn[sb];
        FLOAT const tmp_thm = thm[sb] * scale;
        for (sblock = 0; sblock < 3; ++sblock) {
            psv->en[chn].s[sb][sblock] = tmp_enn;
            psv->thm[chn].s[sb][sblock] = tmp_thm;
        }
    }
}



/**
 * \brief Geometric interpolation, \a x raised to \a r times \a y raised to
 *        1 - \a r.
 *
 * The two endpoints are shortcut, which is worth doing here: \a r is at or
 * above 1 in the overwhelming majority of calls.
 */
static inline FLOAT
NS_INTERP(FLOAT x, FLOAT y, FLOAT r)
{
    /* was pow((x),(r))*pow((y),1-(r)) */
    if (r >= 1.0f)
        return x;       /* 99.7% of the time */
    if (r <= 0.0f)
        return y;
    if (y > 0.0f)
        return powf(x / y, r) * y; /* rest of the time */
    return 0.0f;        /* never happens */
}



/**
 * \brief Perceptual entropy of a short-block granule.
 *
 * Sums a per-band weight times the logarithm of the energy-to-threshold ratio,
 * over the bands where the energy exceeds the threshold. The result estimates
 * how expensive the granule will be to code, and steers the bit reservoir and
 * the VBR bitrate.
 *
 * \note The band weights are tuned for 44.1 kHz only.
 */
static  FLOAT
pecalc_s(III_psy_ratio const *mr, FLOAT masking_lower)
{
    FLOAT   pe_s;
    static const FLOAT regcoef_s[] = {
        11.8,           /* these values are tuned only for 44.1kHz... */
        13.6,
        17.2,
        32,
        46.5,
        51.3,
        57.5,
        67.1,
        71.5,
        84.6,
        97.6,
        130,
/*      255.8 */
    };
    unsigned int sb, sblock;

    pe_s = 1236.28f / 4;
    for (sb = 0; sb < SBMAX_s - 1; sb++) {
        for (sblock = 0; sblock < 3; sblock++) {
            FLOAT const thm = mr->thm.s[sb][sblock];
            assert(sb < dimension_of(regcoef_s));
            if (thm > 0.0f) {
                FLOAT const x = thm * masking_lower;
                FLOAT const en = mr->en.s[sb][sblock];
                if (en > x) {
                    if (en > x * 1e10f) {
                        pe_s += regcoef_s[sb] * (10.0f * LOG10);
                    }
                    else {
                        assert(x > 0);
                        pe_s += regcoef_s[sb] * FAST_LOG10(en / x);
                    }
                }
            }
        }
    }

    return pe_s;
}

/**
 * \brief Perceptual entropy of a long-block granule.
 *
 * \see pecalc_s(), including the note about the sample rate the band weights
 *      were tuned at.
 */
static  FLOAT
pecalc_l(III_psy_ratio const *mr, FLOAT masking_lower)
{
    FLOAT   pe_l;
    static const FLOAT regcoef_l[] = {
        6.8,            /* these values are tuned only for 44.1kHz... */
        5.8,
        5.8,
        6.4,
        6.5,
        9.9,
        12.1,
        14.4,
        15,
        18.9,
        21.6,
        26.9,
        34.2,
        40.2,
        46.8,
        56.5,
        60.7,
        73.9,
        85.7,
        93.4,
        126.1,
/*      241.3 */
    };
    unsigned int sb;

    pe_l = 1124.23f / 4;
    for (sb = 0; sb < SBMAX_l - 1; sb++) {
        FLOAT const thm = mr->thm.l[sb];
        assert(sb < dimension_of(regcoef_l));
        if (thm > 0.0f) {
            FLOAT const x = thm * masking_lower;
            FLOAT const en = mr->en.l[sb];
            if (en > x) {
                if (en > x * 1e10f) {
                    pe_l += regcoef_l[sb] * (10.0f * LOG10);
                }
                else {
                    assert(x > 0);
                    pe_l += regcoef_l[sb] * FAST_LOG10(en / x);
                }
            }
        }
    }

    return pe_l;
}


/**
 * \brief Total, peak and mean FFT line energy for each partition band.
 *
 * The peak and the mean are what calc_mask_index_l() compares to decide how
 * tone-like the band is; the total is the masker strength.
 */
static void
calc_energy(PsyConst_CB2SB_t const *l, FLOAT const *fftenergy, FLOAT * eb, FLOAT * max, FLOAT * avg)
{
    int     b, j;

    for (b = j = 0; b < l->npart; ++b) {
        FLOAT   ebb = 0, m = 0;
        int     i;
        for (i = 0; i < l->numlines[b]; ++i, ++j) {
            FLOAT const el = fftenergy[j];
            assert(el >= 0);
            ebb += el;
            if (m < el)
                m = el;
        }
        eb[b] = ebb;
        max[b] = m;
        avg[b] = ebb * l->rnumlines[b];
        assert(l->rnumlines[b] >= 0);
        assert(ebb >= 0);
        assert(eb[b] >= 0);
        assert(max[b] >= 0);
        assert(avg[b] >= 0);
    }
}


/**
 * \brief Tonality index per partition band, for long blocks.
 *
 * Measured as the ratio of the peak line to the mean, taken over the band and
 * its two neighbours: a spectrum concentrated in few lines is tone-like, one
 * spread evenly is noise-like. The index selects an entry in the table that
 * decides how far below the signal the band's threshold is placed, because a
 * tone masks noise far less effectively than noise masks a tone.
 *
 * This is the only tonality estimator in the encoder.
 */
static void
calc_mask_index_l(lame_internal_flags const *gfc, FLOAT const *max,
                  FLOAT const *avg, unsigned char *mask_idx)
{
    PsyConst_CB2SB_t const *const gdl = &gfc->cd_psy->l;
    FLOAT   m, a;
    int     b, k;
    int const last_tab_entry = sizeof(tab) / sizeof(tab[0]) - 1;
    b = 0;
    a = avg[b] + avg[b + 1];
    assert(a >= 0);
    if (a > 0.0f) {
        m = max[b];
        if (m < max[b + 1])
            m = max[b + 1];
        assert((gdl->numlines[b] + gdl->numlines[b + 1] - 1) > 0);
        a = 20.0f * (m * 2.0f - a)
            / (a * (gdl->numlines[b] + gdl->numlines[b + 1] - 1));
        k = (int) a;
        if (k > last_tab_entry)
            k = last_tab_entry;
        mask_idx[b] = k;
    }
    else {
        mask_idx[b] = 0;
    }

    for (b = 1; b < gdl->npart - 1; b++) {
        a = avg[b - 1] + avg[b] + avg[b + 1];
        assert(a >= 0);
        if (a > 0.0f) {
            m = max[b - 1];
            if (m < max[b])
                m = max[b];
            if (m < max[b + 1])
                m = max[b + 1];
            assert((gdl->numlines[b - 1] + gdl->numlines[b] + gdl->numlines[b + 1] - 1) > 0);
            a = 20.0f * (m * 3.0f - a)
                / (a * (gdl->numlines[b - 1] + gdl->numlines[b] + gdl->numlines[b + 1] - 1));
            k = (int) a;
            if (k > last_tab_entry)
                k = last_tab_entry;
            mask_idx[b] = k;
        }
        else {
            mask_idx[b] = 0;
        }
    }
    assert(b > 0);
    assert(b == gdl->npart - 1);

    a = avg[b - 1] + avg[b];
    assert(a >= 0);
    if (a > 0.0f) {
        m = max[b - 1];
        if (m < max[b])
            m = max[b];
        assert((gdl->numlines[b - 1] + gdl->numlines[b] - 1) > 0);
        a = 20.0f * (m * 2.0f - a)
            / (a * (gdl->numlines[b - 1] + gdl->numlines[b] - 1));
        k = (int) a;
        if (k > last_tab_entry)
            k = last_tab_entry;
        mask_idx[b] = k;
    }
    else {
        mask_idx[b] = 0;
    }
    assert(b == (gdl->npart - 1));
}


/**
 * \brief Windowed long-block FFT and its per-line energy.
 */
static void
vbrpsy_compute_fft_l(lame_internal_flags * gfc, const sample_t * const buffer[2], int chn,
                     int gr_out, FLOAT fftenergy[HBLKSIZE], FLOAT(*wsamp_l)[BLKSIZE])
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    PsyStateVar_t *psv = &gfc->sv_psy;
    plotting_data *plt = cfg->analysis ? gfc->pinfo : 0;
    int     j;

    if (chn < 2) {
        fft_long(gfc, *wsamp_l, chn, buffer);
    }
    else if (chn == 2) {
        FLOAT const sqrt2_half = SQRT2 * 0.5f;
        /* FFT data for mid and side channel is derived from L & R */
        for (j = BLKSIZE - 1; j >= 0; --j) {
            FLOAT const l = wsamp_l[0][j];
            FLOAT const r = wsamp_l[1][j];
            wsamp_l[0][j] = (l + r) * sqrt2_half;
            wsamp_l[1][j] = (l - r) * sqrt2_half;
        }
    }

    /*********************************************************************
    *  compute energies
    *********************************************************************/
    fftenergy[0] = wsamp_l[0][0];
    fftenergy[0] *= fftenergy[0];

    for (j = BLKSIZE / 2 - 1; j >= 0; --j) {
        FLOAT const re = (*wsamp_l)[BLKSIZE / 2 - j];
        FLOAT const im = (*wsamp_l)[BLKSIZE / 2 + j];
        fftenergy[BLKSIZE / 2 - j] = (re * re + im * im) * 0.5f;
    }
    /* total energy */
    {
        FLOAT   totalenergy = 0.0f;
        for (j = 11; j < HBLKSIZE; j++)
            totalenergy += fftenergy[j];

        psv->tot_ener[chn] = totalenergy;
    }

    if (plt) {
        for (j = 0; j < HBLKSIZE; j++) {
            plt->energy[gr_out][chn][j] = plt->energy_save[chn][j];
            plt->energy_save[chn][j] = fftenergy[j];
        }
    }
}


/**
 * \brief Windowed FFT and per-line energy for one short sub-block.
 */
static void
vbrpsy_compute_fft_s(lame_internal_flags const *gfc, const sample_t * const buffer[2], int chn,
                     int sblock, FLOAT(*fftenergy_s)[HBLKSIZE_s], FLOAT(*wsamp_s)[3][BLKSIZE_s])
{
    int     j;

    if (sblock == 0 && chn < 2) {
        fft_short(gfc, *wsamp_s, chn, buffer);
    }
    if (chn == 2) {
        FLOAT const sqrt2_half = SQRT2 * 0.5f;
        /* FFT data for mid and side channel is derived from L & R */
        for (j = BLKSIZE_s - 1; j >= 0; --j) {
            FLOAT const l = wsamp_s[0][sblock][j];
            FLOAT const r = wsamp_s[1][sblock][j];
            wsamp_s[0][sblock][j] = (l + r) * sqrt2_half;
            wsamp_s[1][sblock][j] = (l - r) * sqrt2_half;
        }
    }

    /*********************************************************************
    *  compute energies
    *********************************************************************/
    fftenergy_s[sblock][0] = (*wsamp_s)[sblock][0];
    fftenergy_s[sblock][0] *= fftenergy_s[sblock][0];
    for (j = BLKSIZE_s / 2 - 1; j >= 0; --j) {
        FLOAT const re = (*wsamp_s)[sblock][BLKSIZE_s / 2 - j];
        FLOAT const im = (*wsamp_s)[sblock][BLKSIZE_s / 2 + j];
        fftenergy_s[sblock][BLKSIZE_s / 2 - j] = (re * re + im * im) * 0.5f;
    }
}


    /*********************************************************************
    * compute loudness approximation (used for ATH auto-level adjustment) 
    *********************************************************************/
/**
 * \brief Record the granule's loudness for adjust_ATH(), when the absolute
 *        threshold is set to adapt.
 */
static void
vbrpsy_compute_loudness_approximation_l(lame_internal_flags * gfc, int gr_out, int chn,
                                        const FLOAT fftenergy[HBLKSIZE])
{
    PsyStateVar_t *psv = &gfc->sv_psy;
    if (chn < 2) {      /*no loudness for mid/side ch */
        gfc->ov_psy.loudness_sq[gr_out][chn] = psv->loudness_sq_save[chn];
        psv->loudness_sq_save[chn] = psycho_loudness_approx(fftenergy, gfc->ATH->eql_w);
    }
}


    /**********************************************************************
    *  Apply HPF of fs/4 to the input signal.
    *  This is used for attack detection / handling.
    **********************************************************************/
/**
 * \brief Detect transients and choose long or short blocks.
 *
 * Quantisation noise is spread over the whole synthesis window, so a transient
 * inside a long block can be preceded by noise that the transient itself has
 * not yet arrived to mask - a pre-echo. Shortening the block shortens the
 * window, which is the only remedy the bitstream format offers.
 *
 * The granule is high-pass filtered at a quarter of the sample rate, since a
 * transient shows most clearly where the signal is least tonal, then split
 * into nine sub-blocks whose peak amplitudes are compared against the
 * sub-block two positions earlier. A ratio above the configured threshold is
 * an attack.
 *
 * Two refinements sit on top. A periodic signal would otherwise trip the
 * detector every period, so an attack is discarded where neighbouring
 * sub-blocks carry similar energy at a low absolute level. And a sub-block
 * whose energy arrives in a single spike yields an extra attenuation factor,
 * applied later to its threshold.
 *
 * This function also hands the caller the previous granule's maskings, which
 * is where the model's one-granule delay is realised.
 */
static void
vbrpsy_attack_detection(lame_internal_flags * gfc, const sample_t * const buffer[2], int gr_out,
                        III_psy_ratio masking_ratio[2][2], III_psy_ratio masking_MS_ratio[2][2],
                        FLOAT energy[4], FLOAT sub_short_factor[4][3], int ns_attacks[4][4],
                        int uselongblock[2])
{
    FLOAT   ns_hpfsmpl[2][576];
    SessionConfig_t const *const cfg = &gfc->cfg;
    PsyStateVar_t *const psv = &gfc->sv_psy;
    plotting_data *plt = cfg->analysis ? gfc->pinfo : 0;
    int const n_chn_out = cfg->channels_out;
    /* chn=2 and 3 = Mid and Side channels */
    int const n_chn_psy = (cfg->mode == JOINT_STEREO) ? 4 : n_chn_out;
    int     chn, i, j;

    memset(&ns_hpfsmpl[0][0], 0, sizeof(ns_hpfsmpl));
    /* Don't copy the input buffer into a temporary buffer */
    /* unroll the loop 2 times */
    for (chn = 0; chn < n_chn_out; chn++) {
        static const FLOAT fircoef[] = {
            -8.65163e-18 * 2, -0.00851586 * 2, -6.74764e-18 * 2, 0.0209036 * 2,
            -3.36639e-17 * 2, -0.0438162 * 2, -1.54175e-17 * 2, 0.0931738 * 2,
            -5.52212e-17 * 2, -0.313819 * 2
        };
        /* apply high pass filter of fs/4 */
        const sample_t *const firbuf = &buffer[chn][576 - 350 - NSFIRLEN + 192];
        assert(dimension_of(fircoef) == ((NSFIRLEN - 1) / 2));
        for (i = 0; i < 576; i++) {
            FLOAT   sum1, sum2;
            sum1 = firbuf[i + 10];
            sum2 = 0.0;
            for (j = 0; j < ((NSFIRLEN - 1) / 2) - 1; j += 2) {
                sum1 += fircoef[j] * (firbuf[i + j] + firbuf[i + NSFIRLEN - j]);
                sum2 += fircoef[j + 1] * (firbuf[i + j + 1] + firbuf[i + NSFIRLEN - j - 1]);
            }
            ns_hpfsmpl[chn][i] = sum1 + sum2;
        }
        masking_ratio[gr_out][chn].en = psv->en[chn];
        masking_ratio[gr_out][chn].thm = psv->thm[chn];
        if (n_chn_psy > 2) {
            /* MS maskings  */
            /*percep_MS_entropy         [chn-2]     = gfc -> pe  [chn];  */
            masking_MS_ratio[gr_out][chn].en = psv->en[chn + 2];
            masking_MS_ratio[gr_out][chn].thm = psv->thm[chn + 2];
        }
    }
    for (chn = 0; chn < n_chn_psy; chn++) {
        FLOAT   attack_intensity[12];
        FLOAT   en_subshort[12];
        FLOAT   en_short[4] = { 0, 0, 0, 0 };
        FLOAT const *pf = ns_hpfsmpl[chn & 1];
        int     ns_uselongblock = 1;

        if (chn == 2) {
            for (i = 0, j = 576; j > 0; ++i, --j) {
                FLOAT const l = ns_hpfsmpl[0][i];
                FLOAT const r = ns_hpfsmpl[1][i];
                ns_hpfsmpl[0][i] = l + r;
                ns_hpfsmpl[1][i] = l - r;
            }
        }
        /*************************************************************** 
        * determine the block type (window type)
        ***************************************************************/
        /* calculate energies of each sub-shortblocks */
        for (i = 0; i < 3; i++) {
            en_subshort[i] = psv->last_en_subshort[chn][i + 6];
            assert(psv->last_en_subshort[chn][i + 4] > 0);
            attack_intensity[i] = en_subshort[i] / psv->last_en_subshort[chn][i + 4];
            en_short[0] += en_subshort[i];
        }

        for (i = 0; i < 9; i++) {
            FLOAT const *const pfe = pf + 576 / 9;
            FLOAT   p = 1.;
            for (; pf < pfe; pf++)
                if (p < fabs(*pf))
                    p = fabs(*pf);
            psv->last_en_subshort[chn][i] = en_subshort[i + 3] = p;
            en_short[1 + i / 3] += p;
            if (p > en_subshort[i + 3 - 2]) {
                assert(en_subshort[i + 3 - 2] > 0);
                p = p / en_subshort[i + 3 - 2];
            }
            else if (en_subshort[i + 3 - 2] > p * 10.0f) {
                assert(p > 0);
                p = en_subshort[i + 3 - 2] / (p * 10.0f);
            }
            else {
                p = 0.0;
            }
            attack_intensity[i + 3] = p;
        }

        /* pulse like signal detection for fatboy.wav and so on */
        for (i = 0; i < 3; ++i) {
            FLOAT const enn =
                en_subshort[i * 3 + 3] + en_subshort[i * 3 + 4] + en_subshort[i * 3 + 5];
            FLOAT   factor = 1.f;
            if (en_subshort[i * 3 + 5] * 6 < enn) {
                factor *= 0.5f;
                if (en_subshort[i * 3 + 4] * 6 < enn) {
                    factor *= 0.5f;
                }
            }
            sub_short_factor[chn][i] = factor;
        }

        if (plt) {
            FLOAT   x = attack_intensity[0];
            for (i = 1; i < 12; i++) {
                if (x < attack_intensity[i]) {
                    x = attack_intensity[i];
                }
            }
            plt->ers[gr_out][chn] = plt->ers_save[chn];
            plt->ers_save[chn] = x;
        }

        /* compare energies between sub-shortblocks */
        {
            FLOAT   x = gfc->cd_psy->attack_threshold[chn];
            for (i = 0; i < 12; i++) {
                if (ns_attacks[chn][i / 3] == 0) {
                    if (attack_intensity[i] > x) {
                        ns_attacks[chn][i / 3] = (i % 3) + 1;
                    }
                }
            }
        }
        /* should have energy change between short blocks, in order to avoid periodic signals */
        /* Good samples to show the effect are Trumpet test songs */
        /* GB: tuned (1) to avoid too many short blocks for test sample TRUMPET */
        /* RH: tuned (2) to let enough short blocks through for test sample FSOL and SNAPS */
        for (i = 1; i < 4; i++) {
            FLOAT const u = en_short[i - 1];
            FLOAT const v = en_short[i];
            FLOAT const m = Max(u, v);
            if (m < 40000) { /* (2) */
                if (u < 1.7f * v && v < 1.7f * u) { /* (1) */
                    if (i == 1 && ns_attacks[chn][0] <= ns_attacks[chn][i]) {
                        ns_attacks[chn][0] = 0;
                    }
                    ns_attacks[chn][i] = 0;
                }
            }
        }

        if (ns_attacks[chn][0] <= psv->last_attacks[chn]) {
            ns_attacks[chn][0] = 0;
        }

        if (psv->last_attacks[chn] == 3 ||
            ns_attacks[chn][0] + ns_attacks[chn][1] + ns_attacks[chn][2] + ns_attacks[chn][3]) {
            ns_uselongblock = 0;

            if (ns_attacks[chn][1] && ns_attacks[chn][0]) {
                ns_attacks[chn][1] = 0;
            }
            if (ns_attacks[chn][2] && ns_attacks[chn][1]) {
                ns_attacks[chn][2] = 0;
            }
            if (ns_attacks[chn][3] && ns_attacks[chn][2]) {
                ns_attacks[chn][3] = 0;
            }
        }

        if (chn < 2) {
            uselongblock[chn] = ns_uselongblock;
        }
        else {
            if (ns_uselongblock == 0) {
                uselongblock[0] = uselongblock[1] = 0;
            }
        }

        /* there is a one granule delay.  Copy maskings computed last call
         * into masking_ratio to return to calling program.
         */
        energy[chn] = psv->tot_ener[chn];
    }
}


/**
 * \brief Age the short-block spreading state for a granule whose short-block
 *        masking was not computed.
 */
static void
vbrpsy_skip_masking_s(lame_internal_flags * gfc, int chn, int sblock)
{
    if (sblock == 0) {
        FLOAT  *nbs2 = &gfc->sv_psy.nb_s2[chn][0];
        FLOAT  *nbs1 = &gfc->sv_psy.nb_s1[chn][0];
        int const n = gfc->cd_psy->s.npart;
        int     b;
        for (b = 0; b < n; b++) {
            nbs2[b] = nbs1[b];
        }
    }
}


/**
 * \brief Tonality index per partition band, for short blocks.
 *
 * \see calc_mask_index_l() for what the index means and how it is used.
 */
static void
vbrpsy_calc_mask_index_s(lame_internal_flags const *gfc, FLOAT const *max,
                         FLOAT const *avg, unsigned char *mask_idx)
{
    PsyConst_CB2SB_t const *const gds = &gfc->cd_psy->s;
    FLOAT   m, a;
    int     b, k;
    int const last_tab_entry = dimension_of(tab) - 1;
    b = 0;
    a = avg[b] + avg[b + 1];
    assert(a >= 0);
    if (a > 0.0f) {
        m = max[b];
        if (m < max[b + 1])
            m = max[b + 1];
        assert((gds->numlines[b] + gds->numlines[b + 1] - 1) > 0);
        a = 20.0f * (m * 2.0f - a)
            / (a * (gds->numlines[b] + gds->numlines[b + 1] - 1));
        k = (int) a;
        if (k > last_tab_entry)
            k = last_tab_entry;
        mask_idx[b] = k;
    }
    else {
        mask_idx[b] = 0;
    }

    for (b = 1; b < gds->npart - 1; b++) {
        a = avg[b - 1] + avg[b] + avg[b + 1];
        assert(b + 1 < gds->npart);
        assert(a >= 0);
        if (a > 0.0) {
            m = max[b - 1];
            if (m < max[b])
                m = max[b];
            if (m < max[b + 1])
                m = max[b + 1];
            assert((gds->numlines[b - 1] + gds->numlines[b] + gds->numlines[b + 1] - 1) > 0);
            a = 20.0f * (m * 3.0f - a)
                / (a * (gds->numlines[b - 1] + gds->numlines[b] + gds->numlines[b + 1] - 1));
            k = (int) a;
            if (k > last_tab_entry)
                k = last_tab_entry;
            mask_idx[b] = k;
        }
        else {
            mask_idx[b] = 0;
        }
    }
    assert(b > 0);
    assert(b == gds->npart - 1);

    a = avg[b - 1] + avg[b];
    assert(a >= 0);
    if (a > 0.0f) {
        m = max[b - 1];
        if (m < max[b])
            m = max[b];
        assert((gds->numlines[b - 1] + gds->numlines[b] - 1) > 0);
        a = 20.0f * (m * 2.0f - a)
            / (a * (gds->numlines[b - 1] + gds->numlines[b] - 1));
        k = (int) a;
        if (k > last_tab_entry)
            k = last_tab_entry;
        mask_idx[b] = k;
    }
    else {
        mask_idx[b] = 0;
    }
    assert(b == (gds->npart - 1));
}


/**
 * \brief Masking thresholds for one short sub-block.
 *
 * Spreads each partition's energy over its neighbours through the precomputed
 * spreading matrix, combines the contributions with vbrpsy_mask_add(), and
 * scales the result by the tonality of the bands that contributed.
 *
 * The threshold is then bounded twice: it may not exceed a limit derived from
 * the band's peak line, which keeps a strongly tonal band from being handed a
 * threshold so high that the quantiser takes the difference out of other
 * bands, and it may not exceed the band's own energy. The caller's
 * masking_lower factor is applied around those bounds.
 *
 * Pre-echo control is not applied here; for short blocks it happens after the
 * mapping to scalefactor bands, in L3psycho_anal_vbr(), where the position of
 * the attack within the granule is known.
 */
static void
vbrpsy_compute_masking_s(lame_internal_flags * gfc, const FLOAT(*fftenergy_s)[HBLKSIZE_s],
                         FLOAT * eb, FLOAT * thr, int chn, int sblock)
{
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gds = &gfc->cd_psy->s;
    FLOAT   max[CBANDS], avg[CBANDS];
    int     i, j, b;
    unsigned char mask_idx_s[CBANDS];

    memset(max, 0, sizeof(max));
    memset(avg, 0, sizeof(avg));

    for (b = j = 0; b < gds->npart; ++b) {
        FLOAT   ebb = 0, m = 0;
        int const n = gds->numlines[b];
        for (i = 0; i < n; ++i, ++j) {
            FLOAT const el = fftenergy_s[sblock][j];
            ebb += el;
            if (m < el)
                m = el;
        }
        eb[b] = ebb;
        assert(ebb >= 0);
        max[b] = m;
        assert(n > 0);
        avg[b] = ebb * gds->rnumlines[b];
        assert(avg[b] >= 0);
    }
    assert(b == gds->npart);
    assert(j == 129);
    vbrpsy_calc_mask_index_s(gfc, max, avg, mask_idx_s);
    for (j = b = 0; b < gds->npart; b++) {
        int     kk = gds->s3ind[b][0];
        int const last = gds->s3ind[b][1];
        int const delta = mask_add_delta(mask_idx_s[b]);
        int     dd, dd_n;
        FLOAT   x, ecb, avg_mask;
        FLOAT const masking_lower = gds->masking_lower[b] * gfc->sv_qnt.masking_lower;

        dd = mask_idx_s[kk];
        dd_n = 1;
        ecb = gds->s3[j] * eb[kk] * tab[mask_idx_s[kk]];
        ++j, ++kk;
        while (kk <= last) {
            dd += mask_idx_s[kk];
            dd_n += 1;
            x = gds->s3[j] * eb[kk] * tab[mask_idx_s[kk]];
            ecb = vbrpsy_mask_add(ecb, x, kk - b, delta);
            ++j, ++kk;
        }
        dd = (1 + 2 * dd) / (2 * dd_n);
        avg_mask = tab[dd] * 0.5f;
        ecb *= avg_mask;
#if 0                   /* we can do PRE ECHO control now here, or do it later */
        if (psv->blocktype_old[chn & 0x01] == SHORT_TYPE) {
            /* limit calculated threshold by even older granule */
            FLOAT const t1 = rpelev_s * psv->nb_s1[chn][b];
            FLOAT const t2 = rpelev2_s * psv->nb_s2[chn][b];
            FLOAT const tm = (t2 > 0) ? Min(ecb, t2) : ecb;
            thr[b] = (t1 > 0) ? NS_INTERP(Min(tm, t1), ecb, 0.6) : ecb;
        }
        else {
            /* limit calculated threshold by older granule */
            FLOAT const t1 = rpelev_s * psv->nb_s1[chn][b];
            thr[b] = (t1 > 0) ? NS_INTERP(Min(ecb, t1), ecb, 0.6) : ecb;
        }
#else /* we do it later */
        thr[b] = ecb;
#endif
        psv->nb_s2[chn][b] = psv->nb_s1[chn][b];
        psv->nb_s1[chn][b] = ecb;
        {
            /*  if THR exceeds EB, the quantization routines will take the difference
             *  from other bands. in case of strong tonal samples (tonaltest.wav)
             *  this leads to heavy distortions. that's why we limit THR here.
             */
            x = max[b];
            x *= gds->minval[b];
            x *= avg_mask;
            if (thr[b] > x) {
                thr[b] = x;
            }
        }
        if (masking_lower > 1) {
            thr[b] *= masking_lower;
        }
        if (thr[b] > eb[b]) {
            thr[b] = eb[b];
        }
        if (masking_lower < 1) {
            thr[b] *= masking_lower;
        }

        assert(thr[b] >= 0);
    }
    for (; b < CBANDS; ++b) {
        eb[b] = 0;
        thr[b] = 0;
    }
}


/**
 * \brief Masking thresholds for a long block, including pre-echo control.
 *
 * Spreading, non-linear addition and the two bounds are as in
 * vbrpsy_compute_masking_s(). What this path adds is pre-echo control against
 * time: the threshold may not rise far above what the preceding granule, and
 * the one before that, were allowed - so a quiet passage followed by a surge
 * cannot have its noise floor lifted before the surge arrives to mask it.
 *
 * Where the previous granule was short, only the nearer of the two limits is
 * used, and where no long-block analysis was made for it the limit is
 * estimated from the current band energy instead. The comment on that branch
 * is candid that this is a guess made for speed.
 */
static void
vbrpsy_compute_masking_l(lame_internal_flags * gfc, const FLOAT fftenergy[HBLKSIZE],
                         FLOAT eb_l[CBANDS], FLOAT thr[CBANDS], int chn)
{
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gdl = &gfc->cd_psy->l;
    FLOAT   max[CBANDS], avg[CBANDS];
    unsigned char mask_idx_l[CBANDS + 2];
    int     k, b;

 /*********************************************************************
    *    Calculate the energy and the tonality of each partition.
 *********************************************************************/
    calc_energy(gdl, fftenergy, eb_l, max, avg);
    calc_mask_index_l(gfc, max, avg, mask_idx_l);

 /*********************************************************************
    *      convolve the partitioned energy and unpredictability
    *      with the spreading function, s3_l[b][k]
 ********************************************************************/
    k = 0;
    for (b = 0; b < gdl->npart; b++) {
        FLOAT   x, ecb, avg_mask, t;
        FLOAT const masking_lower = gdl->masking_lower[b] * gfc->sv_qnt.masking_lower;
        /* convolve the partitioned energy with the spreading function */
        int     kk = gdl->s3ind[b][0];
        int const last = gdl->s3ind[b][1];
        int const delta = mask_add_delta(mask_idx_l[b]);
        int     dd = 0, dd_n = 0;

        dd = mask_idx_l[kk];
        dd_n += 1;
        ecb = gdl->s3[k] * eb_l[kk] * tab[mask_idx_l[kk]];
        ++k, ++kk;
        while (kk <= last) {
            dd += mask_idx_l[kk];
            dd_n += 1;
            x = gdl->s3[k] * eb_l[kk] * tab[mask_idx_l[kk]];
            t = vbrpsy_mask_add(ecb, x, kk - b, delta);
#if 0
            ecb += eb_l[kk];
            if (ecb > t) {
                ecb = t;
            }
#else
            ecb = t;
#endif
            ++k, ++kk;
        }
        dd = (1 + 2 * dd) / (2 * dd_n);
        avg_mask = tab[dd] * 0.5f;
        ecb *= avg_mask;

        /****   long block pre-echo control   ****/
        /* dont use long block pre-echo control if previous granule was 
         * a short block.  This is to avoid the situation:   
         * frame0:  quiet (very low masking)  
         * frame1:  surge  (triggers short blocks)
         * frame2:  regular frame.  looks like pre-echo when compared to 
         *          frame0, but all pre-echo was in frame1.
         */
        /* chn=0,1   L and R channels
           chn=2,3   S and M channels.
         */
        if (psv->blocktype_old[chn & 0x01] == SHORT_TYPE) {
            FLOAT const ecb_limit = rpelev * psv->nb_l1[chn][b];
            if (ecb_limit > 0) {
                thr[b] = Min(ecb, ecb_limit);
            }
            else {
                /* Robert 071209:
                   Because we don't calculate long block psy when we know a granule
                   should be of short blocks, we don't have any clue how the granule
                   before would have looked like as a long block. So we have to guess
                   a little bit for this END_TYPE block.
                   Most of the time we get away with this sloppyness. (fingers crossed :)
                   The speed increase is worth it.
                 */
                thr[b] = Min(ecb, eb_l[b] * NS_PREECHO_ATT2);
            }
        }
        else {
            FLOAT   ecb_limit_2 = rpelev2 * psv->nb_l2[chn][b];
            FLOAT   ecb_limit_1 = rpelev * psv->nb_l1[chn][b];
            FLOAT   ecb_limit;
            if (ecb_limit_2 <= 0) {
                ecb_limit_2 = ecb;
            }
            if (ecb_limit_1 <= 0) {
                ecb_limit_1 = ecb;
            }
            if (psv->blocktype_old[chn & 0x01] == NORM_TYPE) {
                ecb_limit = Min(ecb_limit_1, ecb_limit_2);
            }
            else {
                ecb_limit = ecb_limit_1;
            }
            thr[b] = Min(ecb, ecb_limit);
        }
        psv->nb_l2[chn][b] = psv->nb_l1[chn][b];
        psv->nb_l1[chn][b] = ecb;
        {
            /*  if THR exceeds EB, the quantization routines will take the difference
             *  from other bands. in case of strong tonal samples (tonaltest.wav)
             *  this leads to heavy distortions. that's why we limit THR here.
             */
            x = max[b];
            x *= gdl->minval[b];
            x *= avg_mask;
            if (thr[b] > x) {
                thr[b] = x;
            }
        }
        if (masking_lower > 1) {
            thr[b] *= masking_lower;
        }
        if (thr[b] > eb_l[b]) {
            thr[b] = eb_l[b];
        }
        if (masking_lower < 1) {
            thr[b] *= masking_lower;
        }
        assert(thr[b] >= 0);
    }
    for (; b < CBANDS; ++b) {
        eb_l[b] = 0;
        thr[b] = 0;
    }
}


/**
 * \brief Apply the configured short-block policy to the per-channel decision.
 *
 * Short blocks may be forced on, dispensed with entirely, or coupled so both
 * channels always agree.
 */
static void
vbrpsy_compute_block_type(SessionConfig_t const *cfg, int *uselongblock)
{
    int     chn;

    if (cfg->short_blocks == short_block_coupled
        /* force both channels to use the same block type */
        /* this is necessary if the frame is to be encoded in ms_stereo.  */
        /* But even without ms_stereo, FhG  does this */
        && !(uselongblock[0] && uselongblock[1]))
        uselongblock[0] = uselongblock[1] = 0;

    for (chn = 0; chn < cfg->channels_out; chn++) {
        /* disable short blocks */
        if (cfg->short_blocks == short_block_dispensed) {
            uselongblock[chn] = 1;
        }
        if (cfg->short_blocks == short_block_forced) {
            uselongblock[chn] = 0;
        }
    }
}


/**
 * \brief Settle the previous granule's block type and hand it to the caller.
 *
 * A block type cannot be chosen from its own granule alone: the window shape
 * has to transition, so the granule before a short one becomes a start block
 * and the granule after it a stop block. That is only knowable once the
 * following granule has been analysed, which is why the model's results lag
 * its input by one granule.
 */
static void
vbrpsy_apply_block_type(PsyStateVar_t * psv, int nch, int const *uselongblock, int *blocktype_d)
{
    int     chn;

    /* update the blocktype of the previous granule, since it depends on what
     * happend in this granule */
    for (chn = 0; chn < nch; chn++) {
        int     blocktype = NORM_TYPE;
        /* disable short blocks */

        if (uselongblock[chn]) {
            /* no attack : use long blocks */
            assert(psv->blocktype_old[chn] != START_TYPE);
            if (psv->blocktype_old[chn] == SHORT_TYPE)
                blocktype = STOP_TYPE;
        }
        else {
            /* attack : use short blocks */
            blocktype = SHORT_TYPE;
            if (psv->blocktype_old[chn] == NORM_TYPE) {
                psv->blocktype_old[chn] = START_TYPE;
            }
            if (psv->blocktype_old[chn] == STOP_TYPE)
                psv->blocktype_old[chn] = SHORT_TYPE;
        }

        blocktype_d[chn] = psv->blocktype_old[chn]; /* value returned to calling program */
        psv->blocktype_old[chn] = blocktype; /* save for next call to l3psy_anal */
    }
}


/*************************************************************** 
 * compute M/S thresholds from Johnston & Ferreira 1992 ICASSP paper
 ***************************************************************/

/**
 * \brief Adjust mid and side thresholds for joint-stereo coding.
 *
 * Coding mid and side rather than left and right changes where the
 * quantisation noise ends up in the stereo image, and noise placed differently
 * from the signal that is supposed to mask it can become audible - the
 * binaural masking level difference. The correction is applied only where the
 * left and right thresholds are within about 2 dB of each other, which is the
 * case where the two channels are similar enough for the effect to matter.
 *
 * Where the caller set an msfix value, the mid and side thresholds are
 * additionally held down relative to the smaller of the two monophonic
 * thresholds, both first floored at the absolute threshold of hearing.
 *
 * After Johnston and Ferreira, ICASSP 1992; the msfix refinement is Naoki
 * Shibata, 2000.
 */
static void
vbrpsy_compute_MS_thresholds(const FLOAT eb[4][CBANDS], FLOAT thr[4][CBANDS],
                             const FLOAT cb_mld[CBANDS], const FLOAT ath_cb[CBANDS], FLOAT athlower,
                             FLOAT msfix, int n)
{
    FLOAT const msfix2 = msfix * 2.f;
    FLOAT   rside, rmid;
    int     b;
    for (b = 0; b < n; ++b) {
        FLOAT const ebM = eb[2][b];
        FLOAT const ebS = eb[3][b];
        FLOAT const thmL = thr[0][b];
        FLOAT const thmR = thr[1][b];
        FLOAT   thmM = thr[2][b];
        FLOAT   thmS = thr[3][b];

        /* use this fix if L & R masking differs by 2db or less */
        /* if db = 10*log10(x2/x1) < 2 */
        /* if (x2 < 1.58*x1) { */
        if (thmL <= 1.58f * thmR && thmR <= 1.58f * thmL) {
            FLOAT const mld_m = cb_mld[b] * ebS;
            FLOAT const mld_s = cb_mld[b] * ebM;
            FLOAT const tmp_m = Min(thmS, mld_m);
            FLOAT const tmp_s = Min(thmM, mld_s);
            rmid = Max(thmM, tmp_m);
            rside = Max(thmS, tmp_s);
        }
        else {
            rmid = thmM;
            rside = thmS;
        }
        if (msfix > 0.f) {
            /***************************************************************/
            /* Adjust M/S maskings if user set "msfix"                     */
            /***************************************************************/
            /* Naoki Shibata 2000 */
            FLOAT   thmLR, thmMS;
            FLOAT const ath = ath_cb[b] * athlower;
            FLOAT const tmp_l = Max(thmL, ath);
            FLOAT const tmp_r = Max(thmR, ath);
            thmLR = Min(tmp_l, tmp_r);
            thmM = Max(rmid, ath);
            thmS = Max(rside, ath);
            thmMS = thmM + thmS;
            if (thmMS > 0.f && (thmLR * msfix2) < thmMS) {
                FLOAT const f = thmLR * msfix2 / thmMS;
                thmM *= f;
                thmS *= f;
                assert(thmMS > 0.f);
            }
            rmid = Min(thmM, rmid);
            rside = Min(thmS, rside);
        }
        if (rmid > ebM) {
            rmid = ebM;
        }
        if (rside > ebS) {
            rside = ebS;
        }
        thr[2][b] = rmid;
        thr[3][b] = rside;
    }
}


/*
 * NOTE: the bitrate reduction from the inter-channel masking effect is low
 * compared to the chance of getting annyoing artefacts. L3psycho_anal_vbr does
 * not use this feature. (Robert 071216)
*/

/**
 * \internal
 * \brief Run the psychoacoustic model over one granule.
 *
 * The single entry point, used by every encoding mode. See the file
 * description for what the model computes and in what order.
 *
 * Results describe the **previous** granule, not the one in \a buffer: block
 * types cannot be settled until the following granule has been seen.
 *
 * \param gfc               encoder state; the model reads its configuration
 *                          and per-channel history and updates the latter.
 * \param buffer            input samples per channel, centred on the granule.
 * \param gr_out            index of the granule the results are returned for.
 * \param[out] masking_ratio      per-band energies and thresholds, L/R.
 * \param[out] masking_MS_ratio   the same for mid/side, in joint stereo.
 * \param[out] percep_entropy     perceptual entropy, L/R.
 * \param[out] percep_MS_entropy  perceptual entropy, mid/side.
 * \param[out] energy             per-channel granule energy.
 * \param[out] blocktype_d        the settled block type per channel.
 * \return 0.
 */
int
L3psycho_anal_vbr(lame_internal_flags * gfc,
                  const sample_t * const buffer[2], int gr_out,
                  III_psy_ratio masking_ratio[2][2],
                  III_psy_ratio masking_MS_ratio[2][2],
                  FLOAT percep_entropy[2], FLOAT percep_MS_entropy[2],
                  FLOAT energy[4], int blocktype_d[2])
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_CB2SB_t const *const gdl = &gfc->cd_psy->l;
    PsyConst_CB2SB_t const *const gds = &gfc->cd_psy->s;
    plotting_data *plt = cfg->analysis ? gfc->pinfo : 0;

    III_psy_xmin last_thm[4];

    /* fft and energy calculation   */
    FLOAT(*wsamp_l)[BLKSIZE];
    FLOAT(*wsamp_s)[3][BLKSIZE_s];
    FLOAT   fftenergy[HBLKSIZE];
    FLOAT   fftenergy_s[3][HBLKSIZE_s];
    FLOAT   wsamp_L[2][BLKSIZE];
    FLOAT   wsamp_S[2][3][BLKSIZE_s];
    FLOAT   eb[4][CBANDS], thr[4][CBANDS];

    FLOAT   sub_short_factor[4][3];
    FLOAT   thmm;
    FLOAT const pcfact = 0.6f;
    FLOAT const ath_factor =
        (cfg->msfix > 0.f) ? (cfg->ATH_offset_factor * gfc->ATH->adjust_factor) : 1.f;

    const   FLOAT(*const_eb)[CBANDS] = (const FLOAT(*)[CBANDS]) eb;
    const   FLOAT(*const_fftenergy_s)[HBLKSIZE_s] = (const FLOAT(*)[HBLKSIZE_s]) fftenergy_s;

    /* block type  */
    int     ns_attacks[4][4] = { {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0} };
    int     uselongblock[2] = { 1, 1 };

    /* usual variables like loop indices, etc..    */
    int     chn, sb, sblock;

    /* chn=2 and 3 = Mid and Side channels */
    int const n_chn_psy = (cfg->mode == JOINT_STEREO) ? 4 : cfg->channels_out;

    memcpy(&last_thm[0], &psv->thm[0], sizeof(last_thm));

    vbrpsy_attack_detection(gfc, buffer, gr_out, masking_ratio, masking_MS_ratio, energy,
                            sub_short_factor, ns_attacks, uselongblock);

    vbrpsy_compute_block_type(cfg, uselongblock);

    /* LONG BLOCK CASE */
    {
        for (chn = 0; chn < n_chn_psy; chn++) {
            int const ch01 = chn & 0x01;

            wsamp_l = wsamp_L + ch01;
            vbrpsy_compute_fft_l(gfc, buffer, chn, gr_out, fftenergy, wsamp_l);
            vbrpsy_compute_loudness_approximation_l(gfc, gr_out, chn, fftenergy);
            vbrpsy_compute_masking_l(gfc, fftenergy, eb[chn], thr[chn], chn);
        }
        if (cfg->mode == JOINT_STEREO) {
            if ((uselongblock[0] + uselongblock[1]) == 2) {
                vbrpsy_compute_MS_thresholds(const_eb, thr, gdl->mld_cb, gfc->ATH->cb_l,
                                             ath_factor, cfg->msfix, gdl->npart);
            }
        }
        /* TODO: apply adaptive ATH masking here ?? */
        for (chn = 0; chn < n_chn_psy; chn++) {
            convert_partition2scalefac_l(gfc, eb[chn], thr[chn], chn);
            convert_partition2scalefac_l_to_s(gfc, eb[chn], thr[chn], chn);
        }
    }
    /* SHORT BLOCKS CASE */
    {
        int const force_short_block_calc = gfc->cd_psy->force_short_block_calc;
        for (sblock = 0; sblock < 3; sblock++) {
            for (chn = 0; chn < n_chn_psy; ++chn) {
                int const ch01 = chn & 0x01;
                if (uselongblock[ch01] && !force_short_block_calc) {
                    vbrpsy_skip_masking_s(gfc, chn, sblock);
                }
                else {
                    /* compute masking thresholds for short blocks */
                    wsamp_s = wsamp_S + ch01;
                    vbrpsy_compute_fft_s(gfc, buffer, chn, sblock, fftenergy_s, wsamp_s);
                    vbrpsy_compute_masking_s(gfc, const_fftenergy_s, eb[chn], thr[chn], chn,
                                             sblock);
                }
            }
            if (cfg->mode == JOINT_STEREO) {
                if ((uselongblock[0] + uselongblock[1]) == 0) {
                    vbrpsy_compute_MS_thresholds(const_eb, thr, gds->mld_cb, gfc->ATH->cb_s,
                                                 ath_factor, cfg->msfix, gds->npart);
                }
            }
            /* TODO: apply adaptive ATH masking here ?? */
            for (chn = 0; chn < n_chn_psy; ++chn) {
                int const ch01 = chn & 0x01;
                if (!uselongblock[ch01] || force_short_block_calc) {
                    convert_partition2scalefac_s(gfc, eb[chn], thr[chn], chn, sblock);
                }
            }
        }

        /****   short block pre-echo control   ****/
        for (chn = 0; chn < n_chn_psy; chn++) {
            for (sb = 0; sb < SBMAX_s; sb++) {
                FLOAT   new_thmm[3], prev_thm, t1, t2;
                for (sblock = 0; sblock < 3; sblock++) {
                    thmm = psv->thm[chn].s[sb][sblock];
                    thmm *= NS_PREECHO_ATT0;

                    t1 = t2 = thmm;

                    if (sblock > 0) {
                        prev_thm = new_thmm[sblock - 1];
                    }
                    else {
                        prev_thm = last_thm[chn].s[sb][2];
                    }
                    if (ns_attacks[chn][sblock] >= 2 || ns_attacks[chn][sblock + 1] == 1) {
                        t1 = NS_INTERP(prev_thm, thmm, NS_PREECHO_ATT1 * pcfact);
                    }
                    thmm = Min(t1, thmm);
                    if (ns_attacks[chn][sblock] == 1) {
                        t2 = NS_INTERP(prev_thm, thmm, NS_PREECHO_ATT2 * pcfact);
                    }
                    else if ((sblock == 0 && psv->last_attacks[chn] == 3)
                             || (sblock > 0 && ns_attacks[chn][sblock - 1] == 3)) { /* 2nd preceeding block */
                        switch (sblock) {
                        case 0:
                            prev_thm = last_thm[chn].s[sb][1];
                            break;
                        case 1:
                            prev_thm = last_thm[chn].s[sb][2];
                            break;
                        case 2:
                            prev_thm = new_thmm[0];
                            break;
                        }
                        t2 = NS_INTERP(prev_thm, thmm, NS_PREECHO_ATT2 * pcfact);
                    }

                    thmm = Min(t1, thmm);
                    thmm = Min(t2, thmm);

                    /* pulse like signal detection for fatboy.wav and so on */
                    thmm *= sub_short_factor[chn][sblock];

                    new_thmm[sblock] = thmm;
                }
                for (sblock = 0; sblock < 3; sblock++) {
                    psv->thm[chn].s[sb][sblock] = new_thmm[sblock];
                }
            }
        }
    }
    for (chn = 0; chn < n_chn_psy; chn++) {
        psv->last_attacks[chn] = ns_attacks[chn][2];
    }


    /*************************************************************** 
    * determine final block type
    ***************************************************************/
    vbrpsy_apply_block_type(psv, cfg->channels_out, uselongblock, blocktype_d);

    /*********************************************************************
    * compute the value of PE to return ... no delay and advance
    *********************************************************************/
    for (chn = 0; chn < n_chn_psy; chn++) {
        FLOAT  *ppe;
        int     type;
        III_psy_ratio const *mr;

        if (chn > 1) {
            ppe = percep_MS_entropy - 2;
            type = NORM_TYPE;
            if (blocktype_d[0] == SHORT_TYPE || blocktype_d[1] == SHORT_TYPE)
                type = SHORT_TYPE;
            mr = &masking_MS_ratio[gr_out][chn - 2];
        }
        else {
            ppe = percep_entropy;
            type = blocktype_d[chn];
            mr = &masking_ratio[gr_out][chn];
        }
        if (type == SHORT_TYPE) {
            ppe[chn] = pecalc_s(mr, gfc->sv_qnt.masking_lower);
        }
        else {
            ppe[chn] = pecalc_l(mr, gfc->sv_qnt.masking_lower);
        }

        if (plt) {
            plt->pe[gr_out][chn] = ppe[chn];
        }
    }
    return 0;
}




/* 
 *   The spreading function.  Values returned in units of energy
 */
/**
 * \brief The spreading function: how strongly a masker at one bark offset
 *        raises the threshold at another.
 *
 * Asymmetric, as hearing is - masking reaches further upwards in frequency
 * than downwards. The offset is scaled differently on the two sides before a
 * common curve is evaluated, and a shallow notch is subtracted just above the
 * masker.
 *
 * The curve depends on frequency separation only. It carries no term for the
 * masker's level, so the same spread is assumed at every signal level; the
 * result is normalised so that spreading a flat spectrum returns it unchanged.
 */
static  FLOAT
s3_func(FLOAT bark)
{
    FLOAT   tempx, x, tempy, temp;
    tempx = bark;
    if (tempx >= 0)
        tempx *= 3;
    else
        tempx *= 1.5;

    if (tempx >= 0.5 && tempx <= 2.5) {
        temp = tempx - 0.5;
        x = 8.0 * (temp * temp - 2.0 * temp);
    }
    else
        x = 0.0;
    tempx += 0.474;
    tempy = 15.811389 + 7.5 * tempx - 17.5 * sqrt(1.0 + tempx * tempx);

    if (tempy <= -60.0)
        return 0.0;

    tempx = exp((x + tempy) * LN_TO_LOG10);

    /* Normalization.  The spreading function should be normalized so that:
       +inf
       /
       |  s3 [ bark ]  d(bark)   =  1
       /
       -inf
     */
    tempx /= .6609193;
    return tempx;
}

#if 0
static  FLOAT
norm_s3_func(void)
{
    double  lim_a = 0, lim_b = 0;
    double  x = 0, l, h;
    for (x = 0; s3_func(x) > 1e-20; x -= 1);
    l = x;
    h = 0;
    while (fabs(h - l) > 1e-12) {
        x = (h + l) / 2;
        if (s3_func(x) > 0) {
            h = x;
        }
        else {
            l = x;
        }
    }
    lim_a = l;
    for (x = 0; s3_func(x) > 1e-20; x += 1);
    l = 0;
    h = x;
    while (fabs(h - l) > 1e-12) {
        x = (h + l) / 2;
        if (s3_func(x) > 0) {
            l = x;
        }
        else {
            h = x;
        }
    }
    lim_b = h;
    {
        double  sum = 0;
        int const m = 1000;
        int     i;
        for (i = 0; i <= m; ++i) {
            double  x = lim_a + i * (lim_b - lim_a) / m;
            double  y = s3_func(x);
            sum += y;
        }
        {
            double  norm = (m + 1) / (sum * (lim_b - lim_a));
            /*printf( "norm = %lf\n",norm); */
            return norm;
        }
    }
}
#endif

/**
 * \brief Weight used by vbrpsy_compute_MS_thresholds() when transferring
 *        masking between the mid and side channels.
 *
 * Rises with frequency to a plateau. The tree records its origin only as a
 * curve fitted to a published plot, without naming the source.
 */
static  FLOAT
stereo_demask(double f)
{
    /* setup stereo demasking thresholds */
    /* formula reverse enginerred from plot in paper */
    double  arg = freq2bark(f);
    arg = (Min(arg, 15.5) / 15.5);

    return pow(10.0, 1.25 * (1 - cos(PI * arg)) - 2.5);
}

/**
 * \brief Lay out the partition bands for one sample rate and transform size.
 *
 * Walks the FFT lines assigning each to a partition, so that a partition spans
 * approximately a fixed bark width, and records for each scalefactor band
 * which partition it ends in and how the straddling partition should be split.
 */
static void
init_numline(PsyConst_CB2SB_t * gd, FLOAT sfreq, int fft_size,
             int mdct_size, int sbmax, int const *scalepos)
{
    FLOAT   b_frq[CBANDS + 1];
    FLOAT const mdct_freq_frac = sfreq / (2.0f * mdct_size);
    FLOAT const deltafreq = fft_size / (2.0f * mdct_size);
    int     partition[HBLKSIZE] = { 0 };
    int     i, j, ni;
    int     sfb;
    sfreq /= fft_size;
    j = 0;
    ni = 0;
    /* compute numlines, the number of spectral lines in each partition band */
    /* each partition band should be about DELBARK wide. */
    for (i = 0; i < CBANDS; i++) {
        FLOAT   bark1;
        int     j2, nl;
        bark1 = freq2bark(sfreq * j);

        b_frq[i] = sfreq * j;

        for (j2 = j; freq2bark(sfreq * j2) - bark1 < DELBARK && j2 <= fft_size / 2; j2++);

        nl = j2 - j;
        gd->numlines[i] = nl;
        gd->rnumlines[i] = (nl > 0) ? (1.0f / nl) : 0;

        ni = i + 1;

        while (j < j2) {
            assert(j < HBLKSIZE);
            partition[j++] = i;
        }
        if (j > fft_size / 2) {
            j = fft_size / 2;
            ++i;
            break;
        }
    }
    assert(i < CBANDS);
    b_frq[i] = sfreq * j;

    gd->n_sb = sbmax;
    gd->npart = ni;

    {
        j = 0;
        for (i = 0; i < gd->npart; i++) {
            int const nl = gd->numlines[i];
            FLOAT const freq = sfreq * (j + nl / 2);
            gd->mld_cb[i] = stereo_demask(freq);
            j += nl;
        }
        for (; i < CBANDS; ++i) {
            gd->mld_cb[i] = 1;
        }
    }
    for (sfb = 0; sfb < sbmax; sfb++) {
        int     i1, i2, bo;
        int     start = scalepos[sfb];
        int     end = scalepos[sfb + 1];

        i1 = floor(.5 + deltafreq * (start - .5));
        if (i1 < 0)
            i1 = 0;
        i2 = floor(.5 + deltafreq * (end - .5));

        if (i2 > fft_size / 2)
            i2 = fft_size / 2;

        bo = partition[i2];
        gd->bm[sfb] = (partition[i1] + partition[i2]) / 2;
        gd->bo[sfb] = bo;

        /* calculate how much of this band belongs to current scalefactor band */
        {
            FLOAT const f_tmp = mdct_freq_frac * end;
            FLOAT   bo_w = (f_tmp - b_frq[bo]) / (b_frq[bo + 1] - b_frq[bo]);
            if (bo_w < 0) {
                bo_w = 0;
            }
            else {
                if (bo_w > 1) {
                    bo_w = 1;
                }
            }
            gd->bo_weight[sfb] = bo_w;
        }
        gd->mld[sfb] = stereo_demask(mdct_freq_frac * start);
    }
}

/**
 * \brief Centre bark value and bark width of each partition band.
 */
static void
compute_bark_values(PsyConst_CB2SB_t const *gd, FLOAT sfreq, int fft_size,
                    FLOAT * bval, FLOAT * bval_width)
{
    /* compute bark values of each critical band */
    int     k, j = 0, ni = gd->npart;
    sfreq /= fft_size;
    for (k = 0; k < ni; k++) {
        int const w = gd->numlines[k];
        FLOAT   bark1, bark2;

        bark1 = freq2bark(sfreq * (j));
        bark2 = freq2bark(sfreq * (j + w - 1));
        bval[k] = .5 * (bark1 + bark2);

        bark1 = freq2bark(sfreq * (j - .5));
        bark2 = freq2bark(sfreq * (j + w - .5));
        bval_width[k] = bark2 - bark1;
        j += w;
    }
}

/**
 * \brief Precompute the spreading matrix from s3_func().
 *
 * Evaluating the spreading function per band pair on every granule would be
 * wasteful when the band layout is fixed for the session, so the non-negligible
 * values are computed once and stored with the index range they cover. Each
 * row is normalised so that spreading does not change total energy.
 */
static int
init_s3_values(FLOAT ** p, int (*s3ind)[2], int npart,
               FLOAT const *bval, FLOAT const *bval_width, FLOAT const *norm)
{
    FLOAT   s3[CBANDS][CBANDS];
    /* The s3 array is not linear in the bark scale.
     * bval[x] should be used to get the bark value.
     */
    int     i, j, k;
    int     numberOfNoneZero = 0;

    memset(&s3[0][0], 0, sizeof(s3));

    /* s[i][j], the value of the spreading function,
     * centered at band j (masker), for band i (maskee)
     *
     * i.e.: sum over j to spread into signal barkval=i
     * NOTE: i and j are used opposite as in the ISO docs
     */
    for (i = 0; i < npart; i++) {
        for (j = 0; j < npart; j++) {
            FLOAT   v = s3_func(bval[i] - bval[j]) * bval_width[j];
            s3[i][j] = v * norm[i];
        }
    }
    for (i = 0; i < npart; i++) {
        for (j = 0; j < npart; j++) {
            if (s3[i][j] > 0.0f)
                break;
        }
        s3ind[i][0] = j;

        for (j = npart - 1; j > 0; j--) {
            if (s3[i][j] > 0.0f)
                break;
        }
        s3ind[i][1] = j;
        numberOfNoneZero += (s3ind[i][1] - s3ind[i][0] + 1);
    }
    *p = lame_calloc(FLOAT, numberOfNoneZero);
    if (!*p)
        return -1;

    k = 0;
    for (i = 0; i < npart; i++)
        for (j = s3ind[i][0]; j <= s3ind[i][1]; j++)
            (*p)[k++] = s3[i][j];

    return 0;
}

/**
 * \internal
 * \brief Build the per-session constant tables the model needs.
 *
 * Called once per encoder instance. Lays out the partition bands for both
 * transform sizes, precomputes the spreading matrices, the per-band minimum
 * masking values, the stereo demasking weights and the attack thresholds, and
 * initialises the per-channel history the model carries between granules.
 *
 * \return 0 on success, non-zero if a table could not be allocated.
 */
int
psymodel_init(lame_global_flags const *gfp)
{
    lame_internal_flags *const gfc = gfp->internal_flags;
    SessionConfig_t *const cfg = &gfc->cfg;
    PsyStateVar_t *const psv = &gfc->sv_psy;
    PsyConst_t *gd;
    int     i, j, b, sb, k;
    FLOAT   bvl_a = 13, bvl_b = 24;
    FLOAT   snr_l_a = 0, snr_l_b = 0;
    FLOAT   snr_s_a = -8.25, snr_s_b = -4.5;

    FLOAT   bval[CBANDS];
    FLOAT   bval_width[CBANDS];
    FLOAT   norm[CBANDS];
    FLOAT const sfreq = cfg->samplerate_out;

    FLOAT   xav = 10, xbv = 12;
    FLOAT const minval_low = (0.f - cfg->minval);

    if (gfc->cd_psy != 0) {
        return 0;
    }
    memset(norm, 0, sizeof(norm));

    gd = lame_calloc(PsyConst_t, 1);
    if (gd == 0)
        return -1;
    gfc->cd_psy = gd;

    gd->force_short_block_calc = gfp->experimentalZ;

    psv->blocktype_old[0] = psv->blocktype_old[1] = NORM_TYPE; /* the vbr header is long blocks */

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < CBANDS; ++j) {
            psv->nb_l1[i][j] = 1e20;
            psv->nb_l2[i][j] = 1e20;
            psv->nb_s1[i][j] = psv->nb_s2[i][j] = 1.0;
        }
        for (sb = 0; sb < SBMAX_l; sb++) {
            psv->en[i].l[sb] = 1e20;
            psv->thm[i].l[sb] = 1e20;
        }
        for (j = 0; j < 3; ++j) {
            for (sb = 0; sb < SBMAX_s; sb++) {
                psv->en[i].s[sb][j] = 1e20;
                psv->thm[i].s[sb][j] = 1e20;
            }
            psv->last_attacks[i] = 0;
        }
        for (j = 0; j < 9; j++)
            psv->last_en_subshort[i][j] = 10.;
    }


    /* init. for loudness approx. -jd 2001 mar 27 */
    psv->loudness_sq_save[0] = psv->loudness_sq_save[1] = 0.0;



    /*************************************************************************
     * now compute the psychoacoustic model specific constants
     ************************************************************************/
    /* compute numlines, bo, bm, bval, bval_width, mld */
    init_numline(&gd->l, sfreq, BLKSIZE, 576, SBMAX_l, gfc->scalefac_band.l);
    assert(gd->l.npart < CBANDS);
    compute_bark_values(&gd->l, sfreq, BLKSIZE, bval, bval_width);

    /* compute the spreading function */
    for (i = 0; i < gd->l.npart; i++) {
        double  snr = snr_l_a;
        if (bval[i] >= bvl_a) {
            snr = snr_l_b * (bval[i] - bvl_a) / (bvl_b - bvl_a)
                + snr_l_a * (bvl_b - bval[i]) / (bvl_b - bvl_a);
        }
        norm[i] = pow(10.0, snr / 10.0);
    }
    i = init_s3_values(&gd->l.s3, gd->l.s3ind, gd->l.npart, bval, bval_width, norm);
    if (i)
        return i;

    /* compute long block specific values, ATH and MINVAL */
    j = 0;
    for (i = 0; i < gd->l.npart; i++) {
        double  x;

        /* ATH */
        x = FLOAT_MAX;
        for (k = 0; k < gd->l.numlines[i]; k++, j++) {
            FLOAT const freq = sfreq * j / (1000.0 * BLKSIZE);
            FLOAT   level;
            /* freq = Min(.1,freq); *//* ATH below 100 Hz constant, not further climbing */
            level = ATHformula(cfg, freq * 1000) - 20; /* scale to FFT units; returned value is in dB */
            level = pow(10., 0.1 * level); /* convert from dB -> energy */
            level *= gd->l.numlines[i];
            if (x > level)
                x = level;
        }
        gfc->ATH->cb_l[i] = x;

        /* MINVAL.
           For low freq, the strength of the masking is limited by minval
           this is an ISO MPEG1 thing, dont know if it is really needed */
        /* FIXME: it does work to reduce low-freq problems in S53-Wind-Sax
           and lead-voice samples, but introduces some 3 kbps bit bloat too.
           TODO: Further refinement of the shape of this hack.
         */
        x = 20.0 * (bval[i] / xav - 1.0);
        if (x > 6) {
            x = 30;
        }
        if (x < minval_low) {
            x = minval_low;
        }
        if (cfg->samplerate_out < 44000) {
            x = 30;
        }
        x -= 8.;
        gd->l.minval[i] = pow(10.0, x / 10.) * gd->l.numlines[i];
    }

    /************************************************************************
     * do the same things for short blocks
     ************************************************************************/
    init_numline(&gd->s, sfreq, BLKSIZE_s, 192, SBMAX_s, gfc->scalefac_band.s);
    assert(gd->s.npart < CBANDS);
    compute_bark_values(&gd->s, sfreq, BLKSIZE_s, bval, bval_width);

    /* SNR formula. short block is normalized by SNR. is it still right ? */
    j = 0;
    for (i = 0; i < gd->s.npart; i++) {
        double  x;
        double  snr = snr_s_a;
        if (bval[i] >= bvl_a) {
            snr = snr_s_b * (bval[i] - bvl_a) / (bvl_b - bvl_a)
                + snr_s_a * (bvl_b - bval[i]) / (bvl_b - bvl_a);
        }
        norm[i] = pow(10.0, snr / 10.0);

        /* ATH */
        x = FLOAT_MAX;
        for (k = 0; k < gd->s.numlines[i]; k++, j++) {
            FLOAT const freq = sfreq * j / (1000.0 * BLKSIZE_s);
            FLOAT   level;
            /* freq = Min(.1,freq); *//* ATH below 100 Hz constant, not further climbing */
            level = ATHformula(cfg, freq * 1000) - 20; /* scale to FFT units; returned value is in dB */
            level = pow(10., 0.1 * level); /* convert from dB -> energy */
            level *= gd->s.numlines[i];
            if (x > level)
                x = level;
        }
        gfc->ATH->cb_s[i] = x;

        /* MINVAL.
           For low freq, the strength of the masking is limited by minval
           this is an ISO MPEG1 thing, dont know if it is really needed */
        x = 7.0 * (bval[i] / xbv - 1.0);
        if (bval[i] > xbv) {
            x *= 1 + log(1 + x) * 3.1;
        }
        if (bval[i] < xbv) {
            x *= 1 + log(1 - x) * 2.3;
        }
        if (x > 6) {
            x = 30;
        }
        if (x < minval_low) {
            x = minval_low;
        }
        if (cfg->samplerate_out < 44000) {
            x = 30;
        }
        x -= 8;
        gd->s.minval[i] = pow(10.0, x / 10) * gd->s.numlines[i];
    }

    i = init_s3_values(&gd->s.s3, gd->s.s3ind, gd->s.npart, bval, bval_width, norm);
    if (i)
        return i;


    init_mask_add_max_values();
    init_fft(gfc);

    /* setup temporal masking */
    gd->decay = exp(-1.0 * LOG10 / (temporalmask_sustain_sec * sfreq / 192.0));

    {
        FLOAT   msfix;
        msfix = NS_MSFIX;
        if (cfg->use_safe_joint_stereo)
            msfix = 1.0;
        if (fabs(cfg->msfix) > 0.0)
            msfix = cfg->msfix;
        cfg->msfix = msfix;

        /* spread only from npart_l bands.  Normally, we use the spreading
         * function to convolve from npart_l down to npart_l bands 
         */
        for (b = 0; b < gd->l.npart; b++)
            if (gd->l.s3ind[b][1] > gd->l.npart - 1)
                gd->l.s3ind[b][1] = gd->l.npart - 1;
    }

    /*  prepare for ATH auto adjustment:
     *  we want to decrease the ATH by 12 dB per second
     */
#define  frame_duration (576. * cfg->mode_gr / sfreq)
    gfc->ATH->decay = pow(10., -12. / 10. * frame_duration);
    gfc->ATH->adjust_factor = 0.01; /* minimum, for leading low loudness */
    gfc->ATH->adjust_limit = 1.0; /* on lead, allow adjust up to maximum */
#undef  frame_duration

    assert(gd->l.bo[SBMAX_l - 1] <= gd->l.npart);
    assert(gd->s.bo[SBMAX_s - 1] <= gd->s.npart);

    if (cfg->ATHtype != -1) {
        /* compute equal loudness weights (eql_w) */
        FLOAT   freq;
        FLOAT const freq_inc = (FLOAT) cfg->samplerate_out / (FLOAT) (BLKSIZE);
        FLOAT   eql_balance = 0.0;
        freq = 0.0;
        for (i = 0; i < BLKSIZE / 2; ++i) {
            /* convert ATH dB to relative power (not dB) */
            /*  to determine eql_w */
            freq += freq_inc;
            gfc->ATH->eql_w[i] = 1. / pow(10, ATHformula(cfg, freq) / 10);
            eql_balance += gfc->ATH->eql_w[i];
        }
        eql_balance = 1.0 / eql_balance;
        for (i = BLKSIZE / 2; --i >= 0;) { /* scale weights */
            gfc->ATH->eql_w[i] *= eql_balance;
        }
    }
    {
        for (b = j = 0; b < gd->s.npart; ++b) {
            for (i = 0; i < gd->s.numlines[b]; ++i) {
                ++j;
            }
        }
        assert(j == 129);
        for (b = j = 0; b < gd->l.npart; ++b) {
            for (i = 0; i < gd->l.numlines[b]; ++i) {
                ++j;
            }
        }
        assert(j == 513);
    }
    /* short block attack threshold */
    {
        float   x = gfp->attackthre;
        float   y = gfp->attackthre_s;
        if (x < 0) {
            x = NSATTACKTHRE;
        }
        if (y < 0) {
            y = NSATTACKTHRE_S;
        }
        gd->attack_threshold[0] = gd->attack_threshold[1] = gd->attack_threshold[2] = x;
        gd->attack_threshold[3] = y;
    }
    {
        float   sk_s = -10.f, sk_l = -4.7f;
        static float const sk[] =
            { -7.4, -7.4, -7.4, -9.5, -7.4, -6.1, -5.5, -4.7, -4.7, -4.7, -4.7 };
        if (gfp->VBR_q < 4) {
            sk_l = sk_s = sk[0];
        }
        else {
            sk_l = sk_s = sk[gfp->VBR_q] + gfp->VBR_q_frac * (sk[gfp->VBR_q] - sk[gfp->VBR_q + 1]);
        }
        b = 0;
        for (; b < gd->s.npart; b++) {
            float   m = (float) (gd->s.npart - b) / gd->s.npart;
            gd->s.masking_lower[b] = powf(10.f, sk_s * m * 0.1f);
        }
        for (; b < CBANDS; ++b) {
            gd->s.masking_lower[b] = 1.f;
        }
        b = 0;
        for (; b < gd->l.npart; b++) {
            float   m = (float) (gd->l.npart - b) / gd->l.npart;
            gd->l.masking_lower[b] = powf(10.f, sk_l * m * 0.1f);
        }
        for (; b < CBANDS; ++b) {
            gd->l.masking_lower[b] = 1.f;
        }
    }
    memcpy(&gd->l_to_s, &gd->l, sizeof(gd->l_to_s));
    init_numline(&gd->l_to_s, sfreq, BLKSIZE, 192, SBMAX_s, gfc->scalefac_band.s);
    return 0;
}
