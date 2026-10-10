/**
 *
 * Lame ACM wrapper, encode MP3 based RIFF/AVI files in MS Windows
 *
 *  Copyright (c) 2002 Steve Lhomme <steve.lhomme at free.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/*!
	\author Steve Lhomme
	\version \$Id$
*/

#if !defined(STRICT)
#define STRICT
#endif // !defined(STRICT)

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"
#include <lame.h>
#include "adebug.h"
#include "AEncodeProperties.h"
#include "ACM.h"
#include "DialogFont.h"

#ifndef TTS_BALLOON
#define TTS_BALLOON            0x40
#endif // TTS_BALLOON

/** \brief The highest bitrate that LAME encodes, in kbit/s. */
static const unsigned int ABR_BITRATE_LIMIT = 320;
/// The largest step between two ABR bitrates that the dialog offers, in kbit/s.
static const unsigned int ABR_STEP_LIMIT = 16;
/// Room for the text of a VBR quality level, terminator included.
static const size_t VBR_TEXT_CHARS = 32;
/// Room for the line that counts the formats, terminator included.
static const size_t FORMAT_COUNT_CHARS = 80;


// The bitrates of the standard, in kbit/s, highest first: those of MPEG-1, those
// of MPEG-2 (which MPEG-2.5 uses here too), and both lists together.
// FillBitrateTables() fills them from the library before anything reads them.
// The dialog and the configuration file use positions in the_Bitrates.
unsigned int AEncodeProperties::the_Bitrates[18];
unsigned int AEncodeProperties::the_MPEG1_Bitrates[14];
unsigned int AEncodeProperties::the_MPEG2_Bitrates[14];
const unsigned int AEncodeProperties::the_ChannelModes[4] = { STEREO, JOINT_STEREO, DUAL_CHANNEL, MONO };

ToolTipItem AEncodeProperties::Tooltips[TOOLTIP_COUNT]={
	{ IDC_CHECK_ENC_CBR, "Offer constant bitrate formats: every frame has the\r\nbitrate of the format, for example \"44100 Hz,\r\nCBR 128 kbps, Stereo\"." },
	{ IDC_COMBO_CBR_MIN, "The lowest CBR bitrate offered. Each sample rate\r\noffers the bitrates of its MPEG version." },
	{ IDC_COMBO_CBR_MAX, "The highest CBR bitrate offered." },
	{ IDC_CHECK_ENC_ABR, "Offer average bitrate formats: the bitrate follows\r\nthe music and averages out at the bitrate of the\r\nformat. It can improve the quality for the same size." },
	{ IDC_COMBO_ABR_MIN, "The lowest ABR bitrate offered." },
	{ IDC_COMBO_ABR_MAX, "The highest ABR bitrate offered." },
	{ IDC_COMBO_ABR_STEP, "The distance between two ABR bitrates offered,\r\nfrom the highest down." },
	{ IDC_CHECK_ENC_VBR, "Offer one VBR format for each quality level\r\nfrom the best to the lowest one below.\r\n\r\nWith VBR the bitrate follows the music: simple\r\nor quiet passages get fewer bits. A program lists\r\neach format with its level and typical bitrate,\r\nfor example \"44100 Hz, VBR quality 2 (about\r\n190 kbps), Stereo\"." },
	{ IDC_COMBO_VBR_BEST, "The best quality level offered. 0 is the best and\r\ngives the largest files. The bitrate is typical for\r\nmusic at 44.1 kHz stereo." },
	{ IDC_COMBO_VBR_WORST, "The lowest quality level offered. 9 gives the\r\nsmallest files." },
	{ IDC_COMBO_VBR_MIN, "The lowest bitrate of a VBR stream. Silent frames\r\nstill use less, unless the minimum is strictly\r\nenforced. A stream uses the nearest bitrate\r\nthat its sample rate has." },
	{ IDC_COMBO_VBR_MAX, "The highest bitrate of a VBR stream. A stream\r\nuses the nearest bitrate that its sample rate has." },
	{ IDC_CHECK_VBR_ENFORCE_MIN, "Every frame uses at least the minimum bitrate,\r\nsilent frames too." },
	{ IDC_CHECK_ENC_SMART, "Leave out the formats that compress more than\r\nthis: a low bitrate at a high sample rate." },
	{ IDC_COMBO_ENC_STEREO, "Select the channel mode used for encoding:\r\n\r\n- Stereo: the usual one\r\n- Joint stereo: codes what both channels share once, for better compression\r\n- Dual channel: encodes both channels separately\r\n- Mono: one channel" },
	{ IDC_CHECK_CHANNELFORCE, "Use the selected mode even when the input has another number of channels.\r\n\r\nOnly Mono can be forced: stereo input is then encoded as mono." },
	{ IDC_SLIDER_QUALITY, "How hard the encoder works: 0 gives the best quality\r\nand is the slowest, 9 is the fastest. 3 is LAME's default." },
	{ IDC_CHECK_COPYRIGHT, "Mark the encoded data as copyrighted." },
	{ IDC_CHECK_CHECKSUM, "Put a checksum in the encoded data.\r\n\r\nThis can make the file less sensitive to data loss." },
	{ IDC_CHECK_ORIGINAL, "Mark the encoded data as an original file." },
	{ IDC_CHECK_PRIVATE, "Mark the encoded data as private." },
	{ IDC_CHECK_RESERVOIR, "Use the bit reservoir.\r\n\r\nA frame can then use bits that earlier frames left over.\r\nWithout it, every frame contains all of its own data." },
	{ IDC_CHECK_KEEP_ALL, "Encode all frequencies: no lowpass and no highpass\r\nfilter. Otherwise LAME leaves out what the bitrate\r\ncannot carry well." },
	{ IDC_CHECK_STRICT_ISO, "Keep the bit reservoir within the limit of the\r\nISO standard, for decoders that need it." },
	{ IDC_CHECK_FORCE_MS, "Code every frame of joint stereo as mid and side.\r\nOnly with joint stereo." },
	{ IDC_STATIC_CONFIG_VERSION, "The version of LAME in this codec." },
};

/** \name The names in the settings file
    The file holds one \c lame_acm element with one \c encodings element.
    That holds the \c config elements, each with its \c name, and its
    \c default attribute names the one in use. A \c config element holds
    one element per setting.
    @{ */
static const char ELEMENT_ROOT[]          = "lame_acm";
static const char ELEMENT_ENCODINGS[]     = "encodings";
static const char ELEMENT_CONFIG[]        = "config";
static const char ELEMENT_SMART[]         = "Smart";
static const char ELEMENT_ABR[]           = "ABR";
static const char ELEMENT_CBR[]           = "CBR";
static const char ELEMENT_VBR[]           = "VBR";
static const char ELEMENT_COPYRIGHT[]     = "Copyright";
static const char ELEMENT_CRC[]           = "CRC";
static const char ELEMENT_ORIGINAL[]      = "Original";
static const char ELEMENT_PRIVATE[]       = "Private";
static const char ELEMENT_BIT_RESERVOIR[] = "Bit_reservoir";
static const char ELEMENT_QUALITY[]       = "Quality";
static const char ELEMENT_KEEP_ALL_FREQUENCIES[] = "Keep_all_frequencies";
static const char ELEMENT_STRICT_ISO[]    = "Strict_ISO";
static const char ELEMENT_FORCE_MS[]      = "Forced_mid_side";
static const char ELEMENT_CHANNEL[]       = "Channel";
static const char ATTRIBUTE_DEFAULT[]     = "default";
static const char ATTRIBUTE_NAME[]        = "name";
static const char ATTRIBUTE_USE[]         = "use";
static const char ATTRIBUTE_RATIO[]       = "ratio";
static const char ATTRIBUTE_MIN[]         = "min";
static const char ATTRIBUTE_MAX[]         = "max";
static const char ATTRIBUTE_STEP[]        = "step";
static const char ATTRIBUTE_BEST[]        = "best";
static const char ATTRIBUTE_WORST[]       = "worst";
static const char ATTRIBUTE_MODE[]        = "mode";
static const char ATTRIBUTE_FORCE[]       = "force";
static const char ATTRIBUTE_LEVEL[]       = "level";
static const char ATTRIBUTE_ENFORCE_MIN[] = "enforce_min";
static const char VALUE_TRUE[]            = "true";
/** The one configuration that the codec reads and writes. */
static const char CONFIG_CURRENT[]        = "Current";
/** @} */

/** The window property of the configuration dialog that holds its settings. */
static const char DIALOG_PROPERTY[] = "AEncodeProperties-Config";

/* The configuration file carries exactly one value that is not a whole number,
 * the smart-output ratio, and both directions below go through the C locale
 * instead of the host's.  This codec is a DLL running inside whatever
 * application loaded it, so LC_NUMERIC belongs to that application: a ratio
 * written as "2,5" by a host whose locale uses a comma would read back as 2 on
 * the next one.  Everything else in the file is a whole number, a flag or a
 * name, so nothing an earlier build wrote is at risk - but a decimal point in
 * it would be.
 */
static double DoubleFromAttribute(const std::string & the_text)
{
	/* A null locale handle makes these two behave exactly as atof() and
	 * sprintf() do, which is what this code did before, so failing to create
	 * one degrades instead of breaking. */
	_locale_t c_locale = _create_locale(LC_NUMERIC, "C");
	double the_value = _atof_l(the_text.c_str(), c_locale);

	if (c_locale != NULL)
		_free_locale(c_locale);

	return the_value;
}

/**
 * \brief Converts the value of an attribute to an unsigned integer.
 *
 * \param the_text the value of the attribute
 * \return the number. 0 if the text is not a decimal number, is negative, or
 *         is larger than UINT_MAX.
 */
static unsigned int UnsignedFromAttribute(const std::string & the_text)
{
	const char * const start = the_text.c_str();
	char * end = NULL;
	unsigned long the_value;

	errno = 0;
	the_value = strtoul(start, &end, 10);
	if (end == start || *end != '\0' || errno == ERANGE || the_value > UINT_MAX
	    || the_text.find('-') != std::string::npos)
		return 0;
	return (unsigned int) the_value;
}

/**
	\brief Returns the value of the entry that a combo box of the settings
	       dialog shows: the data of the entry.

	\param combo     the combo box.
	\param otherwise what to return when no entry is selected.
	\return the value.
*/
static unsigned int ItemDataOf(HWND combo, unsigned int otherwise)
{
	LRESULT const item = SendMessage(combo, CB_GETCURSEL, 0, 0);

	if (item == CB_ERR)
		return otherwise;
	return (unsigned int) SendMessage(combo, CB_GETITEMDATA, (WPARAM) item, 0);
}

/**
	\brief Selects the entry whose data is the given value: the counterpart
	       of ItemDataOf().

	\param combo the combo box.
	\param value the data of the entry; with no such entry the selection
	       stays as it is.
*/
static void SelectItemData(HWND combo, unsigned int value)
{
	LRESULT const count = SendMessage(combo, CB_GETCOUNT, 0, 0);

	for (LRESULT item = 0; item < count; item++)
	{
		if ((unsigned int) SendMessage(combo, CB_GETITEMDATA, (WPARAM) item, 0) == value)
		{
			SendMessage(combo, CB_SETCURSEL, (WPARAM) item, 0);
			return;
		}
	}
}

/**
	\brief Selects the entry whose value is nearest to the given one, the
	       lower of two as near: a saved value that the list does not have.

	\param combo the combo box.
	\param value the value.
*/
static void SelectNearest(HWND combo, unsigned int value)
{
	LRESULT const count = SendMessage(combo, CB_GETCOUNT, 0, 0);
	LRESULT best = CB_ERR;
	unsigned int best_distance = 0;

	for (LRESULT item = 0; item < count; item++)
	{
		unsigned int const data = (unsigned int) SendMessage(combo, CB_GETITEMDATA, (WPARAM) item, 0);
		unsigned int const distance = data > value ? data - value : value - data;

		if (best == CB_ERR || distance < best_distance || (distance == best_distance && data < value))
		{
			best = item;
			best_distance = distance;
		}
	}
	if (best != CB_ERR)
		SendMessage(combo, CB_SETCURSEL, (WPARAM) best, 0);
}

static void SetAttributeDouble(TiXmlElement * the_elt, const std::string & the_string, const double the_value)
{
	/* Long enough for any double %g can produce, sign and exponent included. */
	char the_text[32];
	_locale_t c_locale = _create_locale(LC_NUMERIC, "C");

	/* %g leaves a whole value whole, so a configuration file written by an
	 * earlier build of this codec comes back byte for byte as it was unless
	 * the ratio really does have a fractional part. */
	_sprintf_s_l(the_text, sizeof the_text, "%.6g", c_locale, the_value);

	if (c_locale != NULL)
		_free_locale(c_locale);

	the_elt->SetAttribute(the_string, std::string(the_text));
}

static BOOL CALLBACK ConfigProc(
  HWND hwndDlg,  // handle to dialog box
  UINT uMsg,     // message
  WPARAM wParam, // first message parameter
  LPARAM lParam  // second message parameter
  )
{
	BOOL bResult = FALSE;
	AEncodeProperties * the_prop;
	the_prop = (AEncodeProperties *) GetProp(hwndDlg, DIALOG_PROPERTY);

	switch (uMsg) {
		case WM_COMMAND:
			if (the_prop != NULL)
			{
				bResult = the_prop->HandleDialogCommand( hwndDlg, wParam, lParam);
			}
			break;
		case WM_INITDIALOG:
			assert(the_prop == NULL);

			the_prop = (AEncodeProperties *) lParam;
			the_prop->my_debug.OutPut("there hwnd = 0x%08X",hwndDlg);

			assert(the_prop != NULL);

			SetProp(hwndDlg, DIALOG_PROPERTY, the_prop);

			the_prop->InitConfigDlg(hwndDlg);

			bResult = TRUE;
			break;

		case WM_HSCROLL:
			// the encoding quality
			if ((HWND)lParam == GetDlgItem(hwndDlg,IDC_SLIDER_QUALITY))
			{
				LRESULT const level = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
				if (level >= 0 && (unsigned int) level < ENCODING_QUALITY_LEVELS)
					SetDlgItemText(hwndDlg, IDC_STATIC_QUALITY_TEXT, EncodingQualityText((unsigned int) level));
			}
			break;

		case WM_NOTIFY:
			if (((LPNMHDR)lParam)->idFrom == IDC_TAB_SETTINGS && ((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
				AEncodeProperties::ShowSettingsTab(hwndDlg, TabCtrl_GetCurSel(((LPNMHDR)lParam)->hwndFrom));
				return TRUE;
			}
			if (TTN_GETDISPINFO == ((LPNMHDR)lParam)->code) {
				NMTTDISPINFO *lphdr = (NMTTDISPINFO *)lParam;
				UINT id = (lphdr->uFlags & TTF_IDISHWND) ? GetWindowLong((HWND)lphdr->hdr.idFrom, GWL_ID) : lphdr->hdr.idFrom;

				*lphdr->lpszText = 0;

				SendMessage(lphdr->hdr.hwndFrom, TTM_SETMAXTIPWIDTH, 0, 5000);

				for(int i=0; i<sizeof AEncodeProperties::Tooltips/sizeof AEncodeProperties::Tooltips[0]; ++i) {
					if (id == AEncodeProperties::Tooltips[i].id)
						lphdr->lpszText = const_cast<char *>(AEncodeProperties::Tooltips[i].tip);
				}

				return TRUE;
			}
			break;

		default:
			bResult = FALSE; // will be treated by DefWindowProc
	}
	return bResult;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


const char * AEncodeProperties::GetChannelModeString(int a_channelID) const
{
	assert(a_channelID < GetChannelLentgh());

	switch (a_channelID) {
		case CHANNEL_INDEX_STEREO:
			return "Stereo";
		case CHANNEL_INDEX_JOINT_STEREO:
			return "Joint stereo";
		case CHANNEL_INDEX_DUAL_CHANNEL:
			return "Dual channel";
		case CHANNEL_INDEX_MONO:
			return "Mono";
		default:
			return NULL;
	}
}

/**
	\brief Fills the_MPEG1_Bitrates, the_MPEG2_Bitrates and the_Bitrates from
	the library, highest first.

	Each call writes the same values. So it is safe to call it for every
	instance.
*/
void AEncodeProperties::FillBitrateTables()
{
	const int count = sizeof(the_MPEG1_Bitrates) / sizeof(the_MPEG1_Bitrates[0]);
	const int total = sizeof(the_Bitrates) / sizeof(the_Bitrates[0]);
	int i, j1 = 0, j2 = 0, k = 0;

	for (i = 0; i < count; i++)
	{
		// lame_get_bitrate() takes the frame header's index, 1 to 14,
		// lowest first; MPEG version 1 is MPEG-1, 0 is MPEG-2.
		the_MPEG1_Bitrates[i] = (unsigned int) lame_get_bitrate(1, count - i);
		the_MPEG2_Bitrates[i] = (unsigned int) lame_get_bitrate(0, count - i);
	}
	// Both lists merged, each value once.
	while ((j1 < count || j2 < count) && k < total)
	{
		if (j2 == count || (j1 < count && the_MPEG1_Bitrates[j1] > the_MPEG2_Bitrates[j2]))
			the_Bitrates[k++] = the_MPEG1_Bitrates[j1++];
		else if (j1 == count || the_MPEG2_Bitrates[j2] > the_MPEG1_Bitrates[j1])
			the_Bitrates[k++] = the_MPEG2_Bitrates[j2++];
		else
		{
			the_Bitrates[k++] = the_MPEG1_Bitrates[j1++];
			j2++;
		}
	}
	assert(k == total && j1 == count && j2 == count);
}

int AEncodeProperties::GetBitrateString(char * string, int string_size, int a_bitrateID) const
{
	assert(a_bitrateID < GetBitrateLentgh());
	assert(string != NULL);

	if (string_size >= 4)
		return snprintf(string, string_size, "%u", the_Bitrates[a_bitrateID]);
	else
		return -1;
}

unsigned int AEncodeProperties::GetChannelModeValue() const
{
	assert(nChannelIndex < GetChannelLentgh());

	return the_ChannelModes[nChannelIndex];
}

unsigned int AEncodeProperties::OutputChannels(const unsigned int input_channels) const
{
	if (bForceChannel && GetChannelModeValue() == MONO && input_channels == 2)
		return 1;
	return input_channels;
}

bool AEncodeProperties::Config(const HINSTANCE Hinstance, const HWND HwndParent)
{

	my_debug.OutPut("here");
	INT_PTR const ret = DialogBoxSystemFont(Hinstance, IDD_CONFIG, HwndParent, ::ConfigProc, (LPARAM) this);
	return ret > 0;
}

bool AEncodeProperties::InitConfigDlg(HWND HwndDlg)
{
	static const char * const tab_names[SETTINGS_TABS] = { "Formats", "Encoding" };
	int i;

	// The two tabs; the first shows
	for (i = 0; i < SETTINGS_TABS; i++)
	{
		TCITEM item;

		item.mask = TCIF_TEXT;
		item.pszText = const_cast<char *>(tab_names[i]);
		SendMessage(GetDlgItem( HwndDlg, IDC_TAB_SETTINGS), TCM_INSERTITEM, i, (LPARAM) &item);
	}
	ShowSettingsTab(HwndDlg, TAB_FORMATS);

	// Add required channel modes
	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_RESETCONTENT , 0, 0);
	for (i=0;i<GetChannelLentgh();i++)
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_ADDSTRING, 0, (LPARAM) GetChannelModeString(i));

	char tmp[sizeof "v" + ACM::VERSION_STRING_CHARS];
	snprintf(tmp, sizeof tmp, "v%s", ACM::GetVersionString());
	SetWindowText( GetDlgItem( HwndDlg, IDC_STATIC_CONFIG_VERSION), tmp);

	// The ranges of the families
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_CBR_MIN), false);
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_CBR_MAX), false);
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_ABR_MIN), false);
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_ABR_MAX), false);
	for (unsigned int step = 1; step <= ABR_STEP_LIMIT; step++)
	{
		char text[sizeof "16 kbps"];
		LRESULT item;

		snprintf(text, sizeof text, "%u kbps", step);
		item = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ABR_STEP), CB_ADDSTRING, 0, (LPARAM) text);
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ABR_STEP), CB_SETITEMDATA, item, step);
	}
	for (unsigned int level = 0; level <= VBR_QUALITY_WORST; level++)
	{
		char text[VBR_TEXT_CHARS];
		LRESULT item;

		VbrQualityText(level, text, sizeof text);
		item = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_VBR_BEST), CB_ADDSTRING, 0, (LPARAM) text);
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_VBR_BEST), CB_SETITEMDATA, item, level);
		item = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_VBR_WORST), CB_ADDSTRING, 0, (LPARAM) text);
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_VBR_WORST), CB_SETITEMDATA, item, level);
	}
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_VBR_MIN), true);
	FillBitrateList(GetDlgItem( HwndDlg, IDC_COMBO_VBR_MAX), true);
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_SETRANGE, TRUE, MAKELONG(0, ENCODING_QUALITY_LEVELS - 1));

	// The Smart filter names its ratio
	{
		char smart[sizeof "Leave out formats above 1000000:1 compressio&n"];

		snprintf(smart, sizeof smart, "Leave out formats above %g:1 compressio&n", SmartRatioMax);
		SetDlgItemText(HwndDlg, IDC_CHECK_ENC_SMART, smart);
	}

	// Tool-Tip initialiasiation
	TOOLINFO ti;
	HWND ToolTipWnd;

	ToolTipWnd = CreateWindowEx(WS_EX_TOPMOST,
        TOOLTIPS_CLASS,
        NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP|TTS_BALLOON ,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        HwndDlg,
        NULL,
        NULL,
        NULL
        );

	SetWindowPos(ToolTipWnd,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* INITIALIZE MEMBERS OF THE TOOLINFO STRUCTURE */
	ti.cbSize		= sizeof(TOOLINFO);
	ti.uFlags		= TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd			= HwndDlg;
	ti.lpszText		= LPSTR_TEXTCALLBACK;

    /* SEND AN ADDTOOL MESSAGE TO THE TOOLTIP CONTROL WINDOW */
	for(i=0; i<TOOLTIP_COUNT; ++i) {
		ti.uId			= (WPARAM)GetDlgItem(HwndDlg, Tooltips[i].id);

		if (ti.uId)
			SendMessage(ToolTipWnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
	}

my_debug.OutPut("call UpdateConfigs");

	UpdateConfigs(HwndDlg);

my_debug.OutPut("call UpdateDlgFromValue");

	UpdateDlgFromValue(HwndDlg);


	my_debug.OutPut("finished InitConfigDlg");


	return true;
}

bool AEncodeProperties::UpdateDlgFromValue(HWND HwndDlg)
{

	int i;

	// Check boxes if required
	::CheckDlgButton( HwndDlg, IDC_CHECK_CHECKSUM,     GetCRCMode()        ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_ORIGINAL,     GetOriginalMode()   ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_PRIVATE,      GetPrivateMode()    ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_COPYRIGHT,    GetCopyrightMode()  ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_ENC_SMART,    GetSmartOutputMode()?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_RESERVOIR,    !GetNoBiResMode() ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_CHANNELFORCE, bForceChannel     ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_KEEP_ALL,     bKeepAllFrequencies ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_STRICT_ISO,   bStrictISO        ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_FORCE_MS,     bForceMS          ?BST_CHECKED:BST_UNCHECKED );

	// Add required channel modes
	for (i=0;i<GetChannelLentgh();i++)
	{
		if (i == nChannelIndex)
		{
			SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_SETCURSEL, i, 0);
			break;
		}
	}
	::EnableWindow(::GetDlgItem( HwndDlg, IDC_CHECK_FORCE_MS), ShowsJointStereo(HwndDlg));

	// The families of formats and their ranges
	::CheckDlgButton( HwndDlg, IDC_CHECK_ENC_CBR, bCbrOutput ? BST_CHECKED : BST_UNCHECKED );
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_CBR_MIN), CbrBitrate_Min);
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_CBR_MAX), CbrBitrate_Max);
	::CheckDlgButton( HwndDlg, IDC_CHECK_ENC_ABR, bAbrOutput ? BST_CHECKED : BST_UNCHECKED );
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_ABR_MIN), AverageBitrate_Min);
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_ABR_MAX), AverageBitrate_Max);
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_ABR_STEP), AverageBitrate_Step);
	UpdateAbrList(HwndDlg);
	::CheckDlgButton( HwndDlg, IDC_CHECK_ENC_VBR, bVbrOutput ? BST_CHECKED : BST_UNCHECKED );
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_VBR_BEST), VbrQuality_Best);
	SelectNearest(GetDlgItem( HwndDlg, IDC_COMBO_VBR_WORST), VbrQuality_Worst);
	SelectItemData(GetDlgItem( HwndDlg, IDC_COMBO_VBR_MIN), VbrBitrate_Min);
	SelectItemData(GetDlgItem( HwndDlg, IDC_COMBO_VBR_MAX), VbrBitrate_Max);
	::CheckDlgButton( HwndDlg, IDC_CHECK_VBR_ENFORCE_MIN, bVbrEnforceMin ? BST_CHECKED : BST_UNCHECKED );
	EnableFamilyControls(HwndDlg);
	UpdateFormatCount(HwndDlg);

	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_SETPOS, TRUE, nQuality);
	::SetDlgItemText(HwndDlg, IDC_STATIC_QUALITY_TEXT, EncodingQualityText(nQuality));
	/**
		\todo Select the right saved config
	*/

	return true;
}

bool AEncodeProperties::UpdateValueFromDlg(HWND HwndDlg)
{
	FormatListSettings const list = FormatListFromDlg(HwndDlg);

	nChannelIndex      = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO),   CB_GETCURSEL, 0, 0);

	bCRC          = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_CHECKSUM)     == BST_CHECKED);
	bCopyright    = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_COPYRIGHT)    == BST_CHECKED);
	bOriginal     = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_ORIGINAL)     == BST_CHECKED);
	bPrivate      = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_PRIVATE)      == BST_CHECKED);
	bNoBitRes     =!(::IsDlgButtonChecked( HwndDlg, IDC_CHECK_RESERVOIR)    == BST_CHECKED);
	bForceChannel = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_CHANNELFORCE) == BST_CHECKED);
	bKeepAllFrequencies = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_KEEP_ALL) == BST_CHECKED);
	bStrictISO    = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_STRICT_ISO)   == BST_CHECKED);
	bForceMS      = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_FORCE_MS)     == BST_CHECKED);

	bCbrOutput          = list.cbr;
	bAbrOutput          = list.abr;
	bVbrOutput          = list.vbr;
	bSmartOutput        = list.smart;
	CbrBitrate_Min      = list.cbr_min;
	CbrBitrate_Max      = list.cbr_max;
	AverageBitrate_Min  = list.abr_min;
	AverageBitrate_Max  = list.abr_max;
	AverageBitrate_Step = list.abr_step;
	VbrQuality_Best     = list.vbr_best;
	VbrQuality_Worst    = list.vbr_worst;
	VbrBitrate_Min      = list.vbr_min;
	VbrBitrate_Max      = list.vbr_max;
	bVbrEnforceMin   = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_VBR_ENFORCE_MIN) == BST_CHECKED);
	nQuality = (unsigned int) SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_GETPOS , 0, 0);

	EnableFamilyControls(HwndDlg);

my_debug.OutPut("nChannelIndex %d, bCRC %d, bCopyright %d, bOriginal %d, bPrivate %d",nChannelIndex, bCRC, bCopyright, bOriginal, bPrivate);

	return true;
}

void AEncodeProperties::ParamsRestore()
{
	// use these default parameters in case one is not found
	bCopyright    = true;
	bCRC          = true;
	bOriginal     = true;
	bPrivate      = true;
	bNoBitRes     = false; // enable bit reservoir
	nQuality      = ENCODING_QUALITY_DEFAULT;
	bKeepAllFrequencies = false;
	bStrictISO    = false;
	bForceMS      = false;
	bForceChannel = false;
	// Every format: the three families, the whole CBR range, no Smart filter
	bSmartOutput  = false;
	bCbrOutput    = true;
	bAbrOutput    = true;
	bVbrOutput    = true;
	CbrBitrate_Min = the_Bitrates[sizeof the_Bitrates / sizeof the_Bitrates[0] - 1];
	CbrBitrate_Max = the_Bitrates[0];

	VbrQuality_Best = 0;
	VbrQuality_Worst = VBR_QUALITY_WORST;
	VbrBitrate_Min = VBR_BITRATE_NO_LIMIT;
	VbrBitrate_Max = VBR_BITRATE_NO_LIMIT;
	bVbrEnforceMin = false;

	AverageBitrate_Min = 80; // a bit lame
	AverageBitrate_Max = 160; // a bit lame
	AverageBitrate_Step = 8; // a bit lame
	SmartRatioMax = 15.0;

	nChannelIndex = CHANNEL_INDEX_JOINT_STEREO;

	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();
	if (CurrentNode != NULL)
	{
		std::string CurrentConfig = "";

		if (CurrentNode->Attribute(ATTRIBUTE_DEFAULT) != NULL)
		{
			CurrentConfig = *CurrentNode->Attribute(ATTRIBUTE_DEFAULT);
		}

		GetValuesFromKey(CurrentConfig, *CurrentNode);

		// The codec offers at least one family: a file that leaves out all
		// three offers CBR
		if (!bCbrOutput && !bAbrOutput && !bVbrOutput)
			bCbrOutput = true;
	}
	else
	{
		/*  Nothing is written here on purpose.  The defaults assigned above are
		    what this instance will encode with, and they reach the file the
		    first time something calls ParamsSave() - which now builds the
		    document when there is none.  Writing at load time instead would
		    have every driver instance write into the directory the codec was
		    installed into, which on an ordinary account is not the codec's to
		    write to, for a file the user has not asked to change. */
	}
}

/*  The counterpart of ParamsRestore(): the parameters this instance holds,
    written back to the configuration they were restored from.

    Only "Current" is written, which is the one configuration the rest of the
    class supports; the dialog's own save says the same thing where it names it.
 */
void AEncodeProperties::ParamsSave()
{
	SaveValuesToStringKey(CONFIG_CURRENT);
}

AEncodeProperties::AEncodeProperties(HMODULE hModule)
 :my_debug(DEBUG_LEVEL_CREATION),
 my_hModule(hModule)
{
	FillBitrateTables();

	std::string path = "";
	if (hModule != NULL)
	{
		char output[MAX_PATH];
		::GetModuleFileName(hModule, output, MAX_PATH);

		path = output;
	}
	my_store_location = path.substr(0,path.find_last_of('\\')+1);
	my_store_location += "lame_acm.xml";

	my_debug.OutPut("store path = %s",my_store_location.c_str());

	// make sure the XML file is present
	HANDLE hFile = ::CreateFile(my_store_location.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_ARCHIVE, NULL );
	::CloseHandle(hFile);
	my_debug.OutPut("AEncodeProperties creation completed (0x%08X)",this);
}

/**
	\brief Loads the configuration file and returns its \c encodings element.

	The file sits beside the codec, where anything may edit or truncate it. A
	document that parses but has another shape is treated as one that does not
	parse: following a null pointer would take down the application that the
	ACM loaded the driver into.

	\return the element. NULL when the file does not load, or has no
	        \c lame_acm element with an \c encodings element in it.
*/
TiXmlElement * AEncodeProperties::LoadEncodings()
{
	// TinyXML keeps the error of a load that failed, and LoadFile() reports it
	// again for a file that loads; this load starts without it
	my_stored_data.ClearError();
	if (!my_stored_data.LoadFile(my_store_location))
		return NULL;

	TiXmlNode * node = my_stored_data.FirstChild(ELEMENT_ROOT);

	if (node == NULL)
		return NULL;

	return node->FirstChildElement(ELEMENT_ENCODINGS);
}

/**
	\brief Returns the \c config element with the given name.

	A \c config element without a name does not match any name.

	\param parent  the element that holds the \c config elements.
	\param name    the name to look for.
	\return the element, or NULL if there is none with that name.
*/
TiXmlElement * AEncodeProperties::FindConfig(const TiXmlNode & parent, const std::string & name)
{
	TiXmlElement * elt = parent.FirstChildElement(ELEMENT_CONFIG);

	while (elt != NULL)
	{
		const std::string * tmpname = elt->Attribute(ATTRIBUTE_NAME);
		if (tmpname != NULL && tmpname->compare(name) == 0)
			break;
		elt = elt->NextSiblingElement(ELEMENT_CONFIG);
	}
	return elt;
}

/*  Save the values to the right XML saved config.

    Whatever the file already holds is read back first, so that saving one
    configuration keeps the others.  A file that is missing, empty or shaped
    some other way is not an error: the elements this needs are created, which
    is what lets a save succeed when the configuration file the installer lays
    down beside the codec has been lost.  Until that was so, this returned
    having written nothing, and a user's settings stopped being kept with
    nothing said about it.

    InsertEndChild() stores a copy and hands back a pointer to that copy, so
    each element is filled in through the pointer it returns and not through
    the one handed to it.
 */
void AEncodeProperties::SaveValuesToStringKey(const std::string & config_name)
{
	// get the current data in the file to keep them
	my_stored_data.LoadFile(my_store_location);

	TiXmlNode* node = my_stored_data.FirstChild(ELEMENT_ROOT);

	if (node == NULL)
	{
		node = my_stored_data.InsertEndChild(TiXmlElement(ELEMENT_ROOT));

		if (node == NULL)
			return;
	}

	TiXmlElement* ConfigNode = node->FirstChildElement(ELEMENT_ENCODINGS);

	if (ConfigNode == NULL)
	{
		TiXmlElement encodings(ELEMENT_ENCODINGS);

		encodings.SetAttribute(ATTRIBUTE_DEFAULT, config_name);

		TiXmlNode* inserted = node->InsertEndChild(encodings);

		if (inserted == NULL)
			return;

		ConfigNode = inserted->ToElement();
	}

	// check if the Node corresponding to the config_name already exist.
	TiXmlElement* tmpNode = FindConfig(*ConfigNode, config_name);

	if (tmpNode == NULL)
	{
		// Create the node
		TiXmlElement created(ELEMENT_CONFIG);

		created.SetAttribute(ATTRIBUTE_NAME,config_name);

		TiXmlNode* inserted = ConfigNode->InsertEndChild(created);

		if (inserted == NULL)
			return;

		tmpNode = inserted->ToElement();
	}

	// save data in the node
	SaveValuesToElement(tmpNode);

	// and save the file
	my_stored_data.SaveFile(my_store_location);
}

void AEncodeProperties::GetValuesFromKey(const std::string & config_name, const TiXmlNode & parentNode)
{
	TiXmlElement* tmpElt;
	TiXmlElement* iterateElmt;

	// find the config that correspond to CurrentConfig
	iterateElmt = FindConfig(parentNode, config_name);

	if (iterateElmt != NULL)
	{
		// get all the parameters saved in this Element
		const std::string * tmpname;

		// Smart output parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_SMART);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bSmartOutput = (tmpname->compare(VALUE_TRUE) == 0);
			
			tmpname = tmpElt->Attribute(ATTRIBUTE_RATIO);
			if (tmpname != NULL)
				SmartRatioMax = DoubleFromAttribute(*tmpname);
		}

		// Smart output parameter
		// CBR parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_CBR);
		if (tmpElt != NULL)
		{
			unsigned int cbr_min = CbrBitrate_Min;
			unsigned int cbr_max = CbrBitrate_Max;

			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bCbrOutput = (tmpname->compare(VALUE_TRUE) == 0);

			tmpname = tmpElt->Attribute(ATTRIBUTE_MIN);
			if (tmpname != NULL)
				cbr_min = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute(ATTRIBUTE_MAX);
			if (tmpname != NULL)
				cbr_max = UnsignedFromAttribute(*tmpname);

			/* A range is taken only if both ends are bitrates of the list and
			   the lowest is not above the highest; any other keeps the one
			   before. */
			if (IsListBitrate(cbr_min) && IsListBitrate(cbr_max) && cbr_min <= cbr_max)
			{
				CbrBitrate_Min = cbr_min;
				CbrBitrate_Max = cbr_max;
			}
		}

		tmpElt = iterateElmt->FirstChildElement(ELEMENT_ABR);
		if (tmpElt != NULL)
		{
			unsigned int abr_min = AverageBitrate_Min;
			unsigned int abr_max = AverageBitrate_Max;
			unsigned int abr_step = AverageBitrate_Step;

			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bAbrOutput = (tmpname->compare(VALUE_TRUE) == 0);

			tmpname = tmpElt->Attribute(ATTRIBUTE_MIN);
			if (tmpname != NULL)
				abr_min = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute(ATTRIBUTE_MAX);
			if (tmpname != NULL)
				abr_max = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute(ATTRIBUTE_STEP);
			if (tmpname != NULL)
				abr_step = UnsignedFromAttribute(*tmpname);

			/* A range is taken only if it steps down from a maximum LAME can
			   encode to a minimum above 0; any other keeps the one before. */
			if (abr_step > 0 && abr_min > 0 && abr_min <= abr_max && abr_max <= ABR_BITRATE_LIMIT)
			{
				AverageBitrate_Min = abr_min;
				AverageBitrate_Max = abr_max;
				AverageBitrate_Step = abr_step;
			}
		}

		// VBR parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_VBR);
		if (tmpElt != NULL)
		{
			unsigned int vbr_best = VbrQuality_Best;
			unsigned int vbr_worst = VbrQuality_Worst;
			unsigned int vbr_min = VbrBitrate_Min;
			unsigned int vbr_max = VbrBitrate_Max;

			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bVbrOutput = (tmpname->compare(VALUE_TRUE) == 0);

			tmpname = tmpElt->Attribute(ATTRIBUTE_BEST);
			if (tmpname != NULL)
				vbr_best = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute(ATTRIBUTE_WORST);
			if (tmpname != NULL)
				vbr_worst = UnsignedFromAttribute(*tmpname);

			/* A range is taken only if it runs from a better level to a worse
			   one within the levels LAME has; any other keeps the one before. */
			if (vbr_best <= vbr_worst && vbr_worst <= VBR_QUALITY_WORST)
			{
				VbrQuality_Best = vbr_best;
				VbrQuality_Worst = vbr_worst;
			}

			tmpname = tmpElt->Attribute(ATTRIBUTE_MIN);
			if (tmpname != NULL)
				vbr_min = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute(ATTRIBUTE_MAX);
			if (tmpname != NULL)
				vbr_max = UnsignedFromAttribute(*tmpname);

			/* The bitrate limits are taken only if each is a bitrate of the list
			   or no limit, and the minimum is not above the maximum; any other
			   pair keeps the one before. */
			if (IsVbrBitrateLimit(vbr_min) && IsVbrBitrateLimit(vbr_max)
			    && (vbr_min == VBR_BITRATE_NO_LIMIT || vbr_max == VBR_BITRATE_NO_LIMIT || vbr_min <= vbr_max))
			{
				VbrBitrate_Min = vbr_min;
				VbrBitrate_Max = vbr_max;
			}

			tmpname = tmpElt->Attribute(ATTRIBUTE_ENFORCE_MIN);
			if (tmpname != NULL)
				bVbrEnforceMin = (tmpname->compare(VALUE_TRUE) == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_COPYRIGHT);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bCopyright = (tmpname->compare(VALUE_TRUE) == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_CRC);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bCRC = (tmpname->compare(VALUE_TRUE) == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_ORIGINAL);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bOriginal = (tmpname->compare(VALUE_TRUE) == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_PRIVATE);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bPrivate = (tmpname->compare(VALUE_TRUE) == 0);
		}
		// Bit reservoir parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_BIT_RESERVOIR);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bNoBitRes = !(tmpname->compare(VALUE_TRUE) == 0);
		}

		// Encoding quality: a level LAME does not have keeps the one before
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_QUALITY);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_LEVEL);
			if (tmpname != NULL)
			{
				unsigned int const level = UnsignedFromAttribute(*tmpname);
				if (level < ENCODING_QUALITY_LEVELS)
					nQuality = level;
			}
		}

		// The advanced settings
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_KEEP_ALL_FREQUENCIES);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bKeepAllFrequencies = (tmpname->compare(VALUE_TRUE) == 0);
		}
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_STRICT_ISO);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bStrictISO = (tmpname->compare(VALUE_TRUE) == 0);
		}
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_FORCE_MS);
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute(ATTRIBUTE_USE);
			if (tmpname != NULL)
				bForceMS = (tmpname->compare(VALUE_TRUE) == 0);
		}
		// Channel mode parameter
		tmpElt = iterateElmt->FirstChildElement(ELEMENT_CHANNEL);
		if (tmpElt != NULL)
		{
			const std::string * tmpStr = tmpElt->Attribute(ATTRIBUTE_MODE);
			if (tmpStr != NULL)
			{
				for (int i=0;i<GetChannelLentgh();i++)
				{
					if (tmpStr->compare(GetChannelModeString(i)) == 0)
					{
						nChannelIndex = i;
						break;
					}
				}
			}
			tmpname = tmpElt->Attribute(ATTRIBUTE_FORCE);
			if (tmpname != NULL)
				bForceChannel = (tmpname->compare(VALUE_TRUE) == 0);
		}
	}
}

void AEncodeProperties::SelectSavedParams(const std::string the_string)
{
	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();

	if (CurrentNode != NULL)
	{
		CurrentNode->SetAttribute(ATTRIBUTE_DEFAULT,the_string);
		GetValuesFromKey(the_string, *CurrentNode);
		my_stored_data.SaveFile(my_store_location);
	}
}

inline void AEncodeProperties::SetAttributeBool(TiXmlElement * the_elt,const std::string & the_string, const bool the_value)
{
	if (the_value == false)
		the_elt->SetAttribute(the_string, "false");
	else
		the_elt->SetAttribute(the_string, VALUE_TRUE);
}

/**
	\brief Returns the child element with the given name. Adds an empty one at
	       the end if the parent has none.

	\param parent the element to look in.
	\param name   the name of the child element.
	\return the child element, or NULL if it could not be added.
*/
TiXmlElement * AEncodeProperties::ChildElement(TiXmlElement & parent, const char * name)
{
	TiXmlElement * child = parent.FirstChildElement(name);

	if (child == NULL)
	{
		TiXmlNode * inserted = parent.InsertEndChild(TiXmlElement(name));

		if (inserted != NULL)
			child = inserted->ToElement();
	}
	return child;
}

void AEncodeProperties::SaveValuesToElement(TiXmlElement * the_element) const
{
	// get all the parameters saved in this Element
	TiXmlElement * tmpElt;

	// Bit Reservoir parameter
	tmpElt = ChildElement(*the_element, ELEMENT_BIT_RESERVOIR);
	if (tmpElt != NULL)
		SetAttributeBool(tmpElt, ATTRIBUTE_USE, !bNoBitRes);

	// Encoding quality
	tmpElt = ChildElement(*the_element, ELEMENT_QUALITY);
	if (tmpElt != NULL)
		tmpElt->SetAttribute(ATTRIBUTE_LEVEL, nQuality);

	// The advanced settings
	tmpElt = ChildElement(*the_element, ELEMENT_KEEP_ALL_FREQUENCIES);
	if (tmpElt != NULL)
		SetAttributeBool(tmpElt, ATTRIBUTE_USE, bKeepAllFrequencies);
	tmpElt = ChildElement(*the_element, ELEMENT_STRICT_ISO);
	if (tmpElt != NULL)
		SetAttributeBool(tmpElt, ATTRIBUTE_USE, bStrictISO);
	tmpElt = ChildElement(*the_element, ELEMENT_FORCE_MS);
	if (tmpElt != NULL)
		SetAttributeBool(tmpElt, ATTRIBUTE_USE, bForceMS);

	// Copyright parameter
	tmpElt = ChildElement(*the_element, ELEMENT_COPYRIGHT);
	if (tmpElt != NULL)
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bCopyright);

	// Smart Output parameter
	tmpElt = ChildElement(*the_element, ELEMENT_SMART);
	if (tmpElt != NULL)
	{
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bSmartOutput);
		SetAttributeDouble( tmpElt, ATTRIBUTE_RATIO, SmartRatioMax);
	}

	// CBR parameter
	tmpElt = ChildElement(*the_element, ELEMENT_CBR);
	if (tmpElt != NULL)
	{
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bCbrOutput);
		tmpElt->SetAttribute(ATTRIBUTE_MIN, CbrBitrate_Min);
		tmpElt->SetAttribute(ATTRIBUTE_MAX, CbrBitrate_Max);
	}

	// ABR parameter
	tmpElt = ChildElement(*the_element, ELEMENT_ABR);
	if (tmpElt != NULL)
	{
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bAbrOutput);
		tmpElt->SetAttribute(ATTRIBUTE_MIN, AverageBitrate_Min);
		tmpElt->SetAttribute(ATTRIBUTE_MAX, AverageBitrate_Max);
		tmpElt->SetAttribute(ATTRIBUTE_STEP, AverageBitrate_Step);
	}

	// VBR parameter
	tmpElt = ChildElement(*the_element, ELEMENT_VBR);
	if (tmpElt != NULL)
	{
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bVbrOutput);
		tmpElt->SetAttribute(ATTRIBUTE_BEST, VbrQuality_Best);
		tmpElt->SetAttribute(ATTRIBUTE_WORST, VbrQuality_Worst);
		tmpElt->SetAttribute(ATTRIBUTE_MIN, VbrBitrate_Min);
		tmpElt->SetAttribute(ATTRIBUTE_MAX, VbrBitrate_Max);
		SetAttributeBool( tmpElt, ATTRIBUTE_ENFORCE_MIN, bVbrEnforceMin);
	}

	// CRC parameter
	tmpElt = ChildElement(*the_element, ELEMENT_CRC);
	if (tmpElt != NULL)
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bCRC);

	// Original parameter
	tmpElt = ChildElement(*the_element, ELEMENT_ORIGINAL);
	if (tmpElt != NULL)
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bOriginal);

	// Private parameter
	tmpElt = ChildElement(*the_element, ELEMENT_PRIVATE);
	if (tmpElt != NULL)
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bPrivate);

	// Channel Mode parameter
	tmpElt = ChildElement(*the_element, ELEMENT_CHANNEL);
	if (tmpElt != NULL)
	{
		tmpElt->SetAttribute(ATTRIBUTE_MODE, GetChannelModeString(nChannelIndex));
		SetAttributeBool( tmpElt, ATTRIBUTE_FORCE, bForceChannel);
	}
}

bool AEncodeProperties::HandleDialogCommand(const HWND parentWnd, const WPARAM wParam, const LPARAM lParam)
{
	UINT command;
	command = GET_WM_COMMAND_ID(wParam, lParam);

	switch (command)
	{
	case IDOK :
	{
		// save parameters
		char string[MAX_PATH];

		snprintf(string, sizeof string, "%s", CONFIG_CURRENT); // only the Current config is supported at the moment
		
		my_debug.OutPut("my_hModule = 0x%08X",my_hModule);
my_debug.OutPut("before : nChannelIndex %d, bCRC %d, bCopyright %d, bOriginal %d, bPrivate %d",nChannelIndex, bCRC, bCopyright, bOriginal, bPrivate);

my_debug.OutPut("call UpdateValueFromDlg");

		UpdateValueFromDlg(parentWnd);

my_debug.OutPut("call ParamsSave");

		ParamsSave(); // only the Current config is supported now

my_debug.OutPut("finished saving");

		RemoveProp(parentWnd, DIALOG_PROPERTY);

		EndDialog(parentWnd, true);
	}
	break;

	case IDCANCEL:
		RemoveProp(parentWnd, DIALOG_PROPERTY);
        EndDialog(parentWnd, false);
		break;

	case IDC_CHECK_ENC_CBR:
	case IDC_CHECK_ENC_ABR:
	case IDC_CHECK_ENC_VBR:
		KeepOneFamily(parentWnd, command);
		EnableFamilyControls(parentWnd);
		UpdateFormatCount(parentWnd);
		break;
	case IDC_CHECK_ENC_SMART:
		UpdateFormatCount(parentWnd);
		break;
	case IDC_COMBO_ENC_STEREO:
		if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
			::EnableWindow(::GetDlgItem( parentWnd, IDC_CHECK_FORCE_MS), ShowsJointStereo(parentWnd));
		break;
	case IDC_COMBO_CBR_MIN:
	case IDC_COMBO_CBR_MAX:
		if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
		{
			KeepRangeInOrder(parentWnd, IDC_COMBO_CBR_MIN, IDC_COMBO_CBR_MAX, command);
			UpdateFormatCount(parentWnd);
		}
		break;
	case IDC_COMBO_ABR_MIN:
	case IDC_COMBO_ABR_MAX:
	case IDC_COMBO_ABR_STEP:
		if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
		{
			KeepRangeInOrder(parentWnd, IDC_COMBO_ABR_MIN, IDC_COMBO_ABR_MAX, command);
			UpdateAbrList(parentWnd);
			UpdateFormatCount(parentWnd);
		}
		break;
	case IDC_COMBO_VBR_BEST:
	case IDC_COMBO_VBR_WORST:
		if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
		{
			KeepRangeInOrder(parentWnd, IDC_COMBO_VBR_BEST, IDC_COMBO_VBR_WORST, command);
			UpdateFormatCount(parentWnd);
		}
		break;
	case IDC_COMBO_VBR_MIN:
	case IDC_COMBO_VBR_MAX:
		if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
		{
			KeepVbrLimitsInOrder(parentWnd, command);
			UpdateFormatCount(parentWnd);
		}
		break;
	}

	return FALSE;
}

void AEncodeProperties::UpdateConfigs(const HWND HwndDlg)
{

	// display all the names of the saved configs
	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();

	if (CurrentNode != NULL)
	{
		std::string CurrentConfig = "";

		if (CurrentNode->Attribute(ATTRIBUTE_DEFAULT) != NULL)
		{
			CurrentConfig = *CurrentNode->Attribute(ATTRIBUTE_DEFAULT);
		}

		TiXmlElement* iterateElmt;

my_debug.OutPut("are we here ?");

		// find the config that correspond to CurrentConfig
		iterateElmt = CurrentNode->FirstChildElement(ELEMENT_CONFIG);
		int Idx = 0;
		while (iterateElmt != NULL)
		{
			const std::string * tmpname = iterateElmt->Attribute(ATTRIBUTE_NAME);
			/**
				\todo support language names
			*/
			if (tmpname != NULL)
			{
				if (tmpname->compare(CurrentConfig) == 0)
				{
					SelectSavedParams(*tmpname);
					UpdateDlgFromValue(HwndDlg);
				}
			}
my_debug.OutPut("Idx = %d",Idx);

			Idx++;
			// only Current config supported now
			iterateElmt = NULL;
my_debug.OutPut("iterateElmt = 0x%08X",iterateElmt);

		}
	}
}
/**
	\brief Returns the bitrates of an ABR range, highest first.

	\param min  the lowest bitrate of the range, in kbit/s
	\param max  the highest bitrate of the range, in kbit/s
	\param step the distance between two bitrates, in kbit/s. It must be
	            greater than 0.
	\return \a max, \a max - \a step and so on, down to the last bitrate
	        that is not below \a min. Empty if \a max is below \a min.
*/
std::vector<unsigned int> AEncodeProperties::AbrLadder(unsigned int min, unsigned int max, unsigned int step)
{
	std::vector<unsigned int> ladder;
	int bitrate;

	for (bitrate = (int) max; bitrate >= (int) min; bitrate -= (int) step)
		ladder.push_back((unsigned int) bitrate);
	return ladder;
}

/** \name The line that lists the ABR bitrates
    Up to ABR_LIST_ALL bitrates are listed in full. A longer range shows its
    first ABR_LIST_FIRST bitrates and its last one.
    @{ */
static const size_t ABR_LIST_ALL   = 4;
static const size_t ABR_LIST_FIRST = 3;
/** @} */

/**
	\brief Writes the bitrates of an ABR range as one line of text, for
	       example "13 bitrates: 160, 154, 148, ..., 88 kbps".

	\param the_Ladder the bitrates, as AbrLadder() returns them
	\param the_Text   receives the text
	\param the_Size   the size of \a the_Text in bytes
*/
static void DescribeAbrLadder(const std::vector<unsigned int> & the_Ladder, char * the_Text, size_t the_Size)
{
	size_t const count = the_Ladder.size();
	size_t const listed = (count <= ABR_LIST_ALL) ? count : ABR_LIST_FIRST;
	size_t used;
	int n;

	n = snprintf(the_Text, the_Size, "%u %s:", (unsigned int) count, (count == 1) ? "bitrate" : "bitrates");
	used = (n > 0) ? (size_t) n : 0;
	for (size_t i = 0; i < listed && used < the_Size; i++)
	{
		n = snprintf(the_Text + used, the_Size - used, "%s %u", (i == 0) ? "" : ",", the_Ladder[i]);
		used += (n > 0) ? (size_t) n : 0;
	}
	if (listed < count && used < the_Size)
	{
		n = snprintf(the_Text + used, the_Size - used, ", ..., %u", the_Ladder[count - 1]);
		used += (n > 0) ? (size_t) n : 0;
	}
	if (used < the_Size)
		snprintf(the_Text + used, the_Size - used, " kbps");
}

/**
	\brief Shows the controls of one tab of the settings dialog and hides
	those of the other one.

	\param hDialog the settings dialog.
	\param tab     TAB_FORMATS or TAB_ENCODING.
*/
void AEncodeProperties::ShowSettingsTab(HWND hDialog, int tab)
{
	static const int formats[] = {
		IDC_CHECK_ENC_CBR, IDC_STATIC_CBR_MIN, IDC_COMBO_CBR_MIN, IDC_STATIC_CBR_MAX, IDC_COMBO_CBR_MAX,
		IDC_CHECK_ENC_ABR, IDC_STATIC_AVERAGE_MIN, IDC_COMBO_ABR_MIN, IDC_STATIC_AVERAGE_MAX, IDC_COMBO_ABR_MAX,
		IDC_STATIC_AVERAGE_STEP, IDC_COMBO_ABR_STEP, IDC_STATIC_AVERAGE_LIST,
		IDC_CHECK_ENC_VBR, IDC_STATIC_VBR_BEST, IDC_COMBO_VBR_BEST, IDC_STATIC_VBR_WORST, IDC_COMBO_VBR_WORST,
		IDC_STATIC_VBR_MIN, IDC_COMBO_VBR_MIN, IDC_STATIC_VBR_MAX, IDC_COMBO_VBR_MAX, IDC_CHECK_VBR_ENFORCE_MIN,
		IDC_CHECK_ENC_SMART, IDC_STATIC_FORMAT_COUNT };
	static const int encoding[] = {
		IDC_STATIC_CHANNEL, IDC_COMBO_ENC_STEREO, IDC_CHECK_CHANNELFORCE, IDC_STATIC_QUALITY, IDC_SLIDER_QUALITY,
		IDC_STATIC_QUALITY_TEXT, IDC_GROUP_FRAME, IDC_CHECK_COPYRIGHT, IDC_CHECK_CHECKSUM, IDC_CHECK_ORIGINAL,
		IDC_CHECK_PRIVATE, IDC_CHECK_RESERVOIR, IDC_GROUP_ADVANCED, IDC_CHECK_KEEP_ALL, IDC_CHECK_STRICT_ISO,
		IDC_CHECK_FORCE_MS };
	size_t i;

	for (i = 0; i < sizeof formats / sizeof formats[0]; i++)
		::ShowWindow(::GetDlgItem( hDialog, formats[i]), tab == TAB_FORMATS ? SW_SHOW : SW_HIDE);
	for (i = 0; i < sizeof encoding / sizeof encoding[0]; i++)
		::ShowWindow(::GetDlgItem( hDialog, encoding[i]), tab == TAB_ENCODING ? SW_SHOW : SW_HIDE);
	SendMessage(::GetDlgItem( hDialog, IDC_TAB_SETTINGS), TCM_SETCURSEL, (WPARAM) tab, 0);
}

/**
	\brief Moves the other end of a range along when the end that was chosen
	       passes it, so that the lower end stays at or below the upper one.

	\param hDialog the settings dialog.
	\param lower   the combo box of the lower end.
	\param upper   the combo box of the upper end.
	\param moved   the one that changed; any other leaves both as they are.
*/
void AEncodeProperties::KeepRangeInOrder(HWND hDialog, int lower, int upper, int moved)
{
	HWND const low = GetDlgItem( hDialog, lower);
	HWND const high = GetDlgItem( hDialog, upper);
	unsigned int const low_value = ItemDataOf(low, 0);
	unsigned int const high_value = ItemDataOf(high, 0);

	if (low_value <= high_value)
		return;
	if (moved == lower)
		SelectItemData(high, low_value);
	else if (moved == upper)
		SelectItemData(low, high_value);
}

/**
	\brief Ticks again the family of formats that was cleared if no other
	       one is ticked.

	\param hDialog the settings dialog.
	\param clicked the check box that changed.
*/
void AEncodeProperties::KeepOneFamily(HWND hDialog, int clicked)
{
	if (::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_CBR) != BST_CHECKED
	    && ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_ABR) != BST_CHECKED
	    && ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_VBR) != BST_CHECKED)
		::CheckDlgButton( hDialog, clicked, BST_CHECKED);
}

/**
	\brief Enables the range controls of each family that is ticked and
	       disables those of each that is not.

	\param hDialog the settings dialog.
*/
void AEncodeProperties::EnableFamilyControls(HWND hDialog)
{
	static const int cbr[] = { IDC_STATIC_CBR_MIN, IDC_COMBO_CBR_MIN, IDC_STATIC_CBR_MAX, IDC_COMBO_CBR_MAX };
	static const int abr[] = { IDC_STATIC_AVERAGE_MIN, IDC_COMBO_ABR_MIN, IDC_STATIC_AVERAGE_MAX, IDC_COMBO_ABR_MAX,
	                           IDC_STATIC_AVERAGE_STEP, IDC_COMBO_ABR_STEP, IDC_STATIC_AVERAGE_LIST };
	static const int vbr[] = { IDC_STATIC_VBR_BEST, IDC_COMBO_VBR_BEST, IDC_STATIC_VBR_WORST, IDC_COMBO_VBR_WORST,
	                           IDC_STATIC_VBR_MIN, IDC_COMBO_VBR_MIN, IDC_STATIC_VBR_MAX, IDC_COMBO_VBR_MAX,
	                           IDC_CHECK_VBR_ENFORCE_MIN };
	BOOL const with_cbr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_CBR) == BST_CHECKED;
	BOOL const with_abr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_ABR) == BST_CHECKED;
	BOOL const with_vbr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_VBR) == BST_CHECKED;
	size_t i;

	for (i = 0; i < sizeof cbr / sizeof cbr[0]; i++)
		::EnableWindow(::GetDlgItem( hDialog, cbr[i]), with_cbr);
	for (i = 0; i < sizeof abr / sizeof abr[0]; i++)
		::EnableWindow(::GetDlgItem( hDialog, abr[i]), with_abr);
	for (i = 0; i < sizeof vbr / sizeof vbr[0]; i++)
		::EnableWindow(::GetDlgItem( hDialog, vbr[i]), with_vbr);
}

/**
	\brief Writes the ABR bitrate line (see ABR_LIST_ALL) for the range the
	       dialog shows.

	\param hDialog the settings dialog.
*/
void AEncodeProperties::UpdateAbrList(HWND hDialog)
{
	char text[ABR_TEXT_CHARS];

	DescribeAbrLadder(AbrLadder(ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_MIN), 0),
	                            ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_MAX), 0),
	                            ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_STEP), 1)),
	                  text, sizeof text);
	::SetDlgItemText(hDialog, IDC_STATIC_AVERAGE_LIST, text);
}

/**
	\brief Tells whether the settings dialog shows joint stereo as the channel
	       mode.

	\param hSettings the settings dialog.
	\return true for joint stereo.
*/
bool AEncodeProperties::ShowsJointStereo(HWND hSettings)
{
	return SendMessage(GetDlgItem( hSettings, IDC_COMBO_ENC_STEREO), CB_GETCURSEL, 0, 0) == CHANNEL_INDEX_JOINT_STEREO;
}

/**
	\brief Returns the FormatListSettings that the settings dialog shows.

	\param hDialog the settings dialog.
	\return the dialog's choices, with the Smart ratio of the settings, which
	        the dialog does not show.
*/
FormatListSettings AEncodeProperties::FormatListFromDlg(HWND hDialog) const
{
	FormatListSettings list = GetFormatListSettings();

	list.cbr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_CBR) == BST_CHECKED;
	list.abr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_ABR) == BST_CHECKED;
	list.vbr = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_VBR) == BST_CHECKED;
	list.smart = ::IsDlgButtonChecked( hDialog, IDC_CHECK_ENC_SMART) == BST_CHECKED;
	list.cbr_min = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_CBR_MIN), list.cbr_min);
	list.cbr_max = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_CBR_MAX), list.cbr_max);
	list.abr_min = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_MIN), list.abr_min);
	list.abr_max = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_MAX), list.abr_max);
	list.abr_step = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_ABR_STEP), list.abr_step);
	list.vbr_best = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_VBR_BEST), list.vbr_best);
	list.vbr_worst = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_VBR_WORST), list.vbr_worst);
	list.vbr_min = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_VBR_MIN), list.vbr_min);
	list.vbr_max = ItemDataOf(GetDlgItem( hDialog, IDC_COMBO_VBR_MAX), list.vbr_max);
	return list;
}

/**
	\brief Writes the line that counts the formats the dialog would offer, by
	       family.

	\param hDialog the settings dialog.
*/
void AEncodeProperties::UpdateFormatCount(HWND hDialog) const
{
	FormatListSettings const list = FormatListFromDlg(hDialog);
	std::vector<bitrate_item> cbr, abr, vbr;
	char text[FORMAT_COUNT_CHARS];

	ACM::BuildFormatList(list, ACM::FAMILY_CBR, cbr);
	ACM::BuildFormatList(list, ACM::FAMILY_ABR, abr);
	ACM::BuildFormatList(list, ACM::FAMILY_VBR, vbr);
	snprintf(text, sizeof text, "The list offers %u formats: %u CBR, %u ABR, %u VBR.",
	         (unsigned int) (cbr.size() + abr.size() + vbr.size()), (unsigned int) cbr.size(),
	         (unsigned int) abr.size(), (unsigned int) vbr.size());
	::SetDlgItemText(hDialog, IDC_STATIC_FORMAT_COUNT, text);
}

/**
	\brief Returns the FormatListSettings of the current settings.

	\return the settings' choices.
*/
FormatListSettings AEncodeProperties::GetFormatListSettings() const
{
	FormatListSettings list;

	list.cbr = bCbrOutput;
	list.abr = bAbrOutput;
	list.vbr = bVbrOutput;
	list.smart = bSmartOutput;
	list.smart_ratio = SmartRatioMax;
	list.cbr_min = CbrBitrate_Min;
	list.cbr_max = CbrBitrate_Max;
	list.abr_min = AverageBitrate_Min;
	list.abr_max = AverageBitrate_Max;
	list.abr_step = AverageBitrate_Step;
	list.vbr_best = VbrQuality_Best;
	list.vbr_worst = VbrQuality_Worst;
	list.vbr_min = VbrBitrate_Min;
	list.vbr_max = VbrBitrate_Max;
	return list;
}

/**
	\brief Tells whether a value is one of the bitrates of the list, the
	       MPEG-1 and MPEG-2 bitrates.

	\param kbps the value, in kbit/s.
	\return true for a bitrate of the list.
*/
bool AEncodeProperties::IsListBitrate(unsigned int kbps)
{
	for (size_t i = 0; i < sizeof the_Bitrates / sizeof the_Bitrates[0]; i++)
	{
		if (the_Bitrates[i] == kbps)
			return true;
	}
	return false;
}

/**
	\brief Tells whether a value is a VBR bitrate limit that the codec takes:
	       VBR_BITRATE_NO_LIMIT or a bitrate of the list.

	\param kbps the value, in kbit/s.
	\return true for a limit the codec takes.
*/
bool AEncodeProperties::IsVbrBitrateLimit(unsigned int kbps)
{
	return kbps == VBR_BITRATE_NO_LIMIT || IsListBitrate(kbps);
}

/**
	\brief Fills a list with the bitrates of the list, highest first, and
	       for a VBR limit "No limit" before them. The data of each entry is
	       its bitrate, VBR_BITRATE_NO_LIMIT for "No limit".

	\param combo         the combo box.
	\param with_no_limit true for a VBR limit.
*/
void AEncodeProperties::FillBitrateList(HWND combo, bool with_no_limit)
{
	char text[sizeof "320 kbps"];
	LRESULT item;

	SendMessage(combo, CB_RESETCONTENT, 0, 0);
	if (with_no_limit)
	{
		item = SendMessage(combo, CB_ADDSTRING, 0, (LPARAM) "No limit");
		SendMessage(combo, CB_SETITEMDATA, item, VBR_BITRATE_NO_LIMIT);
	}
	for (size_t i = 0; i < sizeof the_Bitrates / sizeof the_Bitrates[0]; i++)
	{
		snprintf(text, sizeof text, "%u kbps", the_Bitrates[i]);
		item = SendMessage(combo, CB_ADDSTRING, 0, (LPARAM) text);
		SendMessage(combo, CB_SETITEMDATA, item, the_Bitrates[i]);
	}
}

/**
	\brief Keeps the VBR bitrate limits in order, as KeepRangeInOrder() keeps
	       a range, when both limits are set.

	\param hDialog the configuration dialog.
	\param moved   the combo box that changed: IDC_COMBO_VBR_MIN or
	               IDC_COMBO_VBR_MAX.
*/
void AEncodeProperties::KeepVbrLimitsInOrder(HWND hDialog, int moved)
{
	HWND const min_combo = GetDlgItem( hDialog, IDC_COMBO_VBR_MIN);
	HWND const max_combo = GetDlgItem( hDialog, IDC_COMBO_VBR_MAX);
	unsigned int const lowest = ItemDataOf(min_combo, VBR_BITRATE_NO_LIMIT);
	unsigned int const highest = ItemDataOf(max_combo, VBR_BITRATE_NO_LIMIT);

	if (lowest == VBR_BITRATE_NO_LIMIT || highest == VBR_BITRATE_NO_LIMIT || lowest <= highest)
		return;
	if (moved == IDC_COMBO_VBR_MIN)
		SelectItemData(max_combo, lowest);
	else
		SelectItemData(min_combo, highest);
}

