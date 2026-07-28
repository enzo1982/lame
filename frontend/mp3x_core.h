/**
 *  \file mp3x_core.h
 *  \brief mp3x analyzer core - internal interface.
 *  \internal
 *
 *  The toolkit-independent analyzer engine, extracted from gtkanal.c.
 *
 *  The engine reads one frame of input, encodes it, and re-synthesizes it with
 *  LAME's internal HIP/mpglib decoder, leaving the results in the shared
 *  plotting_data ring declared below. It contains NO GTK/GDK/UI code, so any
 *  frontend - the GTK4 mp3x application, a headless tool, or another client -
 *  can drive the same engine.
 *
 *  This is a mechanical extraction of gtkanal.c's frame engine and shared
 *  analyzer state. The analysis algorithms are unchanged.
 *
 *  \see \ref mp3x_internals for how the frontend drives it. What the resulting
 *  graphs mean is described in the mp3x(1) manual page.
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
 *  The engine fills a frame slot (\c pinfo); the frontend owns navigation - it
 *  decides which decoded frame is currently on screen (\c pplot) and shuffles
 *  the ring as it reads ahead / steps back.
 *  \{
 */
/** The frame slot the engine is currently filling. */
extern plotting_data *pinfo;
/** The frame the frontend is currently displaying. */
extern plotting_data *pplot;
/** Ring buffer: \c READ_AHEAD frames ahead plus \c NUMBACK frames back. */
extern plotting_data  Pinfo[NUMPINFO];
/** \} */

/**
 *  \name Scalefactor-band boundaries
 *
 *  Boundaries for the current sample rate, in MDCT-line units, copied out of
 *  the encoder's internal tables so a frontend can draw the band partition over
 *  the MDCT spectrum without reaching into library internals. Filled by
 *  mp3x_core_step() once encoding has started, and zero before that.
 *  \{
 */
/** Long-block band boundaries (23 entries); indexes the 576 MDCT lines directly. */
extern int mp3x_sfb_l[1 + SBMAX_l];
/** Short-block band boundaries (14 entries), per window. Multiply by 3 for the
    interleaved 576-line short layout. */
extern int mp3x_sfb_s[1 + SBMAX_s];
/** \} */

/**
 *  The display frame: the frame the frontend is showing.
 *
 *  It starts at <tt>&amp;Pinfo[READ_AHEAD]</tt> - the newest frame whose
 *  re-synthesis / mpg123 decode is available, which is the read-ahead lag - and
 *  the frontend can walk it back up to \c NUMBACK frames through the ring for
 *  step-back. Every graph reads \c pdisp; a fresh mp3x_core_step() resets it to
 *  the newest displayable frame.
 */
extern plotting_data *pdisp;

/**
 *  Running tally over every frame analyzed so far.
 *
 *  These are gtkanal's gtkinfo counters, which its Statistics window read.
 *  They are accumulated in the engine because the engine is what sees each
 *  frame; the frontend only displays them.
 */
typedef struct {
    int     frames;      /**< Frames analyzed. */
    double  avebits;     /**< Running mean of main-data bits per frame. */
    int     maxbits;     /**< Largest main-data frame seen. */
    int     approxbits;  /**< Bits a frame holds at this bitrate, less the header. */
    int     mean_bits;   /**< LAME's own per-granule figure, x4 for the frame. */
    int     totemph;     /**< Frames using de-emphasis. */
    int     totms;       /**< Frames using ms_stereo. */
    int     totis;       /**< Frames using intensity stereo. */
    int     totshort;    /**< Granules using short blocks. */
    int     totmix;      /**< Granules using mixed blocks. */
    int     totpreflag;  /**< Granules using preflag. */
} Mp3xStats;

/** Read-only view of the tally; valid for the life of the process. */
const Mp3xStats *mp3x_core_stats(void);

/**
 *  Move the display frame one frame older.
 *  \return 1 if it moved, 0 at the limit (\c NUMBACK frames behind the newest
 *          displayable frame).
 */
int mp3x_core_disp_back(void);
/**
 *  Move the display frame one frame newer.
 *  \return 1 if it moved, 0 at the newest displayable frame.
 */
int mp3x_core_disp_fwd(void);
/** \return How far back the display currently is; 0 is the newest displayable frame. */
int mp3x_core_disp_backpos(void);

/**
 *  Reset all session state: the frame counter, PCM buffer, decoder lag and
 *  handle, statistics, scalefactor-band tables, and plotting ring.
 *
 *  Called by mp3x_session_open_prevalidated() on every File > Open, Open Recent
 *  or replacement, and by mp3x_session_close() during teardown. The
 *  re-synthesis decoder is created lazily for PCM input. Leaves \c pplot and
 *  \c pdisp at the display origin, <tt>&amp;Pinfo[READ_AHEAD]</tt>.
 */
void mp3x_core_init(void);

/**
 *  Read one frame of input, encode it, and re-synthesize it into \c *pinfo.
 *
 *  \param gfp  An initialized encoder configured with
 *              <tt>lame_set_analysis(gfp, 1)</tt>. The caller must have set the
 *              destination slot \c pinfo before calling.
 *  \return The number of PCM samples read for this frame, 0 at end of input, or
 *          a negative LAME error code if encoder or decoder initialization fails.
 */
int  mp3x_core_makeframe(lame_global_flags *gfp);

/**
 *  Advance the analyzer by one frame.
 *
 *  Shuffles the ring buffer, reads/encodes/decodes the next input frame into
 *  the new slot \c pinfo, timestamps it, and points \c pplot at the display
 *  frame. This is the frame-stepping primitive a frontend drives; all analysis
 *  stays inside the core.
 *
 *  \param gfp  The initialized analyzer encoder handle.
 *  \return The number of PCM samples read, 0 at end of input, or a negative
 *          LAME error code.
 */
int  mp3x_core_step(lame_global_flags *gfp);

/**
 *  Shift the plotting ring once after mp3x_core_step() has reported end of input.
 *
 *  For PCM input the first calls collect decoder frames already queued by the
 *  encoder flush; once HIP is empty, and for MP3 input throughout, invalid
 *  sentinels are shifted in. No drain step reads input, encodes, or updates
 *  statistics. The frontend calls this <tt>READ_AHEAD - 1</tt> times to expose
 *  the final delayed display frames.
 */
void mp3x_core_drain_step(void);

/**
 *  Release the engine's internal decoder handle.
 *
 *  Safe to call more than once - for instance from a window-close handler and
 *  again at shutdown.
 */
void mp3x_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* LAME_MP3X_CORE_H */
