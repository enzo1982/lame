/*
 *      mp3x analyzer core - internal interface
 *
 *      Toolkit-independent analyzer engine, extracted from gtkanal.c.
 *
 * The engine reads one frame of input, encodes it, and re-synthesizes it with
 * LAME's internal HIP/mpglib decoder, leaving the results in the shared
 * plotting_data ring declared below. It contains NO GTK/GDK/UI code, so any
 * frontend - the GTK4 mp3x application, a headless tool, or another client -
 * can drive the same engine.
 *
 * This is a mechanical extraction of gtkanal.c's frame engine and shared
 * analyzer state. The analysis algorithms are unchanged.
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

/*
 * Shared analyzer state.
 *
 * The engine fills a frame slot (pinfo); the frontend owns navigation - it
 * decides which decoded frame is currently on screen (pplot) and shuffles the
 * ring as it reads ahead / steps back.
 *
 *   pinfo  - the frame slot the engine is currently filling
 *   pplot  - the frame the frontend is currently displaying
 *   Pinfo  - ring buffer: READ_AHEAD frames ahead + NUMBACK frames back
 */
extern plotting_data *pinfo;
extern plotting_data *pplot;
extern plotting_data  Pinfo[NUMPINFO];

/*
 * Scalefactor-band boundaries for the current sample rate, in MDCT-line units,
 * copied out of the encoder's internal tables so a frontend can draw the band
 * partition over the MDCT spectrum without reaching into library internals.
 * Filled by mp3x_core_step once encoding has started (zero before that). The
 * long-block array indexes the 576 MDCT lines directly; the short-block array
 * is per-window - multiply by 3 for the interleaved 576-line short layout.
 */
extern int mp3x_sfb_l[1 + SBMAX_l];   /* long-block band boundaries  (23 entries) */
extern int mp3x_sfb_s[1 + SBMAX_s];   /* short-block band boundaries (14 entries) */

/*
 * The display frame: the frame the frontend is showing. It starts at
 * &Pinfo[READ_AHEAD] - the newest frame whose re-synthesis / mpg123 decode is
 * available (the read-ahead lag) - and the frontend can walk it back up to
 * NUMBACK frames through the ring for step-back. Every graph reads pdisp; a
 * fresh mp3x_core_step resets it to the newest displayable frame.
 */
extern plotting_data *pdisp;

/*
 * Running tally over every frame analyzed so far - gtkanal's gtkinfo
 * counters, which its Statistics window read. Accumulated here because the
 * engine is what sees each frame; the frontend only displays it.
 */
typedef struct {
    int     frames;      /* frames analyzed */
    double  avebits;     /* running mean of main-data bits per frame */
    int     maxbits;     /* largest main-data frame seen */
    int     approxbits;  /* bits a frame holds at this bitrate, less the header */
    int     mean_bits;   /* LAME's own per-granule figure, x4 for the frame */
    int     totemph;     /* frames using de-emphasis */
    int     totms;       /* frames using ms_stereo */
    int     totis;       /* frames using intensity stereo */
    int     totshort;    /* granules using short blocks */
    int     totmix;      /* granules using mixed blocks */
    int     totpreflag;  /* granules using preflag */
} Mp3xStats;

/* Read-only view of the tally; valid for the life of the process. */
const Mp3xStats *mp3x_core_stats(void);

/* Move the display frame one frame older / newer, within [0 .. NUMBACK] behind
   the newest displayable frame. Return 1 if it moved, 0 at the limit. */
int mp3x_core_disp_back(void);
int mp3x_core_disp_fwd(void);
/* How far back the display currently is (0 = newest displayable frame). */
int mp3x_core_disp_backpos(void);

/*
 * Reset all session state: the frame counter, PCM buffer, decoder lag and
 * handle, statistics, scalefactor-band tables, and plotting ring. Called
 * by mp3x_session_open on every File > Open / Open Recent / replacement,
 * and by mp3x_session_close during teardown. The re-synthesis decoder is
 * created lazily for PCM input. Leaves pplot and pdisp at the display
 * origin (&Pinfo[READ_AHEAD]).
 */
void mp3x_core_init(void);

/*
 * Read one frame of input, encode it, and re-synthesize it into *pinfo.
 * Returns the number of PCM samples read for this frame, 0 at end of input, or
 * a negative LAME error code if encoder or decoder initialization fails.
 *
 * gfp must be an initialized encoder configured with lame_set_analysis(gfp, 1);
 * the frontend must have set the destination slot (pinfo) before calling.
 */
int  mp3x_core_makeframe(lame_global_flags *gfp);

/*
 * Advance the analyzer by one frame: shuffle the ring buffer, read+encode+decode
 * the next input frame into the new slot (pinfo), timestamp it, and point pplot
 * at the display frame. Returns the number of PCM samples read, 0 at end of
 * input, or a negative LAME error code. This is the frame-stepping primitive a
 * frontend drives; all analysis stays inside the core.
 */
int  mp3x_core_step(lame_global_flags *gfp);

/*
 * Shift the plotting ring once after mp3x_core_step() reports end of input.
 * For PCM input, the first calls collect decoder frames already queued by the
 * encoder flush; after HIP is empty (and for MP3 input), invalid sentinels are
 * shifted in. No drain step reads input, encodes, or updates statistics. The
 * frontend calls this READ_AHEAD-1 times to expose the final delayed display
 * frames.
 */
void mp3x_core_drain_step(void);

/*
 * Release the engine's internal decoder handle. Safe to call more than once
 * (e.g. from a window-close handler and again at shutdown).
 */
void mp3x_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* LAME_MP3X_CORE_H */
