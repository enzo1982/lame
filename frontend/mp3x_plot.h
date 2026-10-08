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
 *  It has two parts:
 *
 *  1. Mp3xCanvas: drawing functions (background, baseline, lines, bars, title)
 *     that draw analyzer data on a Cairo surface. Every graph uses them. They
 *     contain no analyzer logic.
 *  2. The analyzer graphs (PCM waveform, re-synthesis overlay, and the others).
 *     Each graph is a small GtkDrawingArea that reads plotting_data and draws
 *     with the canvas.
 *
 *  \see \ref mp3x_internals for how drawing and export work. The mp3x(1) manual
 *  page describes what each graph means.
 */

#ifndef LAME_MP3X_PLOT_H
#define LAME_MP3X_PLOT_H

#include <gtk/gtk.h>

/**
 *  The drawing functions that all graphs share.
 *
 *  The x axis is always the sample or band index <tt>[0..n-1]</tt>, across the
 *  full width. The y axis shows the data range <tt>[ymn..ymx]</tt>, with \c ymx
 *  at the top. For each draw, the caller creates a canvas on the stack and
 *  uses the functions below.
 */
typedef struct {
    cairo_t *cr;                   /**< The Cairo context being drawn into. */
    GtkWidget *widget;             /**< Used for one draw; not owned. */
    int      width;                /**< Full surface width in pixels. */
    int      height;               /**< Full surface height in pixels. */
    int      plot_top;             /**< First pixel row below the title band. */
    int      plot_height;          /**< Pixel rows for the data. */
    double   ymn;                  /**< Data value mapped to the bottom edge. */
    double   ymx;                  /**< Data value mapped to the top edge. */
} Mp3xCanvas;

/** Starts a graph: sets the context, the size and the y range, and paints the
    background. */
void mp3x_canvas_begin(Mp3xCanvas *c, GtkWidget *widget, cairo_t *cr,
                        int width, int height, double ymn, double ymx);
/** Sets the drawing color. The RGB components are 0..1. */
void mp3x_canvas_color(Mp3xCanvas *c, double r, double g, double b);
/** Draws the gray baseline at data y = 0. */
void mp3x_canvas_zero_line(Mp3xCanvas *c);
/** Draws <tt>y[0..n-1]</tt> as a line. x is the index, across the width. */
void mp3x_canvas_series(Mp3xCanvas *c, const double *y, int n);
/** Draws a vertical bar for each value of <tt>y[0..n-1]</tt>, up from the
    baseline at y = 0. */
void mp3x_canvas_bars(Mp3xCanvas *c, const double *y, int n);
/** Draws a vertical line at index \p i of \p n across the width, from data
    y \p y0 to \p y1, in the current color. */
void mp3x_canvas_vline(Mp3xCanvas *c, int i, int n, double y0, double y1);
/** Draws a title near the top left corner, in the current color. */
void mp3x_canvas_title(Mp3xCanvas *c, const char *title);


/* --------------------------------------------------------------------------
 * Analyzer graphs (built on the canvas).
 * -------------------------------------------------------------------------- */

/** The graphs of the analyzer, and the images that File > Export writes. */
typedef enum {
    /** All eight graphs in one image, in the same order as on the screen, at
        the fixed size of the standard layout (600 x 499). It draws the graphs
        again; it does not capture the window. Export only. */
    MP3X_GRAPH_COMPOSITE,
    /** The PCM waveform of the current frame (\c plotting_data.pcmdata, left
        channel). */
    MP3X_GRAPH_PCM,
    /** The original input and the re-synthesized PCM (encoded, then decoded
        again), drawn over each other. */
    MP3X_GRAPH_RESYNTH,
    /** The MDCT log-energy spectrum, 576 lines, of granule 0, left channel. */
    MP3X_GRAPH_MDCT0,
    /** The same for granule 1. */
    MP3X_GRAPH_MDCT1,
    /** The psychoacoustic energy per scalefactor band of granule 0, left
        channel: the signal energy as bars, and the masking threshold and the
        real quantization noise as lines. */
    MP3X_GRAPH_PSY0,
    /** The same for granule 1. */
    MP3X_GRAPH_PSY1,
    /** LAME's scalefactors of the first granule, one vertical bar for each
        band. A long block has \c SBMAX_l bands. A short block has
        <tt>3 * SBMAX_s</tt>: the short bands of the three windows. */
    MP3X_GRAPH_SFB0,
    /** The same for granule 1. */
    MP3X_GRAPH_SFB1
} Mp3xGraph;

GtkWidget *mp3x_plot_new(Mp3xGraph graph);
cairo_status_t mp3x_plot_write_png(Mp3xGraph graph, const char *path);

/**
 *  Sets the display options that all graphs use.
 *
 *  Call it before the redraw when the user changes one of them. All options
 *  are 0 by default.
 *
 *  \param channel     0 for left, 1 for right. If \p ms is 1: 0 for mid, 1 for
 *                     side.
 *  \param ms          0 for left/right, 1 for mid/side.
 *  \param difference  Nonzero to make the re-synthesis graph draw the decoded
 *                     signal minus the original.
 *  \param source      0 for the LAME encoder side, 1 for the side that mpg123
 *                     decoded. Used by the MDCT and scalefactor graphs.
 */
void mp3x_plot_set_options(int channel, int ms, int difference, int source);

/** Switches the scalefactor-band lines on the MDCT graphs on or off. They are
    on by default. */
void mp3x_plot_set_sfblines(int on);

/**
 *  Selects the x axis of the psychoacoustic spectrum.
 *
 *  \param on  0 for one point per scalefactor band. This is the default, and
 *             only this view also draws the masking threshold and the
 *             quantization noise. 1 for one point per FFT bin. The Spectrum
 *             menu sets this option.
 */
void mp3x_plot_set_kbflag(int on);

/**
 *  Selects which of the three windows of a short block the graphs draw. By
 *  default, all three are drawn. In the GUI, the keys 1, 2 and 3 select one
 *  window, and the key 0 selects all three.
 *
 *  \param w0  nonzero to draw the first window.
 *  \param w1  nonzero to draw the second window.
 *  \param w2  nonzero to draw the third window.
 */
void mp3x_plot_set_subblock(int w0, int w1, int w2);

/**
 *  A self-test: draws with every canvas function into a PNG file.
 *  \internal For developers only. On purpose, the File menu does not offer it.
 */
cairo_status_t mp3x_plot_demo_write_png(const char *path, int width, int height);

#endif /* LAME_MP3X_PLOT_H */
