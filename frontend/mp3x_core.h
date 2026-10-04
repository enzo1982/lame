/**
 *  \file mp3x_core.h
 *  \brief mp3x analyzer core - internal interface.
 *  \internal
 *
 *  The analyzer engine. It does not depend on any GUI toolkit.
 *
 *  For each step, the engine reads one frame of input, encodes it, and decodes
 *  it again with the internal HIP/mpglib decoder of LAME. This page calls the
 *  decoding of the encoded frame re-synthesis. The engine stores the results in
 *  the plotting_data ring declared below. It contains no GTK or GDK code. So
 *  any program can use it: the GTK4 mp3x application, a tool without a GUI, or
 *  another client.
 *
 *  \see \ref mp3x_internals for how the frontend uses it. The mp3x(1) manual
 *  page describes what the graphs mean.
 */

#ifndef LAME_MP3X_CORE_H
#define LAME_MP3X_CORE_H

#include "lame.h"
#include "machine.h"            /* base types */
#include "encoder.h"            /* DECDELAY, BLKSIZE, SBMAX_l, SBMAX_s used below */
#include "lame-analysis.h"      /* struct plotting_data, READ_AHEAD, NUMPINFO */
/* Note: struct plotting_data (lame-analysis.h) references constants from
   encoder.h, so this header includes it; consumers need -Ilibmp3lame. */

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  \name Shared analyzer state
 *
 *  The engine fills a frame slot (\c pinfo). The frontend controls the
 *  navigation: it decides which decoded frame the screen shows (\c pdisp), and
 *  shifts the ring when it reads ahead or steps back.
 *  \{
 */
/** The frame slot the engine is currently filling. */
extern plotting_data *pinfo;
/** The ring slot that the decoder fills, <tt>&amp;Pinfo[READ_AHEAD]</tt>.
    The graphs read \c pdisp. */
extern plotting_data *pplot;
/** The ring buffer: \c READ_AHEAD frames ahead and \c NUMBACK frames back. */
extern plotting_data  Pinfo[NUMPINFO];
/** \} */

/**
 *  \name Scalefactor-band boundaries
 *
 *  The band boundaries for the current sample rate, in MDCT lines. They are
 *  copied from the internal tables of the encoder, so that a frontend can draw
 *  the bands over the MDCT spectrum without using library internals.
 *  mp3x_core_step() fills them when encoding has started. Before that, they are
 *  zero.
 *  \{
 */
/** The long-block band boundaries (23 entries). They are indexes into the 576
    MDCT lines. */
extern int mp3x_sfb_l[1 + SBMAX_l];
/** The short-block band boundaries (14 entries), for one window. Multiply them
    by 3 for the interleaved layout of the 576 lines of a short block. */
extern int mp3x_sfb_s[1 + SBMAX_s];
/** \} */

/**
 *  The display frame: the frame that the graphs show.
 *
 *  Every graph reads \c pdisp. It starts at <tt>&amp;Pinfo[READ_AHEAD]</tt>.
 *  This is the newest frame for which the re-synthesis and the mpg123 decode
 *  are available, \c READ_AHEAD frames behind the newest frame read. For
 *  step-back, the frontend can move \c pdisp back by up to \c NUMBACK frames.
 *  Each call to mp3x_core_step() sets it back to the newest frame that can be
 *  shown.
 */
extern plotting_data *pdisp;

/**
 *  Totals over all frames analyzed so far.
 *
 *  The engine updates them for each frame. The frontend only shows them, in the
 *  Statistics window.
 */
typedef struct {
    int     frames;      /**< Frames analyzed. */
    double  avebits;     /**< Mean main-data bits per frame, so far. */
    int     maxbits;     /**< The most main-data bits of any frame so far. */
    int     approxbits;  /**< Bits in a frame at this bitrate, without the
                              header and the side information. Assumes 1152
                              samples per frame (MPEG-1). */
    int     mean_bits;   /**< The encoder's target bits per channel and
                              granule, times 4: the frame total for MPEG-1
                              stereo. */
    int     totemph;     /**< Frames using de-emphasis. */
    int     totms;       /**< Frames using ms_stereo. */
    int     totis;       /**< Frames using intensity stereo. */
    int     totshort;    /**< Granules using short blocks. */
    int     totmix;      /**< Granules using mixed blocks. */
    int     totpreflag;  /**< Granules using preflag. */
} Mp3xStats;

/** Returns the totals, read-only. The pointer is valid until the process
    ends. */
const Mp3xStats *mp3x_core_stats(void);

/**
 *  Moves the display frame one frame back.
 *  \return 1 if it moved. 0 at the limit, \c NUMBACK frames behind the newest
 *          frame that can be shown.
 */
int mp3x_core_disp_back(void);
/**
 *  Moves the display frame one frame forward.
 *  \return 1 if it moved. 0 at the newest frame that can be shown.
 */
int mp3x_core_disp_fwd(void);
/** \return How many frames the display is behind the newest frame that can be
    shown. 0 is the newest frame. */
int mp3x_core_disp_backpos(void);

/**
 *  Resets all session state: the frame counter, the PCM buffer, the decoder
 *  delay and handle, the statistics, the scalefactor-band tables, and the
 *  plotting ring.
 *
 *  mp3x_session_open_prevalidated() calls it on every File > Open, Open Recent
 *  or replacement, and mp3x_session_close() calls it when it closes a session.
 *  For PCM input, the re-synthesis decoder is created when it is first needed.
 *  \c pplot and \c pdisp are set to the start position,
 *  <tt>&amp;Pinfo[READ_AHEAD]</tt>.
 */
void mp3x_core_init(void);

/**
 *  Reads one frame of input, encodes it, and decodes it again into \c *pinfo.
 *
 *  \param gfp  An initialized encoder instance, configured with
 *              <tt>lame_set_analysis(gfp, 1)</tt>. Before the call, the caller
 *              must set the target slot \c pinfo.
 *  \return The number of PCM samples read for this frame. 0 at the end of the
 *          input. A negative LAME error code if the encoder or the decoder
 *          cannot be initialized.
 */
int  mp3x_core_makeframe(lame_global_flags *gfp);

/**
 *  Moves the analyzer forward by one frame.
 *
 *  Shifts the ring buffer, reads, encodes and decodes the next input frame into
 *  the new slot \c pinfo, stores its time, and points \c pplot to the display
 *  frame. A frontend calls this function for each frame. All analysis stays
 *  inside the engine.
 *
 *  \param gfp  The initialized encoder instance of the analyzer.
 *  \return The number of PCM samples read. 0 at the end of the input. A
 *          negative LAME error code on an error.
 */
int  mp3x_core_step(lame_global_flags *gfp);

/**
 *  Shifts the plotting ring once, after mp3x_core_step() has reported the end
 *  of the input.
 *
 *  For PCM input, the first calls collect the decoded frames that the flush of
 *  the encoder left in the queue. When HIP is empty, and for MP3 input always,
 *  invalid placeholder frames are shifted in. A drain step does not read input,
 *  encode, or update the statistics. The frontend calls this function
 *  <tt>READ_AHEAD - 1</tt> times, to show the last frames, which come later
 *  because of the delay.
 */
void mp3x_core_drain_step(void);

/**
 *  Releases the internal decoder handle of the engine.
 *
 *  It is safe to call it more than once, for example from a window-close
 *  handler and again at shutdown.
 */
void mp3x_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* LAME_MP3X_CORE_H */
