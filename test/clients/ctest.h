/**
 * @file
 * @brief Assertions for the Windows client component tests.
 *
 * The autotools build lists the ACM codec, the DirectShow filter and the
 * Blade encoder DLL as @c EXTRA_DIST only. It never compiles them. MSBuild is
 * the only build that compiles the ACM codec and the DirectShow filter. So the
 * CMocka suite under @c test/unit cannot test these components. The tests here
 * stand on their own, and this header takes the place of a test framework.
 *
 * This is a deliberate choice. A test that ships in the distribution must run
 * for anyone who unpacks it. If it needs a test library that must be fetched
 * and built first, these tests are the one part of the tree that does not
 * work out of the box. The tests need nothing more than counting. Each
 * component is called through its own entry points, for example a
 * @c DriverProc that the test loads or a class factory that it calls. So
 * nothing needs a mock.
 *
 * Each test program prints one line per check. It returns non-zero if any
 * check failed. @c maintainer/smoke-clients.ps1 uses the same convention, and
 * a build cell can read it.
 */

#ifndef LAME_TEST_CLIENTS_CTEST_H
#define LAME_TEST_CLIENTS_CTEST_H

#include <stdio.h>
#include <math.h>

/**
 * @brief Buffer size for the detail line of one check.
 *
 * The longest detail line has two numbers and a few words. This size is
 * generous, not calculated. The name makes sure that the four macros below
 * always use the same size.
 */
#define CTEST_DETAIL_CHARS 128

/** @brief Number of checks attempted so far. A run that attempts none fails. */
static int ctest_checks = 0;
/** @brief Number of checks that failed. */
static int ctest_failures = 0;

/**
 * @brief Starts a run: prints its name and turns off output buffering.
 *
 * These tests drive code that can crash the process. Examples are a driver
 * that mishandles a message, or a configuration file with an unexpected
 * shape. If the output is buffered when that happens, the buffer is lost with
 * the process. The transcript is then empty and does not show how far the run
 * got. Without buffering, the last line printed is the last thing that ran.
 */
static void
ctest_start(const char *name)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("%s\n", name);
}

/** @brief Records one check and prints its result and a detail line. */
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

/** @brief Asserts that two unsigned values are equal. The detail line shows both. */
#define CHECK_EQ_U(got, want, what)                                      \
    do {                                                                 \
        unsigned long ctest_g_ = (unsigned long) (got);                  \
        unsigned long ctest_w_ = (unsigned long) (want);                 \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "got %lu, wanted %lu", ctest_g_, ctest_w_);    \
        ctest_record(ctest_g_ == ctest_w_, (what), ctest_d_);            \
    } while (0)

/**
 * @brief Asserts that two doubles are equal within @a tol.
 *
 * The smart output ratio is a configured decimal number. It goes through a
 * round trip in an XML file. The check is that the fractional part survives.
 * The bits do not need to be identical.
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
 * @brief Asserts that two values differ. Most controls in these tests need this.
 *
 * The detail line shows both values, whatever the result. A message that only
 * describes the failure must be worded as if the check failed. It then says
 * something false on every run that passes.
 */
#define CHECK_NE_U(a, b, what)                                           \
    do {                                                                 \
        unsigned long ctest_a_ = (unsigned long) (a);                    \
        unsigned long ctest_b_ = (unsigned long) (b);                    \
        char ctest_d_[CTEST_DETAIL_CHARS];                               \
        sprintf(ctest_d_, "%lu and %lu", ctest_a_, ctest_b_);            \
        ctest_record(ctest_a_ != ctest_b_, (what), ctest_d_);            \
    } while (0)

/** @brief Asserts that an HRESULT is a success code. The detail line shows the code. */
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
 * A run of zero checks fails. Without this rule, a test that stopped early
 * exits 0 having checked nothing. Examples are a component that does not load
 * or a stream that does not open. Any caller that reads only the exit status
 * then sees a pass.
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
