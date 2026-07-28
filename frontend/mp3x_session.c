/**
 *  \file mp3x_session.c
 *  \brief mp3x analyzer session - implementation.
 *  \internal
 *
 *  Per-file lifecycle for the GTK4 mp3x frame analyzer. Each open input file
 *  lives in its own Mp3xSession; closing or replacing the file tears down the
 *  session entirely - idle source, analyzer core, input decoder, \c lame_t - so
 *  that the next open starts from documented clean defaults rather than
 *  accumulated state.
 *
 *  The two open routes, mp3x_session_open_prevalidated() for the GUI path and
 *  mp3x_session_open_cli_initial() for the initial CLI-named file, both funnel
 *  through the same teardown and installation sequence; only the \c parse_args
 *  call differs.
 *
 *  This module is toolkit-independent: no GTK widgets, and no GLib main-loop
 *  code beyond what close and open need for path handling. The GTK frontend in
 *  mp3x_ui.c drives it.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "lame.h"
#include "main.h"
#include "get_audio.h"
#include "parse.h"
#include "console.h"           /* frontend_errorf/debugf/msgf, error_printf */
#include "mp3x_session.h"


/* --------------------------------------------------------------------------
 * Driver accessors
 *
 * The full Mp3xDriver struct is defined in mp3x_ui.c; here we only need two
 * accessors that touch driver-owned state. Declared here (not in the shared
 * mp3x_session.h) because they are internal to the mp3x frontend and the
 * GTK layer is their natural owner.
 * -------------------------------------------------------------------------- */
extern guint64                  mp3x_driver_next_generation(Mp3xDriver *d);
extern FrontendGlobalsBaseline *mp3x_driver_baseline(Mp3xDriver *d);


/* ==========================================================================
 * Frontend-global baseline
 *
 * All five parse.c globals (and get_audio.c's `global`) are process-wide
 * singletons. Because mp3x holds at most one open input at a time, we treat
 * them as current-session state: every mp3x_session_open restores the
 * baseline captured at startup, so options from a prior file or
 * from raw-input CLI flags on the initial file cannot leak into a later
 * GUI-opened self-describing file.
 *
 * The five parse.c structs contain only value data: integers, an enum, a
 * float, and an mp3data_struct containing integers. Struct-copy is therefore
 * a safe capture/restore.
 *
 * get_audio.c's `global` is intentionally NOT captured here; it is fully
 * reset by the existing init_infile()/close_infile() pair. init_infile resets
 * its fields; close_infile releases its owned resources and clears pointers.
 * ========================================================================== */

void
mp3x_apply_documented_defaults(void)
{
    /* These mirror parse_args() at frontend/parse.c:1659-1666 - the values
       it sets unconditionally at the top of every call. */
    global_ui_config.silent             = 0;
    global_ui_config.brhist             = 1;
    global_decoder.mp3_delay            = 0;
    global_decoder.mp3_delay_set        = 0;
    global_decoder.disable_wav_header   = 0;
    global_ui_config.print_clipping_info = 0;

    /* raw-PCM defaults that init_infile assumes when -r is NOT given.
       init_infile:654 reads global_raw_pcm.in_bitwidth; the safe default
       for self-describing WAV/AIFF/MP3 input is 16-bit signed little-endian
       (the value parse_args would set if -r were given without further
       flags). */
    global_raw_pcm.in_bitwidth = 16;
    global_raw_pcm.in_signed   = 1;
    global_raw_pcm.in_endian   = ByteOrderLittleEndian;

    /* global_reader documented defaults - matches post-parse_args-with-no-flags. */
    global_reader.input_format     = sf_unknown;
    global_reader.swapbytes        = 0;
    global_reader.swap_channel     = 0;
    global_reader.input_samplerate = 0;
    global_reader.ignorewavlength  = 0;

    /* global_writer - mp3x has no output stream; flush_write stays at 0. */
    global_writer.flush_write      = 0;

    /* global_decoder.mp3input_data is filled by the MP3 header parser on
       open; start each session from a clean slate. */
    memset(&global_decoder.mp3input_data, 0, sizeof global_decoder.mp3input_data);
}

void
mp3x_globals_capture(FrontendGlobalsBaseline *b)
{
    /* Struct-copy; verified safe (no pointers in any member). */
    b->reader    = global_reader;
    b->writer    = global_writer;
    b->ui_config = global_ui_config;
    b->decoder   = global_decoder;
    b->raw_pcm   = global_raw_pcm;
}

void
mp3x_globals_restore(const FrontendGlobalsBaseline *b)
{
    /* Struct-copy back; restore the captured startup state. */
    global_reader    = b->reader;
    global_writer    = b->writer;
    global_ui_config = b->ui_config;
    global_decoder   = b->decoder;
    global_raw_pcm   = b->raw_pcm;
}


/* ==========================================================================
 * Prevalidation
 *
 * Runs BEFORE the current session is retired. On any failure here the
 * previous session is left intact (per the prevalidated
 * close-then-open replacement policy). The check accepts only regular
 * files (no FIFOs, sockets, devices, mountables) so the readability probe
 * cannot block indefinitely.
 * ========================================================================== */

gboolean
mp3x_prevalidate(GFile *gfile, Mp3xPrevalidateResult *out, GError **err)
{
    /* Initialize output to all-NULL before any work; on every failure path
       the caller will see NULL fields. */
    *out = (Mp3xPrevalidateResult) {0};

    /* Every cleanup pointer is initialized before any goto. */
    gchar            *fs_path = NULL;
    GFileInfo        *info    = NULL;
    GFileInputStream *stream  = NULL;
    gchar            *disp    = NULL;
    gchar            *ctype   = NULL;
    gboolean          ok      = FALSE;

    g_return_val_if_fail(G_IS_FILE(gfile), FALSE);

    /* 1. A real filesystem path is required (LAME takes paths, not URIs). */
    fs_path = g_file_get_path(gfile);
    if (fs_path == NULL) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_NO_FS_PATH,
                    "mp3x requires a resource with a filesystem path");
        goto out;
    }

    /* 2. Query standard::type, size, display-name, content-type, access::can-read. */
    {
        GError *q_err = NULL;
        info = g_file_query_info(gfile,
                G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                G_FILE_ATTRIBUTE_ACCESS_CAN_READ,
                G_FILE_QUERY_INFO_NONE, NULL, &q_err);
        if (info == NULL) {
            g_propagate_error(err, q_err);
            goto out;
        }
    }

    /* 3. Reject directories with the specific message before the general
          nonregular-file branch. */
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_IS_DIRECTORY,
                    "mp3x cannot open a directory");
        goto out;
    }

    /* 4. Accept only regular files. This rules out FIFOs/sockets/devices
          (where a read would block) and shortcuts/mountables (which do not
          resolve to a usable path). Symlinks to regular files show as
          REGULAR here because g_file_query_info follows symlinks by default. */
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_NOT_REGULAR,
                    "mp3x can only open regular files");
        goto out;
    }

    /* 5. Reject confirmed-empty files. A missing size attribute is NOT a
          rejection - the backend may simply not know. */
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SIZE) &&
        g_file_info_get_size(info) == 0) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_EMPTY,
                    "mp3x cannot open an empty file");
        goto out;
    }

    /* 6. access::can-read: missing attribute is treated as unknown (NOT
          false). Only an explicit FALSE is a rejection. */
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ) &&
        !g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_UNREADABLE,
                    "mp3x cannot read '%s' (permission denied)", fs_path);
        goto out;
    }

    /* 7. Real readability probe: open then close. Safe now because we have
          already verified G_FILE_TYPE_REGULAR - no FIFO to block on, no
          character device to hang the read. */
    {
        GError *read_err = NULL;
        stream = g_file_read(gfile, NULL, &read_err);
        if (stream == NULL) {
            g_propagate_error(err, read_err);
            goto out;
        }
        if (!g_input_stream_close(G_INPUT_STREAM(stream), NULL, &read_err)) {
            g_propagate_error(err, read_err);
            goto out;
        }
    }

    /* 8. Capture display-name and content-type if present. */
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME)) {
        disp = g_strdup(g_file_info_get_attribute_string(info,
                    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME));
    }
    if (disp == NULL)
        disp = g_path_get_basename(fs_path);
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)) {
        ctype = g_strdup(g_file_info_get_attribute_string(info,
                    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));
    }

    ok = TRUE;

out:
    /* Single cleanup path - runs on success and every failure. */
    g_clear_object(&stream);
    g_clear_object(&info);

    if (ok) {
        out->fs_path      = fs_path;     /* non-NULL by construction */
        out->display_name = disp;        /* may be NULL */
        out->content_type = ctype;       /* may be NULL */
        return TRUE;
    }
    g_free(disp);
    g_free(ctype);
    g_free(fs_path);
    return FALSE;
}

void
mp3x_prevalidate_result_clear(Mp3xPrevalidateResult *result)
{
    if (result == NULL)
        return;

    g_clear_pointer(&result->fs_path, g_free);
    g_clear_pointer(&result->display_name, g_free);
    g_clear_pointer(&result->content_type, g_free);
}


/* ==========================================================================
 * File-extension classification
 *
 * parse.c:filename_to_type only matches the last 4 characters and so would
 * reject .wave, .aiff, .aifc (their last 4 chars are "wave", "aiff", "aifc"
 * - none of which the function recognizes). Our classifier accepts all the
 * extensions the GUI filter advertises. The format decision still belongs
 * to init_infile's header parsers - this is just the hint
 * that picks which opener runs.
 * ========================================================================== */

sound_file_format
mp3x_classify_extension(const char *path)
{
    const char *dot;

    if (path == NULL)
        return sf_unknown;

    dot = strrchr(path, '.');
    if (dot == NULL || dot == path)
        return sf_unknown;
    dot++;   /* skip the dot */

    if (g_ascii_strcasecmp(dot, "wav")  == 0 ||
        g_ascii_strcasecmp(dot, "wave") == 0)
        return sf_wave;

    if (g_ascii_strcasecmp(dot, "aif")  == 0 ||
        g_ascii_strcasecmp(dot, "aiff") == 0 ||
        g_ascii_strcasecmp(dot, "aifc") == 0)
        return sf_aiff;

    if (g_ascii_strcasecmp(dot, "mp1")  == 0 ||
        g_ascii_strcasecmp(dot, "mp2")  == 0 ||
        g_ascii_strcasecmp(dot, "mp3")  == 0 ||
        g_ascii_strcasecmp(dot, "mpg")  == 0)
        return sf_mp123;

    return sf_unknown;
}


/* ==========================================================================
 * Filename sanitization
 *
 * Used for save-dialog default filenames. Strips directory components and
 * one trailing extension, replaces filesystem-unsafe and control chars
 * with underscore, replaces leading dots in place (without advancing the
 * allocation pointer - that would make g_free invalid).
 * ========================================================================== */

gchar *
mp3x_sanitize_stem(const gchar *display_name)
{
    gchar *base;
    gchar *dot;
    gchar *p;

    if (display_name == NULL || *display_name == '\0')
        return g_strdup("audio");

    /* Strip directory components defensively. */
    base = g_path_get_basename(display_name);

    /* Strip a single trailing extension. */
    dot = strrchr(base, '.');
    if (dot != NULL && dot != base)
        *dot = '\0';

    /* Replace filesystem-unsafe chars in place. NEVER advance the base
       pointer - the original allocation must survive for g_free. */
    for (p = base; *p; p++) {
        const guchar c = (guchar) *p;
        if (c < 0x20 ||
            c == '/' || c == '\\' || c == ':' ||
            c == '*'  || c == '?'  || c == '"' ||
            c == '<'  || c == '>'  || c == '|') {
            *p = '_';
        }
    }

    /* Replace leading dots in place; preserves the allocation pointer. */
    for (p = base; *p == '.'; p++)
        *p = '_';

    /* If everything was stripped (e.g. filename was only dots), fall back. */
    if (*base == '\0') {
        g_free(base);
        return g_strdup("audio");
    }
    return base;
}


/* ==========================================================================
 * Session lifecycle
 * ========================================================================== */

Mp3xSession *
mp3x_session_new(Mp3xDriver *d)
{
    Mp3xSession *s = g_new0(Mp3xSession, 1);

    /* generation is driver-owned and process-monotonic; never 0 (we
       pre-increment, and the driver starts its counter at 0). */
    s->generation = mp3x_driver_next_generation(d);

    /* sensible defaults for an unopened session */
    s->source   = 0;         /* LAME encoder side; recomputed on open */

    return s;
}


/* Internal: the per-file reset applied at the top of every open route.
   Clears the analyzer engine, the driver-visible transport state, and the
   per-file display options. Display options are then reset by the caller
   (the GUI layer may re-apply preserved preferences) before the session
   is installed. */
static void
mp3x_session_reset_fields(Mp3xSession *s)
{
    s->running         = FALSE;
    s->input_exhausted = FALSE;
    s->completed       = FALSE;
    s->failed          = FALSE;
    s->drain_remaining = 0;
    s->frames_done     = 0;
    s->advance_left    = 0;
    s->idle_id         = 0;
}


/* Internal: the post-prevalidation common path. Assumes path is heap-owned
   and transferable. Takes ownership of `path` on success; frees it on
   failure. */
static gboolean
mp3x_session_install(Mp3xSession *s,
                     gchar *path, gchar *display_name, GError **err)
{
    /* 1. lame_init() + NULL check before any lame_set_* */
    s->gf = lame_init();
    if (s->gf == NULL) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_LAME_INIT,
                    "mp3x: lame_init() failed; out of memory");
        g_free(path);
        g_free(display_name);
        return FALSE;
    }

    /* Standard reporting hooks so LAME's diagnostics reach the frontend. */
    lame_set_errorf(s->gf, &frontend_errorf);
    lame_set_debugf(s->gf, &frontend_debugf);
    lame_set_msgf(s->gf, &frontend_msgf);

    /* 2. Turn on the analyzer hooks. */
    (void) lame_set_analysis(s->gf, 1);

    /* 3. init_infile makes the format decision. The
          pre-classified extension hint is set first; if our classifier
          did not recognize the extension, we let init_infile's own header
          parser decide. */
    s->input_format = mp3x_classify_extension(path);
    if (s->input_format != sf_unknown)
        global_reader.input_format = s->input_format;

    if (init_infile(s->gf, path) < 0) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_INIT_INFILE,
                    "mp3x: unable to initialize input file '%s'", path);
        goto fail_with_infile;
    }

    /* 4. lame_init_params. On failure (-1 specifically) LAME documents a
          bitrate-table dump; we mirror the existing frontend behavior. */
    {
        int ret = lame_init_params(s->gf);
        if (ret < 0) {
            if (ret == -1)
                display_bitrates(stderr);
            g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_INIT_PARAMS,
                        "mp3x: fatal error during initialization");
            goto fail_with_infile;
        }
    }

    /* 5. Bring the analyzer engine up. mp3x_core_init is idempotent and
          closes any prior decoder handle it may still hold. */
    mp3x_core_init();

    /* 6. Install the path. */
    s->in_path      = path;
    s->display_name = display_name;
    s->is_open      = TRUE;

    /* 7. Recompute the source default from the actual format. MP3 input
          defaults to mpg123-side analysis; PCM (WAV/AIFF) to LAME-side. */
    s->source = is_mpeg_file_format(global_reader.input_format) ? 1 : 0;

    mp3x_session_reset_fields(s);
    return TRUE;

fail_with_infile:
    close_infile();
    lame_close(s->gf);
    s->gf = NULL;
    g_free(path);
    g_free(display_name);
    return FALSE;
}


/* The GUI-open route: consume the driver's successful prevalidation,
   restore documented defaults, and do not replay CLI options. */
gboolean
mp3x_session_open_prevalidated(Mp3xSession *s, Mp3xDriver *d,
                               Mp3xPrevalidateResult *pre, GError **err)
{
    gchar *path;
    gchar *display_name;

    g_return_val_if_fail(s != NULL, FALSE);
    g_return_val_if_fail(pre != NULL, FALSE);
    g_return_val_if_fail(pre->fs_path != NULL, FALSE);

    /* The GTK layer retires the current session only after its preflight
       succeeds. Restore the startup baseline before opening the replacement. */
    mp3x_globals_restore(mp3x_driver_baseline(d));

    path = g_steal_pointer(&pre->fs_path);
    display_name = g_steal_pointer(&pre->display_name);
    mp3x_prevalidate_result_clear(pre);
    return mp3x_session_install(s, path, display_name, err);
}


/* The initial-CLI-file route: the full original argv is honored for this
   one file. It runs before GtkApplication is created, so parser information
   requests can exit successfully without realizing a window. */
Mp3xCliOpenResult
mp3x_session_open_cli_initial(Mp3xSession *s, Mp3xDriver *d,
                              int argc, char **argv,
                              GError **err)
{
    char  in_path[PATH_MAX + 1]  = {0};
    char  out_path[PATH_MAX + 1] = {0};
    int   pa_ret;
    GFile *gfile = NULL;
    Mp3xPrevalidateResult pre = {0};

    g_return_val_if_fail(s != NULL, MP3X_CLI_OPEN_ERROR);
    g_return_val_if_fail(argv != NULL, MP3X_CLI_OPEN_ERROR);

    /* Restore the startup baseline so any prior file's state is gone. */
    mp3x_globals_restore(mp3x_driver_baseline(d));

    /* lame_init + NULL check before any lame_set_*. */
    s->gf = lame_init();
    if (s->gf == NULL) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_LAME_INIT,
                    "mp3x: lame_init() failed; out of memory");
        return MP3X_CLI_OPEN_ERROR;
    }
    lame_set_errorf(s->gf, &frontend_errorf);
    lame_set_debugf(s->gf, &frontend_debugf);
    lame_set_msgf(s->gf, &frontend_msgf);

    /* parse_args runs against the FULL original argv. It sets Cat-1
       encoder/analyzer options, applies any input-specific options
       (raw forcing, format forcing, byte swap), and copies the first
       positional argument to in_path and the second to out_path.
       parse_args' own error reporting is preserved. */
    pa_ret = parse_args(s->gf, argc, argv, in_path, out_path, NULL, NULL);
    if (pa_ret == -2) {
        lame_close(s->gf);
        s->gf = NULL;
        return MP3X_CLI_OPEN_EXIT_SUCCESS;
    }
    if (pa_ret < 0) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_PARSE_ARGS,
                    "mp3x: command-line parse rejected (code %d)", pa_ret);
        goto fail_with_lame;
    }

    /* If parse_args found no positional input file, this is an
       option-only invocation (e.g., 'mp3x -b 320'). Return a specific
       error so the caller can treat it as "stay empty" without showing
       an error dialog. */
    if (in_path[0] == '\0') {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_NO_INPUT_FILE,
                    "no input file specified");
        goto fail_with_lame;
    }

    /* A parser-selected "-" is LAME's stdin sentinel, not a filesystem path.
       Keep it intact for init_infile and supply session-owned display
       metadata without constructing a GFile. All other CLI inputs follow the
       same regular-file prevalidation used by GUI opens. */
    if (strcmp(in_path, "-") == 0) {
        pre.fs_path = g_strdup("-");
        pre.display_name = g_strdup("standard input");
    }
    else {
        gfile = g_file_new_for_path(in_path);
        if (!mp3x_prevalidate(gfile, &pre, err))
            goto fail_with_lame;
        g_clear_object(&gfile);
    }

    /* Turn on the analyzer hooks (parse_args may not have). */
    (void) lame_set_analysis(s->gf, 1);

    /* init_infile from the actual path. */
    if (init_infile(s->gf, pre.fs_path) < 0) {
        g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_INIT_INFILE,
                    "mp3x: unable to initialize input file '%s'", pre.fs_path);
        goto fail_with_infile;
    }

    /* lame_init_params. */
    {
        int ret = lame_init_params(s->gf);
        if (ret < 0) {
            if (ret == -1)
                display_bitrates(stderr);
            g_set_error(err, MP3X_OPEN_ERROR, MP3X_OPEN_ERR_INIT_PARAMS,
                        "mp3x: fatal error during initialization");
            goto fail_with_infile;
        }
    }

    /* Analyzer engine up. */
    mp3x_core_init();

    /* Take ownership of the prepared path and display metadata. */
    s->in_path      = g_steal_pointer(&pre.fs_path);
    s->display_name = g_steal_pointer(&pre.display_name);
    mp3x_prevalidate_result_clear(&pre);
    s->input_format = global_reader.input_format;
    s->is_open      = TRUE;
    s->source       = is_mpeg_file_format(global_reader.input_format) ? 1 : 0;
    mp3x_session_reset_fields(s);
    return MP3X_CLI_OPENED;

fail_with_infile:
    close_infile();
fail_with_lame:
    g_clear_object(&gfile);
    mp3x_prevalidate_result_clear(&pre);
    lame_close(s->gf);
    s->gf = NULL;
    return MP3X_CLI_OPEN_ERROR;
}


void
mp3x_session_close(Mp3xSession *s)
{
    if (s == NULL || !s->is_open)
        return;

    /* Cancel the idle source first. The GTK layer's source-remove also
       bumps state_epoch on the driver, so any in-flight IdleContext
       fails its generation check; we still drop our local source ID
       defensively. */
    if (s->idle_id != 0) {
        g_source_remove(s->idle_id);
        s->idle_id = 0;
    }

    /* Analyzer engine teardown: releases the PCM-input re-synthesis
       decoder (core_state.hip). No-op for MP3 input. */
    mp3x_core_shutdown();

    /* get_audio.c teardown: releases the MP3 input decoder (global.hip,
       which wraps mpg123), the open FILE, libsndfile handle, ID3 buffer,
       and PCM buffers, then resets the get_audio global. */
    close_infile();

    /* Encoder handle teardown. On normal PCM completion the core has already
       called lame_encode_flush once and fed its bytes to the re-synthesis
       decoder; on early Close there is no need to synthesize an unseen tail.
       Encoded bytes are never written. lame_close releases the encoder,
       internal flags, and encoder-side analyzer slots. */
    if (s->gf != NULL) {
        lame_close(s->gf);
        s->gf = NULL;
    }

    g_free(s->in_path);
    g_free(s->display_name);
    s->in_path      = NULL;
    s->display_name = NULL;
    s->is_open      = FALSE;
}


void
mp3x_session_free(Mp3xSession *s)
{
    if (s == NULL)
        return;

    /* Defensive: if the caller forgot to close, do it now. _close is
       idempotent so a second call from the caller is safe. */
    if (s->is_open)
        mp3x_session_close(s);

    g_free(s);
}
