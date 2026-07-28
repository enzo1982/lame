/*
 *      mp3x plotting layer - Cairo primitives + analyzer graphs
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
 *  \file mp3x_plot.c
 *  \brief The mp3x plotting layer.
 *  \internal
 *
 *  The GTK4/Cairo successor to gpkplotting.c. Mp3xCanvas holds the shared
 *  drawing primitives; the analyzer graphs are built entirely from them and
 *  read only plotting_data - no analyzer logic lives in this layer.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <math.h>

#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include "mp3x_core.h"          /* plotting_data + the shared pinfo/pplot */
#include "mp3x_plot.h"

#define PCM_POINTS     1600     /* plotting_data.pcmdata is [2][1600] */
#define PCM_FULLSCALE  32768.0  /* 16-bit full scale */
#define RESYN_POINTS   1376     /* 224 (previous-frame tail) + 1152 (this frame) */


/* --------------------------------------------------------------------------
 * Display options, shared by every graph and set by the frontend when a toggle
 * changes (mirrors the relevant gtkanal gtkinfo flags). Defaults reproduce the
 * original behavior: left channel, no difference.
 * -------------------------------------------------------------------------- */
static int opt_channel = 0;      /* 0 = left, 1 = right */
static int opt_ms = 0;           /* 0 = L/R, 1 = M/S (channel then picks M or S) */
static int opt_difference = 0;   /* re-synthesis: draw decoded - original */
static int opt_source = 0;       /* 0 = LAME encoder side, 1 = mpg123-decoded */
static int opt_sfblines = 1;     /* scalefactor-band markers on the MDCT graphs */
static int opt_kbflag = 0;       /* psy spectrum: 0 = per band, 1 = per FFT bin */
static int opt_subblock[3] = { 1, 1, 1 };  /* which short-block windows to draw */

void
mp3x_plot_set_options(int channel, int ms, int difference, int source)
{
    opt_channel = channel;
    opt_ms = ms;
    opt_difference = difference;
    opt_source = source;
}

/*
 * gtkanal's subblock_draw. A short-block frame interleaves three windows, so
 * point i belongs to window i % 3; blanking the ones the user switched off
 * leaves a single window standing alone in the graph. Long-block frames have
 * no interleaving and are never touched.
 */
static void
apply_subblock(double *yv, int n, int is_short)
{
    int i;

    if (!is_short || (opt_subblock[0] && opt_subblock[1] && opt_subblock[2]))
        return;
    for (i = 0; i < n; i++)
        if (!opt_subblock[i % 3])
            yv[i] = 0.0;
}

void
mp3x_plot_set_subblock(int w0, int w1, int w2)
{
    opt_subblock[0] = w0;
    opt_subblock[1] = w1;
    opt_subblock[2] = w2;
}

void
mp3x_plot_set_kbflag(int on)
{
    opt_kbflag = on;
}

void
mp3x_plot_set_sfblines(int on)
{
    opt_sfblines = on;
}

/* Pick the displayed value from a left/right pair, honoring L/R vs M/S:
   L, R, M = (L+R)/2, or S = (L-R)/2. */
static double
chan_pick(double l, double r)
{
    if (opt_ms)
        return opt_channel ? 0.5 * (l - r) : 0.5 * (l + r);   /* S : M */
    return opt_channel ? r : l;                                /* R : L */
}

/* The mpg123-decoded companion of the display frame (gtkanal's pplot1): the ring
   frame whose decoder output (frameNum123) aligns with pdisp, plus one. Returns
   NULL if the decode for this frame is not buffered in the ring yet. */
static plotting_data *
mpg123_frame(void)
{
    int i;
    if (pdisp == NULL)
        return NULL;
    for (i = 1; i <= MAXMPGLAG; i++)
        if ((pdisp - i)->frameNum123 == pdisp->frameNum)
            return (pdisp - i) + 1;
    return NULL;
}


/* ==========================================================================
 * Mp3xCanvas - reusable plotting primitives
 * ========================================================================== */

/* map a sample/band index to an x pixel across the full width */
static double
canvas_px(const Mp3xCanvas *c, int i, int n)
{
    return (n > 1) ? (double) i / (n - 1) * c->width : c->width / 2.0;
}

/* map a data value to a y pixel (ymx at the top, ymn at the bottom) */
static double
canvas_py(const Mp3xCanvas *c, double y)
{
    double span = c->ymx - c->ymn;
    if (span == 0.0)
        span = 1.0;
    return c->plot_top + c->plot_height
           - (y - c->ymn) / span * c->plot_height;
}

static PangoLayout *
canvas_layout(const Mp3xCanvas *c, const char *text)
{
    PangoLayout *layout;

    if (c->widget != NULL) {
        /* Inherit the screen widget's font map, font description, base
           direction, DPI, and desktop text scaling. */
        layout = gtk_widget_create_pango_layout(c->widget, text);
    } else {
        PangoFontDescription *desc;

        /* Offscreen PNG output has no widget context; use a deterministic
           point-sized UI font while still letting PangoCairo honor the
           target surface transform. */
        layout = pango_cairo_create_layout(c->cr);
        pango_layout_set_text(layout, text, -1);
        desc = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
    }

    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width(layout, MAX(1, c->width - 8) * PANGO_SCALE);
    return layout;
}

void
mp3x_canvas_begin(Mp3xCanvas *c, GtkWidget *widget, cairo_t *cr,
                   int width, int height, double ymn, double ymx)
{
    PangoLayout *metrics_layout;
    PangoRectangle logical;

    c->cr = cr;
    c->widget = widget;
    c->width = width;
    c->height = height;
    c->ymn = ymn;
    c->ymx = ymx;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    /* Reserve a title band measured with the same Pango context that will
       render the title. Plot primitives map only through the remaining area,
       so text never overlays the data. */
    metrics_layout = canvas_layout(c, "Ag");
    pango_layout_get_pixel_extents(metrics_layout, NULL, &logical);
    c->plot_top = CLAMP(logical.height + 8, 18, MAX(18, height / 2));
    c->plot_height = MAX(1, height - c->plot_top);
    g_object_unref(metrics_layout);
}

void
mp3x_canvas_color(Mp3xCanvas *c, double r, double g, double b)
{
    cairo_set_source_rgb(c->cr, r, g, b);
    cairo_set_line_width(c->cr, 1.0);
}

void
mp3x_canvas_zero_line(Mp3xCanvas *c)
{
    double y = canvas_py(c, 0.0);
    cairo_set_source_rgb(c->cr, 0.25, 0.25, 0.25);
    cairo_set_line_width(c->cr, 1.0);
    cairo_move_to(c->cr, 0.0, y);
    cairo_line_to(c->cr, (double) c->width, y);
    cairo_stroke(c->cr);
}

void
mp3x_canvas_series(Mp3xCanvas *c, const double *y, int n)
{
    int     i;
    for (i = 0; i < n; i++) {
        double px = canvas_px(c, i, n);
        double py = canvas_py(c, y[i]);
        if (i == 0)
            cairo_move_to(c->cr, px, py);
        else
            cairo_line_to(c->cr, px, py);
    }
    cairo_stroke(c->cr);
}

void
mp3x_canvas_bars(Mp3xCanvas *c, const double *y, int n)
{
    double  base = canvas_py(c, 0.0);
    double  bw = (n > 0) ? (double) c->width / n * 0.8 : 1.0;
    int     i;
    if (bw < 1.0)
        bw = 1.0;
    for (i = 0; i < n; i++) {
        double px = canvas_px(c, i, n);
        double py = canvas_py(c, y[i]);
        cairo_rectangle(c->cr, px - bw / 2.0, py, bw, base - py);
        cairo_fill(c->cr);
    }
}

void
mp3x_canvas_vline(Mp3xCanvas *c, int i, int n, double y0, double y1)
{
    double px = canvas_px(c, i, n);
    cairo_set_line_width(c->cr, 1.0);
    cairo_move_to(c->cr, px, canvas_py(c, y0));
    cairo_line_to(c->cr, px, canvas_py(c, y1));
    cairo_stroke(c->cr);
}

void
mp3x_canvas_title(Mp3xCanvas *c, const char *title)
{
    PangoLayout *layout = canvas_layout(c, title);
    PangoRectangle logical;

    pango_layout_get_pixel_extents(layout, NULL, &logical);
    cairo_move_to(c->cr, 4.0 - logical.x, 4.0 - logical.y);
    pango_cairo_update_layout(c->cr, layout);
    pango_cairo_show_layout(c->cr, layout);
    g_object_unref(layout);
}

static cairo_status_t
png_open(int width, int height, cairo_surface_t **surface, cairo_t **cr)
{
    cairo_status_t status;

    *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    *cr = NULL;
    status = cairo_surface_status(*surface);
    if (status != CAIRO_STATUS_SUCCESS)
        return status;

    *cr = cairo_create(*surface);
    return cairo_status(*cr);
}

static cairo_status_t
png_finish(cairo_surface_t *surface, cairo_t *cr, const char *path,
           cairo_status_t status)
{
    if (status == CAIRO_STATUS_SUCCESS && cr != NULL)
        status = cairo_status(cr);
    if (status == CAIRO_STATUS_SUCCESS)
        status = cairo_surface_status(surface);
    if (status == CAIRO_STATUS_SUCCESS)
        status = cairo_surface_write_to_png(surface, path);

    if (cr != NULL)
        cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return status;
}


/* ==========================================================================
 * Graph 1: PCM waveform
 * ========================================================================== */

static void
pcm_render(GtkWidget *widget, cairo_t *cr, int width, int height)
{
    Mp3xCanvas c;
    char    title[96];
    const char *chlabel;

    mp3x_canvas_begin(&c, widget, cr, width, height,
                      -PCM_FULLSCALE, PCM_FULLSCALE);
    mp3x_canvas_zero_line(&c);

    if (pdisp == NULL || pdisp->sampfreq == 0)
        return;                 /* no frame at the display position yet */

    {
        double  y[PCM_POINTS];
        int     i;
        for (i = 0; i < PCM_POINTS; i++)
            y[i] = chan_pick(pdisp->pcmdata[0][i], pdisp->pcmdata[1][i]);
        mp3x_canvas_color(&c, 0.20, 1.00, 0.35);
        mp3x_canvas_series(&c, y, PCM_POINTS);
    }

    chlabel = opt_ms ? (opt_channel ? "S" : "M") : (opt_channel ? "R" : "L");
    g_snprintf(title, sizeof title, "PCM  %s  %.1f kHz", chlabel,
               pdisp->sampfreq / 1000.0);
    mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
    mp3x_canvas_title(&c, title);
}

static void
pcm_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) user_data;
    pcm_render(GTK_WIDGET(area), cr, width, height);
}

GtkWidget *
mp3x_plot_pcm_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(area),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "PCM waveform", -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 600);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 95);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), pcm_draw, NULL, NULL);
    return area;
}

cairo_status_t
mp3x_plot_pcm_write_png(const char *path, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    if (status == CAIRO_STATUS_SUCCESS)
        pcm_render(NULL, cr, width, height);
    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Graph 2: re-synthesis overlay (input vs. encode->decode output)
 * ========================================================================== */

static void
resynth_render(GtkWidget *widget, cairo_t *cr, int width, int height)
{
    Mp3xCanvas c;
    plotting_data *disp = pdisp;    /* the navigable display frame */
    plotting_data *p2 = NULL, *p1 = NULL;
    double  orig[RESYN_POINTS];
    double  resyn[RESYN_POINTS];
    int     i, j;

    mp3x_canvas_begin(&c, widget, cr, width, height,
                      -PCM_FULLSCALE, PCM_FULLSCALE);
    mp3x_canvas_zero_line(&c);

    if (disp == NULL || disp->sampfreq == 0)
        return;

    /* mpg123-lag sync (as in gtkanal's winbox): find the frame whose decoder
       output (frameNum123) corresponds to input frame disp->frameNum */
    for (i = 1; i <= MAXMPGLAG; i++) {
        if ((disp - i)->frameNum123 == disp->frameNum) {
            p2 = disp - i;
            break;
        }
    }
    if (p2 == NULL)
        return;
    p1 = p2 + 1;

    /* assemble the re-synthesis window: 224-sample tail of p1, then 1152 of p2 */
    for (j = 1152 - 224, i = 0; i < 224; i++, j++)
        resyn[i] = chan_pick(p1->pcmdata2[0][j], p1->pcmdata2[1][j]);
    for (i = 0; i < 1152; i++)
        resyn[i + 224] = chan_pick(p2->pcmdata2[0][i], p2->pcmdata2[1][i]);
    for (i = 0; i < RESYN_POINTS; i++)
        orig[i] = chan_pick(disp->pcmdata[0][i], disp->pcmdata[1][i]);

    if (opt_difference) {
        /* the encode error: decoded output minus the original input */
        double  err[RESYN_POINTS];
        for (i = 0; i < RESYN_POINTS; i++)
            err[i] = resyn[i] - orig[i];
        mp3x_canvas_color(&c, 1.00, 0.35, 0.35); /* error (decoded - original) */
        mp3x_canvas_series(&c, err, RESYN_POINTS);
    }
    else {
        mp3x_canvas_color(&c, 0.20, 1.00, 0.35); /* original input */
        mp3x_canvas_series(&c, orig, RESYN_POINTS);
        mp3x_canvas_color(&c, 1.00, 0.55, 0.10); /* re-synthesized output */
        mp3x_canvas_series(&c, resyn, RESYN_POINTS);
    }

    {
        char title[96];
        const char *chlabel = opt_ms ? (opt_channel ? "S" : "M") : (opt_channel ? "R" : "L");
        g_snprintf(title, sizeof title, "Re-synthesis %s  %s  %.1f kHz",
                   opt_difference ? "(diff)" : "", chlabel,
                   disp->sampfreq / 1000.0);
        mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
        mp3x_canvas_title(&c, title);
    }
}

static void
resynth_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) user_data;
    resynth_render(GTK_WIDGET(area), cr, width, height);
}

GtkWidget *
mp3x_plot_resynth_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(area),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Input and re-synthesized PCM waveform", -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 600);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 95);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), resynth_draw, NULL, NULL);
    return area;
}

cairo_status_t
mp3x_plot_resynth_write_png(const char *path, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    if (status == CAIRO_STATUS_SUCCESS)
        resynth_render(NULL, cr, width, height);
    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Graphs 3 & 4: MDCT log-energy spectra (one per granule)
 * ========================================================================== */

static void
mdct_render(GtkWidget *widget, cairo_t *cr, int width, int height, int gr)
{
    Mp3xCanvas c;
    plotting_data *p = pdisp;       /* the navigable display frame */
    plotting_data *src;             /* frame the displayed spectrum comes from */
    const int ch = opt_channel, n = 576;
    const double *lc, *rc;          /* left/right coefficient arrays */
    double  y[576];
    const char *bn;
    char    title[96];
    int     bt, bits, i;

    mp3x_canvas_begin(&c, widget, cr, width, height, 0.0, 11.0);
    if (p == NULL || p->sampfreq == 0)
        return;

    if (opt_source) {               /* mpg123-decoded side */
        src = mpg123_frame();
        if (src == NULL)
            return;                 /* decode for this frame not buffered yet */
        lc = src->mpg123xr[gr][0];
        rc = src->mpg123xr[gr][1];
        bt = src->mpg123blocktype[gr][ch];
        bits = src->mainbits[gr][ch];
    }
    else {                          /* LAME encoder side */
        src = p;
        lc = src->xr[gr][0];
        rc = src->xr[gr][1];
        bt = src->blocktype[gr][ch];
        bits = src->LAMEmainbits[gr][ch];
    }

    /* log energy per MDCT line - same transform as gtkanal */
    for (i = 0; i < n; i++) {
        double coeff = chan_pick(lc[i], rc[i]);
        double e = coeff * coeff * 1e10;
        y[i] = log10(e > 1.0 ? e : 1.0);
    }
    apply_subblock(y, n, bt == 2);

    switch (bt) {
    case 0:  bn = "normal"; break;
    case 1:  bn = "start";  break;
    case 2:  bn = "short";  break;
    case 3:  bn = "end";    break;
    default: bn = "?";      break;
    }
    g_snprintf(title, sizeof title, "MDCT%d %s (%s) bits=%d",
               gr, opt_source ? "mpg123" : "LAME", bn, bits);

    mp3x_canvas_color(&c, 0.55, 0.55, 0.62);
    mp3x_canvas_bars(&c, y, n);

    /* scalefactor-band boundaries over the spectrum, upper bands only (as
       gtkanal): yellow verticals from 0.8*ymx down to the baseline. Boundaries
       come from mp3x_core (mp3x_sfb_l/_s); a short block indexes a per-window
       layout, so scale by 3 to reach the interleaved 576-line spectrum.
       Toggleable via the sfbline control. */
    if (opt_sfblines) {
        int isshort = (bt == SHORT_TYPE);
        const int *band = isshort ? mp3x_sfb_s : mp3x_sfb_l;
        int nsfb  = isshort ? SBMAX_s : SBMAX_l;
        int fac   = isshort ? 3 : 1;
        int first = isshort ? nsfb - 7 : nsfb - 10;
        int k;
        mp3x_canvas_color(&c, 0.85, 0.85, 0.0);
        for (k = first; k < nsfb; k++)
            mp3x_canvas_vline(&c, fac * band[k], n, 0.8 * c.ymx, c.ymn);
    }

    mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
    mp3x_canvas_title(&c, title);
}

static void
mdct_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    mdct_render(GTK_WIDGET(area), cr, width, height,
                GPOINTER_TO_INT(user_data));
}

GtkWidget *
mp3x_plot_mdct_new(int gr)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(area),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   gr == 0 ? "MDCT spectrum, granule 1"
                                           : "MDCT spectrum, granule 2",
                                   -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 300);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 95);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), mdct_draw,
                                   GINT_TO_POINTER(gr), NULL);
    return area;
}

cairo_status_t
mp3x_plot_mdct_write_png(const char *path, int gr, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    if (status == CAIRO_STATUS_SUCCESS)
        mdct_render(NULL, cr, width, height, gr);
    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Graphs 5 & 6: psychoacoustic energy per scalefactor band (one per granule)
 *
 * Three quantities overlaid: signal energy (gray bars), the psy-model masking
 * threshold (blue/green line = how much noise is allowed), and the actual
 * quantization noise after encoding (red line). Where red rises above the
 * threshold line, the distortion is audible.
 * ========================================================================== */

static void
psy_render(GtkWidget *widget, cairo_t *cr, int width, int height, int gr)
{
    Mp3xCanvas c;
    plotting_data *p = pdisp;
    const int ch = opt_channel;     /* psy is L/R only (no M/S energy in the data) */
    const double *en, *thr, *xf;
    double  yv[3 * SBMAX_s];         /* max band count (short blocks) */
    char    title[96];
    int     n, i;

    mp3x_canvas_begin(&c, widget, cr, width, height, 3.0, 15.0);
    if (p == NULL || p->sampfreq == 0)
        return;

    if (opt_kbflag) {
        /* Wave-number view: the raw FFT magnitude the psy model works from,
           one point per bin rather than one per band, so structure narrower
           than a scalefactor band stays visible. Only the lower half of the
           transform is meaningful, which is what HBLKSIZE counts. */
        static double kv[HBLKSIZE];
        double        total = 0.0;

        for (i = 0; i < HBLKSIZE; i++) {
            double e = p->energy[gr][ch][i];
            kv[i] = log10(e > 1.0 ? e : 1.0);
        }
        apply_subblock(kv, HBLKSIZE, p->blocktype[gr][ch] == 2);
        for (i = 0; i < BLKSIZE; i++)
            total += p->energy[gr][ch][i];
        mp3x_canvas_color(&c, 0.45, 0.45, 0.50);
        mp3x_canvas_bars(&c, kv, HBLKSIZE);

        g_snprintf(title, sizeof title, "FFT%d  pe=%.1fK  en=%.2e",
                   gr, p->pe[gr][ch] / 1000.0, total);
        mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
        mp3x_canvas_title(&c, title);
        return;
    }

    if (p->blocktype[gr][ch] == 2) {            /* short block */
        n = 3 * SBMAX_s;
        en = p->en_s[gr][ch];
        thr = p->thr_s[gr][ch];
        xf = p->xfsf_s[gr][ch];
    }
    else {                                      /* long block */
        n = SBMAX_l;
        en = p->en[gr][ch];
        thr = p->thr[gr][ch];
        xf = p->xfsf[gr][ch];
    }

    /* signal energy: gray bars */
    for (i = 0; i < n; i++)
        yv[i] = log10(en[i] > 1.0 ? en[i] : 1.0);
    apply_subblock(yv, n, p->blocktype[gr][ch] == 2);
    mp3x_canvas_color(&c, 0.45, 0.45, 0.50);
    mp3x_canvas_bars(&c, yv, n);

    /* masking threshold (allowed noise): blue for gr0, green for gr1 */
    for (i = 0; i < n; i++)
        yv[i] = log10(thr[i] > 1.0 ? thr[i] : 1.0);
    apply_subblock(yv, n, p->blocktype[gr][ch] == 2);
    if (gr == 0)
        mp3x_canvas_color(&c, 0.35, 0.55, 1.00);
    else
        mp3x_canvas_color(&c, 0.30, 1.00, 0.45);
    mp3x_canvas_series(&c, yv, n);

    /* actual quantization noise: red */
    for (i = 0; i < n; i++)
        yv[i] = log10(xf[i] > 1.0 ? xf[i] : 1.0);
    apply_subblock(yv, n, p->blocktype[gr][ch] == 2);
    mp3x_canvas_color(&c, 1.00, 0.25, 0.25);
    mp3x_canvas_series(&c, yv, n);

    g_snprintf(title, sizeof title, "FFT%d  pe=%.1fK  over=%d",
               gr, p->pe[gr][ch] / 1000.0, p->over[gr][ch]);
    mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
    mp3x_canvas_title(&c, title);
}

static void
psy_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    psy_render(GTK_WIDGET(area), cr, width, height,
               GPOINTER_TO_INT(user_data));
}

GtkWidget *
mp3x_plot_psy_new(int gr)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(area),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   gr == 0 ? "Psychoacoustic spectrum, granule 1"
                                           : "Psychoacoustic spectrum, granule 2",
                                   -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 300);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 95);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), psy_draw,
                                   GINT_TO_POINTER(gr), NULL);
    return area;
}

cairo_status_t
mp3x_plot_psy_write_png(const char *path, int gr, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    if (status == CAIRO_STATUS_SUCCESS)
        psy_render(NULL, cr, width, height, gr);
    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Graphs 7 & 8: scalefactors per band (one per granule)
 *
 * plotting_data stores LAMEsfb as a non-positive log-domain value
 * (-ifqstep * scalefac, set in quantize_pvt.c); its magnitude is the per-band
 * scalefactor - how much that band was amplified before quantization, spending
 * bits to hold its noise down. We plot that magnitude (-sfb) as upright bars,
 * exactly the quantity gtkanal drew. Block type sets the layout: a long block
 * has SBMAX_l (22) bands; a short block has 3*SBMAX_s (39) values - the 13 short
 * bands across the three windows, interleaved window-minor (band b, window w ->
 * index b*3 + w).
 *
 * The source selector switches between LAME and mpg123 scalefactors. The
 * short-block selector can display all three interleaved windows or isolate one
 * of them, matching gtkanal's subblock filter.
 * ========================================================================== */

static void
sfb_render(GtkWidget *widget, cairo_t *cr, int width, int height, int gr)
{
    Mp3xCanvas c;
    plotting_data *p = pdisp;       /* the navigable display frame */
    plotting_data *src;             /* frame the scalefactors come from */
    const int ch = opt_channel;
    const double *sf;
    double  yv[3 * SBMAX_s];        /* per-band magnitude (-sfb) */
    double  ymx, maxv = 0.0;
    char    title[96];
    int     n, i, isshort, bt, gain;

    if (p == NULL || p->sampfreq == 0) {   /* no frame at the display position */
        mp3x_canvas_begin(&c, widget, cr, width, height, -1.0, 10.0);
        return;
    }

    if (opt_source) {               /* mpg123-decoded side */
        src = mpg123_frame();
        if (src == NULL) {
            mp3x_canvas_begin(&c, widget, cr, width, height, -1.0, 10.0);
            return;                 /* decode for this frame not buffered yet */
        }
        bt = src->mpg123blocktype[gr][ch];
        gain = src->qss[gr][ch];
    }
    else {                          /* LAME encoder side */
        src = p;
        bt = src->blocktype[gr][ch];
        gain = src->LAMEqss[gr][ch];
    }
    isshort = (bt == SHORT_TYPE);
    n  = isshort ? 3 * SBMAX_s : SBMAX_l;
    if (opt_source)
        sf = isshort ? src->sfb_s[gr][ch] : src->sfb[gr][ch];
    else
        sf = isshort ? src->LAMEsfb_s[gr][ch] : src->LAMEsfb[gr][ch];

    /* LAMEsfb is non-positive; plot its magnitude. Mirror gtkanal's range: a -1
       floor keeps the zero baseline just off the bottom, and the top expands
       past 10 only when a band's magnitude needs it. */
    for (i = 0; i < n; i++) {
        yv[i] = -sf[i];
        if (yv[i] > maxv)
            maxv = yv[i];
    }
    apply_subblock(yv, n, bt == 2);
    ymx = (maxv + 2.0 > 10.0) ? maxv + 2.0 : 10.0;

    mp3x_canvas_begin(&c, widget, cr, width, height, -1.0, ymx);
    mp3x_canvas_zero_line(&c);
    mp3x_canvas_color(&c, 0.60, 0.45, 0.85);   /* scalefactor bars: violet */
    mp3x_canvas_bars(&c, yv, n);

    if (isshort)
        g_snprintf(title, sizeof title,
                   "SFB%d %s (short) scale=%d preflag=%d gain=%d sub=%d/%d/%d",
                   gr, opt_source ? "mpg123" : "LAME",
                   src->scalefac_scale[gr][ch], src->preflag[gr][ch], gain,
                   src->sub_gain[gr][ch][0], src->sub_gain[gr][ch][1],
                   src->sub_gain[gr][ch][2]);
    else
        g_snprintf(title, sizeof title,
                   "SFB%d %s (long) scale=%d preflag=%d gain=%d",
                   gr, opt_source ? "mpg123" : "LAME",
                   src->scalefac_scale[gr][ch], src->preflag[gr][ch], gain);
    mp3x_canvas_color(&c, 1.00, 1.00, 1.00);
    mp3x_canvas_title(&c, title);
}

static void
sfb_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    sfb_render(GTK_WIDGET(area), cr, width, height,
               GPOINTER_TO_INT(user_data));
}

GtkWidget *
mp3x_plot_sfb_new(int gr)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(area),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   gr == 0 ? "Scalefactor bands, granule 1"
                                           : "Scalefactor bands, granule 2",
                                   -1);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 300);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 95);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), sfb_draw,
                                   GINT_TO_POINTER(gr), NULL);
    return area;
}

cairo_status_t
mp3x_plot_sfb_write_png(const char *path, int gr, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    if (status == CAIRO_STATUS_SUCCESS)
        sfb_render(NULL, cr, width, height, gr);
    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Infrastructure self-test: exercise every canvas primitive
 * ========================================================================== */

cairo_status_t
mp3x_plot_demo_write_png(const char *path, int width, int height)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(width, height, &s, &cr);
    Mp3xCanvas c;
    double  bars[24], line[128];
    int     i;

    if (status != CAIRO_STATUS_SUCCESS)
        return png_finish(s, cr, path, status);

    for (i = 0; i < 24; i++)
        bars[i] = 1.0 + 5.0 * (0.5 + 0.5 * sin(i * 0.5));
    for (i = 0; i < 128; i++)
        line[i] = 6.0 + 4.5 * sin(i * 0.15);

    mp3x_canvas_begin(&c, NULL, cr, width, height, 0.0, 12.0);
    mp3x_canvas_zero_line(&c);
    mp3x_canvas_color(&c, 0.35, 0.35, 0.90);   /* bars */
    mp3x_canvas_bars(&c, bars, 24);
    mp3x_canvas_color(&c, 0.20, 1.00, 0.35);   /* series on top */
    mp3x_canvas_series(&c, line, 128);
    mp3x_canvas_color(&c, 1.00, 1.00, 1.00);   /* title */
    mp3x_canvas_title(&c, "canvas self-test: bars + series + baseline + title");

    return png_finish(s, cr, path, status);
}


/* ==========================================================================
 * Composite screenshot
 *
 * Renders the eight analyzer graphs into one PNG laid out as the on-screen
 * graph stack (PCM and re-synthesis full-width; MDCT/PSY/SFB side-by-side
 * per granule). The dimensions are canonical fixed values that reflect the
 * analyzer's standard layout, NOT the user's current window allocation;
 * callers that want a literal screen capture should composite their own
 * widget read-outs.
 *
 * Each per-plot render is wrapped in a cairo save/clip/translate/restore so
 * its cairo_paint (background fill) and drawing are confined to that plot's
 * rectangle and rendered at the correct offset.
 * ========================================================================== */

/* Layout constants - mirror the on-screen widgets' declared content sizes
   (mp3x_plot_*_new uses 600x95 for full-width and 300x95 for half-width). */
#define COMPOSITE_FULL_W   600
#define COMPOSITE_HALF_W   300
#define COMPOSITE_ROW_H    95
#define COMPOSITE_GAP      6
#define COMPOSITE_TOTAL_H  (5 * COMPOSITE_ROW_H + 4 * COMPOSITE_GAP)

/* Render one plot into a sub-rectangle of the parent surface. The render
   function is called with a width/height matching the rectangle, with the
   cairo state translated and clipped so it cannot bleed into other plots. */
static void
composite_plot(cairo_t *parent_cr, int x, int y, int w, int h,
               void (*render)(GtkWidget *, cairo_t *, int, int))
{
    cairo_save(parent_cr);
    cairo_new_path(parent_cr);
    cairo_rectangle(parent_cr, x, y, w, h);
    cairo_clip(parent_cr);
    cairo_translate(parent_cr, x, y);
    render(NULL, parent_cr, w, h);
    cairo_restore(parent_cr);
}

/* Granule-plot variant - the render function takes an extra granule index. */
static void
composite_plot_gr(cairo_t *parent_cr, int x, int y, int w, int h, int gr,
                  void (*render)(GtkWidget *, cairo_t *, int, int, int))
{
    cairo_save(parent_cr);
    cairo_new_path(parent_cr);
    cairo_rectangle(parent_cr, x, y, w, h);
    cairo_clip(parent_cr);
    cairo_translate(parent_cr, x, y);
    render(NULL, parent_cr, w, h, gr);
    cairo_restore(parent_cr);
}

cairo_status_t
mp3x_plot_composite_write_png(const char *path)
{
    cairo_surface_t *s;
    cairo_t *cr;
    cairo_status_t status = png_open(COMPOSITE_FULL_W, COMPOSITE_TOTAL_H,
                                     &s, &cr);
    int y;

    if (status != CAIRO_STATUS_SUCCESS)
        return png_finish(s, cr, path, status);
    /* Clear the parent surface to a neutral dark gray so the gap rows
       between plots are not left at whatever the underlying surface
       initialized to. */
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_paint(cr);

    /* Row 1: PCM (full width) */
    y = 0;
    composite_plot(cr, 0, y, COMPOSITE_FULL_W, COMPOSITE_ROW_H, pcm_render);

    /* Row 2: Re-synthesis (full width) */
    y += COMPOSITE_ROW_H + COMPOSITE_GAP;
    composite_plot(cr, 0, y, COMPOSITE_FULL_W, COMPOSITE_ROW_H, resynth_render);

    /* Row 3: MDCT granule 0 | MDCT granule 1 (side by side) */
    y += COMPOSITE_ROW_H + COMPOSITE_GAP;
    composite_plot_gr(cr, 0,                  y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 0, mdct_render);
    composite_plot_gr(cr, COMPOSITE_HALF_W,   y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 1, mdct_render);

    /* Row 4: FFT/Psy granule 0 | granule 1 */
    y += COMPOSITE_ROW_H + COMPOSITE_GAP;
    composite_plot_gr(cr, 0,                  y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 0, psy_render);
    composite_plot_gr(cr, COMPOSITE_HALF_W,   y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 1, psy_render);

    /* Row 5: Scalefactors granule 0 | granule 1 */
    y += COMPOSITE_ROW_H + COMPOSITE_GAP;
    composite_plot_gr(cr, 0,                  y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 0, sfb_render);
    composite_plot_gr(cr, COMPOSITE_HALF_W,   y, COMPOSITE_HALF_W, COMPOSITE_ROW_H, 1, sfb_render);

    return png_finish(s, cr, path, status);
}
