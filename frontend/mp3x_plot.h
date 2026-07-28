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

/**
 *  \file mp3x_plot.h
 *  \brief The GTK4/Cairo plotting layer for the analyzer.
 *  \internal
 *
 *  The successor to the GTK1 gpkplotting.c. It has two parts:
 *
 *  1. Mp3xCanvas - reusable drawing primitives (background, baseline, line
 *     series, bars, title) that map analyzer data onto a Cairo surface. Every
 *     graph is built from these; they contain no analyzer logic.
 *  2. The analyzer graphs themselves (PCM waveform, re-synthesis overlay, and
 *     the rest), each a thin GtkDrawingArea that reads plotting_data and draws
 *     with the canvas.
 *
 *  \see \ref mp3x_internals for the drawing and export architecture. What each
 *  graph means to someone reading it is described in the mp3x(1) manual page.
 */

#ifndef LAME_MP3X_PLOT_H
#define LAME_MP3X_PLOT_H

#include <gtk/gtk.h>

/**
 *  Shared plotting primitives.
 *
 *  The x axis is always the sample/band index <tt>[0..n-1]</tt> spread across
 *  the full width; the y axis maps the data range <tt>[ymn..ymx]</tt>, with
 *  \c ymx at the top. Callers stack-allocate a canvas per draw and drive it
 *  with the primitives below.
 */
typedef struct {
    cairo_t *cr;                   /**< The Cairo context being drawn into. */
    GtkWidget *widget;             /**< Borrowed for the duration of one draw. */
    int      width;                /**< Full surface width in pixels. */
    int      height;               /**< Full surface height in pixels. */
    int      plot_top;             /**< First pixel row below the title band. */
    int      plot_height;          /**< Rows available to the data mapping. */
    double   ymn;                  /**< Data value mapped to the bottom edge. */
    double   ymx;                  /**< Data value mapped to the top edge. */
} Mp3xCanvas;

/** Begin a graph: bind the context and size, set the y range, paint the background. */
void mp3x_canvas_begin(Mp3xCanvas *c, GtkWidget *widget, cairo_t *cr,
                        int width, int height, double ymn, double ymx);
/** Set the drawing colour; RGB components run 0..1. */
void mp3x_canvas_color(Mp3xCanvas *c, double r, double g, double b);
/** Draw the grey baseline at data y = 0. */
void mp3x_canvas_zero_line(Mp3xCanvas *c);
/** Draw <tt>y[0..n-1]</tt> as a polyline, x being the index across the width. */
void mp3x_canvas_series(Mp3xCanvas *c, const double *y, int n);
/** Draw <tt>y[0..n-1]</tt> as vertical bars rising from the y = 0 baseline. */
void mp3x_canvas_bars(Mp3xCanvas *c, const double *y, int n);
/** Draw a vertical line at index \p i of \p n across the width, from data-y
    \p y0 to \p y1, in the current colour. */
void mp3x_canvas_vline(Mp3xCanvas *c, int i, int n, double y0, double y1);
/** Draw a title string near the top-left, in the current colour. */
void mp3x_canvas_title(Mp3xCanvas *c, const char *title);


/* --------------------------------------------------------------------------
 * Analyzer graphs (built on the canvas).
 * -------------------------------------------------------------------------- */

/** PCM waveform of the current frame (\c plotting_data.pcmdata, left channel). */
GtkWidget *mp3x_plot_pcm_new(void);
/** Render the PCM waveform graph to a PNG file. */
cairo_status_t mp3x_plot_pcm_write_png(const char *path, int width, int height);

/** Original input against encode-then-decode re-synthesized PCM, overlaid. */
GtkWidget *mp3x_plot_resynth_new(void);
/** Render the re-synthesis comparison graph to a PNG file. */
cairo_status_t mp3x_plot_resynth_write_png(const char *path, int width, int height);

/** MDCT log-energy spectrum, 576 lines, for granule \p gr (0 or 1), left channel. */
GtkWidget *mp3x_plot_mdct_new(int gr);
/** Render an MDCT spectrum graph to a PNG file. */
cairo_status_t mp3x_plot_mdct_write_png(const char *path, int gr,
                                         int width, int height);

/**
 *  Psychoacoustic energy per scalefactor band, granule \p gr, left channel:
 *  signal energy as bars, masking threshold and actual quantization noise as
 *  lines.
 */
GtkWidget *mp3x_plot_psy_new(int gr);
/** Render a psychoacoustic graph to a PNG file. */
cairo_status_t mp3x_plot_psy_write_png(const char *path, int gr,
                                        int width, int height);

/**
 *  LAME's scalefactor magnitude per scalefactor band, granule \p gr, left
 *  channel, as upright bars - laid out for a long block (\c SBMAX_l bands) or a
 *  short block (<tt>3 * SBMAX_s</tt>, the short bands across the three windows).
 */
GtkWidget *mp3x_plot_sfb_new(int gr);
/** Render a scalefactor graph to a PNG file. */
cairo_status_t mp3x_plot_sfb_write_png(const char *path, int gr,
                                        int width, int height);

/**
 *  Set the display options shared by every graph.
 *
 *  Call before redrawing when a toggle changes; the defaults reproduce the
 *  original gtkanal behaviour.
 *
 *  \param channel     0 for left, 1 for right.
 *  \param ms          0 for L/R, 1 for M/S - so \p channel then picks Mid or Side.
 *  \param difference  Non-zero to make the re-synthesis graph draw
 *                     decoded minus original.
 *  \param source      0 for the LAME encoder side, 1 for the mpg123-decoded
 *                     side; applies to the MDCT and scalefactor graphs.
 */
void mp3x_plot_set_options(int channel, int ms, int difference, int source);

/** Toggle the scalefactor-band marker lines on the MDCT graphs; on by default. */
void mp3x_plot_set_sfblines(int on);

/**
 *  Choose the psychoacoustic spectrum's x axis.
 *
 *  \param on  0 for one point per scalefactor band - the default, and what the
 *             noise comparison needs - or 1 for one point per FFT bin. This is
 *             gtkanal's Spectrum menu, its \c gtkinfo.kbflag.
 */
void mp3x_plot_set_kbflag(int on);

/**
 *  Select which of a short block's three interleaved windows to draw; all three
 *  by default. This is gtkanal's \c subblock_draw, selected there - as here -
 *  with the 1/2/3/0 keys.
 */
void mp3x_plot_set_subblock(int w0, int w1, int w2);

/**
 *  Infrastructure self-test: exercise every canvas primitive into a PNG.
 *  \internal Developer-only; deliberately not exposed in the File menu.
 */
cairo_status_t mp3x_plot_demo_write_png(const char *path, int width, int height);

/**
 *  Composite screenshot: render all eight analyzer graphs into a single PNG,
 *  laid out as the on-screen graph stack.
 *
 *  Uses the canonical fixed dimensions (600 x 499) of the analyzer's standard
 *  layout. This is a re-render, not a literal capture of the user's window.
 */
cairo_status_t mp3x_plot_composite_write_png(const char *path);

#endif /* LAME_MP3X_PLOT_H */
