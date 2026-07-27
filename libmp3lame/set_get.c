/* -*- mode: C; mode: fold -*- */
/*
 * set/get functions for lame_global_flags
 *
 * Copyright (c) 2001-2005 Alexander Leidinger
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

/*!
  \file   set_get.c
  \brief  The parameter interface of the public API.

  Every encoder setting a caller can change lives here as a set/get pair. The
  setters record a value and validate it in isolation; whether the resulting
  combination is usable is decided later, by \c lame_init_params().

  Two properties hold throughout and are not repeated on each function:

  - a setter returns 0 on success and -1 if the value was rejected or the
    instance is not usable;
  - **a getter cannot report an error.** Given an unusable instance it returns
    0, which for most of these settings is also a legitimate value, so a
    caller has no way to tell the two apart. Check the instance, not the
    result.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "util.h"
#include "bitstream.h"  /* because of compute_flushbits */

#include "set_get.h"
#include "lame_global_flags.h"

/*
 * input stream description
 */


/*! Tell the encoder how many samples the input has. */
/*!
  Only used to estimate the total number of frames, which is what
  \c lame_get_totalframes() reports and what the length field of the VBR
  header is written from. It does not limit encoding: passing more or fewer
  samples than announced is allowed, and only the estimate suffers.

  The default, 2^32-1, is the documented "length not known" sentinel rather
  than a real count, so leaving it alone is the correct thing to do for a
  stream whose length is not known in advance.

  \param gfp          the encoder instance.
  \param num_samples  number of samples per channel in the input.
  \return 0 on success, -1 if the instance is not usable. No value of
          \a num_samples is rejected.
*/
int
lame_set_num_samples(lame_global_flags * gfp, unsigned long num_samples)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 2^32-1 */
        gfp->num_samples = num_samples;
        return 0;
    }
    return -1;
}

/*! Get the number of samples the input was announced to have. */
/*!
  \param gfp the encoder instance.
  \return the value last set, or the 2^32-1 "unknown" default. 0 if the
          instance is not usable, which a caller cannot tell from a real 0.
*/
unsigned long
lame_get_num_samples(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->num_samples;
    }
    return 0;
}


/*! Set the sample rate of the input, in Hz. */
/*!
  One of the two settings that describe the input to the encoder, the other
  being \c lame_set_num_channels(). Both have defaults - 44100 Hz and 2
  channels - so an encoder that is never told about its input initializes
  successfully and encodes as though it were CD audio. Set them.

  This is the rate of the samples handed to \c lame_encode_buffer(); it is not
  necessarily the rate written into the stream. If it differs from the output
  rate (\c lame_set_out_samplerate()), LAME resamples.

  \param gfp             the encoder instance.
  \param in_samplerate   input sample rate in Hz. Any positive value is
                         accepted here - whether it can be encoded is settled
                         by \c lame_init_params().
  \return 0 on success, -1 if \a in_samplerate is below 1 or the instance is
          not usable.
*/
int
lame_set_in_samplerate(lame_global_flags * gfp, int in_samplerate)
{
    if (is_lame_global_flags_valid(gfp)) {
        if (in_samplerate < 1)
            return -1;
        /* input sample rate in Hz,  default = 44100 Hz */
        gfp->samplerate_in = in_samplerate;
        return 0;
    }
    return -1;
}

/*! Get the input sample rate, in Hz. */
/*!
  \param gfp the encoder instance.
  \return the input sample rate; 0 if the instance is not usable.
*/
int
lame_get_in_samplerate(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->samplerate_in;
    }
    return 0;
}


/*! Set the number of channels in the input. */
/*!
  The second of the two settings that describe the input; see
  \c lame_set_in_samplerate() for why leaving both alone is a trap rather than
  a convenience.

  This is the layout of the buffers handed to the encoder, not the channel
  mode written into the stream - that is \c lame_set_mode(), and LAME will
  encode two-channel input as mono if asked to.

  \param gfp           the encoder instance.
  \param num_channels  1 or 2. More is not supported: the format is MP3 and
                       LAME implements no multichannel extension.
  \return 0 on success, -1 if \a num_channels is outside 1..2 or the instance
          is not usable.
*/
int
lame_set_num_channels(lame_global_flags * gfp, int num_channels)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 2 */
        if (2 < num_channels || 0 >= num_channels) {
            return -1;  /* we don't support more than 2 channels */
        }
        gfp->num_channels = num_channels;
        return 0;
    }
    return -1;
}

/*! Get the number of channels in the input. */
/*!
  \param gfp the encoder instance.
  \return 1 or 2; 0 if the instance is not usable.
*/
int
lame_get_num_channels(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->num_channels;
    }
    return 0;
}


/*! Scale every input sample by this factor before encoding. */
/*!
  Applied to both channels, on top of any per-channel scaling from
  \c lame_set_scale_left() and \c lame_set_scale_right(). Default 1, meaning
  no change; it has no effect on decoding.

  Scaling happens before the psychoacoustic model sees the signal, so this is
  not a volume control on the output - it changes what is encoded, and scaling
  up far enough will clip.

  \param gfp    the encoder instance.
  \param scale  the factor. Not range-checked: 0 silences the input and a
                negative value inverts it, both accepted.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_scale(lame_global_flags * gfp, float scale)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale = scale;
        return 0;
    }
    return -1;
}

/*! Get the overall input scaling factor. */
/*!
  \param gfp the encoder instance.
  \return the factor; 0 if the instance is not usable - and 0 is also a value
          a caller may have set.
*/
float
lame_get_scale(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale;
    }
    return 0;
}


/*! Scale the left channel of the input by this factor before encoding. */
/*!
  Combines with \c lame_set_scale(), which applies to both channels; the two
  multiply. Default 1. No effect on decoding, and none at all on mono input.

  \param gfp    the encoder instance.
  \param scale  the factor, not range-checked. See \c lame_set_scale().
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_scale_left(lame_global_flags * gfp, float scale)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale_left = scale;
        return 0;
    }
    return -1;
}

/*! Get the left-channel input scaling factor. */
/*!
  \param gfp the encoder instance.
  \return the factor; 0 if the instance is not usable, which is also a
          settable value.
*/
float
lame_get_scale_left(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale_left;
    }
    return 0;
}


/*! Scale the right channel of the input by this factor before encoding. */
/*!
  The counterpart of \c lame_set_scale_left(); the same rules apply.

  \param gfp    the encoder instance.
  \param scale  the factor, not range-checked. See \c lame_set_scale().
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_scale_right(lame_global_flags * gfp, float scale)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 */
        gfp->scale_right = scale;
        return 0;
    }
    return -1;
}

/*! Get the right-channel input scaling factor. */
/*!
  \param gfp the encoder instance.
  \return the factor; 0 if the instance is not usable, which is also a
          settable value.
*/
float
lame_get_scale_right(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->scale_right;
    }
    return 0;
}


/*! Set the sample rate written into the MP3 stream, in Hz. */
/*!
  Default 0, which is not a rate but an instruction: let LAME choose, based on
  how much compression the bitrate settings ask for. That is the right answer
  in most cases; set it by hand only when a specific rate is required in the
  output.

  If the value differs from \c lame_set_in_samplerate(), LAME resamples. Only
  the rates the MP3 formats define are accepted, and which ones are available
  depends on the MPEG version - which this rate itself selects:

  | Version  | Rates (kHz)    |
  |----------|----------------|
  | MPEG-1   | 32, 44.1, 48   |
  | MPEG-2   | 16, 22.05, 24  |
  | MPEG-2.5 | 8, 11.025, 12  |

  Unlike most setters here, this one really does validate: a rate that is not
  in the table is refused immediately rather than at \c lame_init_params().
  It has no effect on decoding.

  \param gfp              the encoder instance.
  \param out_samplerate   output sample rate in Hz, or 0 to let LAME decide.
  \return 0 on success, -1 if the rate is not one the format allows, or the
          instance is not usable.
*/
int
lame_set_out_samplerate(lame_global_flags * gfp, int out_samplerate)
{
    if (is_lame_global_flags_valid(gfp)) {
        /*
         * default = 0: LAME picks best value based on the amount
         *              of compression
         * MPEG only allows:
         *  MPEG1    32, 44.1,   48khz
         *  MPEG2    16, 22.05,  24
         *  MPEG2.5   8, 11.025, 12
         *
         * (not used by decoding routines)
         */
        if (out_samplerate != 0) {
            int     v=0;
            if (SmpFrqIndex(out_samplerate, &v) < 0)
                return -1;
        }
        gfp->samplerate_out = out_samplerate;
        return 0;
    }
    return -1;
}

/*! Get the output sample rate, in Hz. */
/*!
  Before \c lame_init_params() this is the requested value, so the default 0
  means "not chosen yet"; afterwards it is the rate actually in use.

  \param gfp the encoder instance.
  \return the rate, 0 if it has not been chosen - and also 0 if the instance
          is not usable.
*/
int
lame_get_out_samplerate(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->samplerate_out;
    }
    return 0;
}




/*
 * general control parameters
 */

/*! Collect the per-frame data an MP3 frame analyzer displays. */
/*!
  Makes the encoder fill the \c plotting_data structure as it works, which is
  what the bundled \c mp3x analyzer reads. It costs time and memory and is of
  no use to an ordinary encode; default off.

  \param gfp       the encoder instance.
  \param analysis  1 to collect, 0 not to.
  \return 0 on success, -1 if \a analysis is not 0 or 1, or the instance is
          not usable.
*/
int
lame_set_analysis(lame_global_flags * gfp, int analysis)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > analysis || 1 < analysis)
            return -1;
        gfp->analysis = analysis;
        return 0;
    }
    return -1;
}

/*! Get whether frame-analyzer data is being collected. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_analysis(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->analysis && 1 >= gfp->analysis);
        return gfp->analysis;
    }
    return 0;
}


/*! Write the Xing/LAME header frame at the front of the stream. */
/*!
  That frame carries the VBR table a player seeks with, the length, the encoder
  delay and padding, and the replay-gain values. Without it a VBR file cannot
  be seeked accurately and its duration is guessed from the first frame.

  Default on for VBR and ABR, off for CBR. Turning it off is for callers who
  must not have a leading non-audio frame; there is no benefit otherwise.

  \param gfp           the encoder instance.
  \param bWriteVbrTag  1 to write the header, 0 not to.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_bWriteVbrTag(lame_global_flags * gfp, int bWriteVbrTag)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (on) for VBR/ABR modes, 0 (off) for CBR mode */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > bWriteVbrTag || 1 < bWriteVbrTag)
            return -1;
        gfp->write_lame_tag = bWriteVbrTag;
        return 0;
    }
    return -1;
}

/*! Get whether the Xing/LAME header frame will be written. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_bWriteVbrTag(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->write_lame_tag && 1 >= gfp->write_lame_tag);
        return gfp->write_lame_tag;
    }
    return 0;
}



/*! Use this instance to decode rather than encode. */
/*!
  A flag for the front end, not for the library: it records that this run is an
  MP3-to-WAV decode so the frontend takes that path. The encoding API itself
  does not consult it, and the decoder proper is the separate \c hip_* family.

  \param gfp          the encoder instance.
  \param decode_only  1 for decoding, 0 for encoding.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_decode_only(lame_global_flags * gfp, int decode_only)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > decode_only || 1 < decode_only)
            return -1;
        gfp->decode_only = decode_only;
        return 0;
    }
    return -1;
}

/*! Get whether this instance is marked as decode-only. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_decode_only(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->decode_only && 1 >= gfp->decode_only);
        return gfp->decode_only;
    }
    return 0;
}


#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
/* 1=encode a Vorbis .ogg file.  default=0 */
/* DEPRECATED */
int CDECL lame_set_ogg(lame_global_flags *, int);
int CDECL lame_get_ogg(const lame_global_flags *);
#else
#endif

/*! Encode a Vorbis .ogg file. */
/*!
  \deprecated Obsolete and inert. LAME once bundled a Vorbis encoder; it does
  not, and this cannot be switched on. The function is kept so that programs
  linked against an older release still resolve it, and its declaration is
  compiled out of the installed header.

  \param gfp  ignored.
  \param ogg  ignored.
  \return always -1.
*/
int
lame_set_ogg(lame_global_flags * gfp, int ogg)
{
    (void) gfp;
    (void) ogg;
    return -1;
}

/*! Get the Vorbis .ogg setting. */
/*!
  \deprecated Obsolete; see \c lame_set_ogg().
  \param gfp  ignored.
  \return always 0.
*/
int
lame_get_ogg(const lame_global_flags * gfp)
{
    (void) gfp;
    return 0;
}


/*
 * Internal algorithm selection.
 * True quality is determined by the bitrate but this variable will effect
 * quality by selecting expensive or cheap algorithms.
 * quality=0..9.  0=best (very slow).  9=worst.  
 * recommended:  3     near-best quality, not too slow
 *               5     good quality, fast
 *               7     ok quality, really fast
 */
/*! Choose how much effort the encoder spends, 0 (most) to 9 (least). */
/*!
  This is not the audio quality setting - that is the bitrate, or
  \c lame_set_VBR_quality(). What it selects is how expensive an algorithm
  LAME uses to reach that bitrate, so it trades encoding *time* against how
  well the chosen bitrate is spent.

  | Value | Meaning                          |
  |-------|----------------------------------|
  | 0     | best, very slow                  |
  | 3     | near-best, not too slow          |
  | 5     | good, fast                       |
  | 7     | acceptable, really fast          |
  | 9     | worst                            |

  Unlike most setters here, this one **clamps instead of rejecting**: a value
  below 0 becomes 0 and above 9 becomes 9, and the call still reports success.

  \param gfp      the encoder instance.
  \param quality  0..9; out-of-range values are clamped, not refused.
  \return 0 on success, -1 only if the instance is not usable.
*/
int
lame_set_quality(lame_global_flags * gfp, int quality)
{
    if (is_lame_global_flags_valid(gfp)) {
        if (quality < 0) {
            gfp->quality = 0;
        }
        else if (quality > 9) {
            gfp->quality = 9;
        }
        else {
            gfp->quality = quality;
        }
        return 0;
    }
    return -1;
}

/*! Get the algorithm-effort setting. */
/*!
  \param gfp the encoder instance.
  \return 0..9, or -1 before \c lame_init_params() if it was never set (LAME
          picks a default there); 0 if the instance is not usable.
*/
int
lame_get_quality(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quality;
    }
    return 0;
}


/*! Set the channel mode written into the stream. */
/*!
  \c STEREO encodes the two channels independently; \c JOINT_STEREO lets the
  encoder use mid/side coding where it pays, which is what makes stereo
  affordable at lower bitrates; \c MONO downmixes. \c DUAL_CHANNEL is in the
  enumeration because the format has it, but LAME does not implement it.

  The default is \c NOT_SET, meaning LAME decides from the input channel count
  and the compression ratio at \c lame_init_params(). This is independent of
  \c lame_set_num_channels(), which describes the input rather than the output.

  \param gfp   the encoder instance.
  \param mode  one of the \c MPEG_mode values.
  \return 0 on success, -1 if \a mode is not a known value or the instance is
          not usable. \c DUAL_CHANNEL is a known value and is accepted, so
          nothing reports that LAME does not implement it - do not set it.
*/
int
lame_set_mode(lame_global_flags * gfp, MPEG_mode mode)
{
    if (is_lame_global_flags_valid(gfp)) {
        int     mpg_mode = mode;
        /* default: lame chooses based on compression ratio and input channels */
        if (mpg_mode < 0 || MAX_INDICATOR <= mpg_mode)
            return -1;  /* Unknown MPEG mode! */
        gfp->mode = mode;
        return 0;
    }
    return -1;
}

/*! Get the channel mode. */
/*!
  Before \c lame_init_params() this is the request, so \c NOT_SET means "LAME
  will decide"; afterwards it is the mode actually in use.

  \param gfp the encoder instance.
  \return the mode; \c NOT_SET if the instance is not usable.
*/
MPEG_mode
lame_get_mode(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(gfp->mode < MAX_INDICATOR);
        return gfp->mode;
    }
    return NOT_SET;
}


#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
/*
  mode_automs.  Use a M/S mode with a switching threshold based on
  compression ratio
  DEPRECATED
*/
int CDECL lame_set_mode_automs(lame_global_flags *, int);
int CDECL lame_get_mode_automs(const lame_global_flags *);
#else
#endif

/*! Use an M/S mode with a threshold based on the compression ratio. */
/*!
  \deprecated Obsolete, and it no longer does what its name says. Whatever
  value is passed - including 0 - it simply selects \c JOINT_STEREO, which is
  what LAME does by default anyway; the argument is validated and then
  discarded. Call \c lame_set_mode() instead. The declaration is compiled out
  of the installed header; the definition remains for programs linked against
  an older release.

  \param gfp          the encoder instance.
  \param mode_automs  0 or 1. Validated, then ignored.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_mode_automs(lame_global_flags * gfp, int mode_automs)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > mode_automs || 1 < mode_automs)
            return -1;
        lame_set_mode(gfp, JOINT_STEREO);
        return 0;
    }
    return -1;
}

/*! Get the automatic-M/S setting. */
/*!
  \deprecated Obsolete; see \c lame_set_mode_automs(). It reports nothing about
  the instance.
  \param gfp  ignored.
  \return always 1.
*/
int
lame_get_mode_automs(const lame_global_flags * gfp)
{
    (void) gfp;
    return 1;
}


/*
 * Force M/S for all frames.  For testing only.
 * Requires mode = 1.
 */
/*! Force mid/side coding on every frame. */
/*!
  Removes the encoder's per-frame choice between L/R and M/S, which is a
  psychoacoustic decision, so this is a testing lever rather than a quality
  one: it will hurt material the encoder would have coded L/R. Requires
  \c JOINT_STEREO. Default off.

  \param gfp       the encoder instance.
  \param force_ms  1 to force M/S, 0 to let the encoder choose.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_force_ms(lame_global_flags * gfp, int force_ms)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > force_ms || 1 < force_ms)
            return -1;
        gfp->force_ms = force_ms;
        return 0;
    }
    return -1;
}

/*! Get whether mid/side coding is forced on every frame. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_force_ms(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->force_ms && 1 >= gfp->force_ms);
        return gfp->force_ms;
    }
    return 0;
}


/*! Use the free-format bitrate. */
/*!
  Free format lets a frame carry any bitrate rather than one of the values the
  standard tabulates, so a rate between or above the table's entries becomes
  possible. Many decoders do not implement it, and LAME warns about rates above
  320 kbps for that reason; a free-format file is not safe to distribute.
  Default off.

  \param gfp          the encoder instance.
  \param free_format  1 to use free format, 0 for a tabulated bitrate.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_free_format(lame_global_flags * gfp, int free_format)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > free_format || 1 < free_format)
            return -1;
        gfp->free_format = free_format;
        return 0;
    }
    return -1;
}

/*! Get whether the free-format bitrate is in use. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_free_format(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->free_format && 1 >= gfp->free_format);
        return gfp->free_format;
    }
    return 0;
}



/*! Measure ReplayGain while encoding. */
/*!
  Runs the ReplayGain analysis over the input as it is encoded, so that the
  track gain can be written into the LAME header. Read the results afterwards
  with \c lame_get_RadioGain() and \c lame_get_AudiophileGain().

  This measures the *input*. To measure what the encoded file will actually
  sound like, add \c lame_set_decode_on_the_fly(), which analyses the decoded
  output instead. Default off.

  \param gfp             the encoder instance.
  \param findReplayGain  1 to analyse, 0 not to.
  \return 0 on success, -1 if the value is not 0 or 1, or the instance is not
          usable.
*/
int
lame_set_findReplayGain(lame_global_flags * gfp, int findReplayGain)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > findReplayGain || 1 < findReplayGain)
            return -1;
        gfp->findReplayGain = findReplayGain;
        return 0;
    }
    return -1;
}

/*! Get whether ReplayGain analysis is enabled. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable.
*/
int
lame_get_findReplayGain(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->findReplayGain && 1 >= gfp->findReplayGain);
        return gfp->findReplayGain;
    }
    return 0;
}


/*! Decode each frame back as it is encoded, and measure the result. */
/*!
  Runs the decoder over LAME's own output while encoding, which is the only way
  to know what the file will really peak at - the encoder can raise the peak
  above the input's. It sets \c lame_get_PeakSample() and the clipping figures
  \c lame_get_noclipGainChange() and \c lame_get_noclipScale(), and, if
  \c lame_set_findReplayGain() is also on, moves the ReplayGain analysis onto
  the decoded audio.

  It roughly doubles the work, which is why it is off by default.

  \param gfp                the encoder instance.
  \param decode_on_the_fly  1 to decode and measure, 0 not to.
  \return 0 on success; -1 if the value is not 0 or 1, the instance is not
          usable, **or the library was built without this feature** - it is
          conditional on \c DECODE_ON_THE_FLY, and a build without it refuses
          every call here.
*/
int
lame_set_decode_on_the_fly(lame_global_flags * gfp, int decode_on_the_fly)
{
    if (is_lame_global_flags_valid(gfp)) {
#ifndef DECODE_ON_THE_FLY
        return -1;
#else
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > decode_on_the_fly || 1 < decode_on_the_fly)
            return -1;

        gfp->decode_on_the_fly = decode_on_the_fly;

        return 0;
#endif
    }
    return -1;
}

/*! Get whether the encoder decodes its own output while encoding. */
/*!
  \param gfp the encoder instance.
  \return 1 or 0; 0 if the instance is not usable, and always 0 in a build
          without \c DECODE_ON_THE_FLY.
*/
int
lame_get_decode_on_the_fly(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->decode_on_the_fly && 1 >= gfp->decode_on_the_fly);
        return gfp->decode_on_the_fly;
    }
    return 0;
}

#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
/* DEPRECATED: now does the same as lame_set_findReplayGain()
   default = 0 (disabled) */
int CDECL lame_set_ReplayGain_input(lame_global_flags *, int);
int CDECL lame_get_ReplayGain_input(const lame_global_flags *);

/* DEPRECATED: now does the same as
   lame_set_decode_on_the_fly() && lame_set_findReplayGain()
   default = 0 (disabled) */
int CDECL lame_set_ReplayGain_decode(lame_global_flags *, int);
int CDECL lame_get_ReplayGain_decode(const lame_global_flags *);

/* DEPRECATED: now does the same as lame_set_decode_on_the_fly()
   default = 0 (disabled) */
int CDECL lame_set_findPeakSample(lame_global_flags *, int);
int CDECL lame_get_findPeakSample(const lame_global_flags *);
#else
#endif

/*! Find the peak sample. */
/*!
  \deprecated Obsolete alias for \c lame_set_decode_on_the_fly(), which is what
  it calls. Use that instead; its documentation applies unchanged.
  \param gfp  the encoder instance.
  \param arg  1 or 0.
  \return whatever \c lame_set_decode_on_the_fly() returns.
*/
int
lame_set_findPeakSample(lame_global_flags * gfp, int arg)
{
    return lame_set_decode_on_the_fly(gfp, arg);
}

/*! Get the peak-sample setting. */
/*!
  \deprecated Obsolete alias for \c lame_get_decode_on_the_fly().
  \param gfp the encoder instance.
  \return whatever \c lame_get_decode_on_the_fly() returns.
*/
int
lame_get_findPeakSample(const lame_global_flags * gfp)
{
    return lame_get_decode_on_the_fly(gfp);
}

/*! Perform ReplayGain analysis on the input. */
/*!
  \deprecated Obsolete alias for \c lame_set_findReplayGain(), which is what it
  calls. Use that instead.
  \param gfp  the encoder instance.
  \param arg  1 or 0.
  \return whatever \c lame_set_findReplayGain() returns.
*/
int
lame_set_ReplayGain_input(lame_global_flags * gfp, int arg)
{
    return lame_set_findReplayGain(gfp, arg);
}

/*! Get the input ReplayGain setting. */
/*!
  \deprecated Obsolete alias for \c lame_get_findReplayGain().
  \param gfp the encoder instance.
  \return whatever \c lame_get_findReplayGain() returns.
*/
int
lame_get_ReplayGain_input(const lame_global_flags * gfp)
{
    return lame_get_findReplayGain(gfp);
}

/*! Perform ReplayGain analysis on the decoded output. */
/*!
  \deprecated Obsolete. It sets \c lame_set_decode_on_the_fly() and
  \c lame_set_findReplayGain() together; call those two instead.
  \param gfp  the encoder instance.
  \param arg  1 or 0.
  \return 0 if both calls succeeded, -1 if either failed - which includes a
          build without \c DECODE_ON_THE_FLY, and leaves the first setting
          already applied.
*/
int
lame_set_ReplayGain_decode(lame_global_flags * gfp, int arg)
{
    if (lame_set_decode_on_the_fly(gfp, arg) < 0 || lame_set_findReplayGain(gfp, arg) < 0)
        return -1;
    else
        return 0;
}

/*! Get whether ReplayGain is being measured on the decoded output. */
/*!
  \deprecated Obsolete; ask \c lame_get_decode_on_the_fly() and
  \c lame_get_findReplayGain() instead.
  \param gfp the encoder instance.
  \return 1 only if both of those are on, 0 otherwise.
*/
int
lame_get_ReplayGain_decode(const lame_global_flags * gfp)
{
    if (lame_get_decode_on_the_fly(gfp) > 0 && lame_get_findReplayGain(gfp) > 0)
        return 1;
    else
        return 0;
}


/*! Tell the encoder how many files a gapless set has. */
/*!
  Part of the gapless split-file support, together with
  \c lame_set_nogap_currentindex() and \c lame_encode_flush_nogap(). Knowing
  the total lets the encoder record, in each file's LAME header, where that
  file sits in the sequence, so a player can join them without a gap.

  \param gfp              the encoder instance.
  \param the_nogap_total  number of files in the set.
  \return 0 on success, -1 if the instance is not usable. The value is not
          range-checked.
*/
int
lame_set_nogap_total(lame_global_flags * gfp, int the_nogap_total)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->nogap_total = the_nogap_total;
        return 0;
    }
    return -1;
}

/*! Get the number of files in the gapless set. */
/*!
  \param gfp the encoder instance.
  \return the count; 0 if the instance is not usable.
*/
int
lame_get_nogap_total(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->nogap_total;
    }
    return 0;
}

/*! Tell the encoder which file of a gapless set is being written. */
/*!
  The companion to \c lame_set_nogap_total(); set it before each file in the
  sequence.

  \param gfp              the encoder instance.
  \param the_nogap_index  index of the current file within the set.
  \return 0 on success, -1 if the instance is not usable. The value is not
          range-checked against the total.
*/
int
lame_set_nogap_currentindex(lame_global_flags * gfp, int the_nogap_index)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->nogap_current = the_nogap_index;
        return 0;
    }
    return -1;
}

/*! Get the index of the current file within the gapless set. */
/*!
  \param gfp the encoder instance.
  \return the index; 0 if the instance is not usable.
*/
int
lame_get_nogap_currentindex(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->nogap_current;
    }
    return 0;
}


/*! Route the library's error messages to a callback of your own. */
/*!
  LAME reports in three streams - errors, debug output and ordinary messages -
  and by default all three go to \c stderr. A library embedded in an
  application usually wants them somewhere else; these three setters are how.

  The callback is handed a \c printf format string and a \c va_list, so an
  implementation is normally a one-line \c vfprintf or \c vsnprintf. It is
  called from inside encoding calls, and the strings it receives are for
  people: their wording is not stable across versions and must not be parsed.

  \code
  static void report(const char *fmt, va_list ap) { vfprintf(mylog, fmt, ap); }
  lame_set_errorf(gfp, report);
  lame_set_msgf(gfp, report);
  lame_set_debugf(gfp, report);
  \endcode

  \param gfp   the encoder instance.
  \param func  the callback, or \c NULL to silence this stream.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_errorf(lame_global_flags * gfp, void (*func) (const char *, va_list))
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.errorf = func;
        return 0;
    }
    return -1;
}

/*! Route the library's debug output to a callback of your own. */
/*!
  The debug stream of the three described under \c lame_set_errorf(); the same
  rules apply.

  \param gfp   the encoder instance.
  \param func  the callback, or \c NULL to silence this stream.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_debugf(lame_global_flags * gfp, void (*func) (const char *, va_list))
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.debugf = func;
        return 0;
    }
    return -1;
}

/*! Route the library's ordinary messages to a callback of your own. */
/*!
  The message stream of the three described under \c lame_set_errorf(). This is
  the one \c lame_print_config() and \c lame_print_internals() write to.

  \param gfp   the encoder instance.
  \param func  the callback, or \c NULL to silence this stream.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_msgf(lame_global_flags * gfp, void (*func) (const char *, va_list))
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->report.msgf = func;
        return 0;
    }
    return -1;
}


/*
 * Set one of
 *  - brate
 *  - compression ratio.
 *
 * Default is compression ratio of 11.
 */
/*! Set the bitrate, in kbps. */
/*!
  For CBR this is the bitrate of every frame; for ABR it is the average
  (\c lame_set_VBR_mean_bitrate_kbps() is the same setting under its ABR name).
  It is the alternative to \c lame_set_compression_ratio() - set one of the
  two, not both, and if neither is set LAME uses a compression ratio of 11.

  A rate above 320 kbps requires free format, and setting one here **silently
  turns the bit reservoir off** as a side effect, because the two cannot be
  combined. Nothing reports that; if you later read
  \c lame_get_disable_reservoir() and find it on, this is why.

  \param gfp    the encoder instance.
  \param brate  bitrate in kbps. Not validated here - whether the value is one
                the chosen MPEG version and sample rate allow is settled by
                \c lame_init_params(), which moves it to the nearest legal
                value rather than failing.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_brate(lame_global_flags * gfp, int brate)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->brate = brate;
        if (brate > 320) {
            gfp->disable_reservoir = 1;
        }
        return 0;
    }
    return -1;
}

/*! Get the bitrate, in kbps. */
/*!
  Before \c lame_init_params() this is what was requested; afterwards it is the
  rate actually chosen, which may differ - LAME moves an unavailable request to
  the nearest legal value.

  \param gfp the encoder instance.
  \return the bitrate in kbps; 0 if the instance is not usable, and also 0 when
          the bitrate was left to be derived from the compression ratio and
          \c lame_init_params() has not run yet.
*/
int
lame_get_brate(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->brate;
    }
    return 0;
}

/*! Set the bitrate indirectly, as a compression ratio. */
/*!
  The ratio of the input's data rate to the output's, so 11 means roughly
  eleven times smaller - which for 44.1 kHz 16-bit stereo works out near
  128 kbps. The alternative to \c lame_set_brate(): set one or the other.
  Default 11.

  \c lame_init_params() turns it into a bitrate, so what
  \c lame_get_brate() reports afterwards is the real setting.

  \param gfp                the encoder instance.
  \param compression_ratio  the ratio. Not range-checked here.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_compression_ratio(lame_global_flags * gfp, float compression_ratio)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->compression_ratio = compression_ratio;
        return 0;
    }
    return -1;
}

/*! Get the compression ratio. */
/*!
  \param gfp the encoder instance.
  \return the ratio; 0 if the instance is not usable. After
          \c lame_init_params() this holds the ratio the chosen bitrate
          actually achieves, whichever of the two was set.
*/
float
lame_get_compression_ratio(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->compression_ratio;
    }
    return 0;
}




/*
 * frame parameters
 */

/*! Set the copyright bit in the frame header. */
/*!
  One of the four flag bits every MPEG audio frame header carries. LAME writes
  it and nothing else acts on it: it is a declaration to whoever reads the
  file, not a restriction the encoder or a decoder enforces. The same value is
  copied into the header of the VBR tag frame.

  \param gfp        the encoder instance.
  \param copyright  1 to set the bit, 0 to clear it. Default 0.
  \return 0 on success, -1 if the instance is not usable or \a copyright is
          neither 0 nor 1.
*/
int
lame_set_copyright(lame_global_flags * gfp, int copyright)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > copyright || 1 < copyright)
            return -1;
        gfp->copyright = copyright;
        return 0;
    }
    return -1;
}

/*! Get the copyright bit. */
/*!
  \param gfp the encoder instance.
  \return 0 or 1; 0 if the instance is not usable, which is also the default.
*/
int
lame_get_copyright(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->copyright && 1 >= gfp->copyright);
        return gfp->copyright;
    }
    return 0;
}


/*! Set the original bit in the frame header. */
/*!
  The companion of \c lame_set_copyright(): the bit that says this is an
  original recording rather than a copy. LAME writes it into every frame
  header and into the VBR tag frame, and nothing reads it back.

  Note the default is 1, not 0 - an encode that says nothing claims to be an
  original.

  \param gfp       the encoder instance.
  \param original  1 to set the bit, 0 to clear it. Default 1.
  \return 0 on success, -1 if the instance is not usable or \a original is
          neither 0 nor 1.
*/
int
lame_set_original(lame_global_flags * gfp, int original)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (enabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > original || 1 < original)
            return -1;
        gfp->original = original;
        return 0;
    }
    return -1;
}

/*! Get the original bit. */
/*!
  \param gfp the encoder instance.
  \return 0 or 1. 0 if the instance is not usable - and since the default is
          1, a 0 here is worth a second look.
*/
int
lame_get_original(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->original && 1 >= gfp->original);
        return gfp->original;
    }
    return 0;
}


/*! Add a CRC checksum to every frame. */
/*!
  Turns on the optional 16-bit CRC over the frame header and side information.
  It costs **two bytes per frame**, taken out of the space available for audio
  data, so at a fixed bitrate the audio is encoded slightly more coarsely; it
  buys a decoder the ability to notice a corrupted frame and mute it instead
  of playing noise.

  The frame header bit has inverted sense - it is written as *no protection* -
  so a caller inspecting a bitstream by hand should expect a 0 here to mean
  the CRC is present.

  \param gfp               the encoder instance.
  \param error_protection  1 to add the checksum, 0 for none. Default 0.
  \return 0 on success, -1 if the instance is not usable or the value is
          neither 0 nor 1.
*/
int
lame_set_error_protection(lame_global_flags * gfp, int error_protection)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > error_protection || 1 < error_protection)
            return -1;
        gfp->error_protection = error_protection;
        return 0;
    }
    return -1;
}

/*! Get the CRC setting. */
/*!
  \param gfp the encoder instance.
  \return 1 if frames carry a CRC, 0 if not or if the instance is not usable.
*/
int
lame_get_error_protection(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->error_protection && 1 >= gfp->error_protection);
        return gfp->error_protection;
    }
    return 0;
}


#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
/* padding_type. 0=pad no frames  1=pad all frames 2=adjust padding(default) */
int CDECL lame_set_padding_type(lame_global_flags *, Padding_type);
Padding_type CDECL lame_get_padding_type(const lame_global_flags *);
#else
#endif

/*! Choose how frames are padded. */
/*!
  \deprecated Obsolete and inert. Padding is not a choice any more: the encoder
  works out per frame whether a padding slot is needed to keep the average
  bitrate exact, which is what \c PAD_ADJUST used to name. \c PAD_NO and
  \c PAD_ALL are gone with it. The declaration is compiled out of the installed
  header; the definition remains so that programs linked against an older
  release still resolve it.

  Unlike the other setters this one **reports success unconditionally** -
  including for an unusable instance - because there is nothing it could fail
  at.

  \param gfp           ignored.
  \param padding_type  ignored.
  \return always 0.
*/
int
lame_set_padding_type(lame_global_flags * gfp, Padding_type padding_type)
{
    (void) gfp;
    (void) padding_type;
    return 0;
}

/*! Get the padding mode. */
/*!
  \deprecated Obsolete; see \c lame_set_padding_type().
  \param gfp  ignored.
  \return always \c PAD_ADJUST, whatever was set.
*/
Padding_type
lame_get_padding_type(const lame_global_flags * gfp)
{
    (void) gfp;
    return PAD_ADJUST;
}


/*! Set the private bit in the frame header. */
/*!
  The fourth header flag, reserved by the standard for private use and given
  no meaning by it. LAME writes it through to every frame header and to the VBR
  tag frame, and never looks at it. An application may use it to smuggle one
  bit past a decoder, at the risk that some other application has already
  chosen a different meaning for it.

  \param gfp        the encoder instance.
  \param extension  1 to set the bit, 0 to clear it. Default 0.
  \return 0 on success, -1 if the instance is not usable or the value is
          neither 0 nor 1.
*/
int
lame_set_extension(lame_global_flags * gfp, int extension)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > extension || 1 < extension)
            return -1;
        gfp->extension = extension;
        return 0;
    }
    return -1;
}

/*! Get the private bit. */
/*!
  \param gfp the encoder instance.
  \return 0 or 1; 0 if the instance is not usable, which is also the default.
*/
int
lame_get_extension(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->extension && 1 >= gfp->extension);
        return gfp->extension;
    }
    return 0;
}


/*! Choose how large a bit reservoir the bitstream may rely on. */
/*!
  Despite the name this is **not a flag** but a three-way choice from
  \c buffer_constraint, and it governs exactly one thing: the ceiling LAME
  respects for `main_data_begin`, i.e. how far back into earlier frames a
  frame's audio data may reach.

  - \c MDB_DEFAULT - a practical ceiling every decoder in circulation copes
    with, the size of a 320 kbps 32 kHz frame.
  - \c MDB_STRICT_ISO - the ceiling the ISO document allows for the layout
    actually in use. Choose this if the output has to satisfy a conformance
    checker.
  - \c MDB_MAXIMUM - the largest the format can express, 7680 bits per
    granule. Gives the bit allocator the most room, and is what LAME uses
    unless told otherwise.

  Note that the default is \c MDB_MAXIMUM, **not 0**. The name invites the
  opposite assumption, and so does the comment in the public header, which
  predates the choice becoming three-way.

  \param gfp  the encoder instance.
  \param val  one of the \c buffer_constraint values.
  \return 0 on success, -1 if the instance is not usable or \a val is outside
          the enumeration.
*/
int
lame_set_strict_ISO(lame_global_flags * gfp, int val)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (val < MDB_DEFAULT || MDB_MAXIMUM < val)
            return -1;
        gfp->strict_ISO = val;
        return 0;
    }
    return -1;
}

/*! Get the bit reservoir constraint. */
/*!
  \param gfp the encoder instance.
  \return one of the \c buffer_constraint values. An unusable instance yields
          \c MDB_DEFAULT, which is a legitimate setting but never the default
          one - see \c lame_set_strict_ISO().
*/
int
lame_get_strict_ISO(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->strict_ISO;
    }
    return 0;
}




/********************************************************************
 * quantization/noise shaping 
 ***********************************************************************/

/*! Forbid frames from borrowing space from earlier ones. */
/*!
  The bit reservoir lets a frame that needs more bits than its share take them
  from the unused tail of frames already written. Switching it off makes every
  frame stand on its own, which costs quality at a given bitrate - a demanding
  passage can no longer be given extra bits - and buys the property that each
  frame decodes without its predecessors.

  One thing sets it without being asked: \c lame_set_brate() above 320 kbps
  turns it on and reports nothing. Reading this getter back is the only way to
  notice.

  \param gfp                the encoder instance.
  \param disable_reservoir  1 to forbid the reservoir, 0 to allow it.
                            Default 0.
  \return 0 on success, -1 if the instance is not usable or the value is
          neither 0 nor 1.
*/
int
lame_set_disable_reservoir(lame_global_flags * gfp, int disable_reservoir)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > disable_reservoir || 1 < disable_reservoir)
            return -1;
        gfp->disable_reservoir = disable_reservoir;
        return 0;
    }
    return -1;
}

/*! Get the bit reservoir setting. */
/*!
  \param gfp the encoder instance.
  \return 1 if the reservoir is off, 0 if it is available or the instance is
          not usable.
*/
int
lame_get_disable_reservoir(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->disable_reservoir && 1 >= gfp->disable_reservoir);
        return gfp->disable_reservoir;
    }
    return 0;
}




/*! Set both quantization comparison functions at once. */
/*!
  Kept for compatibility with code written before the long-block and
  short-block choices were separated. It sets \c lame_set_quant_comp() and
  \c lame_set_quant_comp_short() to the same value; new code should set the
  two directly, because they are usually wanted at different settings.

  \param gfp            the encoder instance.
  \param experimentalX  the comparison function, applied to both.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_experimentalX(lame_global_flags * gfp, int experimentalX)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_set_quant_comp(gfp, experimentalX);
        lame_set_quant_comp_short(gfp, experimentalX);
        return 0;
    }
    return -1;
}

/*! Get the long-block quantization comparison function. */
/*!
  Asymmetric with its setter: the setter writes both values, this reads only
  the long-block one. If the short-block value was changed afterwards, this
  does not say so.

  \param gfp the encoder instance.
  \return what \c lame_get_quant_comp() returns.
*/
int
lame_get_experimentalX(const lame_global_flags * gfp)
{
    return lame_get_quant_comp(gfp);
}


/*! Choose how two candidate quantizations are compared. */
/*!
  The inner loop tries several quantizations of a granule and keeps the one it
  judges best. This selects the yardstick: which of *number of distorted
  scalefactor bands*, *total noise*, *peak noise* and a few weighted
  combinations of them decides the winner. It changes what the encoder
  considers good, not how hard it works - that is \c lame_set_quality().

  Values 0 to 9 name the strategies; anything else behaves as 9. There is no
  ordering among them, so this is a knob for experiments, not a dial to turn up.

  The default is -1, meaning *unset*: \c lame_init_params() then picks 1 for
  long blocks. So a getter call before initialization returns -1, and the same
  call afterwards returns something else without anyone having set it.

  \param gfp         the encoder instance.
  \param quant_type  the strategy, 0 to 9. **Not validated** - the value is
                     stored as given.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_quant_comp(lame_global_flags * gfp, int quant_type)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->quant_comp = quant_type;
        return 0;
    }
    return -1;
}

/*! Get the long-block quantization comparison. */
/*!
  \param gfp the encoder instance.
  \return the strategy, or -1 while it is still unset. 0 if the instance is
          not usable - which is a legitimate strategy, so it does not signal
          anything.
*/
int
lame_get_quant_comp(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quant_comp;
    }
    return 0;
}


/*! Choose the comparison used for short blocks. */
/*!
  As \c lame_set_quant_comp(), applied to granules encoded as short blocks -
  the ones covering a transient, where a different yardstick is often wanted.
  Same value range, same absence of validation.

  Its unset default resolves to 0, not to the 1 the long-block setting gets.

  \param gfp         the encoder instance.
  \param quant_type  the strategy, 0 to 9.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_quant_comp_short(lame_global_flags * gfp, int quant_type)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->quant_comp_short = quant_type;
        return 0;
    }
    return -1;
}

/*! Get the short-block quantization comparison. */
/*!
  \param gfp the encoder instance.
  \return the strategy, or -1 while it is still unset. 0 if the instance is
          not usable.
*/
int
lame_get_quant_comp_short(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->quant_comp_short;
    }
    return 0;
}


/*! Suppress the extra bits normally spent above 16 kHz. */
/*!
  Non-zero stops LAME from giving scalefactor band 21 - the topmost band, above
  roughly 16 kHz - the additional bits it otherwise gets on MPEG-1 material
  sampled above 44 kHz. The result is a smaller file whose top octave is
  coarser.

  \param gfp            the encoder instance.
  \param experimentalY  non-zero to suppress the extra bits. Default 0.
  \return 0 on success, -1 if the instance is not usable. No value is
          rejected.
*/
int
lame_set_experimentalY(lame_global_flags * gfp, int experimentalY)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->experimentalY = experimentalY;
        return 0;
    }
    return -1;
}

/*! Get the sfb21 suppression setting. */
/*!
  \param gfp the encoder instance.
  \return the value last set; 0 if the instance is not usable, which is also
          the default.
*/
int
lame_get_experimentalY(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->experimentalY;
    }
    return 0;
}


/*! Compute short-block masking thresholds even where they are not needed. */
/*!
  The psychoacoustic model normally skips the short-block analysis for a
  granule it has already decided to encode as a long block. Non-zero makes it
  do the work anyway, so the thresholds exist for every granule. Slower, and
  intended for comparing the two paths rather than for production encoding.

  Read once, when the psychoacoustic model is set up during
  \c lame_init_params(); changing it afterwards does nothing.

  \param gfp            the encoder instance.
  \param experimentalZ  non-zero to force the computation. Default 0.
  \return 0 on success, -1 if the instance is not usable. No value is
          rejected.
*/
int
lame_set_experimentalZ(lame_global_flags * gfp, int experimentalZ)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->experimentalZ = experimentalZ;
        return 0;
    }
    return -1;
}

/*! Get the forced short-block analysis setting. */
/*!
  \param gfp the encoder instance.
  \return the value last set; 0 if the instance is not usable, which is also
          the default.
*/
int
lame_get_experimentalZ(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->experimentalZ;
    }
    return 0;
}


/*! Set the packed psychoacoustic tuning word. */
/*!
  Not a flag, despite the name and the header comment: a single \c int in
  which several unrelated settings are packed. \c lame_init_params() unpacks
  it as

  | bits | meaning |
  |------|---------|
  | 0    | currently unused. Reserved for selecting a psychoacoustic model, should a second one be offered again. |
  | 1    | safe joint stereo. Honoured only when the mode really is joint stereo. |
  | 2-7  | bass adjustment |
  | 8-13 | alto, i.e. mid-range, adjustment |
  | 14-19| treble adjustment |
  | 20-25| additional adjustment for the topmost scalefactor band, **added on top of** the treble one |

  Each adjustment is a 6-bit two's complement number in quarter-decibel steps,
  so -32 to 31 quarter-dB, i.e. **-8.00 to +7.75 dB**. Negative values give
  that range less weight in the masking calculation.

  This is the interface the frontend's tuning switches are built on. Compose
  the value with a bitwise OR against what is already there, the way the
  presets do - a bare assignment silently clears the fields a preset set.
  Passing 1 to mean "on" sets bit 0 alone, which selects nothing.

  \param gfp            the encoder instance.
  \param exp_nspsytune  the packed word. **Not validated**; bits above the
                        fields listed are ignored.
  \return 0 on success, -1 if the instance is not usable.
*/
int
lame_set_exp_nspsytune(lame_global_flags * gfp, int exp_nspsytune)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */
        gfp->exp_nspsytune = exp_nspsytune;
        return 0;
    }
    return -1;
}

/*! Get the packed psychoacoustic tuning word. */
/*!
  \param gfp the encoder instance.
  \return the packed word, to be modified and set back. 0 if the instance is
          not usable, which is also the default and therefore says nothing.
*/
int
lame_get_exp_nspsytune(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->exp_nspsytune;
    }
    return 0;
}




/********************************************************************
 * VBR control
 ***********************************************************************/

/* Types of VBR.  default = vbr_off = CBR */
int
lame_set_VBR(lame_global_flags * gfp, vbr_mode VBR)
{
    if (is_lame_global_flags_valid(gfp)) {
        int     vbr_q = VBR;
        if (0 > vbr_q || vbr_max_indicator <= vbr_q)
            return -1;  /* Unknown VBR mode! */
        gfp->VBR = VBR;
        return 0;
    }
    return -1;
}

vbr_mode
lame_get_VBR(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(gfp->VBR < vbr_max_indicator);
        return gfp->VBR;
    }
    return vbr_off;
}


/*
 * VBR quality level.
 *  0 = highest
 *  9 = lowest 
 */
int
lame_set_VBR_q(lame_global_flags * gfp, int VBR_q)
{
    if (is_lame_global_flags_valid(gfp)) {
        int     ret = 0;

        if (0 > VBR_q) {
            ret = -1;   /* Unknown VBR quality level! */
            VBR_q = 0;
        }
        if (9 < VBR_q) {
            ret = -1;
            VBR_q = 9;
        }
        gfp->VBR_q = VBR_q;
        gfp->VBR_q_frac = 0;
        return ret;
    }
    return -1;
}

int
lame_get_VBR_q(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->VBR_q && 10 > gfp->VBR_q);
        return gfp->VBR_q;
    }
    return 0;
}

int
lame_set_VBR_quality(lame_global_flags * gfp, float VBR_q)
{
    if (is_lame_global_flags_valid(gfp)) {
        int     ret = 0;

        if (0 > VBR_q) {
            ret = -1;   /* Unknown VBR quality level! */
            VBR_q = 0;
        }
        if (9.999 < VBR_q) {
            ret = -1;
            VBR_q = 9.999;
        }

        gfp->VBR_q = (int) VBR_q;
        gfp->VBR_q_frac = VBR_q - gfp->VBR_q;

        return ret;
    }
    return -1;
}

float
lame_get_VBR_quality(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_q + gfp->VBR_q_frac;
    }
    return 0;
}


/* Ignored except for VBR = vbr_abr (ABR mode) */
int
lame_set_VBR_mean_bitrate_kbps(lame_global_flags * gfp, int VBR_mean_bitrate_kbps)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_mean_bitrate_kbps = VBR_mean_bitrate_kbps;
        return 0;
    }
    return -1;
}

int
lame_get_VBR_mean_bitrate_kbps(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_mean_bitrate_kbps;
    }
    return 0;
}

int
lame_set_VBR_min_bitrate_kbps(lame_global_flags * gfp, int VBR_min_bitrate_kbps)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_min_bitrate_kbps = VBR_min_bitrate_kbps;
        return 0;
    }
    return -1;
}

int
lame_get_VBR_min_bitrate_kbps(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_min_bitrate_kbps;
    }
    return 0;
}

int
lame_set_VBR_max_bitrate_kbps(lame_global_flags * gfp, int VBR_max_bitrate_kbps)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->VBR_max_bitrate_kbps = VBR_max_bitrate_kbps;
        return 0;
    }
    return -1;
}

int
lame_get_VBR_max_bitrate_kbps(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->VBR_max_bitrate_kbps;
    }
    return 0;
}


/*
 * Strictly enforce VBR_min_bitrate.
 * Normally it will be violated for analog silence.
 */
int
lame_set_VBR_hard_min(lame_global_flags * gfp, int VBR_hard_min)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 (disabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > VBR_hard_min || 1 < VBR_hard_min)
            return -1;

        gfp->VBR_hard_min = VBR_hard_min;

        return 0;
    }
    return -1;
}

int
lame_get_VBR_hard_min(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->VBR_hard_min && 1 >= gfp->VBR_hard_min);
        return gfp->VBR_hard_min;
    }
    return 0;
}


/********************************************************************
 * Filtering control
 ***********************************************************************/

/*
 * Freqency in Hz to apply lowpass.
 *   0 = default = lame chooses
 *  -1 = disabled
 */
int
lame_set_lowpassfreq(lame_global_flags * gfp, int lowpassfreq)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->lowpassfreq = lowpassfreq;
        return 0;
    }
    return -1;
}

int
lame_get_lowpassfreq(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->lowpassfreq;
    }
    return 0;
}


/*
 * Width of transition band (in Hz).
 *  default = one polyphase filter band
 */
int
lame_set_lowpasswidth(lame_global_flags * gfp, int lowpasswidth)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->lowpasswidth = lowpasswidth;
        return 0;
    }
    return -1;
}

int
lame_get_lowpasswidth(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->lowpasswidth;
    }
    return 0;
}


/*
 * Frequency in Hz to apply highpass.
 *   0 = default = lame chooses
 *  -1 = disabled
 */
int
lame_set_highpassfreq(lame_global_flags * gfp, int highpassfreq)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->highpassfreq = highpassfreq;
        return 0;
    }
    return -1;
}

int
lame_get_highpassfreq(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->highpassfreq;
    }
    return 0;
}


/*
 * Width of transition band (in Hz).
 *  default = one polyphase filter band
 */
int
lame_set_highpasswidth(lame_global_flags * gfp, int highpasswidth)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->highpasswidth = highpasswidth;
        return 0;
    }
    return -1;
}

int
lame_get_highpasswidth(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->highpasswidth;
    }
    return 0;
}




/*
 * psycho acoustics and other arguments which you should not change 
 * unless you know what you are doing
 */


/* Adjust masking values. */
int
lame_set_maskingadjust(lame_global_flags * gfp, float adjust)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->maskingadjust = adjust;
        return 0;
    }
    return -1;
}

float
lame_get_maskingadjust(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->maskingadjust;
    }
    return 0;
}

int
lame_set_maskingadjust_short(lame_global_flags * gfp, float adjust)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->maskingadjust_short = adjust;
        return 0;
    }
    return -1;
}

float
lame_get_maskingadjust_short(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->maskingadjust_short;
    }
    return 0;
}

/* Only use ATH for masking. */
int
lame_set_ATHonly(lame_global_flags * gfp, int ATHonly)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHonly = ATHonly;
        return 0;
    }
    return -1;
}

int
lame_get_ATHonly(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHonly;
    }
    return 0;
}


/* Only use ATH for short blocks. */
int
lame_set_ATHshort(lame_global_flags * gfp, int ATHshort)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHshort = ATHshort;
        return 0;
    }
    return -1;
}

int
lame_get_ATHshort(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHshort;
    }
    return 0;
}


/* Disable ATH. */
int
lame_set_noATH(lame_global_flags * gfp, int noATH)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->noATH = noATH;
        return 0;
    }
    return -1;
}

int
lame_get_noATH(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->noATH;
    }
    return 0;
}


/* Select ATH formula. */
int
lame_set_ATHtype(lame_global_flags * gfp, int ATHtype)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* XXX: ATHtype should be converted to an enum. */
        gfp->ATHtype = ATHtype;
        return 0;
    }
    return -1;
}

int
lame_get_ATHtype(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHtype;
    }
    return 0;
}


/* Select ATH formula 4 shape. */
int
lame_set_ATHcurve(lame_global_flags * gfp, float ATHcurve)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATHcurve = ATHcurve;
        return 0;
    }
    return -1;
}

float
lame_get_ATHcurve(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATHcurve;
    }
    return 0;
}


/* Lower ATH by this many db. */
int
lame_set_ATHlower(lame_global_flags * gfp, float ATHlower)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->ATH_lower_db = ATHlower;
        return 0;
    }
    return -1;
}

float
lame_get_ATHlower(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->ATH_lower_db;
    }
    return 0;
}


/* Select ATH adaptive adjustment scheme. */
int
lame_set_athaa_type(lame_global_flags * gfp, int athaa_type)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->athaa_type = athaa_type;
        return 0;
    }
    return -1;
}

int
lame_get_athaa_type(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->athaa_type;
    }
    return 0;
}


#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
int CDECL lame_set_athaa_loudapprox(lame_global_flags * gfp, int athaa_loudapprox);
int CDECL lame_get_athaa_loudapprox(const lame_global_flags * gfp);
#else
#endif

/* Select the loudness approximation used by the ATH adaptive auto-leveling. */
int
lame_set_athaa_loudapprox(lame_global_flags * gfp, int athaa_loudapprox)
{
    (void) gfp;
    (void) athaa_loudapprox;
    return 0;
}

int
lame_get_athaa_loudapprox(const lame_global_flags * gfp)
{
    (void) gfp;
    /* obsolete, the type known under number 2 is the only survival */
    return 2;
}


/* Adjust (in dB) the point below which adaptive ATH level adjustment occurs. */
int
lame_set_athaa_sensitivity(lame_global_flags * gfp, float athaa_sensitivity)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->athaa_sensitivity = athaa_sensitivity;
        return 0;
    }
    return -1;
}

float
lame_get_athaa_sensitivity(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->athaa_sensitivity;
    }
    return 0;
}


/* Predictability limit (ISO tonality formula) */
int     lame_set_cwlimit(lame_global_flags * gfp, int cwlimit);
int     lame_get_cwlimit(const lame_global_flags * gfp);

int
lame_set_cwlimit(lame_global_flags * gfp, int cwlimit)
{
    (void) gfp;
    (void) cwlimit;
    return 0;
}

int
lame_get_cwlimit(const lame_global_flags * gfp)
{
    (void) gfp;
    return 0;
}



/*
 * Allow blocktypes to differ between channels.
 * default:
 *  0 for jstereo => block types coupled
 *  1 for stereo  => block types may differ
 */
int
lame_set_allow_diff_short(lame_global_flags * gfp, int allow_diff_short)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->short_blocks = allow_diff_short ? short_block_allowed : short_block_coupled;
        return 0;
    }
    return -1;
}

int
lame_get_allow_diff_short(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        if (gfp->short_blocks == short_block_allowed)
            return 1;   /* short blocks allowed to differ */
        else
            return 0;   /* not set, dispensed, forced or coupled */
    }
    return 0;
}


/* Use temporal masking effect */
int
lame_set_useTemporal(lame_global_flags * gfp, int useTemporal)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 1 (enabled) */

        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 <= useTemporal && useTemporal <= 1) {
            gfp->useTemporal = useTemporal;
            return 0;
        }
    }
    return -1;
}

int
lame_get_useTemporal(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->useTemporal && 1 >= gfp->useTemporal);
        return gfp->useTemporal;
    }
    return 0;
}


/* Use inter-channel masking effect */
int
lame_set_interChRatio(lame_global_flags * gfp, float ratio)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0.0 (no inter-channel maskin) */
        if (0 <= ratio && ratio <= 1.0) {
            gfp->interChRatio = ratio;
            return 0;
        }
    }
    return -1;
}

float
lame_get_interChRatio(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert((0 <= gfp->interChRatio && gfp->interChRatio <= 1.0) || EQ(gfp->interChRatio, -1.0));
        return gfp->interChRatio;
    }
    return 0;
}


/* Use pseudo substep shaping method */
int
lame_set_substep(lame_global_flags * gfp, int method)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0.0 (no substep noise shaping) */
        if (0 <= method && method <= 7) {
            gfp->substep_shaping = method;
            return 0;
        }
    }
    return -1;
}

int
lame_get_substep(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->substep_shaping && gfp->substep_shaping <= 7);
        return gfp->substep_shaping;
    }
    return 0;
}

/* scalefactors scale */
int
lame_set_sfscale(lame_global_flags * gfp, int val)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->noise_shaping = (val != 0) ? 2 : 1;
        return 0;
    }
    return -1;
}

int
lame_get_sfscale(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return (gfp->noise_shaping == 2) ? 1 : 0;
    }
    return 0;
}

/* subblock gain */
int
lame_set_subblock_gain(lame_global_flags * gfp, int sbgain)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->subblock_gain = sbgain;
        return 0;
    }
    return -1;
}

int
lame_get_subblock_gain(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->subblock_gain;
    }
    return 0;
}


/* Disable short blocks. */
int
lame_set_no_short_blocks(lame_global_flags * gfp, int no_short_blocks)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 <= no_short_blocks && no_short_blocks <= 1) {
            gfp->short_blocks = no_short_blocks ? short_block_dispensed : short_block_allowed;
            return 0;
        }
    }
    return -1;
}

int
lame_get_no_short_blocks(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        switch (gfp->short_blocks) {
        default:
        case short_block_not_set:
            return -1;
        case short_block_dispensed:
            return 1;
        case short_block_allowed:
        case short_block_coupled:
        case short_block_forced:
            return 0;
        }
    }
    return -1;
}


/* Force short blocks. */
int
lame_set_force_short_blocks(lame_global_flags * gfp, int short_blocks)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* enforce disable/enable meaning, if we need more than two values
           we need to switch to an enum to have an apropriate representation
           of the possible meanings of the value */
        if (0 > short_blocks || 1 < short_blocks)
            return -1;

        if (short_blocks == 1)
            gfp->short_blocks = short_block_forced;
        else if (gfp->short_blocks == short_block_forced)
            gfp->short_blocks = short_block_allowed;

        return 0;
    }
    return -1;
}

int
lame_get_force_short_blocks(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        switch (gfp->short_blocks) {
        default:
        case short_block_not_set:
            return -1;
        case short_block_dispensed:
        case short_block_allowed:
        case short_block_coupled:
            return 0;
        case short_block_forced:
            return 1;
        }
    }
    return -1;
}

int
lame_set_short_threshold_lrm(lame_global_flags * gfp, float lrm)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->attackthre = lrm;
        return 0;
    }
    return -1;
}

float
lame_get_short_threshold_lrm(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->attackthre;
    }
    return 0;
}

int
lame_set_short_threshold_s(lame_global_flags * gfp, float s)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->attackthre_s = s;
        return 0;
    }
    return -1;
}

float
lame_get_short_threshold_s(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->attackthre_s;
    }
    return 0;
}

int
lame_set_short_threshold(lame_global_flags * gfp, float lrm, float s)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_set_short_threshold_lrm(gfp, lrm);
        lame_set_short_threshold_s(gfp, s);
        return 0;
    }
    return -1;
}


/*
 * Input PCM is emphased PCM
 * (for instance from one of the rarely emphased CDs).
 *
 * It is STRONGLY not recommended to use this, because psycho does not
 * take it into account, and last but not least many decoders
 * ignore these bits
 */
int
lame_set_emphasis(lame_global_flags * gfp, int emphasis)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* XXX: emphasis should be converted to an enum */
        if (0 <= emphasis && emphasis < 4) {
            gfp->emphasis = emphasis;
            return 0;
        }
    }
    return -1;
}

int
lame_get_emphasis(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        assert(0 <= gfp->emphasis && gfp->emphasis < 4);
        return gfp->emphasis;
    }
    return 0;
}




/***************************************************************/
/* internal variables, cannot be set...                        */
/* provided because they may be of use to calling application  */
/***************************************************************/

/* MPEG version.
 *  0 = MPEG-2
 *  1 = MPEG-1
 * (2 = MPEG-2.5)    
 */
int
lame_get_version(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->cfg.version;
        }
    }
    return 0;
}


/* Encoder delay. */
int
lame_get_encoder_delay(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_enc.encoder_delay;
        }
    }
    return 0;
}

/* padding added to the end of the input */
int
lame_get_encoder_padding(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_enc.encoder_padding;
        }
    }
    return 0;
}


/* Size of MPEG frame. */
int
lame_get_framesize(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const *const cfg = &gfc->cfg;
            return 576 * cfg->mode_gr;
        }
    }
    return 0;
}


/* Number of frames encoded so far. */
int
lame_get_frameNum(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_enc.frame_number;
        }
    }
    return 0;
}

int
lame_get_mf_samples_to_encode(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->sv_enc.mf_samples_to_encode;
        }
    }
    return 0;
}

int     CDECL
lame_get_size_mp3buffer(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            int     size;
            compute_flushbits(gfc, &size);
            return size;
        }
    }
    return 0;
}

int
lame_get_RadioGain(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_rpg.RadioGain;
        }
    }
    return 0;
}

int
lame_get_AudiophileGain(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return 0;
        }
    }
    return 0;
}

float
lame_get_PeakSample(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return (float) gfc->ov_rpg.PeakSample;
        }
    }
    return 0;
}

int
lame_get_noclipGainChange(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_rpg.noclipGainChange;
        }
    }
    return 0;
}

float
lame_get_noclipScale(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return gfc->ov_rpg.noclipScale;
        }
    }
    return 0;
}


/*
 * LAME's estimate of the total number of frames to be encoded.
 * Only valid if calling program set num_samples.
 */
int
lame_get_totalframes(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            SessionConfig_t const *const cfg = &gfc->cfg;
            unsigned long const pcm_samples_per_frame = 576ul * cfg->mode_gr;
            unsigned long pcm_samples_to_encode = gfp->num_samples;
            unsigned long end_padding = 0;
            int frames = 0;

            /* compare against the documented unknown sentinel (lame.h:
               default = 2^32-1); the (0ul-1ul) test alone matches it only
               where long is 32-bit, and is kept to preserve existing LP64
               behavior */
            if (pcm_samples_to_encode == MAX_U_32_NUM
                || pcm_samples_to_encode == (0ul-1ul))
                return 0; /* unknown */

            /* estimate based on user set num_samples: */
            if (cfg->samplerate_in != cfg->samplerate_out) {
                /* resampling, estimate new samples_to_encode */
                double resampled_samples_to_encode = 0.0, frames_f = 0.0;
                if (cfg->samplerate_in > 0) {
                    resampled_samples_to_encode = pcm_samples_to_encode;
                    resampled_samples_to_encode *= cfg->samplerate_out;
                    resampled_samples_to_encode /= cfg->samplerate_in;
                }
                if (resampled_samples_to_encode <= 0.0)
                    return 0; /* unlikely to happen, so what, no estimate! */
                frames_f = floor(resampled_samples_to_encode / pcm_samples_per_frame);
                if (frames_f >= (INT_MAX-2))
                    return 0; /* overflow, happens eventually, no estimate! */
                frames = frames_f;
                resampled_samples_to_encode -= frames * pcm_samples_per_frame;
                pcm_samples_to_encode = ceil(resampled_samples_to_encode);
            }
            else {
                if (pcm_samples_to_encode / pcm_samples_per_frame >= (unsigned long)(INT_MAX-2))
                    return 0; /* overflow, happens eventually, no estimate! */
                frames = pcm_samples_to_encode / pcm_samples_per_frame;
                pcm_samples_to_encode -= frames * pcm_samples_per_frame;
            }
            pcm_samples_to_encode += 576ul;
            end_padding = pcm_samples_per_frame - (pcm_samples_to_encode % pcm_samples_per_frame);
            if (end_padding < 576ul) {
                end_padding += pcm_samples_per_frame;
            }
            pcm_samples_to_encode += end_padding;
            frames += (pcm_samples_to_encode / pcm_samples_per_frame);
            /* check to see if we underestimated totalframes */
            /*    if (totalframes < gfp->frameNum) */
            /*        totalframes = gfp->frameNum; */
            return frames;
        }
    }
    return 0;
}





int
lame_set_preset(lame_global_flags * gfp, int preset)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->preset = preset;
        return apply_preset(gfp, preset, 1);
    }
    return -1;
}



int
lame_set_asm_optimizations(lame_global_flags * gfp, int optim, int mode)
{
    if (is_lame_global_flags_valid(gfp)) {
        mode = (mode == 1 ? 1 : 0);
        switch (optim) {
        case MMX:{
                gfp->asm_optimizations.mmx = mode;
                return optim;
            }
        case AMD_3DNOW:{
                gfp->asm_optimizations.amd3dnow = mode;
                return optim;
            }
        case SSE:{
                gfp->asm_optimizations.sse = mode;
                return optim;
            }
        case AVX2:{
                gfp->asm_optimizations.avx2 = mode;
                return optim;
            }
        default:
            return optim;
        }
    }
    return -1;
}


void
lame_set_write_id3tag_automatic(lame_global_flags * gfp, int v)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->write_id3tag_automatic = v;
    }
}


int
lame_get_write_id3tag_automatic(lame_global_flags const *gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->write_id3tag_automatic;
    }
    return 1;
}


/*

UNDOCUMENTED, experimental settings.  These routines are not prototyped
in lame.h.  You should not use them, they are experimental and may
change.  

*/


/*
 *  just another daily changing developer switch  
 */
void CDECL lame_set_tune(lame_global_flags *, float);

void
lame_set_tune(lame_global_flags * gfp, float val)
{
    if (is_lame_global_flags_valid(gfp)) {
        gfp->tune_value_a = val;
        gfp->tune = 1;
    }
}

/* Custom msfix hack */
void
lame_set_msfix(lame_global_flags * gfp, double msfix)
{
    if (is_lame_global_flags_valid(gfp)) {
        /* default = 0 */
        gfp->msfix = msfix;
    }
}

float
lame_get_msfix(const lame_global_flags * gfp)
{
    if (is_lame_global_flags_valid(gfp)) {
        return gfp->msfix;
    }
    return 0;
}

#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
int CDECL lame_set_preset_expopts(lame_global_flags *, int);
#else
#endif

int
lame_set_preset_expopts(lame_global_flags * gfp, int preset_expopts)
{
    (void) gfp;
    (void) preset_expopts;
    return 0;
}


int
lame_set_preset_notune(lame_global_flags * gfp, int preset_notune)
{
    (void) gfp;
    (void) preset_notune;
    return 0;
}

static int
calc_maximum_input_samples_for_buffer_size(lame_internal_flags const* gfc, size_t buffer_size)
{
    SessionConfig_t const *const cfg = &gfc->cfg;
    int const pcm_samples_per_frame = 576 * cfg->mode_gr;
    int     frames_per_buffer = 0, input_samples_per_buffer = 0;
    int     kbps = 320;

    if (cfg->samplerate_out < 16000)
        kbps = 64;
    else if (cfg->samplerate_out < 32000)
        kbps = 160;
    else
        kbps = 320;
    if (cfg->free_format)
        kbps = cfg->avg_bitrate;
    else if (cfg->vbr == vbr_off) {
        kbps = cfg->avg_bitrate;
    }
    {
        int const pad = 1;
        int const bpf = ((cfg->version + 1) * 72000 * kbps / cfg->samplerate_out + pad);
        size_t const frames = buffer_size / (size_t) bpf;
        /* an encode call takes an int sample count, so a buffer holding more
           frames than that can express is reported at the representable
           ceiling instead of wrapping into a negative estimate */
        frames_per_buffer = frames > (size_t) INT_MAX ? INT_MAX : (int) frames;
    }
    {
        double const ratio = (double) cfg->samplerate_in / cfg->samplerate_out;
        double const samples = (double) pcm_samples_per_frame * frames_per_buffer * ratio;
        input_samples_per_buffer = samples >= (double) INT_MAX ? INT_MAX : (int) samples;
    }
    return input_samples_per_buffer;
}

int
lame_get_maximum_number_of_samples(lame_t gfp, size_t buffer_size)
{
    if (is_lame_global_flags_valid(gfp)) {
        lame_internal_flags const *const gfc = gfp->internal_flags;
        if (is_lame_internal_flags_valid(gfc)) {
            return calc_maximum_input_samples_for_buffer_size(gfc, buffer_size);
        }
    }
    return LAME_GENERICERROR;
}
