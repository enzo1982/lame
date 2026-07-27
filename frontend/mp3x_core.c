/*
 *      mp3x analyzer core - toolkit-independent analyzer engine
 *
 *      Copyright (c) 1999 Mark Taylor  (original gtkanal.c engine)
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

/*
 * This module is the analyzer engine, mechanically extracted from gtkanal.c so
 * that the encode/decode analysis work is separated from any particular UI
 * toolkit. It reads one frame of input, encodes it, and re-synthesizes it with
 * LAME's internal HIP/mpglib decoder, filling the shared plotting_data ring.
 *
 * Nothing here includes GTK/GDK; that is the whole point. The frame algorithm
 * is unchanged from the original gtkmakeframe(): the only differences are that
 * the encoder handle (gfp) is now a parameter rather than a file-global, and
 * the internal-flags pointer (gfc) is derived locally from it.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "main.h"               /* global_reader, ReaderConfig */
#include "lame.h"
#include "machine.h"
#include "encoder.h"            /* DECDELAY */
#include "lame-analysis.h"      /* plotting_data, READ_AHEAD, MAXMPGLAG, NUMPINFO */
#include "get_audio.h"          /* get_audio16, get_hip, is_mpeg_file_format */
#include "lame_global_flags.h"  /* lame_global_flags::internal_flags */
#include "util.h"               /* lame_internal_flags, hip_set_pinfo */
#include "console.h"            /* error_printf, error_flush */
#include "mp3x_core.h"


/* Shared analyzer state (see mp3x_core.h for ownership rules). */
plotting_data *pinfo;
plotting_data *pplot;
plotting_data  Pinfo[NUMPINFO];

/* Scalefactor-band boundaries, copied out of gfc for the frontend (see header). */
int mp3x_sfb_l[1 + SBMAX_l];
int mp3x_sfb_s[1 + SBMAX_s];

/* Display frame and how far back it sits from the newest displayable frame. */
plotting_data *pdisp;
static int disp_back = 0;

/*
 * Persistent state used while stepping through one input. Keep it together so
 * mp3x_core_init() can establish a genuinely fresh analyzer session.
 */
typedef struct {
    hip_t       hip;              /* decoder for the just-encoded data */
    int         mpglag;
    short int   buffer[2][1152];
    int         frame_num;
    int         decoder_flushing; /* encoder flush was fed; HIP may have output queued */
    int         drain_frame_num;  /* synthetic slot number used only for decode mapping */
    int         drain_framesize;
    int         drain_stereo;
    int         drain_sampfreq;
} Mp3xCoreState;

static Mp3xCoreState core_state;


static Mp3xStats stats;


/*
 * Store one decoded frame in the current slot while carrying forward the tail
 * that makes the encoder+decoder delay exactly one frame. The current pinfo
 * still contains the previous newest slot when called from the normal step;
 * the EOF decoder drain prepares the same carry before calling this helper.
 */
static void
store_decoder_pcm(short int pcm[2][1152], int sample_count)
{
    int ch, j;

    for (ch = 0; ch < pinfo->stereo; ch++) {
        for (j = 0; j < pinfo->framesize - DECDELAY; j++)
            pinfo->pcmdata2[ch][j] =
                pinfo->pcmdata2[ch][j + pinfo->framesize];
        for (j = 0; j < pinfo->framesize; j++) {
            pinfo->pcmdata2[ch][j + pinfo->framesize - DECDELAY] =
                (sample_count == -1) ? 0 : pcm[ch][j];
        }
    }
}

const Mp3xStats *
mp3x_core_stats(void)
{
    return &stats;
}

/*
 * Fold one freshly analyzed frame into the tally, exactly as gtkanal's
 * analyze loop did (gtkanal.c:925-944). totbits is the frame's main data,
 * summed over both granules and channels; the average deliberately starts
 * at the second frame, as it did there.
 */
static void
accumulate_stats(plotting_data *p)
{
    int gr, ch, headbits;

    p->totbits = 0;
    for (gr = 0; gr < 2; gr++)
        for (ch = 0; ch < 2; ch++) {
            stats.totshort += (p->mpg123blocktype[gr][ch] == 2);
            stats.totmix += !(p->mixed[gr][ch] == 0);
            stats.totpreflag += (p->preflag[gr][ch] == 1);
            p->totbits += p->mainbits[gr][ch];
        }

    if (p->frameNum > 0)
        stats.avebits = (stats.avebits * (p->frameNum - 1) + p->totbits)
                        / p->frameNum;
    if (p->totbits > stats.maxbits)
        stats.maxbits = p->totbits;

    stats.totemph += !(p->emph == 0);
    stats.totms += !(p->ms_stereo == 0);
    stats.totis += !(p->i_stereo == 0);

    headbits = 32 + ((p->stereo == 2) ? 256 : 136);
    if (p->sampfreq)
        stats.approxbits = (int) (p->bitrate * 1000 * 1152.0 / p->sampfreq)
                           - headbits;
    stats.mean_bits = 4 * p->mean_bits;
    stats.frames = p->frameNum + 1;
}

void
mp3x_core_init(void)
{
    int i;

    if (core_state.hip)
        hip_decode_exit(core_state.hip);

    memset((char *) &core_state, 0, sizeof core_state);
    memset((char *) &stats, 0, sizeof stats);
    memset((char *) Pinfo, 0, sizeof(Pinfo));
    memset((char *) mp3x_sfb_l, 0, sizeof mp3x_sfb_l);
    memset((char *) mp3x_sfb_s, 0, sizeof mp3x_sfb_s);
    for (i = 0; i < NUMPINFO; i++) {
        Pinfo[i].frameNum = -1;
        Pinfo[i].frameNum123 = -1;
    }
    pplot = &Pinfo[READ_AHEAD];
    pdisp = &Pinfo[READ_AHEAD];
    disp_back = 0;
    pinfo = 0;
}


void
mp3x_core_shutdown(void)
{
    if (core_state.hip) {
        hip_decode_exit(core_state.hip);
        core_state.hip = 0;
    }
}


/**********************************************************************
 * read one frame and encode it
 *
 * Verbatim from gtkanal.c:gtkmakeframe(). The encoder handle is now the
 * parameter gfp (previously a file-global); gfc is derived from it here,
 * exactly as the frontend used to derive it once in gtkcontrol().
 **********************************************************************/
int
mp3x_core_makeframe(lame_global_flags *gfp)
{
    lame_internal_flags *const gfc = gfp->internal_flags;
    int     iread = 0;
    short int mpg123pcm[2][1152];
    int     ch, j;
    int     mp3count = 0;
    int     mp3out = 0;
    int     channels_out;
    unsigned char mp3buffer[LAME_MAXMP3BUFFER];
    int     framesize = lame_get_framesize(gfp);

    channels_out = (lame_get_mode(gfp) == MONO) ? 1 : 2;

    pinfo->frameNum = core_state.frame_num;
    pinfo->sampfreq = lame_get_out_samplerate(gfp);
    pinfo->framesize = framesize;
    pinfo->stereo = channels_out;

    /* If the analysis code is enabled, LAME writes data into gfc->pinfo,
     * and mpg123 will write data into pinfo.  Set these so
     * the libraries put this data in the right place: */
    gfc->pinfo = pinfo;
    if (is_mpeg_file_format(global_reader.input_format)) {
        hip_set_pinfo(get_hip(), pplot);
        iread = get_audio16(gfp, core_state.buffer);


        /* add a delay of framesize-DECDELAY, which will make the total delay
         * exactly one frame, so we can sync MP3 output with WAV input */
        for (ch = 0; ch < channels_out; ch++) {
            for (j = 0; j < framesize - DECDELAY; j++)
                pinfo->pcmdata2[ch][j] = pinfo->pcmdata2[ch][j + framesize];
            for (j = 0; j < framesize; j++) /*rescale from int to short int */
                pinfo->pcmdata2[ch][j + framesize - DECDELAY] = core_state.buffer[ch][j];
        }

        pinfo->frameNum123 = core_state.frame_num - 1;
        ++core_state.frame_num;

    }
    else {

        if (core_state.hip == 0) {
            core_state.hip = hip_decode_init();
            if (core_state.hip == 0) {
                error_printf("mp3x: unable to initialize the re-synthesis decoder\n");
                error_flush();
                return LAME_NOMEM;
            }
            core_state.mpglag = 1;
        }
        hip_set_pinfo(core_state.hip, pinfo);

        /* feed data to encoder until encoder produces some output */
        while (lame_get_frameNum(gfp) == pinfo->frameNum) {
            iread = get_audio16(gfp, core_state.buffer);
            if (iread > framesize) {
                /* NOTE: frame analyzer requires that we encode one frame
                 * for each pass through this loop.  If lame_encode_buffer()
                 * is fed data too quickly, it will sometimes encode multiple frames,
                 * breaking this loop.
                 */
                error_printf("Warning: get_audio is returning too much data.\n");
            }
            if (iread <= 0)
                break;  /* eof */

            mp3count = lame_encode_buffer(gfp, core_state.buffer[0],
                                          core_state.buffer[1], iread,
                                          mp3buffer, sizeof(mp3buffer));
            if (mp3count < 0) {
                error_printf("mp3x: lame_encode_buffer failed with error %d\n",
                             mp3count);
                error_flush();
                return mp3count;
            }

            assert(!(mp3count > 0 && lame_get_frameNum(gfp) == pinfo->frameNum));
            /* not possible to produce mp3 data without encoding at least
             * one frame of data which would increment frameNum */
        }

        if (iread <= 0) {
            /*
             * LAME's POSTDELAY padding makes the last real frame decodable.
             * Feed the complete encoder flush to HIP now; hip_decode1 returns
             * at most one decoded frame, and mp3x_core_drain_step() collects
             * any remaining queued frames without reading input or encoding.
             */
            mp3count = lame_encode_flush(gfp, mp3buffer, sizeof(mp3buffer));
            if (mp3count < 0) {
                error_printf("mp3x: lame_encode_flush failed with error %d\n",
                             mp3count);
                error_flush();
                return mp3count;
            }
            core_state.decoder_flushing = 1;
            core_state.drain_frame_num = pinfo->frameNum + 1;
            core_state.drain_framesize = pinfo->framesize;
            core_state.drain_stereo = pinfo->stereo;
            core_state.drain_sampfreq = pinfo->sampfreq;
        }
        core_state.frame_num = lame_get_frameNum(gfp); /* use LAME's frame counter */


        /* decode one frame of output */
        mp3out = hip_decode1(core_state.hip, mp3buffer, (size_t) mp3count,
                             mpg123pcm[0], mpg123pcm[1]); /* re-synthesis to pcm */
        /* mp3out = 0:  need more data to decode */
        /* mp3out = -1:  error.  Assume 0 PCM output. */
        /* mp3out = number of samples output */
        if (mp3out > 0)
            assert(mp3out == pinfo->framesize);
        if (mp3out != 0) {
            /* decoded output is for frame pinfo->frameNum123
             * add a delay of framesize-DECDELAY, which will make the total delay
             * exactly one frame */
            pinfo->frameNum123 = pinfo->frameNum - core_state.mpglag;
            store_decoder_pcm(mpg123pcm, mp3out);
        }
        else {
            if (core_state.mpglag == MAXMPGLAG) {
                error_printf("READ_AHEAD set too low - not enough frame buffering.\n"
                             "MP3x display of input and output PCM data out of sync.\n");
                error_flush();
            }
            else
                core_state.mpglag++;
            pinfo->frameNum123 = -1; /* no frame output */
        }
    }
    return iread;
}


/**********************************************************************
 * advance one frame
 *
 * The ring-buffer stepping the GTK1 frontend used to inline in frameadv1():
 * shift the ring back one slot, read+encode+decode the next frame into the new
 * slot (pinfo), timestamp it, and point pplot at the display frame. A frontend
 * calls this to drive the analyzer; all analysis stays here.
 **********************************************************************/
static void
shift_ring(void)
{
    int     i;

    for (i = NUMPINFO - 1; i > 0; i--)
        memcpy(&Pinfo[i], &Pinfo[i - 1], sizeof(plotting_data));
    pinfo = &Pinfo[0];
}

static void
select_newest_display_frame(void)
{
    pplot = &Pinfo[READ_AHEAD];
    disp_back = 0;
    pdisp = &Pinfo[READ_AHEAD];
}

int
mp3x_core_step(lame_global_flags *gfp)
{
    /* Shift the ring back one slot; the freshly read frame lands in Pinfo[0]. */
    shift_ring();

    pinfo->num_samples = mp3x_core_makeframe(gfp);

    /* refresh the scalefactor-band boundaries for the frontend; they are
       constant per sample rate, so copying each step is cheap and keeps the
       exported arrays valid without reaching into gfc from the UI layer */
    memcpy(mp3x_sfb_l, gfp->internal_flags->scalefac_band.l, sizeof mp3x_sfb_l);
    memcpy(mp3x_sfb_s, gfp->internal_flags->scalefac_band.s, sizeof mp3x_sfb_s);

    if (pinfo->sampfreq)
        pinfo->frametime = pinfo->frameNum * 1152.0 / pinfo->sampfreq;
    else
        pinfo->frametime = 0;

    if (pinfo->num_samples > 0)
        accumulate_stats(pinfo);

    select_newest_display_frame();
    return pinfo->num_samples;
}

/*
 * Once mp3x_core_step() has observed PCM EOF it has fed LAME's complete encoder
 * flush to HIP. hip_decode1 returns at most one frame per call, so the first
 * drain steps collect any decoded frames already queued by that flush. These
 * synthetic slots carry only decoder companions: they never read input,
 * encode, or update statistics. Once HIP needs more data, shift invalid
 * sentinels while the final real frames move to pplot.
 */
void
mp3x_core_drain_step(void)
{
    double carry[2][1152 - DECDELAY];
    short int mpg123pcm[2][1152];
    unsigned char no_input = 0;
    int ch, j, mp3out;

    shift_ring();

    if (core_state.decoder_flushing && core_state.hip) {
        /*
         * Pinfo[0] still contains the previous newest slot after shift_ring().
         * Preserve the portion store_decoder_pcm() carries across a frame,
         * then clear every unrelated analyzer field from this synthetic slot.
         */
        for (ch = 0; ch < core_state.drain_stereo; ch++)
            for (j = 0; j < core_state.drain_framesize - DECDELAY; j++)
                carry[ch][j] =
                    pinfo->pcmdata2[ch][j + core_state.drain_framesize];

        memset(pinfo, 0, sizeof(*pinfo));
        pinfo->frameNum = core_state.drain_frame_num;
        pinfo->frameNum123 = -1;
        pinfo->framesize = core_state.drain_framesize;
        pinfo->stereo = core_state.drain_stereo;
        pinfo->sampfreq = core_state.drain_sampfreq;
        if (pinfo->sampfreq)
            pinfo->frametime =
                pinfo->frameNum * 1152.0 / pinfo->sampfreq;

        for (ch = 0; ch < core_state.drain_stereo; ch++)
            for (j = 0; j < core_state.drain_framesize - DECDELAY; j++)
                pinfo->pcmdata2[ch][j] = carry[ch][j];

        hip_set_pinfo(core_state.hip, pinfo);
        mp3out = hip_decode1(core_state.hip, &no_input, 0,
                             mpg123pcm[0], mpg123pcm[1]);
        if (mp3out > 0)
            assert(mp3out == pinfo->framesize);
        if (mp3out != 0) {
            pinfo->frameNum123 =
                pinfo->frameNum - core_state.mpglag;
            store_decoder_pcm(mpg123pcm, mp3out);
            core_state.drain_frame_num++;
        }
        else {
            core_state.decoder_flushing = 0;
            memset(pinfo, 0, sizeof(*pinfo));
            pinfo->frameNum = -1;
            pinfo->frameNum123 = -1;
        }
    }
    else {
        memset(pinfo, 0, sizeof(*pinfo));
        pinfo->frameNum = -1;
        pinfo->frameNum123 = -1;
    }
    select_newest_display_frame();
}


/**********************************************************************
 * display-frame navigation
 *
 * The display frame walks the ring between the newest displayable frame
 * (&Pinfo[READ_AHEAD]) and NUMBACK frames older. Back moves toward history,
 * forward toward the newest; each returns 1 if it actually moved.
 **********************************************************************/
int
mp3x_core_disp_back(void)
{
    if (disp_back >= NUMBACK)
        return 0;
    if (Pinfo[READ_AHEAD + disp_back + 1].frameNum < 0)
        return 0;
    disp_back++;
    pdisp = &Pinfo[READ_AHEAD + disp_back];
    return 1;
}

int
mp3x_core_disp_fwd(void)
{
    if (disp_back <= 0)
        return 0;
    disp_back--;
    pdisp = &Pinfo[READ_AHEAD + disp_back];
    return 1;
}

int
mp3x_core_disp_backpos(void)
{
    return disp_back;
}
