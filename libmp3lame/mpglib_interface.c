/* -*- mode: C; mode: fold -*- */
/*
 *      LAME MP3 encoding engine
 *
 *      Copyright (c) 1999-2000 Mark Taylor
 *      Copyright (c) 2003 Olcios
 *      Copyright (c) 2008 Robert Hegemann
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
  \file   mpglib_interface.c
  \brief  The decoding side of the public API.

  LAME is an encoder, but it ships a decoder as well, because encoding needs
  one: measuring what the encoded file will actually sound like means decoding
  it again. The same decoder is exposed for its own sake through the functions
  here, whose names all begin with \c hip_.

  Two properties hold throughout and are not repeated on each function:

  - the decoder is **a separate object from the encoder**. It is created by
    \c hip_decode_init(), passed as the first argument to everything here, and
    destroyed by \c hip_decode_exit(); an encoder instance is neither needed
    nor accepted.
  - **the actual decoding is done by libmpg123**, which is optional. A library
    built without it cannot decode at all, and says so where it can still be
    acted on: neither creation call hands back a decoder, so the absence is
    met before any input has been read.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define hip_global_struct mpstr_tag

#ifdef HAVE_MPG123
/* libmpg123 */
#include <mpg123.h>
#ifndef MPG123_API_VERSION
#error "Seems like you got the wrong mpg123 header. No MPG123_API_VERSION defined."
#endif
#if (MPG123_API_VERSION < 45)
#error "Need mpg123 API >= 45."
#endif

#endif /* HAVE_MPG123 */

/* for mpstr_tag */
#include "mpglib/mpglib.h"

#include "lame.h"
#include "machine.h"
#include "encoder.h"

/* for plotting_data */
#ifndef NOANALYSIS
#include "lame-analysis.h"
#endif

#include "util.h"

#if DEPRECATED_OR_OBSOLETE_CODE_REMOVED
/*
 * OBSOLETE:
 * - kept to let it link
 * - forward declaration to silence compiler
 */
int CDECL lame_decode_init(void);
int CDECL lame_decode(
        unsigned char *  mp3buf,
        int              len,
        short            pcm_l[],
        short            pcm_r[] );
int CDECL lame_decode_headers(
        unsigned char*   mp3buf,
        int              len,
        short            pcm_l[],
        short            pcm_r[],
        mp3data_struct*  mp3data );
int CDECL lame_decode1(
        unsigned char*  mp3buf,
        int             len,
        short           pcm_l[],
        short           pcm_r[] );
int CDECL lame_decode1_headers(
        unsigned char*   mp3buf,
        int              len,
        short            pcm_l[],
        short            pcm_r[],
        mp3data_struct*  mp3data );
int CDECL lame_decode1_headersB(
        unsigned char*   mp3buf,
        int              len,
        short            pcm_l[],
        short            pcm_r[],
        mp3data_struct*  mp3data,
        int              *enc_delay,
        int              *enc_padding );
int CDECL lame_decode_exit(void);
#endif

/*! Shut the old global decoder down. */
/*!
  \deprecated Obsolete and inert. The decoder used to be a single global
  object; it is now created per caller, so there is nothing to shut down. Use
  \c hip_decode_exit(). The declaration is compiled out of the installed
  header; the definition remains so that programs linked against an older
  release still resolve it.

  \return always 0.
*/
int
lame_decode_exit(void)
{
    return 0;
}


/*! Start the old global decoder. */
/*!
  \deprecated Obsolete and inert. Use \c hip_decode_init(), which returns the
  decoder instance the rest of the decoding API needs. This function reports
  success without creating anything, so a program that only calls it and then
  decodes gets failures from every later call.

  \return always 0.
*/
int
lame_decode_init(void)
{
    return 0;
}




/* copy mono samples */
#define COPY_MONO(DST_TYPE, SRC_TYPE)                                                           \
    DST_TYPE *pcm_l = (DST_TYPE *)pcm_l_raw;                                                    \
    SRC_TYPE const *p_samples = (SRC_TYPE const *)p;                                            \
    for (i = 0; i < processed_samples; i++)                                                     \
      *pcm_l++ = (DST_TYPE)(*p_samples++);

/* copy stereo samples */
#define COPY_STEREO(DST_TYPE, SRC_TYPE)                                                         \
    DST_TYPE *pcm_l = (DST_TYPE *)pcm_l_raw, *pcm_r = (DST_TYPE *)pcm_r_raw;                    \
    SRC_TYPE const *p_samples = (SRC_TYPE const *)p;                                            \
    for (i = 0; i < processed_samples; i++) {                                                   \
      *pcm_l++ = (DST_TYPE)(*p_samples++);                                                      \
      *pcm_r++ = (DST_TYPE)(*p_samples++);                                                      \
    }




#define OUTSIZE_CLIPPED   (4096*sizeof(short))

/*! Decode one frame through the old global decoder, with delay and padding. */
/*!
  \deprecated Obsolete and inert. Use \c hip_decode1_headersB(), which takes
  the decoder instance this one lacks. **It does not decode**: it returns the
  error code without looking at its arguments, so a caller that ignores the
  result reads uninitialized output buffers.

  \return always -1.
*/
int
lame_decode1_headersB(LAME_UNUSED unsigned char *buffer,
                      LAME_UNUSED int len,
                      LAME_UNUSED short pcm_l[], LAME_UNUSED short pcm_r[],
                      LAME_UNUSED mp3data_struct *mp3data,
                      LAME_UNUSED int *enc_delay, LAME_UNUSED int *enc_padding)
{
    return -1;
}





/*! Decode one frame through the old global decoder, with header data. */
/*!
  \deprecated Obsolete and inert; see \c lame_decode1_headersB(). Use
  \c hip_decode1_headers().
  \return always -1.
*/
int
lame_decode1_headers(LAME_UNUSED unsigned char *buffer,
                     LAME_UNUSED int len, LAME_UNUSED short pcm_l[],
                     LAME_UNUSED short pcm_r[], LAME_UNUSED mp3data_struct *mp3data)
{
    return -1;
}


/*! Decode one frame through the old global decoder. */
/*!
  \deprecated Obsolete and inert; see \c lame_decode1_headersB(). Use
  \c hip_decode1().
  \return always -1.
*/
int
lame_decode1(LAME_UNUSED unsigned char *buffer, LAME_UNUSED int len,
             LAME_UNUSED short pcm_l[], LAME_UNUSED short pcm_r[])
{
    return -1;
}


/*! Decode through the old global decoder, with header data. */
/*!
  \deprecated Obsolete and inert; see \c lame_decode1_headersB(). Use
  \c hip_decode_headers().
  \return always -1.
*/
int
lame_decode_headers(LAME_UNUSED unsigned char *buffer,
                    LAME_UNUSED int len, LAME_UNUSED short pcm_l[],
                    LAME_UNUSED short pcm_r[], LAME_UNUSED mp3data_struct *mp3data)
{
    return -1;
}


/*! Decode through the old global decoder. */
/*!
  \deprecated Obsolete and inert; see \c lame_decode1_headersB(). Use
  \c hip_decode().
  \return always -1.
*/
int
lame_decode(LAME_UNUSED unsigned char *buffer, LAME_UNUSED int len,
            LAME_UNUSED short pcm_l[], LAME_UNUSED short pcm_r[])
{
    return -1;
}




/*! Create a decoder. */
/*!
  The first call of the decoding API. The returned handle is passed to every
  other \c hip_ function and released with \c hip_decode_exit().

  In a library built without libmpg123 **this fails**, returning NULL rather
  than a handle that cannot decode anything. Checking the result is therefore
  enough to find out whether decoding is available, and a program that skips
  the check meets the same absence one call later instead, as an error from
  every decode.

  \since LAME 4.1. Before that this call handed back a handle even in a
         library that could not decode, so a program that has to work against
         an older libmp3lame as well cannot rely on the check alone.

  \return the decoder handle, or NULL if decoding is unavailable or the handle
          could not be allocated - two cases a caller cannot tell apart.
*/
hip_t hip_decode_init(void)
{
    hip_t hip = lame_calloc(hip_global_flags, 1);
    if(!hip)
        return hip;
#ifdef HAVE_MPG123
    mpg123_init();
    hip->mh = mpg123_new(NULL, NULL);
    /* Could allocate on demand only. */
    memset(&hip->mi, 0, sizeof(hip->mi));
    /* Since encoder delay/padding is communicated, I presume implicit
       handling of gapless decoding is not expected. */
    mpg123_param(hip->mh, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0.);
    /* We are going to feed buffers. */
    if(mpg123_open_feed(hip->mh) != MPG123_OK)
    {
        mpg123_delete(hip->mh);
        free(hip);
        hip = NULL;
    }
#endif
    return hip;
}

/*! Create a decoder that trims the encoder's padding itself. */
/*!
  As \c hip_decode_init(), but the decoder honours the gapless information in
  the file: the silence the encoder added at the start and end is dropped, so
  the samples that come out are the ones that went in. A plain decoder leaves
  that to the caller, which is why the other entry points hand back the delay
  and padding figures.

  This **returns NULL in a library built without libmpg123**, since the
  trimming is that library's, on the same terms as \c hip_decode_init().

  \return the decoder handle, or NULL if gapless decoding is unavailable or
          the handle could not be allocated - two cases a caller cannot tell
          apart.
*/
hip_t hip_decode_init_gapless(void)
{
    hip_t hip = lame_calloc(hip_global_flags, 1);
    if(!hip)
        return hip;
#ifdef HAVE_MPG123
    mpg123_init();
    hip->mh = mpg123_new(NULL, NULL);
    /* Could allocate on demand only. */
    memset(&hip->mi, 0, sizeof(hip->mi));
    /* Default on, but make it explicit. */
    mpg123_param(hip->mh, MPG123_ADD_FLAGS, MPG123_GAPLESS, 0.);
    /* We are going to feed buffers. */
    if(mpg123_open_feed(hip->mh) != MPG123_OK)
    {
        mpg123_delete(hip->mh);
        free(hip);
        hip = NULL;
    }
#else
    hip = NULL;
#endif
    return hip;
}



/*! Destroy a decoder. */
/*!
  Releases everything the handle owns. A NULL handle is accepted and ignored,
  so the failure of \c hip_decode_init() does not need a special case.

  \param hip the decoder handle, or NULL.
  \return always 0. There is no failure to report.
*/
int hip_decode_exit(hip_t hip)
{
    if(hip) {
#ifdef HAVE_MPG123
        mpg123_delete(hip->mh); /* Closes implicitly. */
        /* No mpg123_exit(), will be deprecated anyway. */
#endif
        free(hip);
    }
    return 0;
}

#ifdef HAVE_MPG123
/* One decoding routine to cover all API cases. Any output pointer except pcm_l
   and pcm_r, which are always required to be able to store full MPEG frame
   (1152 samples), can be NULL if you are not really interested in it.
   This always works on one whole MPEG frame, even if sample count can be
   smaller after gapless handling. TODO: Optionally turn on gapless decoding?
   If not, the decoder delay also needs to be communicated.
   Or do we just assume 529 samples? */

int hip123_decode1( hip_t hip, unsigned char *buffer, size_t len,
    unsigned char *pcm_l, unsigned char *pcm_r,
    int *enc_delay, int *enc_padding,
    mp3data_struct *mp3data,
    int unclipped) /* If true, produce unclipped float (sample_t) output. */
{
    int ret;
    unsigned char *mpg123buf;
    size_t mpg123fill;
    long rate;
    int channels;
    int encoding;
    int change_format;
    int samples = 0;
    int want_enc = unclipped ? MPG123_ENC_FLOAT_32 : MPG123_ENC_SIGNED_16;

    if(MPG123_OK != mpg123_feed(hip->mh, buffer, len))
        return -1;
    ret = mpg123_getformat(hip->mh, &rate, &channels, &encoding);
    switch(ret) {
        case MPG123_NEED_MORE:
            return 0;
        case MPG123_OK:
            change_format = encoding != want_enc;
        break;
        default:
            return -1;
    }

    if(change_format)
    {
        mpg123_format_none(hip->mh);
        mpg123_format2(hip->mh, 0, MPG123_MONO|MPG123_STEREO, want_enc);
        /* This triggers renegotiation of output format on next decode. */
        mpg123_decoder(hip->mh, NULL);
    }

    /* Now decode for real. */
    mpg123fill = 0; /* Still zero in case of error/need more. */
    ret = mpg123_decode_frame(hip->mh, NULL, &mpg123buf, &mpg123fill);
    /* A second time if we just got notified of new format. */
    if(!mpg123fill && ret == MPG123_NEW_FORMAT)
    {
        mpg123_getformat(hip->mh, &rate, &channels, &encoding);
        ret = mpg123_decode_frame(hip->mh, NULL, &mpg123buf, &mpg123fill);
        /* True paranoia would check the encoding again. */
    }
    if(ret == MPG123_ERR)
        return -1;
    /* MPG123_NEED_MORE and MPG123_DONE (not happening here, though)
        both result in mpg123fill==0, so return 0 here, which is what fits. */
    {
        size_t const bytes_per_sample =
            (unclipped ? sizeof(float) : sizeof(short)) * (size_t) channels;
        size_t const decoded = mpg123fill / bytes_per_sample;
        /* the count is returned in an int and indexes the caller's buffers, so
           a frame yielding more than that can express is rejected rather than
           demultiplexed against a wrapped length */
        if (decoded > (size_t) INT_MAX)
            return -1;
        samples = (int) decoded;
    }
    /* Now demultilex the data in mpg123buf into pcm_l and pcm_r. */
    if(mpg123fill && mpg123buf)
    {
        if(unclipped)
        {
            /* Lame's sample_t could be wider than 32 bit, right? */
            sample_t *spcm_l = (sample_t*)pcm_l;
            sample_t *spcm_r = (sample_t*)pcm_r;
            float    *srcbuf = (float*)mpg123buf;
            int i;

            if(channels == 2) {
                for(i=0; i<samples; ++i) {
                    spcm_l[i] = *srcbuf++;
                    spcm_r[i] = *srcbuf++;
                }
            }
            else
                for(i=0; i<samples; ++i)
                    spcm_l[i] = *srcbuf++;
        }
        else
        {
            /* It's all shorts. */
            short *spcm_l = (short*)pcm_l;
            short *spcm_r = (short*)pcm_r;
            short *srcbuf = (short*)mpg123buf;
            int i;

            if(channels == 2) {
                for(i=0; i<samples; ++i) {
                    spcm_l[i] = *srcbuf++;
                    spcm_r[i] = *srcbuf++;
                }
            }
            else
                memcpy(pcm_l, mpg123buf, sizeof(short)*samples);
        }
    }

    /* If we arrive here, there was some successful parsing of the stream at
       least, so that meaningful info is available. */
    if(mp3data) {
        struct mpg123_frameinfo fi;
        memset(mp3data, 0, sizeof(mp3data_struct));
        /* Re-using last returns from getformat() before. */
        if(MPG123_OK == mpg123_info(hip->mh, &fi)) {
            mp3data->header_parsed = 1;
            mp3data->stereo = channels; /* Channel count correct? Or is dual mono different? */
            mp3data->samplerate = rate;
            mp3data->mode = fi.mode;
            mp3data->mode_ext = fi.mode_ext;
            mp3data->framesize = mpg123_spf(hip->mh);
            mp3data->bitrate = fi.bitrate;
        }
    }
    if(enc_delay) {
        long val;
        mpg123_getstate(hip->mh, MPG123_ENC_DELAY, &val, NULL);
        *enc_delay = val > INT_MAX ? -1 : val;
    }
    if(enc_padding) {
        long val;
        mpg123_getstate(hip->mh, MPG123_ENC_PADDING, &val, NULL);
        *enc_padding = val > INT_MAX ? -1 : val;
    }
    if(hip->pinfo)
        hip_finish_pinfo(hip);
    return samples;
}
#endif


/* we forbid input with more than 1152 samples per channel for output in the unclipped mode */
#define OUTSIZE_UNCLIPPED (1152*2*sizeof(FLOAT))

int
hip_decode1_unclipped(hip_t hip, LAME_UNUSED unsigned char *buffer, LAME_UNUSED size_t len,
                      LAME_UNUSED sample_t pcm_l[], LAME_UNUSED sample_t pcm_r[])
{
    if (hip) {
#ifdef HAVE_MPG123
        return hip123_decode1( hip, buffer, len,
            (unsigned char*)pcm_l, (unsigned char*)pcm_r,
            NULL, NULL, NULL, 1 );
#endif
    }
    return 0; /* not -1 ? */
}

/*! Decode at most one frame, and report what the frame header said. */
/*!
  The same as \c hip_decode1(), and additionally fills in \a mp3data from the
  frame header - sample rate, channel count, bitrate, frame size - which is
  how a caller learns the format of a stream it did not encode itself. The
  fields are only meaningful once \c header_parsed is set.

  \param hip      the decoder handle.
  \param buffer   the encoded bytes to feed in.
  \param len      how many bytes \a buffer holds; 0 to drain what the decoder
                  already has.
  \param pcm_l    receives the left channel; room for a full frame, 1152
                  samples, is required whatever the return value turns out
                  to be.
  \param pcm_r    receives the right channel, on the same terms.
  \param mp3data  receives the frame description. Cleared on every call that
                  gets far enough to parse a header.
  \return the number of samples per channel written, 0 if more input is
          needed first, or -1 on an error - including a NULL \a hip.
*/
int
hip_decode1_headers(hip_t hip, unsigned char *buffer,
                     size_t len, short pcm_l[], short pcm_r[], mp3data_struct * mp3data)
{
#ifdef HAVE_MPG123
    return hip123_decode1( hip, buffer, len,
        (unsigned char*)pcm_l, (unsigned char*)pcm_r,
        NULL, NULL, mp3data, 0 );
#else
    int     enc_delay, enc_padding;
    return hip_decode1_headersB(hip, buffer, len, pcm_l, pcm_r, mp3data, &enc_delay, &enc_padding);
#endif
}


/*! Decode at most one frame. */
/*!
  Feeds \a len bytes to the decoder and returns whatever samples that produced
  - **at most one frame's worth**, so a caller with a large buffer keeps
  calling with a length of 0 until the result is 0, or uses \c hip_decode(),
  which does that loop itself.

  A return of 0 is the normal state of affairs at the start of a stream: MP3
  frames depend on the ones before them, so the first call or two produce
  nothing.

  \param hip     the decoder handle.
  \param buffer  the encoded bytes to feed in.
  \param len     how many bytes \a buffer holds; 0 to drain what the decoder
                 already has.
  \param pcm_l   receives the left channel; room for a full frame, 1152
                 samples, is required.
  \param pcm_r   receives the right channel, on the same terms. Written only
                 for a stereo stream.
  \return the number of samples per channel written, 0 if more input is
          needed first, or -1 on an error.
*/
int
hip_decode1(hip_t hip, unsigned char *buffer, size_t len, short pcm_l[], short pcm_r[])
{
#ifdef HAVE_MPG123
    return hip123_decode1( hip, buffer, len,
        (unsigned char*)pcm_l, (unsigned char*)pcm_r,
        NULL, NULL, NULL, 0 );
#else
    mp3data_struct mp3data;
    return hip_decode1_headers(hip, buffer, len, pcm_l, pcm_r, &mp3data);
#endif
}


/*! Decode everything the input yields, and report the frame header. */
/*!
  \c hip_decode() with the frame description filled in as well; see
  \c hip_decode1_headers() for what \a mp3data holds. Since this decodes
  several frames, \a mp3data describes the **last** one - which matters for a
  stream whose frames are not all alike.

  \param hip      the decoder handle.
  \param buffer   the encoded bytes to feed in.
  \param len      how many bytes \a buffer holds.
  \param pcm_l    receives the left channel.
  \param pcm_r    receives the right channel.
  \param mp3data  receives the description of the last frame decoded.
  \return the total number of samples per channel written, 0 if more input is
          needed, or -1 on an error.
*/
int
hip_decode_headers(hip_t hip, unsigned char *buffer,
                    size_t len, short pcm_l[], short pcm_r[], mp3data_struct * mp3data)
{
    int     ret;
    int     totsize = 0;     /* number of decoded samples per channel */

    for (;;) {
        switch (ret = hip_decode1_headers(hip, buffer, len, pcm_l + totsize, pcm_r + totsize, mp3data)) {
        case -1:
            return ret;
        case 0:
            return totsize;
        default:
            totsize += ret;
            len = 0;    /* future calls to decodeMP3 are just to flush buffers */
            break;
        }
    }
}


/*! Decode everything the input yields. */
/*!
  Repeats \c hip_decode1() until the decoder has nothing more to give, so one
  call turns a buffer of MP3 data into all the samples it contains.

  The convenience has a price the caller must respect: **the output buffers
  have to be large enough for every frame in the input**, not for one frame.
  There is no bound the function itself can enforce, and nothing warns. Where
  the input size is not under the caller's control, \c hip_decode1() and its
  one-frame-at-a-time contract is the safe choice.

  \param hip     the decoder handle.
  \param buffer  the encoded bytes to feed in.
  \param len     how many bytes \a buffer holds.
  \param pcm_l   receives the left channel; sized by the caller for all the
                 frames in \a buffer.
  \param pcm_r   receives the right channel, on the same terms.
  \return the total number of samples per channel written, 0 if more input is
          needed, or -1 on an error.
*/
int
hip_decode(hip_t hip, unsigned char *buffer, size_t len, short pcm_l[], short pcm_r[])
{
    mp3data_struct mp3data;
    return hip_decode_headers(hip, buffer, len, pcm_l, pcm_r, &mp3data);
}


/*! Decode at most one frame, and report the encoder's delay and padding. */
/*!
  \c hip_decode1_headers() plus the two figures needed to undo what the
  encoder added: the samples inserted before the audio and the ones appended
  after it. Discarding those from the decoded stream restores the original
  length, which is what gapless playback of a sequence of files requires.

  \c hip_decode_init_gapless() does this trimming inside the decoder instead,
  and is the simpler choice unless the caller has a reason to see the numbers.

  \param hip          the decoder handle.
  \param buffer       the encoded bytes to feed in.
  \param len          how many bytes \a buffer holds.
  \param pcm_l        receives the left channel; room for a full frame is
                      required.
  \param pcm_r        receives the right channel, on the same terms.
  \param mp3data      receives the frame description.
  \param enc_delay    receives the encoder delay in samples, or -1 if the
                      figure does not fit an \c int.
  \param enc_padding  receives the trailing padding in samples, on the same
                      terms.
  \return the number of samples per channel written, 0 if more input is
          needed first, or -1 on an error - including a NULL \a hip.
*/
int
hip_decode1_headersB(hip_t hip, LAME_UNUSED unsigned char *buffer,
                      LAME_UNUSED size_t len,
                      LAME_UNUSED short pcm_l[], LAME_UNUSED short pcm_r[],
                      LAME_UNUSED mp3data_struct * mp3data,
                      LAME_UNUSED int *enc_delay, LAME_UNUSED int *enc_padding)
{
    if (hip) {
#ifdef HAVE_MPG123
        return hip123_decode1( hip, buffer, len,
            (unsigned char*)pcm_l, (unsigned char*)pcm_r,
            enc_delay, enc_padding, mp3data, 0);
#endif
    }
    return -1;
}


void hip_set_pinfo(hip_t hip, plotting_data* pinfo)
{
    if (hip) {
        hip->pinfo = pinfo;
#ifdef HAVE_MPG123
        mpg123_set_moreinfo(hip->mh, &hip->mi);
#endif
    }
}

void hip_finish_pinfo(LAME_UNUSED hip_t hip)
{
#ifndef NOANALYSIS
#ifdef HAVE_MPG123
    struct mpg123_frameinfo fi;
    plotting_data *pinfo = hip->pinfo;
    if(!hip || !hip->pinfo)
        return;

    /* TODO: convert to pointers to avoid copies. Allocation should be
       on mpg123 side (in form of the struct definition), as that is
       the writing side. */
    memcpy(pinfo->mpg123xr, hip->mi.xr, sizeof(pinfo->mpg123xr));
    memcpy(pinfo->sfb, hip->mi.sfb, sizeof(pinfo->sfb));
    memcpy(pinfo->sfb_s, hip->mi.sfb_s, sizeof(pinfo->sfb_s));
    memcpy(pinfo->qss, hip->mi.qss, sizeof(pinfo->qss));
    memcpy(pinfo->big_values, hip->mi.big_values, sizeof(pinfo->big_values));
    memcpy(pinfo->sub_gain, hip->mi.sub_gain, sizeof(pinfo->sub_gain));
    memcpy(pinfo->scalefac_scale, hip->mi.scalefac_scale, sizeof(pinfo->scalefac_scale));
    memcpy(pinfo->preflag, hip->mi.preflag, sizeof(pinfo->preflag));
    memcpy(pinfo->mpg123blocktype, hip->mi.blocktype, sizeof(pinfo->mpg123blocktype));
    memcpy(pinfo->mixed, hip->mi.mixed, sizeof(pinfo->mixed));
    memcpy(pinfo->mainbits, hip->mi.mainbits, sizeof(pinfo->mainbits));
    memcpy(pinfo->sfbits, hip->mi.sfbits, sizeof(pinfo->sfbits));
    memcpy(pinfo->scfsi, hip->mi.scfsi, sizeof(pinfo->scfsi));
    pinfo->maindata = hip->mi.maindata;
    pinfo->padding  = hip->mi.padding;
    if(MPG123_OK == mpg123_info(hip->mh, &fi)) {
        pinfo->js = (fi.mode == MPG123_M_JOINT);
        pinfo->stereo = fi.mode == MPG123_M_MONO ? 1 : 2;
        pinfo->crc = fi.flags & MPG123_CRC ? 1 : 0;
        pinfo->emph = fi.emphasis;
        pinfo->sampfreq = fi.rate;
        pinfo->bitrate = fi.bitrate;
        pinfo->ms_stereo = pinfo->js ? (fi.mode_ext & 0x2)>>1 : 0;
        pinfo->i_stereo  = pinfo->js ? (fi.mode_ext & 0x1)    : 0;
    }
#endif
#endif
}

/*! Route the decoder's error messages. */
/*!
  The decoder's counterpart of \c lame_set_errorf(), and **it does nothing**:
  the callback is accepted and discarded. The decoding is libmpg123's, and its
  diagnostics are not forwarded, so a caller who wants to know why a decode
  failed has only the -1.

  Kept because the encoder's reporting functions have decoder-side names to
  match, and removing it would break programs that set all six.

  \param hip   ignored.
  \param func  ignored.
*/
void hip_set_errorf(LAME_UNUSED hip_t hip, LAME_UNUSED lame_report_function func)
{
#ifdef HAVE_MPG123
    /* TODO: implement something */
#endif
}

/*! Route the decoder's debug messages. */
/*!
  Does nothing; see \c hip_set_errorf().
  \param hip   ignored.
  \param func  ignored.
*/
void hip_set_debugf(LAME_UNUSED hip_t hip, LAME_UNUSED lame_report_function func)
{
#ifdef HAVE_MPG123
    /* TODO: implement something */
#endif
}

/*! Route the decoder's informational messages. */
/*!
  Does nothing; see \c hip_set_errorf().
  \param hip   ignored.
  \param func  ignored.
*/
void hip_set_msgf  (LAME_UNUSED hip_t hip, LAME_UNUSED lame_report_function func)
{
#ifdef HAVE_MPG123
    /* TODO: implement something */
#endif
}

/* end of mpglib_interface.c */
