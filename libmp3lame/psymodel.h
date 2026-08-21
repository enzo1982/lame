/*
 *	psymodel.h
 *
 *	Copyright (c) 1999 Mark Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * \file
 * \internal
 * \brief Entry points and tuning constants of the psychoacoustic model.
 *
 * \see psymodel.c for what the model computes and in what order.
 */

#ifndef LAME_PSYMODEL_H
#define LAME_PSYMODEL_H


int     L3psycho_anal_vbr(lame_internal_flags * gfc,
                          const sample_t *const buffer[2], int gr,
                          III_psy_ratio ratio[2][2],
                          III_psy_ratio MS_ratio[2][2],
                          FLOAT pe[2], FLOAT pe_MS[2], FLOAT ener[4], int blocktype_d[2]);


int     psymodel_init(lame_global_flags const* gfp);


/**
 * \brief How far a long-block threshold may rise above the previous granule's,
 *        and above the one before that.
 *
 * Pre-echo control: a quiet granule followed by a surge must not have its noise
 * floor lifted before the surge arrives to mask it. The nearer granule
 * constrains more tightly than the older one.
 *
 * \see vbrpsy_compute_masking_l()
 */
#define rpelev 2
#define rpelev2 16      /**< \brief \copybrief rpelev */

/**
 * \brief The short-block counterparts of #rpelev and #rpelev2.
 *
 * Reachable only from the disabled partition-band branch in
 * vbrpsy_compute_masking_s(). Short-block pre-echo control is applied instead
 * after the mapping to scalefactor bands, where the position of the attack
 * inside the granule is known, and that path uses #NS_PREECHO_ATT0 and its
 * companions rather than these.
 */
#define rpelev_s 2
#define rpelev2_s 16    /**< \brief \copybrief rpelev_s */

/** \brief Width of a partition band, in barks. \see init_numline() */
#define DELBARK .34


/**
 * \brief Scale factor bringing psycho_loudness_approx() onto a calibrated
 *        range, where a signal near clipping gives about 1.0.
 *
 * Sensitive to the encoder's internal energy scale: it is not a free parameter.
 */
#define VO_SCALE (1./( 14752*14752 )/(BLKSIZE/2))

/**
 * \brief Time constant of the post-masking sustain, in seconds.
 *
 * Sound remains partly masked for a while after the masker stops. This is the
 * only place the encoder claims that effect: psymodel_init() turns the value
 * into a per-sub-block decay factor, and calc_xmin() uses it to raise a short
 * sub-block's allowance towards the preceding sub-block's where that one was
 * higher.
 *
 * The claim is made only when the caller asks for it - see
 * lame_set_useTemporal(), which defaults on, except under the VBR mode that
 * is itself the default, where it defaults off.
 */
#define temporalmask_sustain_sec 0.01

/**
 * \brief Short-block pre-echo attenuations.
 *
 * Applied to scalefactor-band thresholds once the position of an attack within
 * the granule is known, in L3psycho_anal_vbr(). #NS_PREECHO_ATT0 attenuates
 * every sub-block; the other two weight an interpolation towards the preceding
 * sub-block's threshold, more strongly the closer the attack is.
 */
#define NS_PREECHO_ATT0 0.8
#define NS_PREECHO_ATT1 0.6     /**< \brief \copybrief NS_PREECHO_ATT0 */
#define NS_PREECHO_ATT2 0.3     /**< \brief \copybrief NS_PREECHO_ATT0 */

/** \brief Default bound on mid/side thresholds relative to left/right.
 *  \see vbrpsy_compute_MS_thresholds(), lame_set_msfix() */
#define NS_MSFIX 3.5

/**
 * \brief Fallback energy ratio between sub-blocks that counts as an attack,
 *        for the left, right and mid channels and for the side channel
 *        respectively.
 *
 * Consulted only where the caller left the attack threshold negative.
 * lame_init_params() applies a preset in every mode, CBR and ABR included, and
 * a preset always sets both thresholds - so an encode driven through the
 * frontend or through the documented interfaces never reaches these values.
 *
 * \see vbrpsy_attack_detection(), lame_set_short_threshold_lrm()
 */
#define NSATTACKTHRE 4.4
#define NSATTACKTHRE_S 25       /**< \brief \copybrief NSATTACKTHRE */

#endif /* LAME_PSYMODEL_H */
