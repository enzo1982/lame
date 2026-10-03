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
#include <stdint.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include <cmocka.h>

#include "lame.h"

/*
 * Deprecated ABI stubs: still exported from libmp3lame for binary back-compat,
 * but their prototypes were removed from lame.h (guarded out by
 * DEPRECATED_OR_OBSOLETE_CODE_REMOVED). They are live, reachable lines in
 * set_get.c, so we declare them locally to pin their fixed-answer behaviour
 * rather than leave that slice of the ABI untested. New code must not call
 * these. (Setters/getters that are neither exported nor reachable from the
 * frontend are dead code slated for removal and are therefore not tested.)
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
extern int          lame_set_ReplayGain_decode(lame_global_flags *, int);
extern int          lame_get_ReplayGain_decode(const lame_global_flags *);
extern int          lame_set_findPeakSample(lame_global_flags *, int);
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

/*
 * The instruction-set families the asm_optimizations enum names are x86
 * instruction sets and mean nothing on another architecture, so the test that
 * asserts their behaviour is compiled only where the build targets x86 - the
 * same scope the enum itself has. The getter and setter remain exported
 * everywhere; what is architecture-specific is what the families stand for,
 * not the interface.
 */
#if defined(__i386__) || defined(__i386) || defined(_M_IX86) \
 || defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) \
 || defined(_M_AMD64)
#define ASM_OPTIM_ARCH 1
#else
#define ASM_OPTIM_ARCH 0
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
   targets 1.1.x, so we do not use it.) */
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
 * Without the decoder the setter rejects even a valid value; with it, the
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

    /* The deprecated setter is the same alias in the other direction: whatever
       the build makes decode_on_the_fly answer, it must answer identically. */
    assert_int_equal(lame_set_findPeakSample(gfp, 1), lame_set_decode_on_the_fly(gfp, 1));
    assert_int_equal(lame_get_findPeakSample(gfp), lame_get_decode_on_the_fly(gfp));
    assert_int_equal(lame_set_findPeakSample(NULL, 1), -1);
}

/*
 * ---- ReplayGain_decode: the pair that combines two settings -----------------
 * Unlike the other deprecated aliases this one is not a forwarder - the setter
 * turns on decode_on_the_fly and findReplayGain together, and the getter is an
 * AND of the two. That makes "both on" the only state it reports, which is
 * what the assertions below pin: findReplayGain alone must not be enough.
 * Whether the pair can be turned on at all depends on the decoder, so
 * the build is probed at runtime the same way as above.
 */
static void
test_replaygain_decode(void **state)
{
    lame_t gfp = (lame_t) *state;
    int const r = lame_set_ReplayGain_decode(gfp, 1);

    if (r == 0) {
        assert_int_equal(lame_get_decode_on_the_fly(gfp), 1);
        assert_int_equal(lame_get_findReplayGain(gfp), 1);
        assert_int_equal(lame_get_ReplayGain_decode(gfp), 1);

        /* one half on is not the state this getter reports */
        assert_int_equal(lame_set_decode_on_the_fly(gfp, 0), 0);
        assert_int_equal(lame_get_findReplayGain(gfp), 1);
        assert_int_equal(lame_get_ReplayGain_decode(gfp), 0);

        assert_int_equal(lame_set_ReplayGain_decode(gfp, 0), 0);
        assert_int_equal(lame_get_findReplayGain(gfp), 0);
        assert_int_equal(lame_get_ReplayGain_decode(gfp), 0);
    }
    else {
        /* decode_on_the_fly is the first of the two and rejects the value, so
           the call fails before findReplayGain is touched */
        assert_int_equal(r, -1);
        assert_int_equal(lame_get_findReplayGain(gfp), 0);
        assert_int_equal(lame_get_ReplayGain_decode(gfp), 0);
    }

    assert_int_equal(lame_set_ReplayGain_decode(NULL, 1), -1);
    assert_int_equal(lame_get_ReplayGain_decode(NULL), 0);
}

/*
 * ---- the announced input length ---------------------------------------------
 * lame_set_num_samples() is exercised by test_totalframes.c; the getter and the
 * documented "length not known" default are not, and the default is the value a
 * caller has to be able to recognize.
 */
static void
test_num_samples(void **state)
{
    lame_t gfp = (lame_t) *state;

    /* fresh instance: the documented 2^32-1 sentinel, not a real count */
    assert_true(lame_get_num_samples(gfp) == 0xFFFFFFFFUL);

    assert_int_equal(lame_set_num_samples(gfp, 44100UL * 10), 0);
    assert_true(lame_get_num_samples(gfp) == 44100UL * 10);

    /* documented: no value is rejected, 0 included */
    assert_int_equal(lame_set_num_samples(gfp, 0), 0);
    assert_true(lame_get_num_samples(gfp) == 0);

    assert_int_equal(lame_set_num_samples(NULL, 1), -1);
    assert_true(lame_get_num_samples(NULL) == 0);
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
 * Several settings are stored with a negative "not chosen yet" marker that
 * lame_init_params() resolves later, and reading one back before initializing
 * is legal - it is how a caller finds out that nothing has been chosen. Every
 * such getter is asked on a fresh instance here.
 *
 * This is the shape of test the rest of the file was missing: the round-trip
 * tests set a value first, so none of them ever sees the marker. One getter
 * asserted a range that excluded its own marker and aborted the process
 * instead of answering; assertions are live in release builds of this library
 * (configure.ac leaves NDEBUG undefined on purpose), so it aborted there too.
 */
static void
test_unset_markers(void **state)
{
    lame_t  gfp = (lame_t) *state;

    /* the marker itself, on a fresh instance */
    assert_int_equal(lame_get_useTemporal(gfp), -1);
    ASSERT_FLT_EXACT(lame_get_interChRatio(gfp), -1.0);
    assert_int_equal(lame_get_quant_comp(gfp), -1);
    assert_int_equal(lame_get_quant_comp_short(gfp), -1);
    assert_int_equal(lame_get_ATHtype(gfp), -1);
    assert_int_equal(lame_get_athaa_type(gfp), -1);
    assert_int_equal(lame_get_no_short_blocks(gfp), -1);
    assert_int_equal(lame_get_force_short_blocks(gfp), -1);
    ASSERT_FLT_EXACT(lame_get_msfix(gfp), -1.0);

    /* and the marker gone afterwards. What each one resolves TO is a tuning
       decision - lame_init_params() applies the preset table for the chosen
       bitrate, so two of these end up at values from that table rather than at
       a plain zero - and pinning those here would make this a test of the
       tuning. The property being tested is that the marker does not survive. */
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    assert_int_not_equal(lame_get_quant_comp(gfp), -1);
    assert_int_not_equal(lame_get_quant_comp_short(gfp), -1);
    assert_int_not_equal(lame_get_ATHtype(gfp), -1);
    assert_true(lame_get_interChRatio(gfp) >= 0.0);
    assert_true(lame_get_msfix(gfp) >= 0.0);

    /* The adaptive ATH scheme resolves to a fixed value rather than one from
       the preset table, but which value is documented as LAME's own business
       and free to differ between releases - so the marker going away is the
       property to hold, and naming the number here would pin a choice the
       interface deliberately does not promise. */
    assert_int_not_equal(lame_get_athaa_type(gfp), -1);
    assert_true(lame_get_athaa_type(gfp) >= 0);

    /* these three do not come from the preset table, so their resolved values
       are fixed and can be named: temporal masking on, and the block-type
       choice settled to "coupled", which is neither dispensed nor forced */
    assert_int_equal(lame_get_useTemporal(gfp), 1);
    assert_int_equal(lame_get_no_short_blocks(gfp), 0);
    assert_int_equal(lame_get_force_short_blocks(gfp), 0);
}

/*
 * A scheme the caller chose explicitly must survive initialization untouched -
 * the resolution step exists to fill in an unset value, not to normalise a set
 * one. 0 is the interesting case: it is the value that means "off", and it is
 * also what an over-eager "is this set?" test would mistake for unset.
 */
static void
test_athaa_type_explicit_choice_survives(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_int_equal(lame_set_athaa_type(gfp, 0), 0);
    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    assert_int_equal(lame_get_athaa_type(gfp), 0);
}

/*
 * lame_get_asm_optimizations() answers per family, and its return values are
 * the point of it: the setter accepts a value naming no family at all, sets
 * nothing, and hands that value straight back, so "is this family one you
 * know?" is a question only the getter can answer. Every return value is
 * asserted here, the -2 arm included, because that is the one the item was
 * raised for.
 *
 * Compiled only on x86 - see ASM_OPTIM_ARCH above.
 */
#if ASM_OPTIM_ARCH
static void
test_asm_optimizations_roundtrip(void **state)
{
    lame_t  gfp = (lame_t) *state;
    int const families[] = { SSE, AVX2 };
    int const inert[] = { MMX, AMD_3DNOW };
    size_t  i;

    /* every family that gates something starts out allowed */
    for (i = 0; i < sizeof families / sizeof families[0]; ++i) {
        assert_int_equal(lame_get_asm_optimizations(gfp, families[i]), 1);
    }

    /* MMX and 3DNow! are known values with no code behind them: the setter
       refuses them and the getter never reports them as enabled, whatever
       was asked for. Both halves are asserted, because the pair is what a
       caller sees - a getter that still answered 1 after a refused set would
       be the misleading combination. */
    for (i = 0; i < sizeof inert / sizeof inert[0]; ++i) {
        assert_int_equal(lame_get_asm_optimizations(gfp, inert[i]), 0);
        assert_int_equal(lame_set_asm_optimizations(gfp, inert[i], 1), -2);
        assert_int_equal(lame_get_asm_optimizations(gfp, inert[i]), 0);
        assert_int_equal(lame_set_asm_optimizations(gfp, inert[i], 0), -2);
        assert_int_equal(lame_get_asm_optimizations(gfp, inert[i]), 0);
    }

    /* forbidding one is visible, and does not disturb the others */
    assert_int_equal(lame_set_asm_optimizations(gfp, SSE, 0), SSE);
    assert_int_equal(lame_get_asm_optimizations(gfp, SSE), 0);
    assert_int_equal(lame_get_asm_optimizations(gfp, AVX2), 1);

    /* and allowing it again is too. Only 1 allows: the setter treats every
       other mode as "forbid", so 2 must read back as forbidden. */
    assert_int_equal(lame_set_asm_optimizations(gfp, SSE, 1), SSE);
    assert_int_equal(lame_get_asm_optimizations(gfp, SSE), 1);
    assert_int_equal(lame_set_asm_optimizations(gfp, SSE, 2), SSE);
    assert_int_equal(lame_get_asm_optimizations(gfp, SSE), 0);

    /* a value naming no family: the setter cannot say so, the getter can */
    assert_int_equal(lame_set_asm_optimizations(gfp, 0, 1), 0);
    assert_int_equal(lame_get_asm_optimizations(gfp, 0), -2);
    assert_int_equal(lame_set_asm_optimizations(gfp, AVX2 + 1, 1), AVX2 + 1);
    assert_int_equal(lame_get_asm_optimizations(gfp, AVX2 + 1), -2);
    assert_int_equal(lame_get_asm_optimizations(gfp, -7), -2);

    /* an unusable instance is distinguishable from both of those */
    assert_int_equal(lame_get_asm_optimizations(NULL, SSE), -1);
    assert_int_equal(lame_get_asm_optimizations(NULL, 0), -1);
}
#endif /* ASM_OPTIM_ARCH */

/*
 * lame_get_maximum_number_of_samples() derives its answer from a size_t buffer
 * size and returns it in an int, so every step of the estimate has to be
 * bounded to that int - the frame count, their product with the frame size,
 * and the resampled result. At 44.1 kHz stereo 128 kbps CBR the arithmetic is
 * fully determined (418 bytes per frame, no resampling, and with the LAME tag
 * frame off nothing waiting in the stream), so both the ordinary answers and
 * the two ceilings can be asserted exactly.
 */
static void
test_maximum_number_of_samples(void **state)
{
    lame_t  gfp = (lame_t) *state;

    assert_int_equal(lame_set_in_samplerate(gfp, 44100), 0);
    assert_int_equal(lame_set_num_channels(gfp, 2), 0);
    assert_int_equal(lame_set_brate(gfp, 128), 0);
    assert_int_equal(lame_set_VBR(gfp, vbr_off), 0);
    assert_int_equal(lame_set_bWriteVbrTag(gfp, 0), 0);
    assert_int_equal(lame_init_params(gfp), 0);

    /* ordinary buffers: 65536/418 = 156 frames, 268435456/418 = 642190, one
       of them held back, 1152 samples each - these must be reported exactly,
       not clamped */
    assert_int_equal(lame_get_maximum_number_of_samples(gfp, 65536), 155 * 1152);
    assert_int_equal(lame_get_maximum_number_of_samples(gfp, 268435456), 642189 * 1152);

    /* a buffer whose frame count still fits an int but whose sample count does
       not, and the largest buffer expressible at all: both report the ceiling
       an encode call can accept, never a wrapped or negative estimate */
    assert_int_equal(lame_get_maximum_number_of_samples(gfp, (size_t) 780 * 1024 * 1024),
                     INT_MAX);
    assert_int_equal(lame_get_maximum_number_of_samples(gfp, (size_t) -1), INT_MAX);

    assert_int_equal(lame_get_maximum_number_of_samples(NULL, 65536), LAME_GENERICERROR);
}

/**
 * @brief lame_init_params() refuses an output rate more than 128 times the
 *        input rate, and accepts one exactly 128 times it.
 *
 * Both ways the output rate can come about: set by the caller, and picked by
 * LAME - which for a very low input rate is its lowest, 8 kHz.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_upsampling_ratio_limit(void **state)
{
    static const struct {
        int     in, out, accepted;
    } cases[] = {
        { 250, 32000, 1 },      /* 128 times */
        { 249, 32000, 0 },      /* just over */
        { 1, 44100, 0 },
        { 100, 0, 1 },          /* picked: 8000 Hz, 80 times */
        { 50, 0, 0 },           /* picked: 8000 Hz, 160 times */
        { 8000, 48000, 1 },
    };
    size_t  c;
    (void) state;
    for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        lame_t  gf = lame_init();
        int     r;
        assert_non_null(gf);
        assert_int_equal(lame_set_in_samplerate(gf, cases[c].in), 0);
        assert_int_equal(lame_set_out_samplerate(gf, cases[c].out), 0);
        r = lame_init_params(gf);
        if ((r >= 0) != cases[c].accepted)
            fail_msg("%d Hz to %d Hz: lame_init_params() answered %d", cases[c].in, cases[c].out, r);
        lame_close(gf);
    }
}

/**
 * @brief lame_init_params() refuses a variable bitrate floor above the
 *        ceiling, in every variable bitrate mode, and accepts one equal to it.
 *
 * The comparison is between the snapped rates: at 22.05 kHz a floor of 256
 * becomes 160, still above a ceiling of 128; at 11.025 kHz both become 64.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_vbr_floor_above_ceiling(void **state)
{
    static const vbr_mode modes[] = { vbr_abr, vbr_mt, vbr_rh, vbr_mtrh };
    static const struct {
        int     rate, floor_kbps, ceiling_kbps, accepted;
    } cases[] = {
        { 44100, 256, 128, 0 },
        { 44100, 64, 8, 0 },    /* the ceiling snaps to 32 */
        { 44100, 128, 128, 1 },
        { 44100, 128, 256, 1 },
        { 44100, 160, 0, 1 },   /* no ceiling asked for */
        { 22050, 256, 128, 0 },
        { 11025, 256, 128, 1 },
    };
    size_t  m, c;
    (void) state;
    for (m = 0; m < sizeof modes / sizeof modes[0]; ++m) {
        for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            lame_t  gf = lame_init();
            int     r;
            assert_non_null(gf);
            assert_int_equal(lame_set_out_samplerate(gf, cases[c].rate), 0);
            assert_int_equal(lame_set_VBR(gf, modes[m]), 0);
            assert_int_equal(lame_set_VBR_min_bitrate_kbps(gf, cases[c].floor_kbps), 0);
            assert_int_equal(lame_set_VBR_max_bitrate_kbps(gf, cases[c].ceiling_kbps), 0);
            r = lame_init_params(gf);
            if ((r >= 0) != cases[c].accepted)
                fail_msg("mode %d, %d Hz, floor %d, ceiling %d kbps: lame_init_params() answered %d",
                         (int) modes[m], cases[c].rate, cases[c].floor_kbps, cases[c].ceiling_kbps, r);
            lame_close(gf);
        }
    }
}

/**
 * @brief lame_init_params() copes with the extreme ints a caller can set for
 *        the bitrates and the lowpass frequency.
 *
 * Under UBSan with halt_on_error, arithmetic that overflows on the way fails
 * this test; without it, the test checks that the settings end up usable - a
 * bitrate from the table, a lowpass within the output band.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_extreme_int_settings(void **state)
{
    static const int values[] = { INT_MIN, INT_MAX };
    size_t  v, s;
    (void) state;
    for (v = 0; v < sizeof values / sizeof values[0]; ++v) {
        for (s = 0; s < 5; ++s) {
            lame_t  gf = lame_init();
            int     r;
            assert_non_null(gf);
            switch (s) {
            case 0: (void) lame_set_brate(gf, values[v]); break;
            case 1: (void) lame_set_VBR(gf, vbr_abr); (void) lame_set_VBR_mean_bitrate_kbps(gf, values[v]); break;
            case 2: (void) lame_set_VBR(gf, vbr_mtrh); (void) lame_set_VBR_min_bitrate_kbps(gf, values[v]); break;
            case 3: (void) lame_set_VBR(gf, vbr_mtrh); (void) lame_set_VBR_max_bitrate_kbps(gf, values[v]); break;
            default: (void) lame_set_lowpassfreq(gf, values[v]); break;
            }
            r = lame_init_params(gf);
            if (r >= 0) {
                assert_true(lame_get_brate(gf) >= 0 && lame_get_brate(gf) <= 320);
                assert_true(lame_get_lowpassfreq(gf) <= lame_get_out_samplerate(gf) / 2);
            }
            lame_close(gf);
        }
    }
}

/**
 * @brief A compression ratio small enough to ask for more kbit/s than an int
 *        holds gives the highest bitrate, not an overflow.
 *
 * Under UBSan with halt_on_error the conversion that used to overflow fails
 * this test; either way the result must be the format's top rate.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_tiny_compression_ratio(void **state)
{
    /* normal floats: a denormal could be flushed to 0 under fast maths */
    static const float ratios[] = { 1e-30f, 1e-20f };
    size_t  i;
    (void) state;
    for (i = 0; i < sizeof ratios / sizeof ratios[0]; ++i) {
        lame_t  gf = lame_init();
        assert_non_null(gf);
        assert_int_equal(lame_set_compression_ratio(gf, ratios[i]), 0);
        assert_true(lame_init_params(gf) >= 0);
        assert_int_equal(lame_get_brate(gf), 320);
        lame_close(gf);
    }
}

/**
 * @brief Assembles a float from its IEEE-754 bit pattern, unfoldable by the
 *        compiler - under the fast floating point maths these tests are built
 *        with, a NaN or an infinity the compiler can see is folded away.
 *
 * @param bits the bit pattern.
 * @return the float with that pattern.
 */
static float
float_from_bits(uint32_t bits)
{
    uint32_t volatile opaque = bits;
    uint32_t pattern;
    float   f;

    pattern = opaque;
    memcpy(&f, &pattern, sizeof f);
    return f;
}

/**
 * @brief The floating point setters refuse NaN and the infinities and keep
 *        the value they held, and an encode after such a refusal runs.
 *
 * Each setter first takes a valid value, then each non-finite one; the getter
 * must still report the valid value. lame_set_msfix() returns nothing, so for
 * it only the getter speaks.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_float_setters_refuse_nonfinite(void **state)
{
    static const uint32_t bad_bits[] = { 0x7FC00000u, 0x7F800000u, 0xFF800000u };
    static short pcm[1152 * 4];
    static unsigned char mp3[16384];
    size_t  b;
    (void) state;
    for (b = 0; b < sizeof bad_bits / sizeof bad_bits[0]; ++b) {
        float const bad = float_from_bits(bad_bits[b]);
        lame_t  gf = lame_init();
        assert_non_null(gf);
        assert_int_equal(lame_set_scale(gf, 0.5f), 0);
        assert_int_equal(lame_set_scale(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_scale(gf), 0.5f);
        assert_int_equal(lame_set_scale_left(gf, 0.5f), 0);
        assert_int_equal(lame_set_scale_left(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_scale_left(gf), 0.5f);
        assert_int_equal(lame_set_scale_right(gf, 0.5f), 0);
        assert_int_equal(lame_set_scale_right(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_scale_right(gf), 0.5f);
        assert_int_equal(lame_set_compression_ratio(gf, 11.0f), 0);
        assert_int_equal(lame_set_compression_ratio(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_compression_ratio(gf), 11.0f);
        lame_set_msfix(gf, 1.5);
        lame_set_msfix(gf, (double) bad);
        ASSERT_FLT_EXACT(lame_get_msfix(gf), 1.5f);
        assert_int_equal(lame_set_VBR_quality(gf, 2.5f), 0);
        assert_int_equal(lame_set_VBR_quality(gf, bad), -1);
        ASSERT_FLT_NEAR(lame_get_VBR_quality(gf), 2.5f);
        assert_int_equal(lame_set_ATHlower(gf, 3.0f), 0);
        assert_int_equal(lame_set_ATHlower(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_ATHlower(gf), 3.0f);
        assert_int_equal(lame_set_athaa_sensitivity(gf, -2.0f), 0);
        assert_int_equal(lame_set_athaa_sensitivity(gf, bad), -1);
        ASSERT_FLT_EXACT(lame_get_athaa_sensitivity(gf), -2.0f);
        assert_int_equal(lame_set_num_channels(gf, 1), 0);
        assert_true(lame_init_params(gf) >= 0);
        assert_true(lame_encode_buffer(gf, pcm, pcm, (int) (sizeof pcm / sizeof pcm[0]), mp3, sizeof mp3) >= 0);
        assert_true(lame_encode_flush(gf, mp3, sizeof mp3) >= 0);
        lame_close(gf);
    }
}

/**
 * @brief lame_get_maximum_number_of_samples() keeps its promise: that many
 *        samples per call never overflow the buffer it was asked about.
 *
 * Asked before every call, since what the encoder holds changes from call to
 * call: the first call also hands out what lame_init_params() wrote - the
 * ID3v2 tag, the LAME tag frame - and a resampling encoder returns the output
 * of input it held back the call before. White noise, the input that needs
 * the most bits; resampling up to a ratio of 80, free format, and a tag
 * carrying album art.
 *
 * @param state cmocka fixture state (unused).
 */
static void
test_maximum_number_of_samples_holds(void **state)
{
    static const struct {
        int     in, out, kbps, free_format, vbr, tag;
    } cases[] = {
        { 44100, 44100, 320, 0, 0, 0 },
        { 44100, 44100, 0, 0, 1, 1 },
        { 8000, 44100, 128, 0, 0, 1 },
        { 11025, 32000, 640, 1, 0, 0 },
        { 400, 32000, 32, 0, 0, 0 },
        { 100, 8000, 64, 0, 0, 1 },
    };
    static short pcm_l[65536], pcm_r[65536];
    static unsigned char mp3[16384], art[4096];
    unsigned int s = 1;
    size_t  c;
    int     i;
    (void) state;
    for (i = 0; i < 65536; ++i) {
        s = s * 1103515245u + 12345u;
        pcm_l[i] = (short) (s >> 16);
        s = s * 1103515245u + 12345u;
        pcm_r[i] = (short) (s >> 16);
    }
    memcpy(art, "\x89PNG", 4);
    for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        lame_t  gf = lame_init();
        int     call;
        assert_non_null(gf);
        assert_int_equal(lame_set_in_samplerate(gf, cases[c].in), 0);
        assert_int_equal(lame_set_out_samplerate(gf, cases[c].out), 0);
        assert_int_equal(lame_set_num_channels(gf, 2), 0);
        assert_int_equal(lame_set_quality(gf, 7), 0);
        if (cases[c].vbr) {
            assert_int_equal(lame_set_VBR(gf, vbr_mtrh), 0);
        }
        else {
            assert_int_equal(lame_set_brate(gf, cases[c].kbps), 0);
            assert_int_equal(lame_set_free_format(gf, cases[c].free_format), 0);
        }
        if (cases[c].tag) {
            id3tag_init(gf);
            id3tag_add_v2(gf);
            id3tag_set_title(gf, "a title");
            assert_int_equal(id3tag_set_albumart(gf, (const char *) art, sizeof art), 0);
        }
        assert_int_equal(lame_init_params(gf), 0);
        for (call = 0; call < 12; ++call) {
            int const n = lame_get_maximum_number_of_samples(gf, sizeof mp3);
            int     r;
            assert_true(n > 0);
            r = lame_encode_buffer(gf, pcm_l, pcm_r, n < 65536 ? n : 65536, mp3, sizeof mp3);
            if (r < 0)
                fail_msg("case %d, call %d: %d samples for %u bytes answered %d", (int) c, call, n,
                         (unsigned int) sizeof mp3, r);
        }
        lame_close(gf);
    }
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
        cmocka_unit_test_setup_teardown(test_replaygain_decode, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_num_samples, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_misc_setters, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_readonly_getters, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_unset_markers, gfp_setup, gfp_teardown),
        cmocka_unit_test_setup_teardown(test_athaa_type_explicit_choice_survives,
                                        gfp_setup, gfp_teardown),
#if ASM_OPTIM_ARCH
        cmocka_unit_test_setup_teardown(test_asm_optimizations_roundtrip,
                                        gfp_setup, gfp_teardown),
#endif
        cmocka_unit_test_setup_teardown(test_maximum_number_of_samples, gfp_setup, gfp_teardown),
        cmocka_unit_test(test_maximum_number_of_samples_holds),
        cmocka_unit_test(test_upsampling_ratio_limit),
        cmocka_unit_test(test_vbr_floor_above_ceiling),
        cmocka_unit_test(test_float_setters_refuse_nonfinite),
        cmocka_unit_test(test_extreme_int_settings),
        cmocka_unit_test(test_tiny_compression_ratio),
#if INTERNAL_OPTS
        cmocka_unit_test_setup_teardown(test_internal_opts, gfp_setup, gfp_teardown),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
