/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the instance-free library information API
 *        (version.c, tables.c).
 *
 * Everything tested here is a property of the build rather than of an encoder:
 * the version and build-information strings, and the two accessors that hand a
 * caller the MPEG bitrate and sample-rate tables. None of them takes a
 * lame_global_flags, so none of them is reachable from any test that starts by
 * calling lame_init() - which is why they were the largest block of exported
 * symbols no test named at all.
 *
 * The version functions are deliberately tested against each other rather than
 * against the version macros. Rebuilding the macro expressions in the test
 * would only assert that a copy of the code agrees with the code; asking
 * instead whether the four string forms and the numerical form report the same
 * version is a question that can actually come out wrong, and it is the one a
 * caller cares about. What the strings contain beyond that is documented as
 * unspecified, so nothing here asserts it.
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
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "test_unused.h"

#include "lame.h"

/*
 * ---- version and build information ------------------------------------------
 */

/**
 * @brief Every version string is non-NULL, non-empty and the same on re-entry.
 *
 * The documented contract is a pointer to a static string that stays valid for
 * the lifetime of the process; a second call returning the same pointer is the
 * observable half of that. get_lame_os_bitness() is excluded because it is the
 * one function documented as possibly returning an empty string.
 */
static void
test_version_strings_are_static(LAME_UNUSED void **state)
{
    const char *first[5];
    const char *again[5];
    size_t      i;

    first[0] = get_lame_version();
    first[1] = get_lame_short_version();
    first[2] = get_lame_very_short_version();
    first[3] = get_psy_version();
    first[4] = get_lame_url();

    again[0] = get_lame_version();
    again[1] = get_lame_short_version();
    again[2] = get_lame_very_short_version();
    again[3] = get_psy_version();
    again[4] = get_lame_url();

    for (i = 0; i < sizeof first / sizeof first[0]; ++i) {
        assert_non_null(first[i]);
        assert_true(strlen(first[i]) > 0);
        assert_ptr_equal(first[i], again[i]);
    }
}

/**
 * @brief get_lame_version_numerical() writes every field of the struct.
 *
 * Documented: the structure need not be initialised first, because every field
 * is assigned. Poisoning it beforehand is what turns that promise into
 * something a test can fail - a field the function forgot would still hold the
 * poison pattern afterwards.
 */
static void
test_version_numerical_fills_every_field(LAME_UNUSED void **state)
{
    int const     poison = (int) 0xA5A5A5A5;
    lame_version_t v;

    memset(&v, 0xA5, sizeof v);
    get_lame_version_numerical(&v);

    assert_int_not_equal(v.major, poison);
    assert_int_not_equal(v.minor, poison);
    assert_int_not_equal(v.alpha, poison);
    assert_int_not_equal(v.beta, poison);
    assert_int_not_equal(v.psy_major, poison);
    assert_int_not_equal(v.psy_minor, poison);
    assert_int_not_equal(v.psy_alpha, poison);
    assert_int_not_equal(v.psy_beta, poison);

    /* documented: retained for compatibility, always the empty string */
    assert_non_null(v.features);
    assert_string_equal(v.features, "");

    /* documented: at most one of the two is ever non-zero, for both versions */
    assert_true(v.alpha == 0 || v.beta == 0);
    assert_true(v.psy_alpha == 0 || v.psy_beta == 0);

    assert_true(v.major > 0);
    assert_true(v.minor >= 0);
    assert_true(v.psy_major > 0);
    assert_true(v.psy_minor >= 0);
}

/**
 * @brief The string forms and the numerical form report the same version.
 *
 * Each string is built from the version macros at its own site in version.c,
 * so agreement between them is not structural - a version bump that updates
 * one arm and not another is exactly the failure this catches. Only the
 * leading "major.minor" is checked: everything after it (build type, patch
 * level, build date) is documented as having no guaranteed layout.
 */
static void
test_version_strings_agree_with_numbers(LAME_UNUSED void **state)
{
    lame_version_t v;
    char           mm[64];
    size_t         n;

    get_lame_version_numerical(&v);

    n = (size_t) sprintf(mm, "%d.%d", v.major, v.minor);
    assert_true(strncmp(get_lame_version(), mm, n) == 0);
    assert_true(strncmp(get_lame_short_version(), mm, n) == 0);

    n = (size_t) sprintf(mm, "%d.%d", v.psy_major, v.psy_minor);
    assert_true(strncmp(get_psy_version(), mm, n) == 0);
}

/**
 * @brief The very short version has the fixed layout its documentation states.
 *
 * "LAME", the version, and exactly one build-type character - the one form
 * whose layout the API does promise, because callers copy it into fixed-size
 * fields.
 *
 * The build-type character can only be cross-checked one way. An alpha or beta
 * build reports its patch level in @c alpha or @c beta, so a non-zero one of
 * those must be matched by an 'a' or a 'b'. The converse does not hold: an
 * alpha at patch level 0 - which is what this tree currently is - leaves both
 * fields 0 and is numerically indistinguishable from a release, so a caller
 * cannot use them to ask "is this an alpha?".
 */
static void
test_very_short_version_layout(LAME_UNUSED void **state)
{
    const char *const vs = get_lame_very_short_version();
    lame_version_t    v;
    char              head[64];
    size_t            n;
    char              type;

    get_lame_version_numerical(&v);
    n = (size_t) sprintf(head, "LAME%d.%d", v.major, v.minor);

    assert_true(strlen(vs) > n);
    assert_true(strncmp(vs, head, n) == 0);

    type = vs[n];
    if (v.alpha != 0)
        assert_int_equal(type, 'a');
    else if (v.beta != 0)
        assert_int_equal(type, 'b');
    else
        assert_true(type == 'a' || type == 'b' || type == 'r' || type == ' ');
}

/**
 * @brief get_lame_os_bitness() reports the pointer width of this build.
 *
 * Documented as a property of the library, not of the operating system, so the
 * test asks the same question of its own translation unit.
 */
static void
test_os_bitness_matches_pointer_width(LAME_UNUSED void **state)
{
    const char *const bits = get_lame_os_bitness();

    assert_non_null(bits);
    switch (sizeof(void *)) {
    case 4:
        assert_string_equal(bits, "32bits");
        break;
    case 8:
        assert_string_equal(bits, "64bits");
        break;
    default:
        assert_string_equal(bits, "");
        break;
    }
}

/*
 * ---- the MPEG bitrate and sample-rate tables --------------------------------
 * These exist so a caller of the shared library can read tables that are not
 * themselves exported. The values are fixed by the MPEG specification and
 * cannot change, so they are asserted exactly; what the accessors add on top of
 * them is the range checking, which is the part that could be got wrong.
 */

/** @brief Bitrates for MPEG-2, MPEG-1 and MPEG-2.5, and the out-of-range arms. */
static void
test_bitrate_table(LAME_UNUSED void **state)
{
    /* index 0 is the free-format slot in all three tables */
    assert_int_equal(lame_get_bitrate(0, 0), 0);
    assert_int_equal(lame_get_bitrate(1, 0), 0);
    assert_int_equal(lame_get_bitrate(2, 0), 0);

    /* MPEG-2: 8..160 kbps */
    assert_int_equal(lame_get_bitrate(0, 1), 8);
    assert_int_equal(lame_get_bitrate(0, 14), 160);

    /* MPEG-1: 32..320 kbps */
    assert_int_equal(lame_get_bitrate(1, 1), 32);
    assert_int_equal(lame_get_bitrate(1, 9), 128);
    assert_int_equal(lame_get_bitrate(1, 14), 320);

    /* MPEG-2.5: 8..64 kbps, the rest of the row unused */
    assert_int_equal(lame_get_bitrate(2, 1), 8);
    assert_int_equal(lame_get_bitrate(2, 8), 64);
    assert_int_equal(lame_get_bitrate(2, 9), -1);

    /* index 15 is the "bad" slot of every row */
    assert_int_equal(lame_get_bitrate(0, 15), -1);
    assert_int_equal(lame_get_bitrate(1, 15), -1);
    assert_int_equal(lame_get_bitrate(2, 15), -1);

    /* out of range on either axis, in both directions */
    assert_int_equal(lame_get_bitrate(-1, 1), -1);
    assert_int_equal(lame_get_bitrate(3, 1), -1);
    assert_int_equal(lame_get_bitrate(1, -1), -1);
    assert_int_equal(lame_get_bitrate(1, 16), -1);
}

/** @brief Sample rates for the three MPEG versions, and the out-of-range arms. */
static void
test_samplerate_table(LAME_UNUSED void **state)
{
    assert_int_equal(lame_get_samplerate(0, 0), 22050);
    assert_int_equal(lame_get_samplerate(0, 1), 24000);
    assert_int_equal(lame_get_samplerate(0, 2), 16000);

    assert_int_equal(lame_get_samplerate(1, 0), 44100);
    assert_int_equal(lame_get_samplerate(1, 1), 48000);
    assert_int_equal(lame_get_samplerate(1, 2), 32000);

    assert_int_equal(lame_get_samplerate(2, 0), 11025);
    assert_int_equal(lame_get_samplerate(2, 1), 12000);
    assert_int_equal(lame_get_samplerate(2, 2), 8000);

    /* index 3 is the "bad" slot of every row */
    assert_int_equal(lame_get_samplerate(0, 3), -1);
    assert_int_equal(lame_get_samplerate(1, 3), -1);
    assert_int_equal(lame_get_samplerate(2, 3), -1);

    /* out of range on either axis, in both directions */
    assert_int_equal(lame_get_samplerate(-1, 0), -1);
    assert_int_equal(lame_get_samplerate(3, 0), -1);
    assert_int_equal(lame_get_samplerate(1, -1), -1);
    assert_int_equal(lame_get_samplerate(1, 4), -1);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_version_strings_are_static),
        cmocka_unit_test(test_version_numerical_fills_every_field),
        cmocka_unit_test(test_version_strings_agree_with_numbers),
        cmocka_unit_test(test_very_short_version_layout),
        cmocka_unit_test(test_os_bitness_matches_pointer_width),
        cmocka_unit_test(test_bitrate_table),
        cmocka_unit_test(test_samplerate_table),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
