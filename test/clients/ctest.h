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

#include <windows.h>
#include <stdio.h>
#include <string.h>
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

/** @brief One full turn of the circle, for the argument of the sine. */
#define CTEST_TWO_PI 6.283185307179586

/**
 * @brief Returns one sample of a sine tone.
 * @param n          the index of the sample from the start of the stream.
 * @param rate       the sample rate, in Hz.
 * @param hz         the frequency of the tone, in Hz.
 * @param amplitude  the peak value. It must fit a 16-bit sample.
 * @return the sample, truncated toward zero.
 */
static short
ctest_tone(unsigned long n, unsigned long rate, double hz, double amplitude)
{
    return (short) (amplitude * sin(CTEST_TWO_PI * hz * (double) n / (double) rate));
}

/**
 * @brief Counts the allocated blocks of the process heap.
 *
 * The C runtime allocates from the process heap, in this program and in the
 * component under test. A count before and after a repeated call shows
 * whether the call keeps memory allocated.
 *
 * @return the number of allocated blocks, or -1 if the heap cannot be walked.
 */
static inline long
ctest_heap_blocks(void)
{
    HANDLE heap = GetProcessHeap();
    PROCESS_HEAP_ENTRY entry;
    long blocks = 0;

    if (!HeapLock(heap)) {
        return -1;
    }
    entry.lpData = NULL;
    while (HeapWalk(heap, &entry)) {
        if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
            ++blocks;
        }
    }
    HeapUnlock(heap);
    return blocks;
}

/** @brief The argument that makes a missing component a failure. */
#define CTEST_REQUIRE_ARG "--require"

/** @brief What ctest_component_path() found. */
typedef enum {
    CTEST_NO_PATH,              /**< no path to the component could be formed */
    CTEST_ABSENT,               /**< the path names no file */
    CTEST_FOUND                 /**< the file is there */
} ctest_component;

/**
 * @brief Finds the component under test.
 *
 * An argument other than #CTEST_REQUIRE_ARG names the component. Without one,
 * the component is @p name in the directory of this executable. The build
 * writes it there. Whether a missing component is a skip or a failure
 * is the caller's decision.
 *
 * @param argc     the test's argument count.
 * @param argv     the test's arguments.
 * @param name     the file name of the component.
 * @param out      receives the path of the component.
 * @param n        the size of @p out.
 * @param require  set to 1 if #CTEST_REQUIRE_ARG was given, to 0 if not.
 * @return what was found at the path.
 */
static ctest_component
ctest_component_path(int argc, char **argv, const char *name,
                     char *out, size_t n, int *require)
{
    const char *given = NULL;
    int i, len;

    *require = 0;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], CTEST_REQUIRE_ARG, sizeof(CTEST_REQUIRE_ARG)) == 0) {
            *require = 1;
        } else {
            given = argv[i];
        }
    }

    if (given != NULL) {
        len = snprintf(out, n, "%s", given);
    } else {
        char self[MAX_PATH];
        char *slash;
        DWORD got = GetModuleFileNameA(NULL, self, MAX_PATH);

        if (got == 0 || got >= MAX_PATH || (slash = strrchr(self, '\\')) == NULL) {
            return CTEST_NO_PATH;
        }
        slash[1] = '\0';
        len = snprintf(out, n, "%s%s", self, name);
    }
    if (len < 0 || (size_t) len >= n) {
        return CTEST_NO_PATH;
    }
    return GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES ? CTEST_ABSENT : CTEST_FOUND;
}

/** @brief What a component wrote to its standard error, and where it went. */
typedef struct {
    char    path[MAX_PATH];     /**< the file that receives it */
    HMODULE module;             /**< the component, loaded with that file as its stderr */
} ctest_stderr;

/**
 * @brief Loads a component so that its standard error goes to a file.
 *
 * Each component links the C runtime statically, so its stderr is its own.
 * That runtime takes the process's standard error handle when the component
 * loads. The function points the handle at a new file for the load and puts
 * it back afterwards. The component stays loaded until the process ends, so a
 * later LoadLibrary() of the same file gets this copy and its stderr.
 *
 * It must be the first load of the component that runs its code. A load as a
 * data file does not count.
 *
 * @param dll  the path of the component.
 * @param err  receives the file and the module.
 * @return 1 if the file was made and the component loaded, else 0.
 */
static int
ctest_load_with_stderr_file(const char *dll, ctest_stderr *err)
{
    char    dir[MAX_PATH];
    HANDLE  file, before;

    err->module = NULL;
    if (GetTempPathA(MAX_PATH, dir) == 0 || GetTempFileNameA(dir, "lst", 0, err->path) == 0) {
        return 0;
    }
    file = CreateFileA(err->path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    before = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_ERROR_HANDLE, file);
    err->module = LoadLibraryA(dll);
    SetStdHandle(STD_ERROR_HANDLE, before);
    return err->module != NULL;
}

/**
 * @brief Checks that a component has written nothing to its standard error.
 *
 * The detail of a failure starts with what was written.
 *
 * @param err   the file, from ctest_load_with_stderr_file().
 * @param what  the description of the check.
 */
static void
ctest_stderr_empty(const ctest_stderr *err, const char *what)
{
    char    detail[CTEST_DETAIL_CHARS] = "";
    char    head[CTEST_DETAIL_CHARS / 2] = "";
    long    size = -1;
    FILE   *f = fopen(err->path, "rb");
    char   *p;

    if (f != NULL) {
        size_t got;

        if (fseek(f, 0, SEEK_END) == 0) {
            size = ftell(f);
        }
        rewind(f);
        got = fread(head, 1, sizeof(head) - 1, f);
        head[got] = '\0';
        fclose(f);
    }
    for (p = head; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
    snprintf(detail, sizeof detail, "%ld bytes: %s", size, head);
    ctest_record(size == 0, what, detail);
}

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
