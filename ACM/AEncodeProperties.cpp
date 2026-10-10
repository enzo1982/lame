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

#ifndef TTS_BALLOON
#define TTS_BALLOON            0x40
#endif // TTS_BALLOON

/** \brief The highest bitrate that LAME encodes, in kbit/s. */
static const unsigned int ABR_BITRATE_LIMIT = 320;

// The bitrates of the standard, in kbit/s, highest first: those of MPEG-1, those
// of MPEG-2 (which MPEG-2.5 uses here too), and both lists together.
// FillBitrateTables() fills them from the library before anything reads them.
// The dialog and the configuration file use positions in the_Bitrates.
unsigned int AEncodeProperties::the_Bitrates[18];
unsigned int AEncodeProperties::the_MPEG1_Bitrates[14];
unsigned int AEncodeProperties::the_MPEG2_Bitrates[14];
const unsigned int AEncodeProperties::the_ChannelModes[4] = { STEREO, JOINT_STEREO, DUAL_CHANNEL, MONO };

ToolTipItem AEncodeProperties::Tooltips[14]={
	{ IDC_CHECK_ENC_ABR, "Allow encoding with an average bitrate\r\ninstead of a constant one.\r\n\r\nIt can improve the quality for the same bitrate." },
	{ IDC_CHECK_COPYRIGHT, "Mark the encoded data as copyrighted." },
	{ IDC_CHECK_CHECKSUM, "Put a checksum in the encoded data.\r\n\r\nThis can make the file less sensitive to data loss." },
	{ IDC_CHECK_ORIGINAL, "Mark the encoded data as an original file." },
	{ IDC_CHECK_PRIVATE, "Mark the encoded data as private." },
	{ IDC_CHECK_RESERVOIR, "Use the bit reservoir.\r\n\r\nA frame can then use bits that earlier frames left over.\r\nWithout it, every frame contains all of its own data." },
	{ IDC_COMBO_ENC_STEREO, "Select the type of stereo mode used for encoding:\r\n\r\n- Stereo : the usual one\r\n- Joint-Stereo : mix both channel to achieve better compression\r\n- Dual Channel : treat both channel as separate\r\n- Mono : one channel" },
	{ IDC_CHECK_CHANNELFORCE, "Use the selected mode even when the input has another number of channels.\r\n\r\nOnly Mono can be forced: stereo input is then encoded as mono." },
	{ IDC_CHECK_ENC_SMART, "Disable bitrate when there is too much compression.\r\n(default 1:15 ratio)" },
	{ IDC_STATIC_CONFIG_VERSION, "Version of this codec.\r\n\r\nvX.X.X is the version of the codec interface.\r\nX.XX is the version of the encoding engine." },
	{ IDC_SLIDER_AVERAGE_MIN, "Select the minimum Average Bitrate allowed." },
	{ IDC_SLIDER_AVERAGE_MAX, "Select the maximum Average Bitrate allowed." },
	{ IDC_SLIDER_AVERAGE_STEP, "Select the step of Average Bitrate between the min and max.\r\n\r\nA step of 5 between 152 and 165 means you have :\r\n165, 160 and 155" },
	{ IDC_SLIDER_AVERAGE_SAMPLE, "Check the resulting values of the (min,max,step) combination.\r\n\r\nUse the keyboard to navigate (right -> left)." },
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
static const char ELEMENT_COPYRIGHT[]     = "Copyright";
static const char ELEMENT_CRC[]           = "CRC";
static const char ELEMENT_ORIGINAL[]      = "Original";
static const char ELEMENT_PRIVATE[]       = "Private";
static const char ELEMENT_BIT_RESERVOIR[] = "Bit_reservoir";
static const char ELEMENT_CHANNEL[]       = "Channel";
static const char ATTRIBUTE_DEFAULT[]     = "default";
static const char ATTRIBUTE_NAME[]        = "name";
static const char ATTRIBUTE_USE[]         = "use";
static const char ATTRIBUTE_RATIO[]       = "ratio";
static const char ATTRIBUTE_MIN[]         = "min";
static const char ATTRIBUTE_MAX[]         = "max";
static const char ATTRIBUTE_STEP[]        = "step";
static const char ATTRIBUTE_MODE[]        = "mode";
static const char ATTRIBUTE_FORCE[]       = "force";
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
			// check if it's the ABR sliders
			if ((HWND)lParam == GetDlgItem(hwndDlg,IDC_SLIDER_AVERAGE_MIN))
			{
				the_prop->UpdateDlgFromSlides(hwndDlg);
			}
			else if ((HWND)lParam == GetDlgItem(hwndDlg,IDC_SLIDER_AVERAGE_MAX))
			{
				the_prop->UpdateDlgFromSlides(hwndDlg);
			}
			else if ((HWND)lParam == GetDlgItem(hwndDlg,IDC_SLIDER_AVERAGE_STEP))
			{
				the_prop->UpdateDlgFromSlides(hwndDlg);
			}
			else if ((HWND)lParam == GetDlgItem(hwndDlg,IDC_SLIDER_AVERAGE_SAMPLE))
			{
				the_prop->UpdateDlgFromSlides(hwndDlg);
			}
			break;

		case WM_NOTIFY:
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
			return "Joint-stereo";
		case CHANNEL_INDEX_DUAL_CHANNEL:
			return "Dual Channel";
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
	INT_PTR const ret = ::DialogBoxParam(Hinstance, MAKEINTRESOURCE(IDD_CONFIG), HwndParent, ::ConfigProc, (LPARAM) this);
	return ret > 0;
}

bool AEncodeProperties::InitConfigDlg(HWND HwndDlg)
{

	int i;

	// Add required channel modes
	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_RESETCONTENT , NULL, NULL);
	for (i=0;i<GetChannelLentgh();i++)
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_ADDSTRING, NULL, (LPARAM) GetChannelModeString(i));

	char tmp[sizeof "v" + ACM::VERSION_STRING_CHARS];
	snprintf(tmp, sizeof tmp, "v%s", ACM::GetVersionString());
	SetWindowText( GetDlgItem( HwndDlg, IDC_STATIC_CONFIG_VERSION), tmp);

	// Add ABR Sliders
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MIN), TBM_SETRANGE, TRUE, MAKELONG(8,320));
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MAX), TBM_SETRANGE, TRUE, MAKELONG(8,320));
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_STEP), TBM_SETRANGE, TRUE, MAKELONG(1,16));

	// Tool-Tip initialiasiation
	TOOLINFO ti;
	HWND ToolTipWnd;
	char DisplayStr[30] = "test tooltip";

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
	for(i=0; i<sizeof Tooltips/sizeof Tooltips[0]; ++i) {
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
	::CheckDlgButton( HwndDlg, IDC_CHECK_ENC_ABR,      GetAbrOutputMode()  ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_RESERVOIR,    !GetNoBiResMode() ?BST_CHECKED:BST_UNCHECKED );
	::CheckDlgButton( HwndDlg, IDC_CHECK_CHANNELFORCE, bForceChannel     ?BST_CHECKED:BST_UNCHECKED );
	
	// Add required channel modes
	for (i=0;i<GetChannelLentgh();i++)
	{
		if (i == nChannelIndex)
		{
			SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_SETCURSEL, i, NULL);
			break;
		}
	}

	// Add VBR Quality
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MIN), TBM_SETPOS, TRUE, AverageBitrate_Min);
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MAX), TBM_SETPOS, TRUE, AverageBitrate_Max);
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_STEP), TBM_SETPOS, TRUE, AverageBitrate_Step);
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETPOS, TRUE, AverageBitrate_Max);

	UpdateDlgFromSlides(HwndDlg);

	EnableAbrOptions(HwndDlg, GetAbrOutputMode());






	/**
		\todo Select the right saved config
	*/

	return true;
}

bool AEncodeProperties::UpdateValueFromDlg(HWND HwndDlg)
{
	nChannelIndex      = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO),   CB_GETCURSEL, NULL, NULL);

	bCRC          = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_CHECKSUM)     == BST_CHECKED);
	bCopyright    = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_COPYRIGHT)    == BST_CHECKED);
	bOriginal     = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_ORIGINAL)     == BST_CHECKED);
	bPrivate      = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_PRIVATE)      == BST_CHECKED);
	bSmartOutput  = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_ENC_SMART)    == BST_CHECKED);
	bAbrOutput    = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_ENC_ABR)      == BST_CHECKED);
	bNoBitRes     =!(::IsDlgButtonChecked( HwndDlg, IDC_CHECK_RESERVOIR)    == BST_CHECKED);
	bForceChannel = (::IsDlgButtonChecked( HwndDlg, IDC_CHECK_CHANNELFORCE) == BST_CHECKED);

	AverageBitrate_Min  = SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MIN), TBM_GETPOS , NULL, NULL);
	AverageBitrate_Max  = SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_MAX), TBM_GETPOS , NULL, NULL);
	AverageBitrate_Step = SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_AVERAGE_STEP), TBM_GETPOS , NULL, NULL);

	EnableAbrOptions(HwndDlg, bAbrOutput);

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
	bForceChannel = false;
	bSmartOutput  = true;
	bAbrOutput    = true;
	
	AverageBitrate_Min = 80; // a bit lame
	AverageBitrate_Max = 160; // a bit lame
	AverageBitrate_Step = 8; // a bit lame
	SmartRatioMax = 15.0;

	nChannelIndex = CHANNEL_INDEX_JOINT_STEREO;
	nMinBitrateIndex = 6; // 128 kbps (works for both MPEGI and II)

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

	// Smart Output parameter
	tmpElt = ChildElement(*the_element, ELEMENT_ABR);
	if (tmpElt != NULL)
	{
		SetAttributeBool( tmpElt, ATTRIBUTE_USE, bAbrOutput);
		tmpElt->SetAttribute(ATTRIBUTE_MIN, AverageBitrate_Min);
		tmpElt->SetAttribute(ATTRIBUTE_MAX, AverageBitrate_Max);
		tmpElt->SetAttribute(ATTRIBUTE_STEP, AverageBitrate_Step);
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

		case IDC_CHECK_ENC_ABR:
			EnableAbrOptions(parentWnd, ::IsDlgButtonChecked( parentWnd, IDC_CHECK_ENC_ABR) == BST_CHECKED);
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

void AEncodeProperties::UpdateDlgFromSlides(HWND hwndDlg) const
{
	UINT value_min, value_max, value_step, value;
	char tmp[4];

	value_min = SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_MIN), TBM_GETPOS, NULL, NULL);
	value_max = SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_MAX), TBM_GETPOS, NULL, NULL);

	if (value_min>value_max)
	{
		SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_MIN), TBM_SETPOS, TRUE, value_max);
		UpdateDlgFromSlides(hwndDlg);
		return;
	}

	snprintf(tmp, sizeof tmp, "%3u", value_min);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_MIN_VALUE), tmp);
	
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETRANGEMIN, TRUE, value_min);

	snprintf(tmp, sizeof tmp, "%3u", value_max);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_MAX_VALUE), tmp);
	
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETRANGEMAX, TRUE, value_max);
	
	value_step = SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_STEP), TBM_GETPOS, NULL, NULL);
	snprintf(tmp, sizeof tmp, "%3u", value_step);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_STEP_VALUE), tmp);

	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_CLEARTICS, TRUE, 0);
	std::vector<unsigned int> const tics = AbrLadder(value_min, value_max, value_step);
	for (size_t i = 0; i < tics.size(); i++)
	{
		SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETTIC, 0, tics[i]);
	}
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETLINESIZE, 0, value_step);
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETPAGESIZE, 0, value_step);
	
	value = SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_GETPOS, NULL, NULL);
	snprintf(tmp, sizeof tmp, "%3u", value);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_SAMPLE_VALUE), tmp);
}

void AEncodeProperties::EnableAbrOptions(HWND hDialog, bool enable)
{
	::EnableWindow(::GetDlgItem( hDialog, IDC_SLIDER_AVERAGE_MIN), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_SLIDER_AVERAGE_MAX), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_SLIDER_AVERAGE_STEP), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_SLIDER_AVERAGE_SAMPLE), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_MIN), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_MAX), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_STEP), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_SAMPLE), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_MIN_VALUE), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_MAX_VALUE), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_STEP_VALUE), enable);
	::EnableWindow(::GetDlgItem( hDialog, IDC_STATIC_AVERAGE_SAMPLE_VALUE), enable);
}

