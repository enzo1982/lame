/**
 *  \file mp3x_session.h
 *  \brief mp3x analyzer session - internal interface.
 *  \internal
 *
 *  The toolkit-independent per-file session lifecycle for the GTK4 mp3x frame
 *  analyzer. One Mp3xSession exists for each open input file; the application
 *  as a whole can be empty (no session) or hold a single session at a time.
 *
 *  \code
 *  Mp3xPrevalidateResult pre;
 *  Mp3xSession *s = mp3x_session_new(driver);
 *  if (mp3x_prevalidate(file, &pre, &err) &&
 *      mp3x_session_open_prevalidated(s, driver, &pre, &err)) {
 *      ... analyzer runs ...
 *      mp3x_session_close(s);
 *  }
 *  mp3x_prevalidate_result_clear(&pre);
 *  mp3x_session_free(s);
 *  \endcode
 *
 *  The session owns a fresh \c lame_t for the file it represents. Open and
 *  Close define its state transitions; the GTK frontend routes File > Open,
 *  Open Recent, File > Close and File > Quit through those functions.
 *
 *  Nothing here includes GTK/GDK. The session engine is toolkit-independent so
 *  the GTK layer in mp3x_ui.c can drive it without coupling.
 *
 *  \see \ref mp3x_internals for the ownership rules that make a stale async
 *  callback safe.
 */

#ifndef LAME_MP3X_SESSION_H
#define LAME_MP3X_SESSION_H

#include <glib.h>
#include <gio/gio.h>            /* GFile, GError */

#include "lame.h"
#include "main.h"               /* ReaderConfig, WriterConfig, UiConfig,
                                   DecoderConfig, RawPCMConfig */
#include "get_audio.h"          /* sound_file_format */
#include "mp3x_core.h"          /* Mp3xStats */

#ifdef __cplusplus
extern "C" {
#endif


/**
 *  A snapshot of the five process-wide frontend configuration structs.
 *
 *  parse.c exposes \c global_reader, \c global_writer, \c global_ui_config,
 *  \c global_decoder and \c global_raw_pcm, plus get_audio.c's
 *  \c get_audio_global_data. They are mutated by \c parse_args and by
 *  \c init_infile. mp3x_session treats them as the per-file session state they
 *  actually are: every session open restores a captured startup baseline before
 *  any new configuration is applied, so options from a prior file - or
 *  raw-input CLI flags given for the initial file - cannot leak into a later
 *  GUI-opened file.
 *
 *  Every member of these five structs is plain value data with no pointers, and
 *  \c mp3data_struct inside \c DecoderConfig holds only integers, so struct-copy
 *  is a safe capture and restore mechanism.
 */
typedef struct {
    ReaderConfig    reader;
    WriterConfig    writer;
    UiConfig        ui_config;
    DecoderConfig   decoder;
    RawPCMConfig    raw_pcm;
} FrontendGlobalsBaseline;


/* --------------------------------------------------------------------------
 * Mp3xSession
 *
 * All state tied to one open input file. The GTK frontend installs at most
 * one of these in its driver at a time; closing the file or replacing it
 * with another tears the old one down entirely.
 *
 * The session owns: the lame_t (encoder/analyzer handle), heap copies of
 * the path and display name, and all per-file transport state (running,
 * input/drain completion, frames_done, idle-source ID). It does NOT own GTK
 * widgets, the open input FILE (that belongs to get_audio.c's global), or any decoder
 * handles (those belong to mp3x_core and get_audio.c).
 *
 * Display view preferences (channel, ms, difference, sfblines, kbflag,
 * subblock) are driver-lifetime - they survive file swaps when valid - and
 * live in Mp3xDriver, not here. The `source` field is per-file because it
 * is recomputed from each file's format (MP3 → mpg123, PCM → LAME).
 * -------------------------------------------------------------------------- */
typedef struct Mp3xSession {
    /* Identity, captured at creation */
    guint64          generation;     /* unique among every session this driver
                                        has ever created; never 0 */

    /* Input */
    lame_t           gf;             /* owned; created in _open, closed in _close */
    gchar           *in_path;        /* heap-owned UTF-8 filesystem path; NULL
                                        when session is not open */
    gchar           *display_name;   /* heap-owned display name (basename or
                                        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME);
                                        NULL when not open */
    sound_file_format input_format;  /* sf_wave / sf_aiff / sf_mp123 / sf_unknown */

    /* Transport state - per file, reset on every open */
    gboolean         running;        /* TRUE while auto-stepping */
    gboolean         input_exhausted; /* TRUE after the one EOF core step */
    gboolean         completed;      /* TRUE after the delayed ring is drained */
    gboolean         failed;         /* TRUE if the analyzer stopped on an error */
    int              drain_remaining; /* pure ring shifts left after EOF */
    int              frames_done;    /* frames analyzed so far */
    int              advance_left;   /* 0 = free-run, -1 = to end, N = N frames */

    /* Per-file display state - source is recomputed from file format */
    int              source;         /* 0 = LAME encoder side, 1 = mpg123 decoder */

    /* Idle source - per file, cancelled on close/replace */
    guint            idle_id;        /* 0 when no auto-step source is installed */

    /* TRUE between mp3x_session_open success and the first mp3x_session_close.
       Used defensively to make Close idempotent. */
    gboolean         is_open;
} Mp3xSession;


/* --------------------------------------------------------------------------
 * Mp3xDriver (forward declaration - full definition is in mp3x_ui.c)
 *
 * The driver owns the current session pointer; it also owns a monotonic
 * generation counter that ensures no in-flight async callback can ever
 * match a freed session.
 * -------------------------------------------------------------------------- */
typedef struct Mp3xDriver Mp3xDriver;


/* --------------------------------------------------------------------------
 * Prevalidation
 *
 * mp3x_prevalidate() rejects obviously-unusable selections before retiring
 * the current file: directories, empty files, special files (FIFOs, sockets,
 * block/char devices), and resources without a filesystem path. The check
 * is advisory; the format decision is left to init_infile.
 *
 * The GFile is taken directly from GtkFileDialog so no string round-trip is
 * involved. Output struct is initialized to all-NULL by the caller; on any
 * failure every field remains NULL.
 * -------------------------------------------------------------------------- */
typedef struct {
    gchar *fs_path;        /* heap, never NULL on success */
    gchar *display_name;   /* heap, may be NULL if attribute missing */
    gchar *content_type;   /* heap, may be NULL if attribute missing */
} Mp3xPrevalidateResult;

typedef enum {
    MP3X_CLI_OPEN_ERROR = -1,
    MP3X_CLI_OPEN_EXIT_SUCCESS = 0,
    MP3X_CLI_OPENED = 1
} Mp3xCliOpenResult;

#define MP3X_OPEN_ERROR            (g_quark_from_static_string("mp3x-open-error"))
#define MP3X_OPEN_ERR_NO_FS_PATH   0
#define MP3X_OPEN_ERR_NOT_REGULAR  1
#define MP3X_OPEN_ERR_IS_DIRECTORY 2
#define MP3X_OPEN_ERR_EMPTY        3
#define MP3X_OPEN_ERR_UNREADABLE   4
#define MP3X_OPEN_ERR_LAME_INIT    5
#define MP3X_OPEN_ERR_PARSE_ARGS   6
#define MP3X_OPEN_ERR_INIT_INFILE  7
#define MP3X_OPEN_ERR_INIT_PARAMS  8
#define MP3X_OPEN_ERR_NO_INPUT_FILE 9


/* --------------------------------------------------------------------------
 * Session lifecycle
 * -------------------------------------------------------------------------- */

/* Allocate a fresh, empty session. Captures the next monotonic generation
   from the driver. Does not open any file. */
Mp3xSession   *mp3x_session_new(Mp3xDriver *d);

/* The shared GUI open route. Used by File > Open and Open Recent.

   The GTK driver prevalidates before retiring its current
   session, then passes the successful result here. This function consumes
   fs_path/display_name and clears every result field. On an initialization
   failure the application remains empty with err set.

   `pre` must contain a successful mp3x_prevalidate() result. */
gboolean       mp3x_session_open_prevalidated(Mp3xSession *s, Mp3xDriver *d,
                                               Mp3xPrevalidateResult *pre,
                                               GError **err);

/* Initial CLI route, called before GtkApplication construction. It passes the
   full argv to parse_args so LAME options apply to the initial file. Returns
   OPENED, informational EXIT_SUCCESS (parse_args code -2), or ERROR. */
Mp3xCliOpenResult
               mp3x_session_open_cli_initial(Mp3xSession *s, Mp3xDriver *d,
                                              int argc, char **argv,
                                              GError **err);

/* Close the input file and release every per-file resource: cancel the
   idle source, run mp3x_core_shutdown, close_infile, lame_close. Safe to
   call on an already-closed or never-opened session. Leaves the session
   struct allocated but empty; mp3x_session_free releases the struct. */
void           mp3x_session_close(Mp3xSession *s);

/* Free the session struct. Caller must call _close first if the session
   was ever opened. */
void           mp3x_session_free(Mp3xSession *s);


/* --------------------------------------------------------------------------
 * Frontend-global baseline
 *
 * Captured once in lame_main, BEFORE any parse_args call.
 * Every open route restores from this baseline before any per-file
 * configuration is applied.
 * -------------------------------------------------------------------------- */

/* Apply documented defaults to all five frontend globals, mirroring what
   parse_args would set unconditionally at lines 1659-1666 plus the raw-PCM
   defaults init_infile assumes when -r is NOT given. Does NOT call
   parse_args; safe to invoke at any time. */
void           mp3x_apply_documented_defaults(void);

/* Snapshot the current state of all five frontend globals into the
   baseline. Struct-copy; all fields verified to be plain value data. */
void           mp3x_globals_capture(FrontendGlobalsBaseline *b);

/* Restore all five frontend globals from the baseline. Struct-copy. */
void           mp3x_globals_restore(const FrontendGlobalsBaseline *b);


/* --------------------------------------------------------------------------
 * Prevalidation (exposed so the GTK layer can show specific error dialogs)
 * -------------------------------------------------------------------------- */

/* Initialize result to all-NULL, then validate. Returns TRUE and fills *out
   on success; returns FALSE and sets *err on every failure. Out parameter
   must be non-NULL; *out is set to all-NULL on entry. */
gboolean       mp3x_prevalidate(GFile *gfile,
                                 Mp3xPrevalidateResult *out,
                                 GError **err);

/* Free every owned field and reset the result to all-NULL. Safe repeatedly. */
void           mp3x_prevalidate_result_clear(Mp3xPrevalidateResult *result);


/* --------------------------------------------------------------------------
 * File-extension classification
 *
 * Returns sf_wave, sf_aiff, sf_mp123 for known extensions, sf_unknown
   otherwise. Case-insensitive; broader than parse.c's filename_to_type
   (which only matches the last 4 characters and so would miss .wave/.aiff/
   .aifc). The format decision still belongs to init_infile's
   header parsers - this is just the hint that picks which opener runs. */
sound_file_format mp3x_classify_extension(const char *path);


/* --------------------------------------------------------------------------
 * Filename sanitization
 *
 * Returns a heap-owned basename with the extension stripped and every
   filesystem-unsafe character (/ \ : * ? " < > |) and control character
   replaced with underscore. Leading dots are NOT collapsed by advancing
   the allocation pointer; they are replaced in place. The returned string
   is suitable for constructing save-dialog default filenames. */
gchar         *mp3x_sanitize_stem(const gchar *display_name);


#ifdef __cplusplus
}
#endif

#endif /* LAME_MP3X_SESSION_H */
