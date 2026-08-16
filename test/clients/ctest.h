/**
 * @file
 * @brief Assertions for the Windows client component tests.
 *
 * The ACM codec and the DirectShow filter are built by MSBuild and by nothing
 * else, so the CMocka suite under @c test/unit cannot reach them: both
 * components are @c EXTRA_DIST only and are never compiled by the autotools
 * build. These tests therefore stand on their own, and this header is what they
 * stand on instead of a framework.
 *
 * That is a deliberate choice rather than a gap. A test that ships in the
 * distribution has to run for whoever unpacks it, and requiring a test library
 * to be fetched and built first would make these the one part of the tree that
 * does not work out of the box. Nothing here needs more than counting: both
 * components are reached through their own entry points - a @c DriverProc the
 * test loads, a class factory it calls - so there is nothing to mock.
 *
 * Each test program prints one line per check and returns non-zero if any of
 * them failed, which is the contract @c maintainer/smoke-clients.ps1 already
 * uses and the one a build cell can read.
 */

#ifndef LAME_TEST_CLIENTS_CTEST_H
#define LAME_TEST_CLIENTS_CTEST_H

#include <stdio.h>
#include <math.h>

/**
 * @brief Room for one check's detail line.
 *
 * The longest of them is two numbers and a few words of prose, so this is
 * generous rather than calculated - the point of the name is that the four
 * macros below cannot drift apart.
 */
#define CTEST_DETAIL_CHARS 128

/** @brief Checks attempted so far. A run that attempts none is a failure. */
static int ctest_checks = 0;
/** @brief Checks that failed. */
static int ctest_failures = 0;

/** @brief Records one check and prints its outcome with a detail line. */
static void
ctest_record(int ok, const char *what, const char *detail)
{
    ++ctest_checks;
    if (!ok) {
        ++ctest_failures;
    }
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (detail != NULL && detail[0] != '\0') {
        printf("        %s\n", detail);
    }
}

/** @brief Asserts a condition. */
#define CHECK(cond, what) \
    ctest_record((cond) ? 1 : 0, (what), "")

/** @brief Asserts an unsigned equality, reporting both values when it fails. */
#define CHECK_EQ_U(got, want, what)                                      \
    do {                                                                 \
        unsigned long ctest_g_ = (unsigned long) (got);                  \
        unsigned long ctest_w_ = (unsigned long) (want);                 \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "got %lu, wanted %lu", ctest_g_, ctest_w_);    \
        ctest_record(ctest_g_ == ctest_w_, (what), ctest_d_);            \
    } while (0)

/**
 * @brief Asserts a double equality within @a tol.
 *
 * The smart output ratio is a configured decimal that survives a round trip
 * through XML, so what matters is that the fractional part is still there -
 * not that the bits are identical.
 */
#define CHECK_EQ_D(got, want, tol, what)                                 \
    do {                                                                 \
        double ctest_g_ = (double) (got);                                \
        double ctest_w_ = (double) (want);                               \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "got %f, wanted %f", ctest_g_, ctest_w_);      \
        ctest_record(fabs(ctest_g_ - ctest_w_) <= (tol), (what), ctest_d_); \
    } while (0)

/**
 * @brief Asserts two values differ - the shape most controls here need.
 *
 * The detail line names both values whatever the outcome. A message that only
 * describes the failure has to be written as though the check failed, and then
 * says something false on every run that passed.
 */
#define CHECK_NE_U(a, b, what)                                           \
    do {                                                                 \
        unsigned long ctest_a_ = (unsigned long) (a);                    \
        unsigned long ctest_b_ = (unsigned long) (b);                    \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "%lu and %lu", ctest_a_, ctest_b_);            \
        ctest_record(ctest_a_ != ctest_b_, (what), ctest_d_);            \
    } while (0)

/** @brief Asserts an HRESULT succeeded, reporting the code when it did not. */
#define REQUIRE_HR(hr, what)                                             \
    do {                                                                 \
        HRESULT ctest_hr_ = (hr);                                        \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "hr = 0x%08lX", (unsigned long) ctest_hr_);    \
        ctest_record(SUCCEEDED(ctest_hr_), (what), ctest_d_);            \
    } while (0)

/**
 * @brief Prints the summary and returns the program's exit status.
 *
 * A run of zero checks reports failure. A test that stopped early - a component
 * that would not load, a stream that would not open - otherwise exits 0 having
 * asserted nothing, which reads as a pass in every caller that only looks at
 * the status.
 */
static int
ctest_summary(const char *name)
{
    if (ctest_checks == 0) {
        printf("%s: no checks ran\n", name);
        return 1;
    }
    printf("%s: %d checks, %d failed\n", name, ctest_checks, ctest_failures);
    return ctest_failures == 0 ? 0 : 1;
}

#endif /* LAME_TEST_CLIENTS_CTEST_H */
