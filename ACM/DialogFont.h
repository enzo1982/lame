/**
 * @file
 * @brief Dialogs in the font of the desktop, for the ACM codec and the
 *        DirectShow filter.
 *
 * A dialog resource names its font. These functions give a dialog the font
 * that the desktop uses for dialogs and message boxes instead, so the dialog
 * follows the typeface and the text size of the desktop. Dialog units derive
 * from the font, so the whole layout scales with it.
 */

#if !defined(_DIALOGFONT_H__INCLUDED_)
#define _DIALOGFONT_H__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <vector>

#include <windows.h>

bool SystemDialogFont(LOGFONTW & the_Font);
bool DialogTemplateWithFont(const HINSTANCE the_Instance, const WORD the_Dialog, const LOGFONTW & the_Font, std::vector<BYTE> & the_Template);
INT_PTR DialogBoxSystemFont(const HINSTANCE the_Instance, const WORD the_Dialog, const HWND the_Parent, const DLGPROC the_Proc, const LPARAM the_Param);

#endif // !defined(_DIALOGFONT_H__INCLUDED_)
