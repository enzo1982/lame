/*
 *      mp3x plotting layer - internal interface
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
 * The GTK4/Cairo plotting layer for the analyzer - the successor to the GTK1
 * gpkplotting.c. It has two parts:
 *
 *   1. Mp3xCanvas - reusable drawing primitives (background, baseline, line
 *      series, bars, title) that map analyzer data onto a Cairo surface. Every
 *      graph is built from these; they contain no analyzer logic.
 *   2. The analyzer graphs themselves (PCM waveform, re-synthesis overlay),
 *      each a thin GtkDrawingArea that reads plotting_data and draws with the
 *      canvas.
 */

#ifndef LAME_MP3X_PLOT_H
#define LAME_MP3X_PLOT_H

#include <gtk/gtk.h>

/* --------------------------------------------------------------------------
 * Mp3xCanvas: shared plotting primitives.
 *
 * The x axis is always the sample/band index [0..n-1] spread across the full
 * width; the y axis maps the data range [ymn..ymx] (ymx at the top). Callers
 * stack-allocate a canvas per draw and drive it with the primitives below.
 * -------------------------------------------------------------------------- */
typedef struct {
    cairo_t *cr;
    GtkWidget *widget;             /* borrowed for the duration of one draw */
    int      width;
    int      height;
    int      plot_top;
    int      plot_height;
    double   ymn;
    double   ymx;
} Mp3xCanvas;

/* Begin a graph: bind cr + size, set the y range, and paint the background. */
void mp3x_canvas_begin(Mp3xCanvas *c, GtkWidget *widget, cairo_t *cr,
                        int width, int height, double ymn, double ymx);
/* Set the drawing color (RGB components 0..1). */
void mp3x_canvas_color(Mp3xCanvas *c, double r, double g, double b);
/* Draw the gray baseline at data y = 0. */
void mp3x_canvas_zero_line(Mp3xCanvas *c);
/* Draw y[0..n-1] as a polyline (x = index across the width). */
void mp3x_canvas_series(Mp3xCanvas *c, const double *y, int n);
/* Draw y[0..n-1] as vertical bars rising from the y = 0 baseline. */
void mp3x_canvas_bars(Mp3xCanvas *c, const double *y, int n);
/* Draw a vertical line at index i (of n across the width), from data-y y0 to
   y1, in the current color. */
void mp3x_canvas_vline(Mp3xCanvas *c, int i, int n, double y0, double y1);
/* Draw a title string near the top-left, in the current color. */
void mp3x_canvas_title(Mp3xCanvas *c, const char *title);


/* --------------------------------------------------------------------------
 * Analyzer graphs (built on the canvas).
 * -------------------------------------------------------------------------- */

/* PCM waveform of the current frame (plotting_data.pcmdata, left channel). */
GtkWidget *mp3x_plot_pcm_new(void);
cairo_status_t mp3x_plot_pcm_write_png(const char *path, int width, int height);

/* Original input vs. encode->decode re-synthesized PCM, overlaid. */
GtkWidget *mp3x_plot_resynth_new(void);
cairo_status_t mp3x_plot_resynth_write_png(const char *path, int width, int height);

/* MDCT log-energy spectrum (576 lines) for granule gr (0 or 1), left channel. */
GtkWidget *mp3x_plot_mdct_new(int gr);
cairo_status_t mp3x_plot_mdct_write_png(const char *path, int gr,
                                         int width, int height);

/* Psychoacoustic energy per scalefactor band, granule gr, left channel: signal
   energy (bars), masking threshold and actual quantization noise (lines). */
GtkWidget *mp3x_plot_psy_new(int gr);
cairo_status_t mp3x_plot_psy_write_png(const char *path, int gr,
                                        int width, int height);

/* LAME's scalefactor magnitude per scalefactor band, granule gr, left channel:
   upright bars, laid out for a long block (SBMAX_l bands) or a short block
   (3*SBMAX_s = the short bands across the three windows). */
GtkWidget *mp3x_plot_sfb_new(int gr);
cairo_status_t mp3x_plot_sfb_write_png(const char *path, int gr,
                                        int width, int height);

/* Display options shared by every graph: channel (0 = left, 1 = right), ms
   (0 = L/R, 1 = M/S so channel picks Mid or Side), difference mode (re-synthesis
   draws decoded - original), and source (0 = LAME, 1 = mpg123-decoded, for the
   MDCT and scalefactor graphs). Call before redrawing when a toggle changes;
   defaults reproduce the original behavior. */
void mp3x_plot_set_options(int channel, int ms, int difference, int source);

/* Toggle the scalefactor-band marker lines on the MDCT graphs (default on). */
void mp3x_plot_set_sfblines(int on);

/* Psy spectrum x-axis: 0 = one point per scalefactor band (the default, and
   what the noise comparison needs), 1 = one point per FFT bin. gtkanal's
   Spectrum menu, gtkinfo.kbflag. */
void mp3x_plot_set_kbflag(int on);

/* Which of a short block's three interleaved windows to draw; all three by
   default. gtkanal's subblock_draw, selected there with the 1/2/3/0 keys. */
void mp3x_plot_set_subblock(int w0, int w1, int w2);

/* Infrastructure self-test: exercise every canvas primitive into a PNG.
   Developer-only; not exposed in the user-facing File menu. */
cairo_status_t mp3x_plot_demo_write_png(const char *path, int width, int height);

/* Composite screenshot: render all eight analyzer graphs into a single PNG
   laid out as the on-screen graph stack. Uses canonical fixed dimensions
   (600 x 499) matching the analyzer's standard layout; not a literal screen
   capture of the user's current window. */
cairo_status_t mp3x_plot_composite_write_png(const char *path);

#endif /* LAME_MP3X_PLOT_H */
