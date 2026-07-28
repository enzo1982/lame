/*
 *      mp3x - GTK4 frame-analyzer frontend
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
 *  \file mp3x_ui.c
 *  \brief The GTK4 mp3x application: window, menus and transport.
 *  \internal
 *
 *  The frontend drives the toolkit-independent analyzer engine in mp3x_core.c,
 *  displays the analyzer's eight plots via mp3x_plot.c, and owns the
 *  application and window lifecycle plus the File menu.
 *
 *  The ownership and lifetime rules summarised below are set out in full, with
 *  the transport and end-of-input reasoning, on the \ref mp3x_internals page.
 *  What the program looks like to someone using it is the mp3x(1) manual page.
 */

/*
 * Application/session architecture:
 *
 *   - Mp3xDriver is heap-allocated, refcounted via gatomicrefcount, and
 *     owns the application-lifetime state: GtkApplication borrow, window
 *     borrow, menu models, CSS, current session pointer, view preferences,
 *     captured frontend-global baseline, in-flight async request slots,
 *     and the monotonic state-epoch and session-generation counters.
 *
 *   - Mp3xSession is heap-allocated per open file, owns the lame_t and
 *     per-file transport state, and is fully torn down on Close or file
 *     replacement (see mp3x_session.c).
 *
 *   - IdleContext is allocated per g_idle_add call, captures (driver,
 *     session, generation) at install time, and is freed by its destroy
 *     notify. Stale contexts detect the mismatch and bail harmlessly.
 *
 *   - FileDialogRequest is allocated per GtkFileDialog open/save, holds a
 *     strong driver ref + fresh GCancellable + captured state epoch + the
 *     GtkFileDialog object. Driver slots are non-owning identity pointers;
 *     the matching callback clears its slot and finalizes the request.
 *
 *   - Shutdown is idempotent and deferred: when an async request is in
 *     flight when Quit/Close fires, the main loop keeps running until the
 *     last request's callback has run; only then does g_application_quit
     fire and the main loop exit.
 *
 * File > Open and Open Recent share mp3x_driver_open_file. The initial CLI
 * route runs before GTK construction so parser informational exits need no
 * window; CLI encoder/analyzer options apply only to that initial file.
 * Later GUI-selected files use clean documented LAME defaults.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef HAVE_MPG123
#include <mpg123.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lame.h"
#include "main.h"
#include "parse.h"
#include "get_audio.h"
#include "console.h"
#include "mp3x_core.h"
#include "mp3x_plot.h"
#include "mp3x_session.h"


/* ==========================================================================
 * Mp3xDriver
 * ========================================================================== */

/* Forward declarations of types that appear in Mp3xDriver. Full struct
   definitions follow immediately after Mp3xDriver so every helper below
   sees them. */
typedef struct FileDialogRequest FileDialogRequest;
typedef struct IdleContext       IdleContext;

struct Mp3xDriver {
    /* Atomic cross-thread reference count. The driver is referenced by:
       - the startup code (1 ref)
       - every in-flight FileDialogRequest (+1 each)
       - every installed IdleContext (+1 each)
       It is freed when the last ref drops. */
    gatomicrefcount      ref_count;

    /* Main-thread-only fields (GTK thread-default main context). */
    gboolean             shutting_down;
    gboolean             analysis_failed;     /* cumulative process exit latch */
    int                  outstanding_requests;
    guint64              state_epoch;          /* bumped on every session transition */
    guint64              next_session_generation;

    /* Borrowed from lame_main / GtkApplication - NOT unref'd by us. */
    GtkApplication      *app;                  /* valid while g_application_run runs */
    GtkWindow           *win;                  /* owned by GtkApplication; cleared by on_window_destroy */

    /* Owned - freed in _finalize. */
    GMenuModel          *menubar;
    GtkCssProvider      *noncomposite_css;
    Mp3xSession         *session;              /* NULL when empty */
    FrontendGlobalsBaseline baseline;          /* plain value data */
    FileDialogRequest   *open_request;         /* identity pointer; non-owning */
    FileDialogRequest   *save_request;         /* identity pointer; non-owning */

    /* View preferences - driver-lifetime, persist across file swaps. */
    int                  channel;              /* 0 = left, 1 = right */
    int                  ms;                   /* 0 = L/R, 1 = M/S */
    gboolean             difference;
    int                  sfblines;
    int                  kbflag;
    int                  subblock;             /* -1 = all, else 0..2 */
    gboolean             plot_advancing;

    /* UI shell - owned by GtkApplication / GtkWindow; we just hold pointers
       set up in on_activate. */
    GtkWidget           *header;
    GtkWidget           *plot;
    GtkWidget           *plot_resynth;
    GtkWidget           *plot_mdct[2];
    GtkWidget           *plot_psy[2];
    GtkWidget           *plot_sfb[2];
    GtkWidget           *btn_playpause;
    GtkWidget           *btn_back;
    GtkWidget           *btn_step;
    GtkWidget           *status;
    GtkWidget           *progress;
};


/* --------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------- */

/* Built in this file. */
static void     on_startup            (GtkApplication *app, gpointer data);
static void     on_activate           (GtkApplication *app, gpointer data);
static gboolean on_close_request      (GtkWindow *win, gpointer user_data);
static void     on_window_destroy     (GtkWindow *win, gpointer user_data);
static gboolean on_idle               (gpointer data);
static void     on_idle_destroy       (gpointer data);
static gboolean mp3x_idle_step        (gpointer data);
static void     mp3x_idle_step_install(Mp3xDriver *d);
static gboolean mp3x_do_one_frame     (Mp3xDriver *d);
static void     mp3x_start_session    (Mp3xDriver *d);

static Mp3xDriver *mp3x_driver_ref    (Mp3xDriver *d);
static void        mp3x_driver_unref  (Mp3xDriver *d);

static void        update_transport   (Mp3xDriver *d);

static gboolean mp3x_driver_open_file (Mp3xDriver *d, GFile *gfile, GError **err);
static void     mp3x_driver_close_file(Mp3xDriver *d);
static void     mp3x_driver_request_shutdown(Mp3xDriver *d);
static void     mp3x_driver_show_error(Mp3xDriver *d, const char *fmt, ...);
static void     mp3x_driver_refresh_for_session(Mp3xDriver *d);
static void     mp3x_driver_refresh_for_empty  (Mp3xDriver *d);

/* Accessors consumed by mp3x_session.c. */
guint64                  mp3x_driver_next_generation(Mp3xDriver *d);
FrontendGlobalsBaseline *mp3x_driver_baseline       (Mp3xDriver *d);


/* ==========================================================================
 * FileDialogRequest - one per GtkFileDialog open/save
 *
 * Defined here (not in mp3x_session.h) because it exists only for the GTK
 * async-dialog machinery in this file.
 * ========================================================================== */

struct FileDialogRequest {
    Mp3xDriver    *d;                  /* strong ref; dropped in _finalize */
    guint64        state_epoch;        /* captured at creation */
    Mp3xSession   *session_at_request; /* captured; NULL OK for Open */
    guint64        generation_at_request;
    GCancellable  *cancellable;        /* fresh per request, owned */
    GtkFileDialog *dlg;                /* owned */
    gboolean       is_save;            /* Open if FALSE, Save/Export if TRUE */
    int            export_kind;        /* 0 = composite screenshot,
                                          1 = pcm, 2 = resynth,
                                          3 = mdct-0, 4 = mdct-1,
                                          5 = psy-0,  6 = psy-1,
                                          7 = sfb-0,  8 = sfb-1 */
};

struct IdleContext {
    Mp3xDriver  *d;                /* strong ref; dropped in destroy notify */
    Mp3xSession *session_at_install;
    guint64      generation_at_install;
};


/* ==========================================================================
 * Driver lifecycle
 * ========================================================================== */

static Mp3xDriver *
mp3x_driver_new(void)
{
    Mp3xDriver *d = g_new0(Mp3xDriver, 1);
    g_atomic_ref_count_init(&d->ref_count);

    /* View-preference defaults - gtkanal's defaults preserved. */
    d->channel    = 0;     /* left */
    d->ms         = 0;     /* L/R */
    d->difference = FALSE;
    d->sfblines   = 1;     /* band markers on */
    d->kbflag     = 0;     /* per band */
    d->subblock   = -1;    /* all short-block windows */
    d->plot_advancing = FALSE;

    return d;
}

Mp3xDriver *
mp3x_driver_ref(Mp3xDriver *d)
{
    if (d != NULL)
        g_atomic_ref_count_inc(&d->ref_count);
    return d;
}

static void
mp3x_driver_finalize(Mp3xDriver *d)
{
    /* No in-flight callbacks at this point - they'd hold a ref. */
    g_clear_pointer(&d->session, mp3x_session_free);
    g_clear_pointer(&d->menubar, g_object_unref);
    g_clear_pointer(&d->noncomposite_css, g_object_unref);

    /* Request slots must already be NULL - the matching callbacks clear
       them, and we cannot be finalizing while any request holds a ref. */
    g_assert(d->open_request == NULL);
    g_assert(d->save_request == NULL);

    /* d->app and d->win are borrowed; not unref'd here. */
    g_free(d);
}

static void
mp3x_driver_unref(Mp3xDriver *d)
{
    if (d == NULL)
        return;
    if (g_atomic_ref_count_dec(&d->ref_count))
        mp3x_driver_finalize(d);
}

guint64
mp3x_driver_next_generation(Mp3xDriver *d)
{
    return ++d->next_session_generation;
}

FrontendGlobalsBaseline *
mp3x_driver_baseline(Mp3xDriver *d)
{
    return &d->baseline;
}


/* ==========================================================================
 * Frontend-global baseline
 *
 * Captured ONCE at startup, before any parse_args call. mp3x_session_open
 * and mp3x_session_open_cli_initial both restore from this baseline before
 * applying any new configuration.
 * ========================================================================== */

static void
mp3x_driver_capture_baseline(Mp3xDriver *d)
{
    mp3x_apply_documented_defaults();
    mp3x_globals_capture(&d->baseline);
}


/* ==========================================================================
 * Driver shutdown (idempotent, deferred)
 * ========================================================================== */

void
mp3x_driver_request_shutdown(Mp3xDriver *d)
{
    if (d->shutting_down)
        return;
    d->shutting_down = TRUE;

    /* 1. Cancel in-flight Open/Save. Their callbacks finalize on the next
          main-loop iteration. */
    if (d->open_request)
        g_cancellable_cancel(d->open_request->cancellable);
    if (d->save_request)
        g_cancellable_cancel(d->save_request->cancellable);

    /* 2. Retire the active session. Stale IdleContexts will see NULL and
          their generation check fails; stale save callbacks see a state-epoch
          mismatch and bail. */
    if (d->session) {
        Mp3xSession *s = d->session;
        d->session = NULL;
        d->state_epoch++;
        d->analysis_failed |= s->failed;
        mp3x_session_close(s);
        mp3x_session_free(s);
    }

    /* 3. Hide the window so the user sees shutdown register immediately.
          Widget pointers stay valid until gtk_window_destroy in step 5. */
    if (d->win)
        gtk_widget_set_visible(GTK_WIDGET(d->win), FALSE);

    /* 4. If nothing is pending, finish now. Otherwise the last
          request_finalize performs gtk_window_destroy + g_application_quit. */
    if (d->outstanding_requests == 0) {
        if (d->win) {
            GtkWindow *win = d->win;
            d->win = NULL;          /* clear BEFORE destroy so the destroy
                                       handler sees NULL and does not re-enter */
            gtk_window_destroy(win);
        }
        if (d->app)
            g_application_quit(G_APPLICATION(d->app));
    }
}


/* ==========================================================================
 * FileDialogRequest lifecycle
 * ========================================================================== */

static FileDialogRequest *
request_new(Mp3xDriver *d, gboolean is_save, int export_kind)
{
    FileDialogRequest *req;

    if (d->shutting_down)
        return NULL;   /* prevent new requests during shutdown */

    /* Cancel any prior in-flight request of the same kind. */
    if (is_save) {
        if (d->save_request)
            g_cancellable_cancel(d->save_request->cancellable);
    } else {
        if (d->open_request)
            g_cancellable_cancel(d->open_request->cancellable);
    }

    req = g_new0(FileDialogRequest, 1);
    req->d                     = mp3x_driver_ref(d);
    req->state_epoch           = d->state_epoch;
    req->session_at_request    = d->session;   /* captured; NULL OK for Open */
    req->generation_at_request = d->session ? d->session->generation : 0;
    req->cancellable           = g_cancellable_new();
    req->dlg                   = gtk_file_dialog_new();
    req->is_save               = is_save;
    req->export_kind           = export_kind;

    /* Hold the application alive while this request is in flight. Released
       in request_finalize. */
    if (d->app)
        g_application_hold(G_APPLICATION(d->app));
    d->outstanding_requests++;

    if (is_save)
        d->save_request = req;
    else
        d->open_request = req;
    return req;
}

/* Called EXACTLY ONCE per request, on every success/error/dismissal/
   cancellation/shutdown path. The order matters: driver unref is LAST. */
static void
request_finalize(FileDialogRequest *req)
{
    Mp3xDriver *d = req->d;

    /* Clear our slot only if it still points at us. */
    if (d->open_request == req) d->open_request = NULL;
    if (d->save_request == req) d->save_request = NULL;

    /* Decrement outstanding_requests while d is alive. Main-thread only. */
    d->outstanding_requests--;
    const gboolean now_zero = (d->outstanding_requests == 0);
    const gboolean finish_shutdown = now_zero && d->shutting_down;

    /* Release this request's GApplication hold (paired with request_new). */
    if (d->app)
        g_application_release(G_APPLICATION(d->app));

    /* Release the per-request owned GLib objects. */
    g_clear_object(&req->cancellable);
    g_clear_object(&req->dlg);
    g_free(req);

    /* If this was the last in-flight request and shutdown is in progress,
       finish shutdown now (window destruction + quit). The driver is still
       referenced by our captured d pointer. */
    if (finish_shutdown) {
        if (d->win) {
            GtkWindow *win = d->win;
            d->win = NULL;
            gtk_window_destroy(win);
        }
        if (d->app)
            g_application_quit(G_APPLICATION(d->app));
    }

    /* Drop this request's strong driver ref LAST. If this is the last ref,
       the driver finalizes here. */
    mp3x_driver_unref(d);
}


/* ==========================================================================
 * Open Audio File - GtkFileDialog async path
 * ========================================================================== */

/* Forward decl - the per-export save path. */
static void request_start_save(Mp3xDriver *d, int export_kind);

/* Build the supported-files filter set from the formats mp3x actually opens.
   Patterns verified at runtime through the test matrix. */
static GtkFileFilter *
make_audio_filter(void)
{
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "Supported Audio Files");

    /* MPEG Layer I/II/III */
    gtk_file_filter_add_pattern(f, "*.mp1");
    gtk_file_filter_add_pattern(f, "*.mp2");
    gtk_file_filter_add_pattern(f, "*.mp3");
    gtk_file_filter_add_pattern(f, "*.mpg");
    gtk_file_filter_add_mime_type(f, "audio/mpeg");
    gtk_file_filter_add_mime_type(f, "audio/mp1");
    gtk_file_filter_add_mime_type(f, "audio/mp2");
    gtk_file_filter_add_mime_type(f, "audio/mp3");

    /* WAV */
    gtk_file_filter_add_pattern(f, "*.wav");
    gtk_file_filter_add_pattern(f, "*.wave");
    gtk_file_filter_add_mime_type(f, "audio/wav");
    gtk_file_filter_add_mime_type(f, "audio/x-wav");

    /* AIFF / AIFC */
    gtk_file_filter_add_pattern(f, "*.aif");
    gtk_file_filter_add_pattern(f, "*.aiff");
    gtk_file_filter_add_pattern(f, "*.aifc");
    gtk_file_filter_add_mime_type(f, "audio/aiff");
    gtk_file_filter_add_mime_type(f, "audio/x-aiff");

    return f;
}

static GtkFileFilter *
make_all_files_filter(void)
{
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "All Files");
    gtk_file_filter_add_pattern(f, "*");
    return f;
}

static void
on_open_dialog_finished(GObject *source, GAsyncResult *res, gpointer user_data)
{
    FileDialogRequest *req = user_data;
    Mp3xDriver        *d   = req->d;
    GtkFileDialog     *dlg = GTK_FILE_DIALOG(source);

    /* Bail immediately if shutdown is in progress or state has changed. */
    if (d->shutting_down || req->state_epoch != d->state_epoch) {
        request_finalize(req);
        return;
    }

    GError  *err = NULL;
    GFile   *gfile = gtk_file_dialog_open_finish(dlg, res, &err);

    if (gfile == NULL) {
        /* GTK_DIALOG_ERROR_DISMISSED and GTK_DIALOG_ERROR_CANCELLED are
           silent. Other errors get a modal dialog. */
        if (!g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            mp3x_driver_show_error(d, "Could not open file: %s", err->message);
        }
        g_clear_error(&err);
        request_finalize(req);
        return;
    }
    g_clear_error(&err);

    /* State epoch may have changed during the dialog (Close/replace/Quit).
       Re-check before retiring anything. */
    if (req->state_epoch != d->state_epoch) {
        g_object_unref(gfile);
        request_finalize(req);
        return;
    }

    /* The shared open route. mp3x_session_open handles prevalidation,
       retiring the old session, installing the new one, and bumping
       state_epoch. On failure, leaves the app in the empty state with err. */
    GError *open_err = NULL;
    if (!mp3x_driver_open_file(d, gfile, &open_err)) {
        mp3x_driver_show_error(d, "%s", open_err->message);
        g_clear_error(&open_err);
    }

    g_object_unref(gfile);
    request_finalize(req);
}

static void
act_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    FileDialogRequest *req;
    (void) action; (void) parameter;

    if (d->shutting_down)
        return;

    req = request_new(d, /*is_save=*/FALSE, /*export_kind=*/0);
    if (req == NULL)
        return;

    /* Filters */
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *audio_filter = make_audio_filter();
    GtkFileFilter *all_filter = make_all_files_filter();
    g_list_store_append(filters, audio_filter);
    g_list_store_append(filters, all_filter);
    g_object_unref(audio_filter);
    g_object_unref(all_filter);
    gtk_file_dialog_set_filters(req->dlg, G_LIST_MODEL(filters));
    g_object_unref(filters);

    gtk_file_dialog_set_title(req->dlg, "Open Audio File");

    gtk_file_dialog_open(req->dlg,
                         d->win ? GTK_WINDOW(d->win) : NULL,
                         req->cancellable,
                         on_open_dialog_finished,
                         req);
}


/* ==========================================================================
 * Open Recent
 * ========================================================================== */

/* Register a successfully-opened file with GtkRecentManager. */
static void
mp3x_recent_add(GFile *gfile, const char *content_type)
{
    GtkRecentManager *rm = gtk_recent_manager_get_default();
    GtkRecentData     data;
    gchar            *uri;
    static const gchar *groups[] = { "mp3x", NULL };

    uri = g_file_get_uri(gfile);
    if (uri == NULL)
        return;

    memset(&data, 0, sizeof data);
    data.display_name = NULL;
    data.description  = NULL;
    data.mime_type    = (gchar *)(content_type ? content_type
                                               : "application/octet-stream");
    data.app_name     = (gchar *)"mp3x";
    data.app_exec     = (gchar *)"mp3x %f";
    data.groups       = (gchar **)groups;
    data.is_private   = TRUE;   /* only mp3x-registered items show in our submenu */

    gtk_recent_manager_add_full(rm, uri, &data);
    g_free(uri);
}

/* Build the Open Recent submenu from the recent-manager items that were
   registered by mp3x. Returns NULL if there are none. */
static GMenu *
mp3x_recent_build_submenu(void)
{
    GtkRecentManager *rm = gtk_recent_manager_get_default();
    GList            *items, *it;
    GMenu            *menu = NULL;
    int               count = 0;

    items = gtk_recent_manager_get_items(rm);
    for (it = items; it != NULL; it = it->next) {
        GtkRecentInfo *info = it->data;

        if (!gtk_recent_info_has_application(info, "mp3x") ||
            !gtk_recent_info_has_group(info, "mp3x"))
            goto next;

        /* Prune missing files. */
        if (!gtk_recent_info_exists(info)) {
            gchar *uri = g_strdup(gtk_recent_info_get_uri(info));
            if (uri) {
                gtk_recent_manager_remove_item(rm, uri, NULL);
                g_free(uri);
            }
            goto next;
        }

        if (menu == NULL)
            menu = g_menu_new();

        gchar *display = gtk_recent_info_get_short_name(info);
        const gchar *uri_c = gtk_recent_info_get_uri(info);
        gchar *uri = g_strdup(uri_c);
        GMenuItem *item = g_menu_item_new(display ? display : uri, NULL);
        g_menu_item_set_action_and_target_value(item, "win.recent",
                                                g_variant_new_string(uri));
        g_menu_append_item(menu, item);
        g_object_unref(item);
        g_free(display);
        g_free(uri);

        if (++count >= 10)
            break;

    next:
        ;
    }
    g_list_free_full(items, (GDestroyNotify) gtk_recent_info_unref);
    return menu;
}

/* Rebuild the Open Recent submenu in the menu bar. */
static void
mp3x_recent_refresh(Mp3xDriver *d)
{
    if (d->menubar == NULL)
        return;

    /* Find the File menu, then find the Open Recent item inside it, then
       replace its submenu. The menu was built in build_menubar; we rely on
       its structure being stable. */
    GMenu *file_menu = NULL;
    gint  n_top = g_menu_model_get_n_items(d->menubar);
    for (int i = 0; i < n_top; i++) {
        gchar *label = NULL;
        if (g_menu_model_get_item_attribute(d->menubar, i, G_MENU_ATTRIBUTE_LABEL, "s", &label)) {
            if (g_str_has_prefix(label, "_File") || g_str_has_prefix(label, "File")) {
                GMenuModel *sub = g_menu_model_get_item_link(d->menubar, i, G_MENU_LINK_SUBMENU);
                g_free(label);
                if (sub) {
                    file_menu = G_MENU(sub);
                    break;
                }
                continue;
            }
            g_free(label);
        }
    }
    if (file_menu == NULL)
        return;

    /* Find the "Open Recent" submenu by label. */
    gint n_items = g_menu_model_get_n_items(G_MENU_MODEL(file_menu));
    for (int i = 0; i < n_items; i++) {
        gchar *label = NULL;
        if (g_menu_model_get_item_attribute(G_MENU_MODEL(file_menu), i,
                                            G_MENU_ATTRIBUTE_LABEL, "s", &label)) {
            if (strstr(label, "Open Recent") || strstr(label, "Recent")) {
                GMenuModel *sub = g_menu_model_get_item_link(G_MENU_MODEL(file_menu), i,
                                                              G_MENU_LINK_SUBMENU);
                GMenu *recent_menu = mp3x_recent_build_submenu();
                /* Always keep the "Open Recent" item in the menu. Just clear
                   and repopulate its submenu. Removing the item entirely
                   (as a previous version did) means it can never come back
                   on a first-run system where the recent list starts empty. */
                if (sub) {
                    gint nsub = g_menu_model_get_n_items(sub);
                    while (nsub-- > 0)
                        g_menu_remove(G_MENU(sub), 0);
                    if (recent_menu) {
                        gint nnew = g_menu_model_get_n_items(G_MENU_MODEL(recent_menu));
                        for (int k = 0; k < nnew; k++) {
                            GMenuItem *it = g_menu_item_new_from_model(G_MENU_MODEL(recent_menu), k);
                            g_menu_append_item(G_MENU(sub), it);
                            g_object_unref(it);
                        }
                    }
                }
                g_clear_object(&sub);
                g_clear_object(&recent_menu);
                g_free(label);
                g_object_unref(file_menu);
                return;
            }
            g_free(label);
        }
    }
    g_object_unref(file_menu);
}

static void
act_open_recent(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    const char *uri = g_variant_get_string(parameter, NULL);
    GFile      *gfile;
    GError     *err = NULL;
    (void) action;

    if (d->shutting_down)
        return;

    gfile = g_file_new_for_uri(uri);
    if (!mp3x_driver_open_file(d, gfile, &err)) {
        mp3x_driver_show_error(d, "%s", err->message);
        g_clear_error(&err);
    }
    g_object_unref(gfile);
}


/* ==========================================================================
 * File > Close
 * ========================================================================== */

static void
act_close(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    (void) action; (void) parameter;
    mp3x_driver_close_file(d);
}


/* ==========================================================================
 * File > Save Screenshot and File > Export
 * ========================================================================== */

static const char *
export_suffix(int export_kind)
{
    switch (export_kind) {
    case 0: return "screenshot";
    case 1: return "pcm";
    case 2: return "resynthesis";
    case 3: return "mdct-0";
    case 4: return "mdct-1";
    case 5: return "psy-0";
    case 6: return "psy-1";
    case 7: return "scalefactors-0";
    case 8: return "scalefactors-1";
    default: return "audio";
    }
}

static gchar *
build_default_save_name(Mp3xDriver *d, int export_kind)
{
    const char *display = d->session && d->session->display_name
                          ? d->session->display_name : "audio";
    gchar *stem    = mp3x_sanitize_stem(display);
    gchar *filename = g_strdup_printf("mp3x-%s-%s.png", stem, export_suffix(export_kind));
    g_free(stem);
    return filename;
}

/* State carried through the async save: the kind, the captured session/generation,
   and the chosen target path. Allocated when the save dialog finishes. */
typedef struct {
    Mp3xDriver *d;
    int         export_kind;
    guint64     state_epoch;
    Mp3xSession *session_at_request;
    guint64     generation_at_request;
    gchar      *path;       /* heap target path */
} SaveWriteTask;

static void
save_write_task_free(SaveWriteTask *task)
{
    if (task == NULL) return;
    g_free(task->path);
    g_free(task);
}

/* The actual write, invoked after the dialog returns a path. Runs on the
   main thread because that is where the dialog callback fires. */
static void
perform_save_write(SaveWriteTask *task)
{
    Mp3xDriver *d = task->d;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;

    /* Bail if state has changed under us. */
    if (task->state_epoch != d->state_epoch ||
        task->session_at_request != d->session ||
        (d->session && task->generation_at_request != d->session->generation)) {
        save_write_task_free(task);
        return;
    }

    /* Write the appropriate PNG. Each function is in mp3x_plot.c. */
    switch (task->export_kind) {
    case 0: status = mp3x_plot_composite_write_png(task->path); break;
    case 1: status = mp3x_plot_pcm_write_png(task->path, 600, 150); break;
    case 2: status = mp3x_plot_resynth_write_png(task->path, 600, 150); break;
    case 3: status = mp3x_plot_mdct_write_png(task->path, 0, 300, 150); break;
    case 4: status = mp3x_plot_mdct_write_png(task->path, 1, 300, 150); break;
    case 5: status = mp3x_plot_psy_write_png(task->path, 0, 300, 150); break;
    case 6: status = mp3x_plot_psy_write_png(task->path, 1, 300, 150); break;
    case 7: status = mp3x_plot_sfb_write_png(task->path, 0, 300, 150); break;
    case 8: status = mp3x_plot_sfb_write_png(task->path, 1, 300, 150); break;
    default: status = CAIRO_STATUS_INVALID_STATUS; break;
    }

    if (status != CAIRO_STATUS_SUCCESS)
        mp3x_driver_show_error(d, "Could not write '%s': %s",
                               task->path, cairo_status_to_string(status));

    save_write_task_free(task);
}

static void
on_save_dialog_finished(GObject *source, GAsyncResult *res, gpointer user_data)
{
    FileDialogRequest *req = user_data;
    Mp3xDriver        *d   = req->d;
    GtkFileDialog     *dlg = GTK_FILE_DIALOG(source);

    if (d->shutting_down ||
        req->state_epoch != d->state_epoch ||
        req->session_at_request != d->session ||
        (d->session && req->generation_at_request != d->session->generation)) {
        request_finalize(req);
        return;
    }

    GError *err = NULL;
    GFile  *gfile = gtk_file_dialog_save_finish(dlg, res, &err);

    if (gfile == NULL) {
        if (!g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            mp3x_driver_show_error(d, "Could not save: %s", err->message);
        }
        g_clear_error(&err);
        request_finalize(req);
        return;
    }
    g_clear_error(&err);

    /* Re-check state after the dialog returned. */
    if (req->state_epoch != d->state_epoch ||
        req->session_at_request != d->session) {
        g_object_unref(gfile);
        request_finalize(req);
        return;
    }

    /* Resolve to a filesystem path; only local saves are supported. */
    gchar *path = g_file_get_path(gfile);
    g_object_unref(gfile);
    if (path == NULL) {
        mp3x_driver_show_error(d, "Could not save: selected location has no filesystem path");
        request_finalize(req);
        return;
    }

    /* If the user typed a name with no .png extension, do not silently append
       one - the user's choice is honored. */

    SaveWriteTask *task = g_new0(SaveWriteTask, 1);
    task->d                    = d;
    task->export_kind          = req->export_kind;
    task->state_epoch          = req->state_epoch;
    task->session_at_request   = req->session_at_request;
    task->generation_at_request = req->generation_at_request;
    task->path                 = path;   /* takes ownership */

    perform_save_write(task);

    request_finalize(req);
}

static void
request_start_save(Mp3xDriver *d, int export_kind)
{
    FileDialogRequest *req;
    GtkFileFilter     *png_filter;
    GListStore        *filters;
    gchar             *default_name;

    if (d->shutting_down || d->session == NULL)
        return;

    req = request_new(d, /*is_save=*/TRUE, export_kind);
    if (req == NULL)
        return;

    png_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(png_filter, "PNG image");
    gtk_file_filter_add_pattern(png_filter, "*.png");
    gtk_file_filter_add_mime_type(png_filter, "image/png");

    filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, png_filter);
    gtk_file_dialog_set_filters(req->dlg, G_LIST_MODEL(filters));
    g_object_unref(filters);
    g_object_unref(png_filter);

    default_name = build_default_save_name(d, export_kind);
    gtk_file_dialog_set_initial_name(req->dlg, default_name);
    g_free(default_name);

    gtk_file_dialog_set_title(req->dlg,
                              export_kind == 0 ? "Save Screenshot" : "Export Plot");

    gtk_file_dialog_save(req->dlg,
                         d->win ? GTK_WINDOW(d->win) : NULL,
                         req->cancellable,
                         on_save_dialog_finished,
                         req);
}

static void act_screenshot   (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 0); }
static void act_export_pcm   (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 1); }
static void act_export_resynth(GSimpleAction*a, GVariant*p, gpointer d) { (void)a;(void)p; request_start_save(d, 2); }
static void act_export_mdct0 (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 3); }
static void act_export_mdct1 (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 4); }
static void act_export_psy0  (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 5); }
static void act_export_psy1  (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 6); }
static void act_export_sfb0  (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 7); }
static void act_export_sfb1  (GSimpleAction *a, GVariant *p, gpointer d) { (void)a;(void)p; request_start_save(d, 8); }


/* ==========================================================================
 * The shared GUI session-opening route (File > Open and Open Recent).
 * The initial CLI route is parsed before GTK construction in lame_main().
 * ========================================================================== */

gboolean
mp3x_driver_open_file(Mp3xDriver *d, GFile *gfile, GError **err)
{
    Mp3xPrevalidateResult pre = {0};
    Mp3xSession          *new_session;
    gchar                *content_type;
    gboolean              ok;

    g_return_val_if_fail(d != NULL, FALSE);
    g_return_val_if_fail(G_IS_FILE(gfile), FALSE);

    /* Prevalidate BEFORE retiring the current session. On any failure here
       the previous session is left intact. */
    ok = mp3x_prevalidate(gfile, &pre, err);
    if (!ok)
        return FALSE;
    content_type = g_strdup(pre.content_type);

    /* Retire the current session. */
    if (d->session) {
        Mp3xSession *old = d->session;
        d->session = NULL;
        d->state_epoch++;
        d->analysis_failed |= old->failed;
        mp3x_session_close(old);
        mp3x_session_free(old);
    }

    /* Create the new session and try to open it. */
    new_session = mp3x_session_new(d);
    if (!mp3x_session_open_prevalidated(new_session, d, &pre, err)) {
        /* Open failed. Application is in the empty state. Bump the epoch so
           any in-flight requests against the dead session bail; mark the
           session as failed to preserve the per-session failure flag. */
        d->state_epoch++;
        mp3x_session_free(new_session);
        mp3x_prevalidate_result_clear(&pre);
        g_free(content_type);
        mp3x_driver_refresh_for_empty(d);
        return FALSE;
    }

    d->session = new_session;
    d->state_epoch++;   /* transition: empty/old -> new session */

    /* Add to Open Recent now that the open succeeded. */
    mp3x_recent_add(gfile, content_type);
    g_free(content_type);

    /* Refresh UI for the new session. */
    mp3x_driver_refresh_for_session(d);
    mp3x_recent_refresh(d);

    mp3x_start_session(d);

    return TRUE;
}

/* Production starts in normal Play mode. Explicitly instrumented test builds
   may select a transport route without exposing environment behavior in the
   shipped binary. */
static void
mp3x_start_session(Mp3xDriver *d)
{
    if (d->session == NULL)
        return;

#ifdef MP3X_ENABLE_TEST_HOOKS
    if (g_getenv("MP3X_STEP_TO_END")) {
        /* -2 is test-build-only: one transition per idle dispatch while the
           visible transport remains paused, equivalent to repeated Step. */
        d->session->running = FALSE;
        d->session->advance_left = -2;
        mp3x_idle_step_install(d);
        return;
    }
    if (g_getenv("MP3X_ADVANCE_TO_END")) {
        d->session->running = TRUE;
        d->session->advance_left = -1;
        mp3x_idle_step_install(d);
        return;
    }
    if (g_getenv("MP3X_START_PAUSED")) {
        d->session->running = FALSE;
        mp3x_do_one_frame(d);   /* show frame 1, then wait */
        return;
    }
#endif
    d->session->running = TRUE;
    mp3x_idle_step_install(d);
}

static void
mp3x_driver_close_file(Mp3xDriver *d)
{
    if (d->session == NULL)
        return;

    /* Cancel save/export in flight - the file is going away. */
    if (d->save_request)
        g_cancellable_cancel(d->save_request->cancellable);

    Mp3xSession *old = d->session;
    d->session = NULL;
    d->state_epoch++;
    d->analysis_failed |= old->failed;
    mp3x_session_close(old);
    mp3x_session_free(old);

    mp3x_driver_refresh_for_empty(d);
    mp3x_recent_refresh(d);
}


/* ==========================================================================
 * Error display
 * ========================================================================== */

static void
mp3x_driver_show_error(Mp3xDriver *d, const char *fmt, ...)
{
    gchar     *msg;
    va_list    ap;
    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    if (d->win) {
        GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", msg);
        gtk_alert_dialog_set_buttons(dlg, (const char *[]){"_OK", NULL});
        if (d->win) gtk_alert_dialog_show(dlg, d->win);
        g_object_unref(dlg);
    } else {
        /* Window not yet built; fall back to console. */
        fprintf(stderr, "mp3x: %s\n", msg);
    }
    g_free(msg);
}


/* ==========================================================================
 * Test-harness env hooks
 *
 * Off by default; set MP3X_QUIT_WHEN_DONE to fire a clean shutdown once the
 * analyzer reaches end-of-input (used by the headless test matrix), and
 * MP3X_SHOT* to write the indicated graph PNGs at the same point. These
 * are NOT user-facing; the user-facing equivalents are File > Save
 * Screenshot and File > Export.
 * ========================================================================== */

#ifdef MP3X_ENABLE_TEST_HOOKS
static void
maybe_autoquit(Mp3xDriver *d)
{
    if (g_getenv("MP3X_QUIT_WHEN_DONE"))
        mp3x_driver_request_shutdown(d);
}

static void
maybe_shot(Mp3xDriver *d)
{
    const char *path  = g_getenv("MP3X_SHOT");
    const char *rpath = g_getenv("MP3X_SHOT_RESYNTH");
    const char *mpath = g_getenv("MP3X_SHOT_MDCT");
    const char *mpath1 = g_getenv("MP3X_SHOT_MDCT1");
    const char *ppath = g_getenv("MP3X_SHOT_PSY");
    const char *ppath1 = g_getenv("MP3X_SHOT_PSY1");
    const char *spath = g_getenv("MP3X_SHOT_SFB");
    const char *spath1 = g_getenv("MP3X_SHOT_SFB1");
    const char *cpath = g_getenv("MP3X_SHOT_COMPOSITE");
    const char *report_path = g_getenv("MP3X_STATE_REPORT");
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
#define TEST_SHOT(call) do { \
        cairo_status_t shot_status_ = (call); \
        if (status == CAIRO_STATUS_SUCCESS) status = shot_status_; \
    } while (0)
    if (path)  TEST_SHOT(mp3x_plot_pcm_write_png(path, 600, 150));
    if (rpath) TEST_SHOT(mp3x_plot_resynth_write_png(rpath, 600, 150));
    if (mpath) TEST_SHOT(mp3x_plot_mdct_write_png(mpath, 0, 300, 150));
    if (mpath1) TEST_SHOT(mp3x_plot_mdct_write_png(mpath1, 1, 300, 150));
    if (ppath) TEST_SHOT(mp3x_plot_psy_write_png(ppath, 0, 300, 150));
    if (ppath1) TEST_SHOT(mp3x_plot_psy_write_png(ppath1, 1, 300, 150));
    if (spath) TEST_SHOT(mp3x_plot_sfb_write_png(spath, 0, 300, 150));
    if (spath1) TEST_SHOT(mp3x_plot_sfb_write_png(spath1, 1, 300, 150));
    if (cpath) TEST_SHOT(mp3x_plot_composite_write_png(cpath));
#undef TEST_SHOT
    if (status != CAIRO_STATUS_SUCCESS) {
        d->analysis_failed = TRUE;
        fprintf(stderr, "mp3x test export: %s\n",
                cairo_status_to_string(status));
    }
    if (report_path && d->session && pdisp) {
        int gr, ch, mainbits_sum = 0;
        int i, j, resynth_companion_offset = -1;
        double resynth_peak = 0.0;
        gchar *report;

        for (gr = 0; gr < 2; gr++)
            for (ch = 0; ch < 2; ch++)
                mainbits_sum += pdisp->mainbits[gr][ch];
        for (i = 1; i <= MAXMPGLAG; i++) {
            plotting_data *decoded = pdisp - i;

            if (decoded->frameNum123 != pdisp->frameNum)
                continue;
            resynth_companion_offset = i;
            for (ch = 0; ch < pdisp->stereo; ch++) {
                plotting_data *previous = decoded + 1;

                for (j = 1152 - 224; j < 1152; j++) {
                    double const value = previous->pcmdata2[ch][j];
                    double const magnitude = value < 0.0 ? -value : value;
                    if (magnitude > resynth_peak)
                        resynth_peak = magnitude;
                }
                for (j = 0; j < 1152; j++) {
                    double const value = decoded->pcmdata2[ch][j];
                    double const magnitude = value < 0.0 ? -value : value;
                    if (magnitude > resynth_peak)
                        resynth_peak = magnitude;
                }
            }
            break;
        }
        report = g_strdup_printf(
            "frames_done=%d\npdisp_frame=%d\ncompleted=%d\n"
            "drain_remaining=%d\ntotbits=%d\nmainbits_sum=%d\n"
            "resynth_companion_offset=%d\nresynth_peak=%.17g\n",
            d->session->frames_done, pdisp->frameNum,
            d->session->completed, d->session->drain_remaining,
            pdisp->totbits, mainbits_sum,
            resynth_companion_offset, resynth_peak);
        g_file_set_contents(report_path, report, -1, NULL);
        g_free(report);
    }
}
#else
static void maybe_autoquit(Mp3xDriver *d) { (void) d; }
static void maybe_shot(Mp3xDriver *d) { (void) d; }
#endif


/* ==========================================================================
 * Idle source - the analyzer's frame-stepping source
 * ========================================================================== */

/* The shared "do exactly one frame of analysis" body. Returns TRUE if the
   caller should keep going (running and not at end), FALSE if it should stop.
   Used by both the idle callback and the Step button. Safe to call directly
   from any main-thread handler; it does NOT touch GLib source state directly,
   only session state via the driver. */
static gboolean
mp3x_do_one_frame(Mp3xDriver *d)
{
    Mp3xSession *s = d->session;
    gboolean queued;

    if (s == NULL || s->failed || s->completed)
        return FALSE;

    if (s->input_exhausted) {
        g_assert(s->drain_remaining > 0);
        mp3x_core_drain_step();
        s->drain_remaining--;
    } else {
        int n;

#ifdef MP3X_ENABLE_TEST_HOOKS
        const char *fail_after = g_getenv("MP3X_FAIL_AFTER");
        if (fail_after != NULL && s->frames_done >= (int) g_ascii_strtoll(fail_after, NULL, 10))
            n = LAME_INTERNALERROR;
        else
#endif
            n = mp3x_core_step(s->gf);

        if (n < 0) {
            s->failed = TRUE;
            s->running = FALSE;
            d->analysis_failed = TRUE;
            mp3x_driver_refresh_for_session(d);
            maybe_autoquit(d);
            return FALSE;
        }
        if (n == 0) {
            /* The EOF core step shifted once and fed LAME's encoder flush to
               HIP. Drain steps collect only decoder output already queued by
               that flush, then use sentinels; no more input/stat work occurs. */
            s->input_exhausted = TRUE;
            s->drain_remaining = READ_AHEAD - 1;
        } else {
            s->frames_done++;
        }
    }

    if (s->input_exhausted && s->drain_remaining == 0) {
        s->completed = TRUE;
        s->running = FALSE;
    }

    /* Redraw every iteration unless we are mid-advance without
       plot_advancing, in which case we redraw only on completion. */
    queued = (s->advance_left != 0);
    if (!queued || d->plot_advancing || s->completed) {
        mp3x_driver_refresh_for_session(d);
    }

    if (s->completed) {
        maybe_shot(d);
        maybe_autoquit(d);
        return FALSE;
    }

    /* Advance countdown. */
    if (s->advance_left > 0) {
        s->advance_left--;
        if (s->advance_left == 0) {
            s->running = FALSE;
            if (!d->plot_advancing)
                mp3x_driver_refresh_for_session(d);
            return FALSE;
        }
    }
    /* advance_left == -1 means run to end; do not decrement. */

    return TRUE;
}

/* The testable body. Pure function of the captured IdleContext + the driver's
   current state. Safe to call with a synthetic context that GLib never owns. */
gboolean
mp3x_idle_step(gpointer data)
{
    IdleContext *ctx = data;
    Mp3xDriver  *d   = ctx->d;

    if (d->shutting_down)              return G_SOURCE_REMOVE;
    if (d->session == NULL)            return G_SOURCE_REMOVE;
    if (d->session != ctx->session_at_install) return G_SOURCE_REMOVE;
    if (d->session->generation != ctx->generation_at_install) return G_SOURCE_REMOVE;

    /* Pause guard lives here (in the idle callback), NOT in
       mp3x_do_one_frame, so that on_step can call mp3x_do_one_frame
       directly to advance exactly one frame while paused. */
    if (!d->session->running && d->session->advance_left == 0)
        return G_SOURCE_REMOVE;

    gboolean more = mp3x_do_one_frame(d);
    if (!more) {
        /* We are being asked to stop. Clear our idle_id on the session so
           the next do_play can install a fresh source. */
        Mp3xSession *s = d->session;
        if (s && s->idle_id != 0)
            s->idle_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean
on_idle(gpointer data)
{
    return mp3x_idle_step(data);
}

static void
on_idle_destroy(gpointer data)
{
    IdleContext *ctx = data;
    /* Drop the strong driver ref captured at install time. Done AFTER the
       context no longer needs any driver field. */
    mp3x_driver_unref(ctx->d);
    g_free(ctx);
}

/* Install the idle source for the current session, capturing (driver, session,
   generation) at install time. */
static void
mp3x_idle_step_install(Mp3xDriver *d)
{
    if (d->session == NULL)
        return;
    if (d->session->idle_id != 0)
        return;

    IdleContext *ctx = g_new0(IdleContext, 1);
    ctx->d                  = mp3x_driver_ref(d);
    ctx->session_at_install = d->session;
    ctx->generation_at_install = d->session->generation;

    d->session->idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                           on_idle, ctx, on_idle_destroy);
}


/* ==========================================================================
 * Transport handlers
 *
 * Reach the per-file session through d->session; safe-NULL if no file.
 * ========================================================================== */

static void
do_play(Mp3xDriver *d)
{
    Mp3xSession *s = d->session;
    if (s == NULL || s->failed || s->completed || s->running)
        return;
    s->advance_left = 0;
    s->running = TRUE;
    if (s->idle_id == 0)
        mp3x_idle_step_install(d);
    update_transport(d);
}

static void
do_advance(Mp3xDriver *d, int n)
{
    Mp3xSession *s = d->session;
    if (s == NULL || s->failed || s->completed)
        return;
    if (n > 0 && s->advance_left > 0)
        s->advance_left += n;
    else
        s->advance_left = n;
    s->running = TRUE;
    if (s->idle_id == 0)
        mp3x_idle_step_install(d);
    update_transport(d);
}

static void
do_pause(Mp3xDriver *d)
{
    Mp3xSession *s = d->session;
    if (s == NULL || !s->running)
        return;
    s->running = FALSE;
    /* Actually stop the idle source so the analyzer halts immediately.
       Without this the source keeps firing on every main-loop iteration
       and the running flag is silently ignored. */
    if (s->idle_id) {
        g_source_remove(s->idle_id);
        s->idle_id = 0;
    }
    update_transport(d);
}

static void
on_playpause(GtkButton *btn, gpointer data)
{
    Mp3xDriver *d = data; (void) btn;
    Mp3xSession *s = d->session;
    if (s == NULL) return;
    if (s->running) do_pause(d); else do_play(d);
}

static void
on_step(GtkButton *btn, gpointer data)
{
    Mp3xDriver *d = data; (void) btn;
    Mp3xSession *s = d->session;
    if (s == NULL || s->running) return;
    if (mp3x_core_disp_backpos() > 0) {
        mp3x_core_disp_fwd();
        mp3x_driver_refresh_for_session(d);
    } else if (!s->failed && !s->completed) {
        /* Step exactly one frame manually. Don't go through the idle
           callback wrapper - call the shared body directly. */
        mp3x_do_one_frame(d);
    }
}

static void
on_back(GtkButton *btn, gpointer data)
{
    Mp3xDriver *d = data; (void) btn;
    Mp3xSession *s = d->session;
    if (s == NULL) return;
    if (s->running) {
        do_pause(d);
        if (s->idle_id) {
            g_source_remove(s->idle_id);
            s->idle_id = 0;
        }
    }
    if (mp3x_core_disp_back())
        mp3x_driver_refresh_for_session(d);
}


/* --------------------------------------------------------------------------
 * Display-option actions (channel / source / difference / sfb / subblock /
 * spectrum / plot-advancing / fullscreen). All operate on driver-lifetime
 * view preferences plus per-session source.
 * -------------------------------------------------------------------------- */

static void
activate_radio(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void) data;
    g_action_change_state(G_ACTION(action), parameter);
}

static void
activate_toggle(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    GVariant *state = g_action_get_state(G_ACTION(action));
    (void) parameter; (void) data;
    g_action_change_state(G_ACTION(action),
                          g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_variant_unref(state);
}

static void
mp3x_apply_display(Mp3xDriver *d)
{
    int source = d->session ? d->session->source : 0;
    mp3x_plot_set_options(d->channel, d->ms, d->difference, source);
    mp3x_plot_set_sfblines(d->sfblines);
    mp3x_plot_set_kbflag(d->kbflag);
    mp3x_plot_set_subblock(d->subblock < 0 || d->subblock == 0,
                           d->subblock < 0 || d->subblock == 1,
                           d->subblock < 0 || d->subblock == 2);
}

static void
change_channel_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    const char *s = g_variant_get_string(state, NULL);
    int view = (s[0] == 'R') ? 1 : (s[0] == 'M') ? 2 : (s[0] == 'S') ? 3 : 0;
    d->ms      = view / 2;
    d->channel = view % 2;
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (d->session) mp3x_driver_refresh_for_session(d);
}

static void
change_source_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    Mp3xSession *s = d->session;
    if (s) {
        s->source = g_str_equal(g_variant_get_string(state, NULL), "mpg123") ? 1 : 0;
    }
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (s) mp3x_driver_refresh_for_session(d);
}

static void
change_diff_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    d->difference = g_variant_get_boolean(state);
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (d->session) mp3x_driver_refresh_for_session(d);
}

static void
change_sfbline_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    d->sfblines = g_variant_get_boolean(state);
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (d->session) mp3x_driver_refresh_for_session(d);
}

static void
change_subblock_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    const char *v = g_variant_get_string(state, NULL);
    d->subblock = g_str_equal(v, "all") ? -1 : (int)(v[0] - '0');
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (d->session) mp3x_driver_refresh_for_session(d);
}

static void
change_spectrum_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    d->kbflag = g_str_equal(g_variant_get_string(state, NULL), "wave");
    g_simple_action_set_state(action, state);
    mp3x_apply_display(d);
    if (d->session) mp3x_driver_refresh_for_session(d);
}

static void
change_plotadv_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    d->plot_advancing = g_variant_get_boolean(state);
    g_simple_action_set_state(action, state);
}

static void
change_fullscreen_state(GSimpleAction *action, GVariant *state, gpointer data)
{
    Mp3xDriver *d = data;
    if (d->win == NULL) return;
    if (g_variant_get_boolean(state))
        gtk_window_fullscreen(d->win);
    else
        gtk_window_unfullscreen(d->win);
    g_simple_action_set_state(action, state);
}


/* --------------------------------------------------------------------------
 * Menu transport wrappers - reuse the button handlers (which ignore button).
 * -------------------------------------------------------------------------- */

static void act_playpause (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;on_playpause(NULL,d);}
static void act_step      (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;on_step(NULL,d);}
static void act_back      (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;on_back(NULL,d);}
static void act_adv10     (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;do_advance(d,10);}
static void act_adv100    (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;do_advance(d,100);}
static void act_last      (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;do_advance(d,-1);}
static void act_play      (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;do_play(d);}
static void act_pause     (GSimpleAction *a, GVariant *p, gpointer d){(void)a;(void)p;do_pause(d);}


/* --------------------------------------------------------------------------
 * Tools > Statistics and Help > Documentation/About
 * -------------------------------------------------------------------------- */

static void
act_stats(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    const Mp3xStats *s = mp3x_core_stats();
    GtkWidget       *win, *label;
    char            *text;
    (void) action; (void) parameter;

    text = g_strdup_printf(
        "frames processed so far: %d\n"
        "granules processed so far: %d\n\n"
        "mean bits/frame (approximate): %d\n"
        "mean bits/frame (from LAME): %d\n"
        "bitsize of largest frame: %d\n"
        "average bits/frame: %3.1f\n\n"
        "ms_stereo frames: %d\n"
        "i_stereo frames: %d\n"
        "de-emphasis frames: %d\n"
        "short block granules: %d\n"
        "mixed block granules: %d\n"
        "preflag granules: %d",
        s->frames, 4 * s->frames,
        s->approxbits, s->mean_bits, s->maxbits, s->avebits,
        s->totms, s->totis, s->totemph,
        s->totshort, s->totmix, s->totpreflag);

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Statistics");
    gtk_window_set_default_size(GTK_WINDOW(win), 350, 260);
    if (d->win)
        gtk_window_set_transient_for(GTK_WINDOW(win), d->win);

    label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_can_focus(label, FALSE);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_window_set_child(GTK_WINDOW(win), label);
    gtk_window_present(GTK_WINDOW(win));
    g_free(text);
}

static const char mp3x_documentation[] =
    "Frame header information: "
    "First the bitrate, sampling frequency and mono, stereo or jstereo "
    "indicators are displayed.  If the bitstream is jstereo, then mid/side "
    "stereo or intensity stereo may be on (indicated in red).  If "
    "de-emphasis is used, this is also indicated in red.  The mdb value is "
    "main_data_begin.  The encoded data starts this many bytes *before* the "
    "frame header.  A large value of mdb means the bitstream has saved some "
    "bits into the reservoir, which it may allocate for some future frame. "
    "The two numbers after mdb are the size (in bits) used to encode the "
    "MDCT coefficients for this frame, followed by the size of the bit "
    "reservoir before encoding this frame.  The maximum frame size and a "
    "running average are given in the Tools > Statistics window.  A large "
    "maximum frame size indicates the bitstream has made use of the bit "
    "reservoir.\n\n"

    "PCM data (top graph): "
    "The PCM data is plotted in black.  The layer3 frame is divided into 2 "
    "granules of 576 samples (marked with yellow vertical lines).  In the "
    "case of normal, start and stop blocks, the MDCT coefficients for each "
    "granule are computed using a 1152 sample window centered over the "
    "granule.  In the case of short blocks, the granule is further divided "
    "into 3 blocks of 192 samples (also marked with yellow vertical lines)."
    "The MDCT coefficients for these blocks are computed using 384 sample "
    "windows centered over the 192 sample window.  (This info not available "
    "when analyzing .mp3 files.)  For the psycho-acoustic model, a windowed "
    "FFT is computed for each granule.  The range of these windows is "
    "denoted by the blue and green bars.\n\n"

    "PCM re-synthesis data (second graph): "
    "Same as the PCM window described above.  The data displayed is the "
    "result of encoding and then decoding the original sample.\n\n"

    "MDCT windows: "
    "Shows the energy in the MDCT spectrum for granule 0 (left window) "
    "and granule 1 (right window).  The text also shows the blocktype "
    "used, the number of bits used to encode the coefficients and the "
    "number of extra bits allocated from the reservoir.  The Src control "
    "will toggle between the original unquantized MDCT coefficients "
    "and the compressed (quantized) coefficients.\n\n"

    "FFT window: "
    "The gray bars show the energy in the FFT spectrum used by the "
    "psycho-acoustic model.  Granule 0 is in the left window, granule 1 in "
    "the right window.  The green and blue bars show how much distortion is "
    "allowable, as computed by the psycho-acoustic model. The red bars show "
    "the actual distortion after encoding.  There is one FFT for each "
    "granule, computed with a 1024 Hann window centered over the "
    "appropriate granule.  (The range of this 1024 sample window is shown "
    "by the blue and green bars in the PCM data window.)  The Analysis > "
    "Spectrum menu will toggle between showing the energy in equally spaced "
    "frequency domain and the scale factor bands used by layer3.  Finally, "
    "the perceptual entropy, total energy and number of scalefactor bands "
    "with audible distortion is shown.  (This info not available when "
    "analyzing .mp3 files.)";

static void
act_docs(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    GtkWidget *win, *scroller, *label;
    (void) action; (void) parameter;

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Documentation");
    gtk_window_set_default_size(GTK_WINDOW(win), 450, 500);
    if (d->win)
        gtk_window_set_transient_for(GTK_WINDOW(win), d->win);

    label = gtk_label_new(mp3x_documentation);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_can_focus(label, FALSE);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);

    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), label);
    gtk_window_set_child(GTK_WINDOW(win), scroller);
    gtk_window_present(GTK_WINDOW(win));
}

static void
act_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    GtkAlertDialog *dlg = gtk_alert_dialog_new("mp3x");
    char        *detail, *decoder;
    (void) action; (void) parameter;

#ifdef HAVE_MPG123
    decoder = g_strdup_printf("decoder:  libmpg123 %s\n"
                              "Michael Hipp (www.mpg123.de)\n\n",
                              mpg123_distversion(NULL, NULL, NULL));
#else
    decoder = g_strdup("");
#endif

    detail = g_strdup_printf(
        "LAME %s\n%s\n\n"
        "psycho-acoustic model:  GPSYCHO version %s\n\n"
        "%s"
        "Encoder, decoder & psy-models based on ISO\n"
        "demonstration source.",
        get_lame_version(), get_lame_url(), get_psy_version(), decoder);
    g_free(decoder);
    gtk_alert_dialog_set_detail(dlg, detail);
    if (d->win) gtk_alert_dialog_show(dlg, d->win);
    g_free(detail);
    g_object_unref(dlg);
}

static void
act_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    (void) action; (void) parameter;
    mp3x_driver_request_shutdown(d);
}


/* ==========================================================================
 * Action tables
 * ========================================================================== */

static const GActionEntry mp3x_win_actions[] = {
    /* File menu */
    { .name = "open",      .activate = act_open },
    { .name = "recent",    .activate = act_open_recent, .parameter_type = "s" },
    { .name = "close",     .activate = act_close },
    { .name = "screenshot", .activate = act_screenshot },
    { .name = "export-pcm",     .activate = act_export_pcm },
    { .name = "export-resynth", .activate = act_export_resynth },
    { .name = "export-mdct-0",  .activate = act_export_mdct0 },
    { .name = "export-mdct-1",  .activate = act_export_mdct1 },
    { .name = "export-psy-0",   .activate = act_export_psy0 },
    { .name = "export-psy-1",   .activate = act_export_psy1 },
    { .name = "export-sfb-0",   .activate = act_export_sfb0 },
    { .name = "export-sfb-1",   .activate = act_export_sfb1 },

    /* Transport */
    { .name = "playpause", .activate = act_playpause },
    { .name = "play",      .activate = act_play },
    { .name = "pause",     .activate = act_pause },
    { .name = "step",      .activate = act_step },
    { .name = "back",      .activate = act_back },
    { .name = "adv10",     .activate = act_adv10 },
    { .name = "adv100",    .activate = act_adv100 },
    { .name = "last",      .activate = act_last },

    /* Stateful display options */
    { .name = "plotadv",    .activate = activate_toggle,
      .state = "false",     .change_state = change_plotadv_state },
    { .name = "spectrum",   .activate = activate_radio, .parameter_type = "s",
      .state = "'bands'",   .change_state = change_spectrum_state },
    { .name = "subblock",   .activate = activate_radio, .parameter_type = "s",
      .state = "'all'",     .change_state = change_subblock_state },
    { .name = "channel",    .activate = activate_radio, .parameter_type = "s",
      .state = "'L'",       .change_state = change_channel_state },
    { .name = "source",     .activate = activate_radio, .parameter_type = "s",
      .state = "'lame'",    .change_state = change_source_state },
    { .name = "diff",       .activate = activate_toggle,
      .state = "false",     .change_state = change_diff_state },
    { .name = "sfblines",   .activate = activate_toggle,
      .state = "true",      .change_state = change_sfbline_state },
    { .name = "fullscreen", .activate = activate_toggle,
      .state = "false",     .change_state = change_fullscreen_state },
};

static const GActionEntry mp3x_app_actions[] = {
    { .name = "quit",  .activate = act_quit },
    { .name = "about", .activate = act_about },
    { .name = "docs",  .activate = act_docs },
    { .name = "stats", .activate = act_stats },
};


/* ==========================================================================
 * Menu builder
 *
 * On macOS, Quit and About are lifted by GTK into the application menu
 * automatically from app.quit and app.about, so we do not repeat them in
 * File/Help. File itself is NOT suppressed on macOS - it carries Open,
 * Open Recent, Close, Save Screenshot, Export.
 * ========================================================================== */

static void
menu_append_radio(GMenu *menu, const char *label, const char *action, const char *target)
{
    GMenuItem *it = g_menu_item_new(label, NULL);
    g_menu_item_set_action_and_target_value(it, action, g_variant_new_string(target));
    g_menu_append_item(menu, it);
    g_object_unref(it);
}

static GMenuModel *
build_menubar(gboolean native_app_menu)
{
    GMenu *bar = g_menu_new();
    GMenu *m, *sub, *sub2;

    /* ---- File ---- */
    m = g_menu_new();
    g_menu_append(m, "Open Audio File…", "win.open");

    /* Open Recent placeholder - submenu is populated/refreshed at runtime. */
    sub = g_menu_new();
    g_menu_append_submenu(m, "Open Recent", G_MENU_MODEL(sub));
    g_object_unref(sub);

    g_menu_append(m, "Close", "win.close");
    g_menu_append(m, "Save Screenshot…", "win.screenshot");

    sub = g_menu_new();                                  /* Export ▸ */
    g_menu_append(sub, "PCM Waveform…",       "win.export-pcm");
    g_menu_append(sub, "Re-synthesis…",       "win.export-resynth");

    sub2 = g_menu_new();                                  /* MDCT ▸ */
    g_menu_append(sub2, "Granule 0…", "win.export-mdct-0");
    g_menu_append(sub2, "Granule 1…", "win.export-mdct-1");
    g_menu_append_submenu(sub, "MDCT", G_MENU_MODEL(sub2));
    g_object_unref(sub2);

    sub2 = g_menu_new();                                  /* FFT/Psy ▸ */
    g_menu_append(sub2, "Granule 0…", "win.export-psy-0");
    g_menu_append(sub2, "Granule 1…", "win.export-psy-1");
    g_menu_append_submenu(sub, "FFT / Psychoacoustic", G_MENU_MODEL(sub2));
    g_object_unref(sub2);

    sub2 = g_menu_new();                                  /* Scalefactors ▸ */
    g_menu_append(sub2, "Granule 0…", "win.export-sfb-0");
    g_menu_append(sub2, "Granule 1…", "win.export-sfb-1");
    g_menu_append_submenu(sub, "Scalefactors", G_MENU_MODEL(sub2));
    g_object_unref(sub2);

    g_menu_append_submenu(m, "Export", G_MENU_MODEL(sub));
    g_object_unref(sub);

    if (!native_app_menu)
        g_menu_append(m, "Quit", "app.quit");

    g_menu_append_submenu(bar, "_File", G_MENU_MODEL(m));
    g_object_unref(m);

    /* ---- Analysis ---- */
    m = g_menu_new();
    sub = g_menu_new();
    menu_append_radio(sub, "Left",  "win.channel", "L");
    menu_append_radio(sub, "Right", "win.channel", "R");
    menu_append_radio(sub, "Mid",   "win.channel", "M");
    menu_append_radio(sub, "Side",  "win.channel", "S");
    g_menu_append_submenu(m, "Channel", G_MENU_MODEL(sub));
    g_object_unref(sub);

    sub = g_menu_new();
    menu_append_radio(sub, "LAME Encoder",   "win.source", "lame");
    menu_append_radio(sub, "mpg123 Decoder", "win.source", "mpg123");
    g_menu_append_submenu(m, "Source", G_MENU_MODEL(sub));
    g_object_unref(sub);

    g_menu_append(m, "Difference Mode", "win.diff");

    sub = g_menu_new();                                   /* Scalefactor Bands ▸ */
    g_menu_append(sub, "Show Band Lines", "win.sfblines");
    sub2 = g_menu_new();
    menu_append_radio(sub2, "All Windows", "win.subblock", "all");
    menu_append_radio(sub2, "Window 1",    "win.subblock", "0");
    menu_append_radio(sub2, "Window 2",    "win.subblock", "1");
    menu_append_radio(sub2, "Window 3",    "win.subblock", "2");
    g_menu_append_submenu(sub, "Short Blocks", G_MENU_MODEL(sub2));
    g_object_unref(sub2);
    g_menu_append_submenu(m, "Scalefactor Bands", G_MENU_MODEL(sub));
    g_object_unref(sub);

    sub = g_menu_new();
    menu_append_radio(sub, "Scalefactor Bands", "win.spectrum", "bands");
    menu_append_radio(sub, "Wave Number",        "win.spectrum", "wave");
    g_menu_append_submenu(m, "Spectrum", G_MENU_MODEL(sub));
    g_object_unref(sub);

    /* Statistics sits under Analysis (it is analysis data) with a separator
       above it, so a single-item section doesn't leave the menu looking
       half-empty. */
    {
        GMenu *stats_section = g_menu_new();
        g_menu_append(stats_section, "Statistics", "app.stats");
        g_menu_append_section(m, NULL, G_MENU_MODEL(stats_section));
        g_object_unref(stats_section);
    }

    g_menu_append_submenu(bar, "_Analysis", G_MENU_MODEL(m));
    g_object_unref(m);

    /* ---- View ---- */
    m = g_menu_new();
    g_menu_append(m, "Plot While Advancing", "win.plotadv");
    g_menu_append(m, "Fullscreen",           "win.fullscreen");
    g_menu_append_submenu(bar, "_View", G_MENU_MODEL(m));
    g_object_unref(m);

    /* ---- Transport ---- */
    m = g_menu_new();
    g_menu_append(m, "Play",              "win.play");
    g_menu_append(m, "Pause",             "win.pause");
    g_menu_append(m, "Step Forward",      "win.step");
    g_menu_append(m, "Step Back",         "win.back");
    g_menu_append(m, "Advance 10 Frames", "win.adv10");
    g_menu_append(m, "Advance 100 Frames","win.adv100");
    g_menu_append(m, "Last Frame",        "win.last");
    g_menu_append_submenu(bar, "_Transport", G_MENU_MODEL(m));
    g_object_unref(m);

    /* ---- Help ---- */
    m = g_menu_new();
    g_menu_append(m, "mp3x Manual", "app.docs");
    if (!native_app_menu)
        g_menu_append(m, "About mp3x", "app.about");
    g_menu_append_submenu(bar, "_Help", G_MENU_MODEL(m));
    g_object_unref(m);

    return G_MENU_MODEL(bar);
}


/* ==========================================================================
 * CSS for non-composited X11 (preserved from the v2 patch)
 * ========================================================================== */

static void
install_noncomposited_menu_css(GdkDisplay *display)
{
    GtkCssProvider *css;

    if (display == NULL || gdk_display_is_composited(display))
        return;

    css = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(css,
        "popover.menu { border: none; box-shadow: none; }\n"
        "popover.menu > contents { border: none; box-shadow: none; }\n");
#else
    gtk_css_provider_load_from_data(css,
        "popover.menu { border: none; box-shadow: none; }\n"
        "popover.menu > contents { border: none; box-shadow: none; }\n", -1);
#endif
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_message("mp3x: display not composited - applied menu CSS fallback");
}


/* ==========================================================================
 * Refresh helpers
 * ========================================================================== */

/* Frame-header line: bitrate/mode/flags/bv/scfsi/mdb. Reads pdisp. */
static void
set_header(GtkWidget *label, int channel)
{
    plotting_data *p = pdisp;
    const int   ch = channel;
    const char *mode, *ON = "#d01010", *OFF = "#909090";
    char        buf[512];

    if (p == NULL || p->sampfreq == 0) {
        gtk_label_set_text(GTK_LABEL(label), "");
        return;
    }
    mode = (p->stereo == 2) ? (p->js ? "js" : "stereo") : "mono";
    g_snprintf(buf, sizeof buf,
        "<tt>%.1f kHz  %d kbps  %s   "
        "<span foreground=\"%s\">ms</span> "
        "<span foreground=\"%s\">is</span> "
        "<span foreground=\"%s\">crc</span> "
        "<span foreground=\"%s\">pad</span> "
        "<span foreground=\"%s\">em</span>    "
        "bv=%d,%d  scfsi=%d  mdb=%d  %d/%d</tt>",
        p->sampfreq / 1000.0, p->bitrate, mode,
        p->ms_stereo ? ON : OFF,
        p->i_stereo  ? ON : OFF,
        p->crc       ? ON : OFF,
        p->padding   ? ON : OFF,
        p->emph      ? ON : OFF,
        p->big_values[0][ch], p->big_values[1][ch],
        p->scfsi[ch],
        p->maindata, p->totbits, p->totbits + p->resvsize);
    gtk_label_set_markup(GTK_LABEL(label), buf);
}

static void
redraw_all(Mp3xDriver *d)
{
    if (d->plot)          gtk_widget_queue_draw(d->plot);
    if (d->plot_resynth)  gtk_widget_queue_draw(d->plot_resynth);
    if (d->plot_mdct[0])  gtk_widget_queue_draw(d->plot_mdct[0]);
    if (d->plot_mdct[1])  gtk_widget_queue_draw(d->plot_mdct[1]);
    if (d->plot_psy[0])   gtk_widget_queue_draw(d->plot_psy[0]);
    if (d->plot_psy[1])   gtk_widget_queue_draw(d->plot_psy[1]);
    if (d->plot_sfb[0])   gtk_widget_queue_draw(d->plot_sfb[0]);
    if (d->plot_sfb[1])   gtk_widget_queue_draw(d->plot_sfb[1]);
}

static void
set_status_readout(Mp3xDriver *d)
{
    plotting_data *p = pdisp;
    const char    *mode;
    char           buf[160];

    if (d->status == NULL)
        return;

    if (d->session == NULL) {
        gtk_label_set_text(GTK_LABEL(d->status), "No file loaded");
        if (d->progress)
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress), 0.0);
        return;
    }

    if (d->session->failed) {
        gtk_label_set_text(GTK_LABEL(d->status), "Analysis failed");
        return;
    }

    if (p == NULL || p->sampfreq == 0) {
        gtk_label_set_text(GTK_LABEL(d->status),
                           d->session->input_exhausted
                           ? "Finishing analysis…" : "Loading…");
        return;
    }

    mode = (p->stereo == 2) ? (p->js ? "Joint Stereo" : "Stereo") : "Mono";
    if (d->session->completed)
        g_snprintf(buf, sizeof buf, "Frame %d / %d  %.2fs\n%.1f kHz\n%d kbps\n%s",
                   p->frameNum, d->session->frames_done, p->frametime,
                   p->sampfreq / 1000.0, p->bitrate, mode);
    else
        g_snprintf(buf, sizeof buf, "Frame %d / ?  %.2fs\n%.1f kHz\n%d kbps\n%s",
                   p->frameNum, p->frametime,
                   p->sampfreq / 1000.0, p->bitrate, mode);
    gtk_label_set_text(GTK_LABEL(d->status), buf);

    if (d->progress && d->session) {
        int total = lame_get_totalframes(d->session->gf);
        if (d->session->completed) total = d->session->frames_done;
        if (total > 1) {
            /* Progress reflects how much of the file has been analyzed,
               not which frame is currently displayed. The display frame
               lags behind the analysis by READ_AHEAD (40) frames because
               of the plotting ring; using pdisp->frameNum here would
               make the bar stall at ~99% at end-of-file. */
            double frac = (double) d->session->frames_done / (double) total;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress),
                                          CLAMP(frac, 0.0, 1.0));
            g_snprintf(buf, sizeof buf, "%d%%", (int)(CLAMP(frac, 0.0, 1.0) * 100.0));
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(d->progress), buf);
        }
    }
}

static void
update_transport(Mp3xDriver *d)
{
    Mp3xSession *s = d->session;
    gboolean     running = s && s->running;
    gboolean     has_file = (s != NULL);
    gboolean     terminal = !s || s->completed || s->failed;
    gboolean     can_step = s && !running
                            && (mp3x_core_disp_backpos() > 0 || !terminal);
    gboolean     can_back = s && !running && pdisp && pdisp->frameNum > 0;

    if (d->btn_playpause) {
        gtk_button_set_label(GTK_BUTTON(d->btn_playpause),
                             s && s->failed ? "Failed"
                             : (s && s->completed ? "Done"
                                : (running ? "Pause" : "Play")));
        gtk_widget_set_sensitive(d->btn_playpause, has_file && !terminal);
    }
    if (d->btn_step)
        gtk_widget_set_sensitive(d->btn_step, can_step);
    if (d->btn_back)
        gtk_widget_set_sensitive(d->btn_back, can_back);
}

static void
update_action_sensitivity(Mp3xDriver *d)
{
    gboolean has_file = (d->session != NULL);
    gboolean running = has_file && d->session->running;
    gboolean terminal = !has_file || d->session->completed || d->session->failed;
    gboolean can_step = has_file && !running
                        && (mp3x_core_disp_backpos() > 0 || !terminal);
    gboolean can_back = has_file && !running && pdisp && pdisp->frameNum > 0;
    GActionMap *m = d->win ? G_ACTION_MAP(d->win) : NULL;
    if (m == NULL) return;

    static const char *const file_actions[] = {
        "close", "screenshot",
        "export-pcm", "export-resynth",
        "export-mdct-0", "export-mdct-1",
        "export-psy-0", "export-psy-1",
        "export-sfb-0", "export-sfb-1"
    };
    for (gsize i = 0; i < G_N_ELEMENTS(file_actions); i++) {
        GAction *a = g_action_map_lookup_action(m, file_actions[i]);
        if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), has_file);
    }

#define SET_ACTION_ENABLED(name, enabled) do { \
        GAction *a_ = g_action_map_lookup_action(m, (name)); \
        if (a_) g_simple_action_set_enabled(G_SIMPLE_ACTION(a_), (enabled)); \
    } while (0)
    SET_ACTION_ENABLED("play", has_file && !running && !terminal);
    SET_ACTION_ENABLED("pause", running);
    SET_ACTION_ENABLED("step", can_step);
    SET_ACTION_ENABLED("back", can_back);
    SET_ACTION_ENABLED("adv10", has_file && !terminal);
    SET_ACTION_ENABLED("adv100", has_file && !terminal);
    SET_ACTION_ENABLED("last", has_file && !terminal);
#undef SET_ACTION_ENABLED
}

static void
refresh_display(Mp3xDriver *d)
{
    set_header(d->header, d->channel);
    set_status_readout(d);
    redraw_all(d);
}

static void
mp3x_driver_refresh_for_session(Mp3xDriver *d)
{
    /* Title reflects the active file. */
    if (d->win && d->session && d->session->display_name) {
        char *title = g_strdup_printf("MP3x: %.70s", d->session->display_name);
        gtk_window_set_title(d->win, title);
        g_free(title);
    }

    /* Source default may have changed (MP3 vs PCM input). */
    mp3x_apply_display(d);
    refresh_display(d);
    update_transport(d);
    update_action_sensitivity(d);

    /* If channel was R but new file is mono, normalize to L. */
    if (d->session && d->channel == 1) {
        plotting_data *p = pdisp;
        if (p && p->stereo < 2) {
            d->channel = 0;
            /* reflect in the channel action state */
            if (d->win) {
                GAction *a = g_action_map_lookup_action(G_ACTION_MAP(d->win), "channel");
                if (a) g_simple_action_set_state(G_SIMPLE_ACTION(a), g_variant_new_string("L"));
            }
            mp3x_apply_display(d);
        }
    }

    /* Sync the source action's state to the session's recomputed source. */
    if (d->win && d->session) {
        GAction *a = g_action_map_lookup_action(G_ACTION_MAP(d->win), "source");
        if (a)
            g_simple_action_set_state(G_SIMPLE_ACTION(a),
                                      g_variant_new_string(d->session->source ? "mpg123" : "lame"));
    }
}

static void
mp3x_driver_refresh_for_empty(Mp3xDriver *d)
{
    if (d->win)
        gtk_window_set_title(d->win, "MP3x");

    /* Clear the analyzer plotting ring so stale data from the previous
       file doesn't bleed into the empty-state redraw. On X11 the redraw
       fires immediately after Close, exposing whatever was last in the
       ring; macOS masks this because its redraw timing differs.
       mp3x_core_init is safe here: after mp3x_session_close has already
       called mp3x_core_shutdown (which zeroed core_state.hip), the
       init's hip guard is a no-op, and the memset on Pinfo clears
       pdisp->sampfreq so the render functions bail on their own check. */
    mp3x_core_init();

    /* Clear graphs so they display a deliberate empty state, not stale data. */
    mp3x_apply_display(d);
    set_header(d->header, d->channel);
    set_status_readout(d);
    redraw_all(d);
    update_transport(d);
    update_action_sensitivity(d);
}


/* ==========================================================================
 * Keyboard transport
 * ========================================================================== */

static gboolean
on_key(GtkEventControllerKey *kc, guint keyval, guint keycode,
       GdkModifierType state, gpointer data)
{
    Mp3xDriver *d = data;
    (void) kc; (void) keycode; (void) state;

    if (d->session == NULL)
        return FALSE;

    if (keyval == GDK_KEY_space) {
        on_playpause(NULL, d);
        return TRUE;
    }
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_3) {
        const char v[2] = { (char)(keyval == GDK_KEY_0 ? 'a' : keyval - 1), 0 };
        if (d->win)
            g_action_group_change_action_state(G_ACTION_GROUP(d->win), "subblock",
                                               g_variant_new_string(keyval == GDK_KEY_0 ? "all" : v));
        return TRUE;
    }
    if (keyval == GDK_KEY_Right) { on_step(NULL, d); return TRUE; }
    if (keyval == GDK_KEY_Left)  { on_back(NULL, d); return TRUE; }
    return FALSE;
}


/* ==========================================================================
 * Window close
 * ========================================================================== */

static gboolean
on_close_request(GtkWindow *win, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    (void) win;
    mp3x_driver_request_shutdown(d);
    return TRUE;   /* stop the default close; shutdown destroys the window */
}

static void
on_window_destroy(GtkWindow *win, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    (void) win;
    if (d->win == GTK_WINDOW(win))
        d->win = NULL;
}


/* ==========================================================================
 * Application signal handlers
 * ========================================================================== */

static void
on_startup(GtkApplication *app, gpointer data)
{
    Mp3xDriver *d = data;
    GMenuModel *menubar;
    GdkDisplay *display;
    gboolean    on_macos;

    g_action_map_add_action_entries(G_ACTION_MAP(app), mp3x_app_actions,
                                    G_N_ELEMENTS(mp3x_app_actions), d);

    display  = gdk_display_get_default();
    on_macos = (display != NULL
                && g_str_equal(G_OBJECT_TYPE_NAME(display), "GdkMacosDisplay"));

    /* Accelerators. macOS gets Cmd-; Linux/Windows gets Ctrl-. */
    static const char *const open_mac[]  = { "<Meta>o", NULL };
    static const char *const open_pc[]   = { "<Ctrl>o",  NULL };
    static const char *const close_mac[] = { "<Meta>w", NULL };
    static const char *const close_pc[]  = { "<Ctrl>w",  NULL };
    static const char *const ss_mac[]    = { "<Meta>s", NULL };
    static const char *const ss_pc[]     = { "<Ctrl>s",  NULL };
    static const char *const quit_mac[]  = { "<Meta>q", NULL };
    static const char *const quit_pc[]   = { "<Ctrl>q",  NULL };

    gtk_application_set_accels_for_action(app, "win.open",  on_macos ? open_mac  : open_pc);
    gtk_application_set_accels_for_action(app, "win.close", on_macos ? close_mac : close_pc);
    gtk_application_set_accels_for_action(app, "win.screenshot",
                                          on_macos ? ss_mac : ss_pc);
    gtk_application_set_accels_for_action(app, "app.quit",  on_macos ? quit_mac  : quit_pc);

    menubar = build_menubar(on_macos);
    gtk_application_set_menubar(app, menubar);
    /* Both the application and driver own one reference: set_menubar()
       acquires the application's; the constructor return stays with d. */
    d->menubar = menubar;

    install_noncomposited_menu_css(display);
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    Mp3xDriver *d = user_data;
    GtkWidget  *win;
    GtkWidget  *box;
    GtkWidget  *plots;
    GtkWidget  *scroller;

    if (d->win != NULL) {
        gtk_window_present(d->win);
        return;
    }

    win = gtk_application_window_new(app);
    d->win = GTK_WINDOW(win);

    /* Install window action map before any toolbar button can fire. */
    g_action_map_add_action_entries(G_ACTION_MAP(win), mp3x_win_actions,
                                    G_N_ELEMENTS(mp3x_win_actions), d);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), TRUE);

    gtk_window_set_title(d->win, "MP3x");

    /* Close/destroy handlers. */
    g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), d);
    g_signal_connect(win, "destroy",       G_CALLBACK(on_window_destroy), d);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    /* Header line */
    d->header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(d->header), 0.0);
    gtk_widget_set_margin_start(d->header, 6);
    gtk_widget_set_margin_top(d->header, 6);
    gtk_box_append(GTK_BOX(box), d->header);

    /* Control bar */
    {
        GtkWidget *bar  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *chan_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *src_group  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *chan_seg   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *src_seg    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *t;
        static const char *const chan_ids[4] = { "L", "R", "M", "S" };
        int i;

        d->btn_back      = gtk_button_new_with_label("Back");
        d->btn_playpause = gtk_button_new_with_label("Play");
        d->btn_step      = gtk_button_new_with_label("Step");
        g_signal_connect(d->btn_back,      "clicked", G_CALLBACK(on_back),      d);
        g_signal_connect(d->btn_playpause, "clicked", G_CALLBACK(on_playpause), d);
        g_signal_connect(d->btn_step,      "clicked", G_CALLBACK(on_step),      d);
        gtk_box_append(GTK_BOX(row1), d->btn_back);
        gtk_box_append(GTK_BOX(row1), d->btn_playpause);
        gtk_box_append(GTK_BOX(row1), d->btn_step);

        gtk_widget_add_css_class(chan_seg, "linked");
        for (i = 0; i < 4; i++) {
            t = gtk_toggle_button_new_with_label(chan_ids[i]);
            gtk_actionable_set_action_name(GTK_ACTIONABLE(t), "win.channel");
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(t),
                                                   g_variant_new_string(chan_ids[i]));
            gtk_box_append(GTK_BOX(chan_seg), t);
        }
        gtk_box_append(GTK_BOX(chan_group), gtk_label_new("Ch"));
        gtk_box_append(GTK_BOX(chan_group), chan_seg);

        gtk_widget_add_css_class(src_seg, "linked");
        t = gtk_toggle_button_new_with_label("LAME");
        gtk_actionable_set_action_name(GTK_ACTIONABLE(t), "win.source");
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(t), g_variant_new_string("lame"));
        gtk_box_append(GTK_BOX(src_seg), t);
        t = gtk_toggle_button_new_with_label("mpg123");
        gtk_actionable_set_action_name(GTK_ACTIONABLE(t), "win.source");
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(t), g_variant_new_string("mpg123"));
        gtk_box_append(GTK_BOX(src_seg), t);
        gtk_box_append(GTK_BOX(src_group), gtk_label_new("Src"));
        gtk_box_append(GTK_BOX(src_group), src_seg);

        gtk_box_append(GTK_BOX(row2), chan_group);
        gtk_box_append(GTK_BOX(row2), src_group);
        t = gtk_toggle_button_new_with_label("Difference");
        gtk_actionable_set_action_name(GTK_ACTIONABLE(t), "win.diff");
        gtk_box_append(GTK_BOX(row2), t);
        t = gtk_toggle_button_new_with_label("SFB Lines");
        gtk_actionable_set_action_name(GTK_ACTIONABLE(t), "win.sfblines");
        gtk_box_append(GTK_BOX(row2), t);

        gtk_box_append(GTK_BOX(left), row1);
        gtk_box_append(GTK_BOX(left), row2);

        d->status = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(d->status), 1.0);
        gtk_label_set_justify(GTK_LABEL(d->status), GTK_JUSTIFY_RIGHT);
        gtk_widget_set_hexpand(d->status, TRUE);
        gtk_widget_set_valign(d->status, GTK_ALIGN_CENTER);

        gtk_box_append(GTK_BOX(bar), left);
        gtk_box_append(GTK_BOX(bar), d->status);
        gtk_widget_set_margin_start(bar, 6);
        gtk_widget_set_margin_end(bar, 6);
        gtk_box_append(GTK_BOX(box), bar);
    }

    /* Graphs in a scroller */
    plots = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 500);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), plots);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(box), scroller);

    /* Progress bar */
    d->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(d->progress), TRUE);
    gtk_widget_set_margin_start(d->progress, 6);
    gtk_widget_set_margin_end(d->progress, 6);
    gtk_box_append(GTK_BOX(box), d->progress);

    /* View-preference overrides exist only in explicitly instrumented builds. */
#ifdef MP3X_ENABLE_TEST_HOOKS
    if (g_getenv("MP3X_CHANNEL")) d->channel = 1;
    if (g_getenv("MP3X_MS"))      d->ms = 1;
    if (g_getenv("MP3X_DIFF"))    d->difference = TRUE;
    if (g_getenv("MP3X_SOURCE") && d->session)
        d->session->source = 1;
    if (g_getenv("MP3X_NOSFBLINES")) d->sfblines = 0;
    if (g_getenv("MP3X_KBFLAG"))     d->kbflag = 1;
#endif

    /* Graph widgets */
    d->plot = mp3x_plot_pcm_new();
    gtk_widget_set_hexpand(d->plot, TRUE);
    gtk_widget_set_vexpand(d->plot, TRUE);
    gtk_box_append(GTK_BOX(plots), d->plot);

    d->plot_resynth = mp3x_plot_resynth_new();
    gtk_widget_set_hexpand(d->plot_resynth, TRUE);
    gtk_widget_set_vexpand(d->plot_resynth, TRUE);
    gtk_box_append(GTK_BOX(plots), d->plot_resynth);

    {
        GtkWidget *mdct_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        d->plot_mdct[0] = mp3x_plot_mdct_new(0);
        d->plot_mdct[1] = mp3x_plot_mdct_new(1);
        gtk_widget_set_hexpand(d->plot_mdct[0], TRUE);
        gtk_widget_set_vexpand(d->plot_mdct[0], TRUE);
        gtk_widget_set_hexpand(d->plot_mdct[1], TRUE);
        gtk_widget_set_vexpand(d->plot_mdct[1], TRUE);
        gtk_box_append(GTK_BOX(mdct_row), d->plot_mdct[0]);
        gtk_box_append(GTK_BOX(mdct_row), d->plot_mdct[1]);
        gtk_box_append(GTK_BOX(plots), mdct_row);
    }
    {
        GtkWidget *psy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        d->plot_psy[0] = mp3x_plot_psy_new(0);
        d->plot_psy[1] = mp3x_plot_psy_new(1);
        gtk_widget_set_hexpand(d->plot_psy[0], TRUE);
        gtk_widget_set_vexpand(d->plot_psy[0], TRUE);
        gtk_widget_set_hexpand(d->plot_psy[1], TRUE);
        gtk_widget_set_vexpand(d->plot_psy[1], TRUE);
        gtk_box_append(GTK_BOX(psy_row), d->plot_psy[0]);
        gtk_box_append(GTK_BOX(psy_row), d->plot_psy[1]);
        gtk_box_append(GTK_BOX(plots), psy_row);
    }
    {
        GtkWidget *sfb_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        d->plot_sfb[0] = mp3x_plot_sfb_new(0);
        d->plot_sfb[1] = mp3x_plot_sfb_new(1);
        gtk_widget_set_hexpand(d->plot_sfb[0], TRUE);
        gtk_widget_set_vexpand(d->plot_sfb[0], TRUE);
        gtk_widget_set_hexpand(d->plot_sfb[1], TRUE);
        gtk_widget_set_vexpand(d->plot_sfb[1], TRUE);
        gtk_box_append(GTK_BOX(sfb_row), d->plot_sfb[0]);
        gtk_box_append(GTK_BOX(sfb_row), d->plot_sfb[1]);
        gtk_box_append(GTK_BOX(plots), sfb_row);
    }

    /* Keyboard transport */
    {
        GtkEventController *kc = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
        g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), d);
        gtk_widget_add_controller(win, kc);
    }

    /* Sync stateful action states to driver view prefs. */
    {
        static const char *const chan[4] = { "L", "R", "M", "S" };
        GActionMap *m = G_ACTION_MAP(win);
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "channel")),
            g_variant_new_string(chan[d->ms * 2 + d->channel]));
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "source")),
            g_variant_new_string(d->session && d->session->source ? "mpg123" : "lame"));
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "diff")),
            g_variant_new_boolean(d->difference));
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "sfblines")),
            g_variant_new_boolean(d->sfblines));
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "plotadv")),
            g_variant_new_boolean(d->plot_advancing));
        g_simple_action_set_state(
            G_SIMPLE_ACTION(g_action_map_lookup_action(m, "spectrum")),
            g_variant_new_string(d->kbflag ? "wave" : "bands"));
        {
            char sb[2] = { (char)('0' + d->subblock), 0 };
            g_simple_action_set_state(
                G_SIMPLE_ACTION(g_action_map_lookup_action(m, "subblock")),
                g_variant_new_string(d->subblock < 0 ? "all" : sb));
        }
    }

    gtk_window_set_child(d->win, box);
    gtk_window_present(d->win);

    /* The initial CLI session, if any, was parsed and opened before GTK was
       constructed. Install its visible state now; informational CLI exits
       never reach this handler and therefore never realize a window. */
    mp3x_apply_display(d);
    if (d->session != NULL) {
        if (d->session->in_path &&
            strcmp(d->session->in_path, "-") != 0) {
            GFile *gfile = g_file_new_for_path(d->session->in_path);
            mp3x_recent_add(gfile, NULL);
            g_object_unref(gfile);
        }
        mp3x_driver_refresh_for_session(d);
        mp3x_start_session(d);
    } else {
        mp3x_driver_refresh_for_empty(d);
    }
    mp3x_recent_refresh(d);
}


/* ==========================================================================
 * Entry point
 *
 * lame_main is called from main.c with a lame_t that mp3x ignores. main.c
 * owns and closes that handle.
 * mp3x creates its own lame_t per session via mp3x_session_open.
 * ========================================================================== */

int
lame_main(lame_t main_c_gf_borrowed, int argc, char **argv)
{
    Mp3xDriver   *d;
    GtkApplication *app;
    int           status;

    (void) main_c_gf_borrowed;   /* main.c owns and closes this; mp3x never uses it */

    d = mp3x_driver_new();

    /* 1. Capture frontend-global baseline BEFORE any parse_args call. This
          snapshot is the startup state mp3x_session_open restores from on
          every GUI Open, so Cat-2/3 options on the initial CLI file cannot
          leak into later GUI-selected files. */
    mp3x_driver_capture_baseline(d);

    /* 2. Parse and open an initial CLI file before GTK exists. The legacy
          parser uses -2 for informational success; preserve that contract
          without creating a window or entering the application main loop. */
    if (argc > 1) {
        Mp3xSession *initial = mp3x_session_new(d);
        GError *err = NULL;
        Mp3xCliOpenResult result =
            mp3x_session_open_cli_initial(initial, d, argc, argv, &err);

        if (result == MP3X_CLI_OPEN_EXIT_SUCCESS) {
            mp3x_session_free(initial);
            mp3x_driver_unref(d);
            return 0;
        }
        if (result == MP3X_CLI_OPEN_ERROR) {
            if (err != NULL)
                fprintf(stderr, "mp3x: %s\n", err->message);
            g_clear_error(&err);
            mp3x_session_free(initial);
            mp3x_driver_unref(d);
            return 1;
        }
        d->session = initial;
        d->state_epoch++;
    }

    /* 3. Create the GtkApplication. */
    app = gtk_application_new("org.sourceforge.lame.mp3x", G_APPLICATION_NON_UNIQUE);
    d->app = app;   /* driver borrows during g_application_run */

    g_signal_connect(app, "startup",  G_CALLBACK(on_startup),  d);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), d);

    /* 4. Do NOT pass LAME's argv to GApplication - parse_args already
          consumed them in spirit, and GApplication would reject the
          input-file argument. */
    status = g_application_run(G_APPLICATION(app), 0, NULL);

    /* 5. After run returns, every async request has settled (deferred
          shutdown guarantees it). Clear the borrowed app pointer and drop
          the local owned reference. */
    d->app = NULL;
    g_object_unref(app);

    /* 6. Analyzer execution failures are cumulative for this process. Session
          retirement latches them in the driver, so later success or shutdown
          cannot erase the nonzero result. */
    {
        int ret = status;
        if ((d->analysis_failed || (d->session && d->session->failed)) &&
            ret == 0)
            ret = 1;
        mp3x_driver_unref(d);
        return ret;
    }
}
