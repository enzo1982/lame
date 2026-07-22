/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the libmp3lame parameter API (set_get.c).
 *
 * set_get.c is almost entirely getter/setter pairs that the CLI frontend never
 * exercises: it drives the encoder through a handful of paths, so the coverage
 * harness leaves ~500 of its ~930 lines unexecuted (report/uncovered.txt). Those
 * lines are not dead - they are the public ABI every third-party caller uses -
 * so they are exactly what a unit test should pin down. Each function is probed
 * three ways where it applies: a valid round-trip, an invalid-@p gfp call (the
 * `is_lame_global_flags_valid` false branch, i.e. the `return -1` / default
 * tail), and an out-of-range value (the validation-reject branch). The first
 * pins behaviour; the latter two are the branches the frontend never reaches.
 *
 * Library-level tests: they link libmp3lame and call the exported API directly.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "lame.h"

/*
 * Deprecated ABI stubs: still exported from libmp3lame for binary back-compat,
 * but their prototypes were removed from lame.h (guarded out by
 * DEPRECATED_OR_OBSOLETE_CODE_REMOVED). They are live, reachable lines in
 * set_get.c, so we declare them locally to pin their fixed-answer behaviour
 * rather than leave that slice of the ABI untested. New code must not call
 * these. (Setters/getters that are neither exported nor reachable from the
 * frontend are dead code slated for removal - ISSUES.md - and are not tested.)
 */
extern int          lame_set_mode_automs(lame_global_flags *, int);
extern int          lame_get_mode_automs(const lame_global_flags *);
extern int          lame_set_ogg(lame_global_flags *, int);
extern int          lame_get_ogg(const lame_global_flags *);
extern int          lame_set_athaa_loudapprox(lame_global_flags *, int);
extern int          lame_get_athaa_loudapprox(const lame_global_flags *);
extern int          lame_set_cwlimit(lame_global_flags *, int);
extern int          lame_get_cwlimit(const lame_global_flags *);
extern int          lame_set_preset_expopts(lame_global_flags *, int);
extern int          lame_set_ReplayGain_input(lame_global_flags *, int);
extern int          lame_get_ReplayGain_input(const lame_global_flags *);
extern int          lame_get_findPeakSample(const lame_global_flags *);
extern int          lame_set_padding_type(lame_global_flags *, Padding_type);
extern Padding_type lame_get_padding_type(const lame_global_flags *);

/*
 * Internal-only tuning setters. These are gated exactly as the frontend gates
 * them (frontend/parse.c): a normal build cannot reach them (release frontends
 * #define them to no-ops and the shared library does not export them), while a
 * build with _ALLOW_INTERNAL_OPTIONS pulls in the private set_get.h and calls
 * the real functions. The tests below follow the same gate, so they are live
 * only in the developer/alpha configuration the functions themselves live in.
 * Reaching the (unexported) symbols additionally needs a static link - see the
 * test's Makefile.am entry.
 */
#if defined _ALLOW_INTERNAL_OPTIONS
#define INTERNAL_OPTS 1
#include "set_get.h"
#else
#define INTERNAL_OPTS 0
#endif

/** @brief A fresh encoder context for each test; @p *state carries it. */
static int
gfp_setup(void **state)
{
    lame_t gfp = lame_init();
    if (gfp == NULL)
        return -1;
    *state = gfp;
    return 0;
}

static int
gfp_teardown(void **state)
{
    lame_close((lame_t) *state);
    return 0;
}

/* A value like 0.5 is exactly representable, so a stored-then-read float
   round-trips bit-for-bit and can be compared with ==. Computed results use an
   epsilon instead. (assert_float_equal is CMocka 2.0-only; the tree still
   targets 1.1.x, so we do not use it - see guidance/testing.md.) */
#define ASSERT_FLT_EXACT(got, want) assert_true((got) == (float) (want))
#define ASSERT_FLT_NEAR(got, want)  assert_true(fabs((double) (got) - (double) (want)) < 1e-6)

typedef int   (*int_setter)(lame_global_flags *, int);
typedef int   (*int_getter)(const lame_global_flags *);
typedef int   (*flt_setter)(lame_global_flags *, float);
typedef float (*flt_getter)(const lame_global_flags *);

struct int_pair {
    const char *name;
    int_setter  set;
    int_getter  get;
};

struct flt_pair {
    const char *name;
    flt_setter  set;
    flt_getter  get;
};

/*
 * ---- 0/1 boolean-validated setters -----------------------------------------
 * Store only 0 or 1, reject anything else with -1, and the matching getter
 * returns 0 on an invalid gfp.
 */
static const struct int_pair boolean_pairs[] = {
    { "analysis",          lame_set_analysis,          lame_get_analysis },
    { "bWriteVbrTag",      lame_set_bWriteVbrTag,      lame_get_bWriteVbrTag },
    { "decode_only",       lame_set_decode_only,       lame_get_decode_only },
    { "force_ms",          lame_set_force_ms,          lame_get_force_ms },
    { "free_format",       lame_set_free_format,       lame_get_free_format },
    { "findReplayGain",    lame_set_findReplayGain,    lame_get_findReplayGain },
    { "copyright",         lame_set_copyright,         lame_get_copyright },
    { "original",          lame_set_original,          lame_get_original },
    { "error_protection",  lame_set_error_protection,  lame_get_error_protection },
    { "extension",         lame_set_extension,         lame_get_extension },
    { "disable_reservoir", lame_set_disable_reservoir, lame_get_disable_reservoir },
    { "VBR_hard_min",      lame_set_VBR_hard_min,      lame_get_VBR_hard_min },
    { "useTemporal",       lame_set_useTemporal,       lame_get_useTemporal },
};

static void
test_boolean_validated(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t i;
    for (i = 0; i < sizeof boolean_pairs / sizeof boolean_pairs[0]; ++i) {
        const struct int_pair *p = &boolean_pairs[i];
        assert_int_equal(p->set(gfp, 1), 0);
        assert_int_equal(p->get(gfp), 1);
        assert_int_equal(p->set(gfp, 0), 0);
        assert_int_equal(p->get(gfp), 0);
        /* out of the {0,1} domain -> rejected, value left untouched */
        assert_int_equal(p->set(gfp, 2), -1);
        assert_int_equal(p->set(gfp, -1), -1);
        assert_int_equal(p->get(gfp), 0);
        /* invalid gfp -> the -1 / default tail */
        assert_int_equal(p->set(NULL, 1), -1);
        assert_int_equal(p->get(NULL), 0);
    }
}

/*
 * ---- plain integer setters (store verbatim, no validation) ------------------
 */
static const struct int_pair int_pairs[] = {
    { "nogap_total",           lame_set_nogap_total,           lame_get_nogap_total },
    { "nogap_currentindex",    lame_set_nogap_currentindex,    lame_get_nogap_currentindex },
    { "quant_comp",            lame_set_quant_comp,            lame_get_quant_comp },
    { "quant_comp_short",      lame_set_quant_comp_short,      lame_get_quant_comp_short },
    { "experimentalY",         lame_set_experimentalY,         lame_get_experimentalY },
    { "experimentalZ",         lame_set_experimentalZ,         lame_get_experimentalZ },
    { "exp_nspsytune",         lame_set_exp_nspsytune,         lame_get_exp_nspsytune },
    { "VBR_mean_bitrate_kbps", lame_set_VBR_mean_bitrate_kbps, lame_get_VBR_mean_bitrate_kbps },
    { "VBR_min_bitrate_kbps",  lame_set_VBR_min_bitrate_kbps,  lame_get_VBR_min_bitrate_kbps },
    { "VBR_max_bitrate_kbps",  lame_set_VBR_max_bitrate_kbps,  lame_get_VBR_max_bitrate_kbps },
    { "lowpassfreq",           lame_set_lowpassfreq,           lame_get_lowpassfreq },
    { "lowpasswidth",          lame_set_lowpasswidth,          lame_get_lowpasswidth },
    { "highpassfreq",          lame_set_highpassfreq,          lame_get_highpassfreq },
    { "highpasswidth",         lame_set_highpasswidth,         lame_get_highpasswidth },
    { "ATHonly",               lame_set_ATHonly,               lame_get_ATHonly },
    { "ATHshort",              lame_set_ATHshort,              lame_get_ATHshort },
    { "noATH",                 lame_set_noATH,                 lame_get_noATH },
    { "ATHtype",               lame_set_ATHtype,               lame_get_ATHtype },
    { "athaa_type",            lame_set_athaa_type,            lame_get_athaa_type },
};

static void
test_int_roundtrip(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t i;
    for (i = 0; i < sizeof int_pairs / sizeof int_pairs[0]; ++i) {
        const struct int_pair *p = &int_pairs[i];
        assert_int_equal(p->set(gfp, 7), 0);
        assert_int_equal(p->get(gfp), 7);
        assert_int_equal(p->set(gfp, -3), 0);
        assert_int_equal(p->get(gfp), -3);
        assert_int_equal(p->set(NULL, 7), -1);
        assert_int_equal(p->get(NULL), 0);
    }
}

/*
 * ---- plain float setters (store verbatim, no validation) --------------------
 */
static const struct flt_pair flt_pairs[] = {
    { "scale",                lame_set_scale,                lame_get_scale },
    { "scale_left",           lame_set_scale_left,           lame_get_scale_left },
    { "scale_right",          lame_set_scale_right,          lame_get_scale_right },
    { "compression_ratio",    lame_set_compression_ratio,    lame_get_compression_ratio },
    { "ATHlower",             lame_set_ATHlower,             lame_get_ATHlower },
    { "athaa_sensitivity",    lame_set_athaa_sensitivity,    lame_get_athaa_sensitivity },
};

static void
test_float_roundtrip(void **state)
{
    lame_t gfp = (lame_t) *state;
    size_t i;
    for (i = 0; i < sizeof flt_pairs / sizeof flt_pairs[0]; ++i) {
        const struct flt_pair *p = &flt_pairs[i];
        assert_int_equal(p->set(gfp, 0.5f), 0);
        ASSERT_FLT_EXACT(p->get(gfp), 0.5f);
        assert_int_equal(p->set(NULL, 0.5f), -1);
        ASSERT_FLT_EXACT(p->get(NULL), 0.0f);
    }
}

/*
 * ---- range-validated setters ------------------------------------------------
 */
static void
test_ranges(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* input samplerate: must be >= 1 */
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_get_in_samplerate(gfp), 44100);
    assert_int_equal(lame_set_in_samplerate(gfp, 0), -1);
    assert_int_equal(lame_set_in_samplerate(gfp, -5), -1);
    assert_int_equal(lame_set_in_samplerate(NULL, 44100), -1);
    assert_int_equal(lame_get_in_samplerate(NULL), 0);

    /* channels: 1 or 2 only */
    assert_int_equal(lame_set_num_channels(gfp, 1), 0);
    assert_int_equal(lame_get_num_channels(gfp), 1);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_get_num_channels(gfp), 2);
    assert_int_equal(lame_set_num_channels(gfp, 0), -1);
    assert_int_equal(lame_set_num_channels(gfp, 3), -1);
    assert_int_equal(lame_get_num_channels(NULL), 0);

    /* output samplerate: 0 (auto) or a legal MPEG rate */
    assert_int_equal(lame_set_out_samplerate(gfp, 0), 0);
    assert_int_equal(lame_get_out_samplerate(gfp), 0);
    assert_int_equal(lame_set_out_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_get_out_samplerate(gfp), 44100);
    assert_int_equal(lame_set_out_samplerate(gfp, 12345), -1);
    assert_int_equal(lame_set_out_samplerate(NULL, 44100), -1);
    assert_int_equal(lame_get_out_samplerate(NULL), 0);

    /* mode: 0 .. MAX_INDICATOR-1 */
    assert_int_equal(lame_set_mode(gfp, STEREO), 0);
    assert_int_equal(lame_get_mode(gfp), STEREO);
    assert_int_equal(lame_set_mode(gfp, MONO), 0);
    assert_int_equal(lame_get_mode(gfp), MONO);
    assert_int_equal(lame_set_mode(gfp, (MPEG_mode) -1), -1);
    assert_int_equal(lame_set_mode(gfp, MAX_INDICATOR), -1);
    assert_int_equal(lame_get_mode(NULL), NOT_SET);

    /* VBR: 0 .. vbr_max_indicator-1 */
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_get_VBR(gfp), vbr_off);
    assert_int_equal(lame_set_VBR(gfp, vbr_mtrh), 0);
    assert_int_equal(lame_get_VBR(gfp), vbr_mtrh);
    assert_int_equal(lame_set_VBR(gfp, (vbr_mode) -1), -1);
    assert_int_equal(lame_set_VBR(gfp, vbr_max_indicator), -1);
    assert_int_equal(lame_get_VBR(NULL), vbr_off);

    /* emphasis: 0 .. 3 */
    assert_int_equal(lame_set_emphasis(gfp, 3), 0);
    assert_int_equal(lame_get_emphasis(gfp), 3);
    assert_int_equal(lame_set_emphasis(gfp, 4), -1);
    assert_int_equal(lame_set_emphasis(gfp, -1), -1);
    assert_int_equal(lame_get_emphasis(NULL), 0);

    /* strict_ISO: MDB_DEFAULT .. MDB_MAXIMUM */
    assert_int_equal(lame_set_strict_ISO(gfp, MDB_DEFAULT), 0);
    assert_int_equal(lame_get_strict_ISO(gfp), MDB_DEFAULT);
    assert_int_equal(lame_set_strict_ISO(gfp, MDB_MAXIMUM), 0);
    assert_int_equal(lame_get_strict_ISO(gfp), MDB_MAXIMUM);
    assert_int_equal(lame_set_strict_ISO(gfp, MDB_MAXIMUM + 1), -1);
    assert_int_equal(lame_set_strict_ISO(gfp, MDB_DEFAULT - 1), -1);
    assert_int_equal(lame_get_strict_ISO(NULL), 0);

    /* interChRatio: 0.0 .. 1.0 */
    assert_int_equal(lame_set_interChRatio(gfp, 0.0f), 0);
    ASSERT_FLT_EXACT(lame_get_interChRatio(gfp), 0.0f);
    assert_int_equal(lame_set_interChRatio(gfp, 1.0f), 0);
    ASSERT_FLT_EXACT(lame_get_interChRatio(gfp), 1.0f);
    assert_int_equal(lame_set_interChRatio(gfp, 1.5f), -1);
    assert_int_equal(lame_set_interChRatio(gfp, -0.5f), -1);
    ASSERT_FLT_EXACT(lame_get_interChRatio(NULL), 0.0f);
}

/*
 * ---- clamping setters -------------------------------------------------------
 */
static void
test_clamping(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* quality clamps silently to 0..9 and always returns 0 for a valid gfp */
    assert_int_equal(lame_set_quality(gfp, 3), 0);
    assert_int_equal(lame_get_quality(gfp), 3);
    assert_int_equal(lame_set_quality(gfp, -5), 0);
    assert_int_equal(lame_get_quality(gfp), 0);
    assert_int_equal(lame_set_quality(gfp, 100), 0);
    assert_int_equal(lame_get_quality(gfp), 9);
    assert_int_equal(lame_set_quality(NULL, 3), -1);
    assert_int_equal(lame_get_quality(NULL), 0);

    /* VBR_q clamps to 0..9 but flags the clamp with -1 */
    assert_int_equal(lame_set_VBR_q(gfp, 5), 0);
    assert_int_equal(lame_get_VBR_q(gfp), 5);
    assert_int_equal(lame_set_VBR_q(gfp, -1), -1);
    assert_int_equal(lame_get_VBR_q(gfp), 0);
    assert_int_equal(lame_set_VBR_q(gfp, 100), -1);
    assert_int_equal(lame_get_VBR_q(gfp), 9);
    assert_int_equal(lame_set_VBR_q(NULL, 5), -1);
    assert_int_equal(lame_get_VBR_q(NULL), 0);

    /* VBR_quality carries a fractional part; 4.5 -> int 4 + frac 0.5 */
    assert_int_equal(lame_set_VBR_quality(gfp, 4.5f), 0);
    ASSERT_FLT_NEAR(lame_get_VBR_quality(gfp), 4.5);
    assert_int_equal(lame_set_VBR_quality(gfp, -1.0f), -1);
    ASSERT_FLT_NEAR(lame_get_VBR_quality(gfp), 0.0);
    assert_int_equal(lame_set_VBR_quality(gfp, 100.0f), -1);
    assert_int_equal(lame_set_VBR_quality(NULL, 4.5f), -1);
    ASSERT_FLT_EXACT(lame_get_VBR_quality(NULL), 0.0f);
}

/*
 * ---- setters with side effects on other fields ------------------------------
 */
static void
test_side_effects(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* experimentalX fans out to quant_comp AND quant_comp_short */
    assert_int_equal(lame_set_experimentalX(gfp, 5), 0);
    assert_int_equal(lame_get_experimentalX(gfp), 5);
    assert_int_equal(lame_get_quant_comp(gfp), 5);
    assert_int_equal(lame_get_quant_comp_short(gfp), 5);
    assert_int_equal(lame_set_experimentalX(NULL, 5), -1);

    /* mode_automs forces JOINT_STEREO; its getter is a hard-wired 1 */
    assert_int_equal(lame_set_mode_automs(gfp, 1), 0);
    assert_int_equal(lame_get_mode(gfp), JOINT_STEREO);
    assert_int_equal(lame_get_mode_automs(gfp), 1);
    assert_int_equal(lame_set_mode_automs(gfp, 2), -1);
    assert_int_equal(lame_set_mode_automs(NULL, 1), -1);

    /* brate above 320 kbps forces the reservoir off */
    assert_int_equal(lame_set_disable_reservoir(gfp, 0), 0);
    assert_int_equal(lame_set_brate(gfp, 400), 0);
    assert_int_equal(lame_get_brate(gfp), 400);
    assert_int_equal(lame_get_disable_reservoir(gfp), 1);
    assert_int_equal(lame_set_brate(NULL, 128), -1);
    assert_int_equal(lame_get_brate(NULL), 0);

    /* allow_diff_short toggles the short_blocks coupling */
    assert_int_equal(lame_set_allow_diff_short(gfp, 1), 0);
    assert_int_equal(lame_get_allow_diff_short(gfp), 1);
    assert_int_equal(lame_set_allow_diff_short(gfp, 0), 0);
    assert_int_equal(lame_get_allow_diff_short(gfp), 0);
    assert_int_equal(lame_set_allow_diff_short(NULL, 1), -1);
    assert_int_equal(lame_get_allow_diff_short(NULL), 0);

    /* no_short_blocks: 0/1 accepted, mapped through the short_blocks enum */
    assert_int_equal(lame_set_no_short_blocks(gfp, 1), 0);
    assert_int_equal(lame_get_no_short_blocks(gfp), 1);
    assert_int_equal(lame_set_no_short_blocks(gfp, 0), 0);
    assert_int_equal(lame_get_no_short_blocks(gfp), 0);
    assert_int_equal(lame_set_no_short_blocks(gfp, 2), -1);
    assert_int_equal(lame_get_no_short_blocks(NULL), -1);

    /* force_short_blocks: setting then clearing returns to "not forced" */
    assert_int_equal(lame_set_force_short_blocks(gfp, 1), 0);
    assert_int_equal(lame_get_force_short_blocks(gfp), 1);
    assert_int_equal(lame_set_force_short_blocks(gfp, 0), 0);
    assert_int_equal(lame_get_force_short_blocks(gfp), 0);
    assert_int_equal(lame_set_force_short_blocks(gfp, 2), -1);
    assert_int_equal(lame_get_force_short_blocks(NULL), -1);
}

/*
 * ---- deprecated / obsolete stubs --------------------------------------------
 * Kept for ABI; they ignore input and return a fixed answer.
 */
static void
test_deprecated_stubs(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* ogg encoding was removed: set always fails, get is always 0 */
    assert_int_equal(lame_set_ogg(gfp, 1), -1);
    assert_int_equal(lame_get_ogg(gfp), 0);

    /* padding type is fixed at PAD_ADJUST */
    assert_int_equal(lame_set_padding_type(gfp, PAD_ALL), 0);
    assert_int_equal(lame_get_padding_type(gfp), PAD_ADJUST);

    /* the only surviving loudness approximation is number 2 */
    assert_int_equal(lame_set_athaa_loudapprox(gfp, 1), 0);
    assert_int_equal(lame_get_athaa_loudapprox(gfp), 2);

    /* cwlimit is a no-op that reads back 0 */
    assert_int_equal(lame_set_cwlimit(gfp, 5), 0);
    assert_int_equal(lame_get_cwlimit(gfp), 0);

    /* preset_expopts is accepted and discarded */
    assert_int_equal(lame_set_preset_expopts(gfp, 1), 0);

    /* findPeakSample is an alias of decode_on_the_fly; ReplayGain_input of
       findReplayGain. Both round-trip identically to their targets. */
    assert_int_equal(lame_set_ReplayGain_input(gfp, 1), 0);
    assert_int_equal(lame_get_ReplayGain_input(gfp), 1);
    assert_int_equal(lame_get_findReplayGain(gfp), 1);
    assert_int_equal(lame_set_ReplayGain_input(gfp, 0), 0);
    assert_int_equal(lame_get_ReplayGain_input(gfp), 0);
}

/*
 * ---- decode-on-the-fly: behaviour depends on build config -------------------
 * Without DECODE_ON_THE_FLY the setter rejects even a valid value; with it, the
 * usual 0/1 contract holds. Probe at runtime so the test is config-agnostic.
 */
static void
test_decode_on_the_fly(void **state)
{
    lame_t gfp = (lame_t) *state;
    int const r = lame_set_decode_on_the_fly(gfp, 0);
    if (r == 0) {
        assert_int_equal(lame_get_decode_on_the_fly(gfp), 0);
        assert_int_equal(lame_set_decode_on_the_fly(gfp, 1), 0);
        assert_int_equal(lame_get_decode_on_the_fly(gfp), 1);
        assert_int_equal(lame_set_decode_on_the_fly(gfp, 2), -1);
        /* findPeakSample is a straight alias */
        assert_int_equal(lame_get_findPeakSample(gfp), 1);
    }
    else {
        assert_int_equal(r, -1); /* built without decode-on-the-fly support */
    }
    assert_int_equal(lame_set_decode_on_the_fly(NULL, 0), -1);
    assert_int_equal(lame_get_decode_on_the_fly(NULL), 0);
}

/*
 * ---- message-handler and void setters ---------------------------------------
 */
static void
dummy_report(const char *fmt, va_list ap)
{
    (void) fmt;
    (void) ap;
}

static void
test_misc_setters(void **state)
{
    lame_t gfp = (lame_t) *state;

    assert_int_equal(lame_set_errorf(gfp, dummy_report), 0);
    assert_int_equal(lame_set_debugf(gfp, dummy_report), 0);
    assert_int_equal(lame_set_msgf(gfp, dummy_report), 0);
    assert_int_equal(lame_set_errorf(NULL, dummy_report), -1);
    assert_int_equal(lame_set_debugf(NULL, dummy_report), -1);
    assert_int_equal(lame_set_msgf(NULL, dummy_report), -1);

    /* msfix is a void setter with a float getter */
    lame_set_msfix(gfp, 0.5);
    ASSERT_FLT_EXACT(lame_get_msfix(gfp), 0.5f);
    lame_set_msfix(NULL, 0.5); /* must not crash */
    ASSERT_FLT_EXACT(lame_get_msfix(NULL), 0.0f);

    /* write_id3tag_automatic is a void setter; default read-back is 1 */
    lame_set_write_id3tag_automatic(gfp, 0);
    assert_int_equal(lame_get_write_id3tag_automatic(gfp), 0);
    lame_set_write_id3tag_automatic(gfp, 1);
    assert_int_equal(lame_get_write_id3tag_automatic(gfp), 1);
    lame_set_write_id3tag_automatic(NULL, 0); /* must not crash */
    assert_int_equal(lame_get_write_id3tag_automatic(NULL), 1);

    /* asm_optimizations echoes the optimisation id back; default case too */
    assert_int_equal(lame_set_asm_optimizations(gfp, MMX, 1), MMX);
    assert_int_equal(lame_set_asm_optimizations(gfp, AMD_3DNOW, 0), AMD_3DNOW);
    assert_int_equal(lame_set_asm_optimizations(gfp, SSE, 1), SSE);
    assert_int_equal(lame_set_asm_optimizations(gfp, 999, 1), 999); /* default arm */
    assert_int_equal(lame_set_asm_optimizations(NULL, MMX, 1), -1);

    /* preset routes into apply_preset; just exercise the valid and null arms */
    (void) lame_set_preset(gfp, V2);
    assert_int_equal(lame_set_preset(NULL, V2), -1);
}

/*
 * ---- read-only getters (populated by lame_init_params) ----------------------
 * These read through internal_flags, so they need a configured encoder. They
 * are exercised here mainly for coverage and NULL-safety; exact values are the
 * encoder's business, checked elsewhere.
 */
static void
test_readonly_getters(void **state)
{
    lame_t gfp = (lame_t) *state;

    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    /* MPEG-1 at 44.1 kHz -> version 1, one granule pair -> framesize 1152 */
    assert_int_equal(lame_get_version(gfp), 1);
    assert_int_equal(lame_get_framesize(gfp), 1152);
    assert_true(lame_get_encoder_delay(gfp) > 0);
    assert_int_equal(lame_get_frameNum(gfp), 0); /* nothing encoded yet */

    /* these must not crash and must be internally consistent; values vary */
    (void) lame_get_encoder_padding(gfp);
    (void) lame_get_mf_samples_to_encode(gfp);
    (void) lame_get_size_mp3buffer(gfp);
    (void) lame_get_RadioGain(gfp);
    (void) lame_get_AudiophileGain(gfp);
    (void) lame_get_PeakSample(gfp);
    (void) lame_get_noclipGainChange(gfp);
    (void) lame_get_noclipScale(gfp);
    assert_true(lame_get_maximum_number_of_samples(gfp, 65536) > 0);

    /* NULL arm of every read-only getter */
    assert_int_equal(lame_get_version(NULL), 0);
    assert_int_equal(lame_get_encoder_delay(NULL), 0);
    assert_int_equal(lame_get_encoder_padding(NULL), 0);
    assert_int_equal(lame_get_framesize(NULL), 0);
    assert_int_equal(lame_get_frameNum(NULL), 0);
    assert_int_equal(lame_get_mf_samples_to_encode(NULL), 0);
    assert_int_equal(lame_get_size_mp3buffer(NULL), 0);
    assert_int_equal(lame_get_RadioGain(NULL), 0);
    assert_int_equal(lame_get_AudiophileGain(NULL), 0);
    ASSERT_FLT_EXACT(lame_get_PeakSample(NULL), 0.0f);
    assert_int_equal(lame_get_noclipGainChange(NULL), 0);
    ASSERT_FLT_EXACT(lame_get_noclipScale(NULL), 0.0f);
    assert_int_equal(lame_get_maximum_number_of_samples(NULL, 65536), LAME_GENERICERROR);
}

/*
 * ---- internal-only tuning setters (INTERNAL_OPTS builds) --------------------
 * Same three-way probe as the exported API: round-trip, invalid gfp, and the
 * validation-reject arm where one exists.
 */
#if INTERNAL_OPTS
static void
test_internal_opts(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* plain float round-trips */
    assert_int_equal(lame_set_maskingadjust(gfp, 0.5f), 0);
    ASSERT_FLT_EXACT(lame_get_maskingadjust(gfp), 0.5f);
    assert_int_equal(lame_set_maskingadjust(NULL, 0.5f), -1);
    ASSERT_FLT_EXACT(lame_get_maskingadjust(NULL), 0.0f);

    assert_int_equal(lame_set_maskingadjust_short(gfp, 0.5f), 0);
    ASSERT_FLT_EXACT(lame_get_maskingadjust_short(gfp), 0.5f);
    assert_int_equal(lame_set_maskingadjust_short(NULL, 0.5f), -1);

    assert_int_equal(lame_set_ATHcurve(gfp, 0.5f), 0);
    ASSERT_FLT_EXACT(lame_get_ATHcurve(gfp), 0.5f);
    assert_int_equal(lame_set_ATHcurve(NULL, 0.5f), -1);

    /* short_threshold lrm/s, individually and combined */
    assert_int_equal(lame_set_short_threshold_lrm(gfp, 1.5f), 0);
    ASSERT_FLT_EXACT(lame_get_short_threshold_lrm(gfp), 1.5f);
    assert_int_equal(lame_set_short_threshold_s(gfp, 2.5f), 0);
    ASSERT_FLT_EXACT(lame_get_short_threshold_s(gfp), 2.5f);
    assert_int_equal(lame_set_short_threshold(gfp, 3.5f, 4.5f), 0);
    ASSERT_FLT_EXACT(lame_get_short_threshold_lrm(gfp), 3.5f);
    ASSERT_FLT_EXACT(lame_get_short_threshold_s(gfp), 4.5f);
    assert_int_equal(lame_set_short_threshold_lrm(NULL, 1.5f), -1);
    assert_int_equal(lame_set_short_threshold_s(NULL, 2.5f), -1);
    assert_int_equal(lame_set_short_threshold(NULL, 1.5f, 2.5f), -1);
    ASSERT_FLT_EXACT(lame_get_short_threshold_lrm(NULL), 0.0f);
    ASSERT_FLT_EXACT(lame_get_short_threshold_s(NULL), 0.0f);

    /* substep: range-validated 0..7 */
    assert_int_equal(lame_set_substep(gfp, 7), 0);
    assert_int_equal(lame_get_substep(gfp), 7);
    assert_int_equal(lame_set_substep(gfp, 8), -1);
    assert_int_equal(lame_set_substep(gfp, -1), -1);
    assert_int_equal(lame_set_substep(NULL, 7), -1);
    assert_int_equal(lame_get_substep(NULL), 0);

    /* sfscale maps to noise_shaping 2/1 */
    assert_int_equal(lame_set_sfscale(gfp, 1), 0);
    assert_int_equal(lame_get_sfscale(gfp), 1);
    assert_int_equal(lame_set_sfscale(gfp, 0), 0);
    assert_int_equal(lame_get_sfscale(gfp), 0);
    assert_int_equal(lame_set_sfscale(NULL, 1), -1);
    assert_int_equal(lame_get_sfscale(NULL), 0);

    /* subblock_gain: plain int */
    assert_int_equal(lame_set_subblock_gain(gfp, 7), 0);
    assert_int_equal(lame_get_subblock_gain(gfp), 7);
    assert_int_equal(lame_set_subblock_gain(NULL, 7), -1);
    assert_int_equal(lame_get_subblock_gain(NULL), 0);

    /* preset_notune is an accept-and-discard stub - returns 0 unconditionally */
    assert_int_equal(lame_set_preset_notune(gfp, 1), 0);
    assert_int_equal(lame_set_preset_notune(NULL, 1), 0);

    /* tune is a void internal setter with no getter; exercise both arms */
    lame_set_tune(gfp, 2.0f);
    lame_set_tune(NULL, 2.0f); /* must not crash */
}
#endif /* INTERNAL_OPTS */

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_boolean_validated, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_int_roundtrip, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_float_roundtrip, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_ranges, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_clamping, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_side_effects, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_deprecated_stubs, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_decode_on_the_fly, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_misc_setters, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_readonly_getters, gfp_setup, gfp_teardown),
#if INTERNAL_OPTS
        cmocka_unit_test_setup_teardown(test_internal_opts, gfp_setup, gfp_teardown),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
