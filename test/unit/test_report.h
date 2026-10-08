/**
 * @file
 * @ingroup unit_tests
 * @brief Collects what the library reports through a message or an error
 *        callback.
 *
 * A test installs report_capture() with lame_set_errorf() or lame_set_msgf(),
 * and calls report_reset() before the call under test. The collected text and
 * the count are file-scope state, so a program has one collector.
 */
#ifndef LAME_TEST_REPORT_H
#define LAME_TEST_REPORT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/** @brief The size of #report_text, in bytes. */
#define REPORT_TEXT_BYTES 65536
/** @brief The longest report line that is kept whole, in bytes. */
#define REPORT_LINE_BYTES 1024

/** @brief The text that report_capture() collected. */
static char report_text[REPORT_TEXT_BYTES];
/** @brief The number of bytes in #report_text. */
static size_t report_len;
/** @brief The number of calls of report_capture(). */
static int report_calls;

/**
 * @brief Collects one report of the library. This is a report callback.
 *
 * Each call counts. The text of a call is appended to #report_text, or left
 * out when it does not fit there.
 *
 * @param format  the printf format string.
 * @param ap      the arguments for @p format.
 */
static inline void
report_capture(const char *format, va_list ap)
{
    char    line[REPORT_LINE_BYTES];
    size_t const room = sizeof report_text - report_len;
    int     n;

    report_calls++;
    if (format == NULL)
        return;
    n = vsnprintf(line, sizeof line, format, ap);
    if (n <= 0)
        return;
    n = snprintf(report_text + report_len, room, "%s", line);
    if (n > 0 && (size_t) n < room)
        report_len += (size_t) n;
    else
        report_text[report_len] = '\0';
}

/** @brief Empties the collector before a call that must fill it. */
static inline void
report_reset(void)
{
    report_len = 0;
    report_calls = 0;
    report_text[0] = '\0';
}

#endif /* LAME_TEST_REPORT_H */
