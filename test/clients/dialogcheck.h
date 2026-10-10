/**
 * @file
 * @brief Layout checks for the dialogs of the Windows client components.
 *
 * The ACM codec and the DirectShow filter lay out their dialogs by the same
 * rules, and their tests check them with the same function,
 * check_dialog_layout(). Include it after ctest.h.
 */

#ifndef LAME_TEST_CLIENTS_DIALOGCHECK_H
#define LAME_TEST_CLIENTS_DIALOGCHECK_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/**
 * @brief Gets the font that the desktop uses for dialogs and message boxes,
 *        from the system and not from the component under test.
 * @param font  receives the font.
 * @return 1, or 0 if the system does not report it.
 */
static int
desktop_dialog_font(LOGFONTW *font)
{
    NONCLIENTMETRICSW metrics;

    memset(&metrics, 0, sizeof metrics);
    metrics.cbSize = sizeof metrics;
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0))
        return 0;
    *font = metrics.lfMessageFont;
    return 1;
}

/** @brief Room for the text of one control of a dialog. */
enum { CONTROL_TEXT_CHARS = 256 };

/**
 * @brief Finds the access key of a control text: the character after a
 *        single "&".
 * @param text  the text.
 * @return the key in upper case, or 0 when the text has none.
 */
static WCHAR
access_key(const WCHAR *text)
{
    size_t i;

    for (i = 0; text[i] != 0; i++) {
        if (text[i] == L'&') {
            if (text[i + 1] != L'&')
                return (WCHAR) towupper(text[i + 1]);
            i++;
        }
    }
    return 0;
}

/**
 * @brief Measures one line of a control text as the control draws it: a
 *        single "&" marks the access key and takes no room.
 * @param dc    a device context with the dialog's font selected.
 * @param text  the text.
 * @return the size of the text in pixels.
 */
static SIZE
text_extent(HDC dc, const WCHAR *text)
{
    WCHAR shown[CONTROL_TEXT_CHARS];
    size_t i, n = 0;
    SIZE extent = { 0, 0 };

    for (i = 0; text[i] != 0 && n + 1 < CONTROL_TEXT_CHARS; i++) {
        if (text[i] == L'&' && text[i + 1] != 0)
            i++;
        shown[n++] = text[i];
    }
    shown[n] = 0;
    ::GetTextExtentPoint32W(dc, shown, (int) n, &extent);
    return extent;
}

/**
 * @brief Checks the layout of a dialog that the test created: each control
 *        lies inside the dialog, its text fits into it, and the controls
 *        have their own access keys.
 *
 * A static text may wrap, as the control wraps it. A check box needs room
 * for its box and one average character besides the text, a group box and a
 * push button one average character on each side. A combo box needs room for
 * its longest item and the drop-down button. Only the visible controls count:
 * a dialog with tabs is checked one tab at a time.
 *
 * @param dialog  the dialog, with its texts filled in.
 * @param keys_expected  how many visible controls have an access key.
 * @param what    the dialog and the font, for the check names.
 */
static void
check_dialog_layout(HWND dialog, size_t keys_expected, const char *what)
{
    HFONT const font = (HFONT) ::SendMessageW(dialog, WM_GETFONT, 0, 0);
    HDC const dc = ::GetDC(dialog);
    HGDIOBJ const old_font = ::SelectObject(dc, font);
    WCHAR keys[CONTROL_TEXT_CHARS];
    size_t nkeys = 0;
    int outside = 0, cut = 0, duplicate = 0, measured = 0;
    TEXTMETRICW metrics;
    RECT client;
    HWND child;
    char name[200];

    ::GetTextMetricsW(dc, &metrics);
    ::GetClientRect(dialog, &client);
    for (child = ::GetWindow(dialog, GW_CHILD); child != NULL; child = ::GetWindow(child, GW_HWNDNEXT)) {
        WCHAR text[CONTROL_TEXT_CHARS], class_name[32];
        LONG const style = ::GetWindowLongW(child, GWL_STYLE);
        RECT box, room;
        LONG need_width = 0, need_height = 0;
        WCHAR key;
        size_t k;

        /* A hidden control, such as one of another tab, is not checked. */
        if ((style & WS_VISIBLE) == 0)
            continue;
        ::GetWindowRect(child, &box);
        ::MapWindowPoints(NULL, dialog, (POINT *) &box, 2);
        ::GetClientRect(child, &room);
        ::GetClassNameW(child, class_name, sizeof class_name / sizeof class_name[0]);
        ::GetWindowTextW(child, text, CONTROL_TEXT_CHARS);
        if (box.left < client.left || box.top < client.top || box.right > client.right || box.bottom > client.bottom) {
            outside++;
            printf("        outside the dialog: \"%ls\"\n", text);
        }
        key = access_key(text);
        if (key != 0) {
            for (k = 0; k < nkeys; k++)
                if (keys[k] == key)
                    break;
            if (k < nkeys) {
                duplicate++;
                printf("        access key %lc used twice: \"%ls\"\n", key, text);
            } else if (nkeys < CONTROL_TEXT_CHARS) {
                keys[nkeys++] = key;
            }
        }
        if (_wcsicmp(class_name, L"Button") == 0) {
            SIZE const extent = text_extent(dc, text);
            LONG const type = style & BS_TYPEMASK;

            if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX)
                need_width = ::GetSystemMetrics(SM_CXMENUCHECK) + metrics.tmAveCharWidth + extent.cx;
            else
                need_width = 2 * metrics.tmAveCharWidth + extent.cx;
            need_height = (type == BS_GROUPBOX) ? 0 : extent.cy;
        } else if (_wcsicmp(class_name, L"Static") == 0) {
            if ((style & SS_TYPEMASK) == SS_ICON)
                continue;
            RECT wrapped = { 0, 0, room.right, 0 };
            ::DrawTextW(dc, text, -1, &wrapped, DT_CALCRECT | DT_WORDBREAK);
            need_width = wrapped.right;
            need_height = wrapped.bottom;
        } else if (_wcsicmp(class_name, L"ComboBox") == 0) {
            LRESULT const items = ::SendMessageW(child, CB_GETCOUNT, 0, 0);
            LRESULT i;

            for (i = 0; i < items; i++) {
                WCHAR item[CONTROL_TEXT_CHARS];
                LRESULT const length = ::SendMessageW(child, CB_GETLBTEXTLEN, i, 0);

                if (length < 0 || length >= CONTROL_TEXT_CHARS)
                    continue;
                ::SendMessageW(child, CB_GETLBTEXT, i, (LPARAM) item);
                SIZE const extent = text_extent(dc, item);
                LONG const width = ::GetSystemMetrics(SM_CXVSCROLL) + metrics.tmAveCharWidth + extent.cx;
                if (width > need_width)
                    need_width = width;
            }
        } else {
            continue;
        }
        measured++;
        if (need_width > room.right || need_height > room.bottom) {
            cut++;
            printf("        cut off: \"%ls\" needs %ldx%ld, has %ldx%ld\n", text, (long) need_width,
                   (long) need_height, (long) room.right, (long) room.bottom);
        }
    }
    ::SelectObject(dc, old_font);
    ::ReleaseDC(dialog, dc);
    snprintf(name, sizeof name, "%s: every control lies inside the dialog", what);
    CHECK(outside == 0, name);
    snprintf(name, sizeof name, "%s: every text fits into its control", what);
    CHECK(measured > 0 && cut == 0, name);
    snprintf(name, sizeof name, "%s: %u controls have an access key, each its own", what, (unsigned) keys_expected);
    CHECK(nkeys == keys_expected && duplicate == 0, name);
    printf("        %d texts measured, %u access keys\n", measured, (unsigned) nkeys);
}

/**
 * @brief Tells whether the text of a static control fits into it, wrapped as
 *        the control wraps it.
 * @param control  the static control, with its text set.
 * @return 1 if the text fits, else 0.
 */
static inline int
static_text_fits(HWND control)
{
    WCHAR text[CONTROL_TEXT_CHARS];
    HFONT const font = (HFONT) SendMessageW(control, WM_GETFONT, 0, 0);
    HDC const dc = GetDC(control);
    HGDIOBJ const old_font = SelectObject(dc, font);
    RECT room, wrapped;

    GetClientRect(control, &room);
    GetWindowTextW(control, text, CONTROL_TEXT_CHARS);
    SetRect(&wrapped, 0, 0, room.right, 0);
    DrawTextW(dc, text, -1, &wrapped, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(dc, old_font);
    ReleaseDC(control, dc);
    return wrapped.right <= room.right && wrapped.bottom <= room.bottom;
}

#endif /* LAME_TEST_CLIENTS_DIALOGCHECK_H */
