/**
 * @file
 * @brief Dialogs in the font of the desktop: the implementation of
 *        DialogFont.h.
 */

#if !defined(STRICT)
#define STRICT
#endif // STRICT

#include <limits.h>
#include <string.h>
#include <wchar.h>

#include "DialogFont.h"

/** \name The layout of a DLGTEMPLATEEX
    The fields up to the menu have fixed sizes; the menu, the window class
    and the title have variable sizes. With DS_SETFONT, the title is
    followed by the point size, the weight, the italic flag, the character
    set and the typeface name. The first control starts at the next
    DWORD boundary.
    @{ */
static const WORD   DIALOGEX_VERSION      = 1;
static const WORD   DIALOGEX_SIGNATURE    = 0xFFFF;
static const size_t DIALOGEX_STYLE_OFFSET = 12;
static const size_t DIALOGEX_MENU_OFFSET  = 26;
static const size_t DIALOGEX_FONT_BYTES   = 6;
/** @} */

/** \brief The first word of a template field that holds a number, not a text. */
static const WORD TEMPLATE_ORDINAL = 0xFFFF;
static const int POINTS_PER_INCH = 72;

/*!
	Skips a text of a dialog template: WCHARs up to and including the
	terminator.

	\param the_Template the template
	\param the_Size the size of the template in bytes
	\param the_Offset where the text starts
	\return where the next field starts, or 0 if the text runs past the end
*/
static size_t SkipTemplateText(const BYTE * the_Template, const size_t the_Size, size_t the_Offset)
{
	WORD character;

	do {
		if (the_Offset + sizeof character > the_Size)
			return 0;
		memcpy(&character, the_Template + the_Offset, sizeof character);
		the_Offset += sizeof character;
	} while (character != 0);
	return the_Offset;
}

/*!
	Skips the menu or the window class field of a dialog template. The
	field holds nothing (one 0 word), a number (TEMPLATE_ORDINAL and the
	number) or a text.

	\param the_Template the template
	\param the_Size the size of the template in bytes
	\param the_Offset where the field starts
	\return where the next field starts, or 0 if the field runs past the end
*/
static size_t SkipTemplateField(const BYTE * the_Template, const size_t the_Size, const size_t the_Offset)
{
	WORD first;

	if (the_Offset + sizeof first > the_Size)
		return 0;
	memcpy(&first, the_Template + the_Offset, sizeof first);
	if (first == TEMPLATE_ORDINAL)
		return (the_Offset + 2 * sizeof first <= the_Size) ? the_Offset + 2 * sizeof first : 0;
	return SkipTemplateText(the_Template, the_Size, the_Offset);
}

/*!
	\param the_Offset an offset in a dialog template
	\return the offset rounded up to the next DWORD boundary
*/
static size_t AlignToDword(const size_t the_Offset)
{
	return (the_Offset + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);
}

/*!
	Gets the font that the desktop uses for the text of dialog boxes and
	message boxes. Its typeface and size follow the desktop settings.

	\param the_Font receives the font
	\return true, or false if the system does not report it
*/
bool SystemDialogFont(LOGFONTW & the_Font)
{
	NONCLIENTMETRICSW metrics;

	memset(&metrics, 0, sizeof metrics);
	metrics.cbSize = sizeof metrics;
	if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0))
		return false;
	the_Font = metrics.lfMessageFont;
	return true;
}

/*!
	Copies a dialog template from the resources and gives the copy another
	font. The dialog units of a dialog derive from its font, so the whole
	layout grows and shrinks with the font.

	\param the_Instance the module that holds the dialog resource
	\param the_Dialog the resource ID of the dialog. It must be a
	       DIALOGEX resource with a FONT statement.
	\param the_Font the font the copy uses. Its height is in pixels of the
	       screen, as SystemDialogFont() returns it.
	\param the_Template receives the copy, for DialogBoxIndirectParam()
	\return true, or false if the resource is missing or is not such a
	        template
*/
bool DialogTemplateWithFont(const HINSTANCE the_Instance, const WORD the_Dialog, const LOGFONTW & the_Font, std::vector<BYTE> & the_Template)
{
	HRSRC const resource = ::FindResource(the_Instance, MAKEINTRESOURCE(the_Dialog), RT_DIALOG);
	HGLOBAL const loaded = (resource != NULL) ? ::LoadResource(the_Instance, resource) : NULL;
	const BYTE * const source = (loaded != NULL) ? (const BYTE *) ::LockResource(loaded) : NULL;
	size_t const size = (source != NULL) ? ::SizeofResource(the_Instance, resource) : 0;
	WORD version, signature;
	DWORD style;

	if (size < DIALOGEX_MENU_OFFSET)
		return false;
	memcpy(&version, source, sizeof version);
	memcpy(&signature, source + sizeof version, sizeof signature);
	memcpy(&style, source + DIALOGEX_STYLE_OFFSET, sizeof style);
	if (version != DIALOGEX_VERSION || signature != DIALOGEX_SIGNATURE || (style & DS_SETFONT) == 0)
		return false;

	size_t const menu_end = SkipTemplateField(source, size, DIALOGEX_MENU_OFFSET);
	size_t const class_end = (menu_end != 0) ? SkipTemplateField(source, size, menu_end) : 0;
	size_t const font_start = (class_end != 0) ? SkipTemplateText(source, size, class_end) : 0;
	size_t const font_end = (font_start != 0) ? SkipTemplateText(source, size, font_start + DIALOGEX_FONT_BYTES) : 0;
	size_t const items_start = AlignToDword(font_end);
	size_t const face_chars = wcsnlen(the_Font.lfFaceName, LF_FACESIZE);
	if (font_end == 0 || items_start > size || face_chars == 0 || face_chars == LF_FACESIZE)
		return false;

	HDC const screen = ::GetDC(NULL);
	if (screen == NULL)
		return false;
	int const pixels_per_inch = ::GetDeviceCaps(screen, LOGPIXELSY);
	::ReleaseDC(NULL, screen);
	LONG const pixels = (the_Font.lfHeight < 0) ? -the_Font.lfHeight : the_Font.lfHeight;
	int const points = ::MulDiv(pixels, POINTS_PER_INCH, pixels_per_inch);
	if (points <= 0 || points > USHRT_MAX)
		return false;

	WORD const point_size = (WORD) points;
	WORD const weight = (WORD) the_Font.lfWeight;
	BYTE const italic = (the_Font.lfItalic != 0) ? TRUE : FALSE;
	BYTE const charset = the_Font.lfCharSet;
	// The copy names its font, so the system must not replace it with the shell font.
	DWORD const named_font_style = style & ~DS_FIXEDSYS;
	const BYTE * const face = (const BYTE *) the_Font.lfFaceName;

	the_Template.assign(source, source + font_start);
	memcpy(&the_Template[DIALOGEX_STYLE_OFFSET], &named_font_style, sizeof named_font_style);
	the_Template.insert(the_Template.end(), (const BYTE *) &point_size, (const BYTE *) &point_size + sizeof point_size);
	the_Template.insert(the_Template.end(), (const BYTE *) &weight, (const BYTE *) &weight + sizeof weight);
	the_Template.push_back(italic);
	the_Template.push_back(charset);
	the_Template.insert(the_Template.end(), face, face + (face_chars + 1) * sizeof(WCHAR));
	the_Template.resize(AlignToDword(the_Template.size()), 0);
	the_Template.insert(the_Template.end(), source + items_start, source + size);
	return true;
}

/*!
	Shows a modal dialog from the resources in the font of the desktop
	(SystemDialogFont()). If that font or the template copy is not
	available, it shows the dialog as the resource describes it.

	\param the_Instance the module that holds the dialog resource
	\param the_Dialog the resource ID of the dialog
	\param the_Parent the owner window
	\param the_Proc the dialog procedure
	\param the_Param the value that WM_INITDIALOG passes to the_Proc
	\return the value of EndDialog(), or -1 if the dialog could not be shown
*/
INT_PTR DialogBoxSystemFont(const HINSTANCE the_Instance, const WORD the_Dialog, const HWND the_Parent, const DLGPROC the_Proc, const LPARAM the_Param)
{
	LOGFONTW font;
	std::vector<BYTE> dialog_template;

	if (SystemDialogFont(font) && DialogTemplateWithFont(the_Instance, the_Dialog, font, dialog_template))
		return ::DialogBoxIndirectParam(the_Instance, (LPCDLGTEMPLATE) &dialog_template[0], the_Parent, the_Proc, the_Param);
	return ::DialogBoxParam(the_Instance, MAKEINTRESOURCE(the_Dialog), the_Parent, the_Proc, the_Param);
}
