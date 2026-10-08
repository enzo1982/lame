/**
 *
 * Lame ACM wrapper, encode/decode MP3 based RIFF/AVI files in MS Windows
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

#ifdef _MSC_VER
// no problem with unknown pragmas
#pragma warning(disable: 4068)
#endif

#include "resource.h"
#include <lame.h>
#include "adebug.h"
#include "AEncodeProperties.h"
#include "ACM.h"
//#include "AParameters/AParameters.h"

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
//const char         AEncodeProperties::the_Presets[][13] = {"None", "CD", "Studio", "Hi-Fi", "Phone", "Voice", "Radio", "Tape", "FM", "AM", "SW"};
//const LAME_QUALTIY_PRESET AEncodeProperties::the_Presets[] = {LQP_NOPRESET, LQP_R3MIX_QUALITY, LQP_NORMAL_QUALITY, LQP_LOW_QUALITY, LQP_HIGH_QUALITY, LQP_VERYHIGH_QUALITY, LQP_VOICE_QUALITY, LQP_PHONE, LQP_SW, LQP_AM, LQP_FM, LQP_VOICE, LQP_RADIO, LQP_TAPE, LQP_HIFI, LQP_CD, LQP_STUDIO};

ToolTipItem AEncodeProperties::Tooltips[15]={
	{ IDC_CHECK_ENC_ABR, "Allow encoding with an average bitrate\r\ninstead of a constant one.\r\n\r\nIt can improve the quality for the same bitrate." },
	{ IDC_CHECK_COPYRIGHT, "Mark the encoded data as copyrighted." },
	{ IDC_CHECK_CHECKSUM, "Put a checksum in the encoded data.\r\n\r\nThis can make the file less sensitive to data loss." },
	{ IDC_CHECK_ORIGINAL, "Mark the encoded data as an original file." },
	{ IDC_CHECK_PRIVATE, "Mark the encoded data as private." },
	{ IDC_CHECK_RESERVOIR, "Use the bit reservoir.\r\n\r\nA frame can then use bits that earlier frames left over.\r\nWithout it, every frame contains all of its own data." },
	{ IDC_COMBO_ENC_STEREO, "Select the type of stereo mode used for encoding:\r\n\r\n- Stereo : the usual one\r\n- Joint-Stereo : mix both channel to achieve better compression\r\n- Dual Channel : treat both channel as separate\r\n- Mono : one channel" },
	{ IDC_CHECK_CHANNELFORCE, "Use the selected mode even when the input has another number of channels.\r\n\r\nOnly Mono can be forced: stereo input is then encoded as mono." },
	{ IDC_STATIC_DECODING, "Decoding not supported for the moment by the codec." },
	{ IDC_CHECK_ENC_SMART, "Disable bitrate when there is too much compression.\r\n(default 1:15 ratio)" },
	{ IDC_STATIC_CONFIG_VERSION, "Version of this codec.\r\n\r\nvX.X.X is the version of the codec interface.\r\nX.XX is the version of the encoding engine." },
	{ IDC_SLIDER_AVERAGE_MIN, "Select the minimum Average Bitrate allowed." },
	{ IDC_SLIDER_AVERAGE_MAX, "Select the maximum Average Bitrate allowed." },
	{ IDC_SLIDER_AVERAGE_STEP, "Select the step of Average Bitrate between the min and max.\r\n\r\nA step of 5 between 152 and 165 means you have :\r\n165, 160 and 155" },
	{ IDC_SLIDER_AVERAGE_SAMPLE, "Check the resulting values of the (min,max,step) combination.\r\n\r\nUse the keyboard to navigate (right -> left)." },
};
//int AEncodeProperties::tst = 0;

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

#if 0
#pragma argsused
static UINT CALLBACK DLLFindCallback(
  HWND hdlg,      // handle to child dialog box
  UINT uiMsg,     // message identifier
  WPARAM wParam,  // message parameter
  LPARAM lParam   // message parameter
  )
{
	UINT result = 0;

	switch (uiMsg)
	{
		case WM_NOTIFY:
			OFNOTIFY * info = (OFNOTIFY *)lParam;
			if (info->hdr.code == CDN_FILEOK)
			{
				result = 1; // by default we don't accept the file

				// Check if the selected file is a valid DLL with all the required functions
				ALameDLL * tstFile = new ALameDLL;
				if (tstFile != NULL)
				{
					if (tstFile->Load(info->lpOFN->lpstrFile))
					{
						result = 0;
					}

					delete tstFile;
				}

				if (result == 1)
				{
					TCHAR output[250];
					::LoadString(AOut::GetInstance(),IDS_STRING_DLL_UNRECOGNIZED,output,250);
					AOut::MyMessageBox( output, MB_OK|MB_ICONEXCLAMATION, hdlg);
					SetWindowLong(hdlg, DWL_MSGRESULT , -100);
				}
			}
	}

	return result;
}

#pragma argsused
static int CALLBACK BrowseFolderCallbackroc(
    HWND hwnd,
    UINT uMsg,
    LPARAM lParam,
    LPARAM lpData
    )
{
	AEncodeProperties * the_prop;
	the_prop = (AEncodeProperties *) lpData;


	if (uMsg == BFFM_INITIALIZED)
	{
//		char FolderName[MAX_PATH];
//		SHGetPathFromIDList((LPITEMIDLIST) lParam,FolderName);
//ADbg tst;
//tst.OutPut("init folder to %s ",the_prop->GetOutputDirectory());
//		CreateFile();
		::SendMessage(hwnd, BFFM_SETSELECTION, (WPARAM)TRUE, (LPARAM)the_prop->GetOutputDirectory());
	}/* else if (uMsg == BFFM_SELCHANGED)
	{
		// verify that the folder is writable
//		::SendMessage(hwnd, BFFM_ENABLEOK, 0, (LPARAM)0); // disable
		char FolderName[MAX_PATH];
		SHGetPathFromIDList((LPITEMIDLIST) lParam, FolderName);
		
//		if (CreateFile(FolderName,STANDARD_RIGHTS_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL) == INVALID_HANDLE_VALUE)
		if ((GetFileAttributes(FolderName) & FILE_ATTRIBUTE_DIRECTORY) != 0)
			::SendMessage(hwnd, BFFM_ENABLEOK, 0, (LPARAM)1); // enable
		else
			::SendMessage(hwnd, BFFM_ENABLEOK, 0, (LPARAM)0); // disable
//ADbg tst;
//tst.OutPut("change folder to %s ",FolderName);
	}*/

	return 0;
}
#endif
#pragma argsused
static BOOL CALLBACK ConfigProc(
  HWND hwndDlg,  // handle to dialog box
  UINT uMsg,     // message
  WPARAM wParam, // first message parameter
  LPARAM lParam  // second message parameter
  )
{
	BOOL bResult = FALSE;
	AEncodeProperties * the_prop;
	the_prop = (AEncodeProperties *) GetProp(hwndDlg, "AEncodeProperties-Config");

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

			SetProp(hwndDlg, "AEncodeProperties-Config", the_prop);

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
	assert(a_channelID < sizeof(the_ChannelModes));

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
			assert(a_channelID);
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

const int AEncodeProperties::GetBitrateString(char * string, int string_size, int a_bitrateID) const
{
	assert(a_bitrateID < GetBitrateLentgh());
	assert(string != NULL);

	if (string_size >= 4)
		return wsprintf(string,"%d",the_Bitrates[a_bitrateID]);
	else
		return -1;
}

const unsigned int AEncodeProperties::GetChannelModeValue() const
{
	assert(nChannelIndex < GetChannelLentgh());

	return the_ChannelModes[nChannelIndex];
}

const unsigned int AEncodeProperties::OutputChannels(const unsigned int input_channels) const
{
	if (bForceChannel && GetChannelModeValue() == MONO && input_channels == 2)
		return 1;
	return input_channels;
}

const unsigned int AEncodeProperties::GetBitrateValue() const
{
	assert(nMinBitrateIndex < GetBitrateLentgh());

	return the_Bitrates[nMinBitrateIndex];
}

inline const int AEncodeProperties::GetBitrateValueMPEG2(DWORD & bitrate) const
{
	int i;

	for (i=0;i<sizeof(the_MPEG2_Bitrates)/sizeof(unsigned int);i++)
	{
		if (the_MPEG2_Bitrates[i] == the_Bitrates[nMinBitrateIndex])
		{
			bitrate = the_MPEG2_Bitrates[i];
			return 0;
		}
		else if (the_MPEG2_Bitrates[i] < the_Bitrates[nMinBitrateIndex])
		{
			bitrate = the_MPEG2_Bitrates[i];
			return -1;
		}
	}
	
	bitrate = 160;
	return -1;
}

inline const int AEncodeProperties::GetBitrateValueMPEG1(DWORD & bitrate) const
{
	int i;

	for (i=sizeof(the_MPEG1_Bitrates)/sizeof(unsigned int)-1;i>=0;i--)
	{
		if (the_MPEG1_Bitrates[i] == the_Bitrates[nMinBitrateIndex])
		{
			bitrate = the_MPEG1_Bitrates[i];
			return 0;
		}
		else if (the_MPEG1_Bitrates[i] > the_Bitrates[nMinBitrateIndex])
		{
			bitrate = the_MPEG1_Bitrates[i];
			return 1;
		}
	}
	
	bitrate = 32;
	return 1;
}
#if 0
const int AEncodeProperties::GetBitrateValue(DWORD & bitrate, const DWORD MPEG_Version) const
{
	assert((MPEG_Version == MPEG1) || (MPEG_Version == MPEG2));
	assert(nMinBitrateIndex < sizeof(the_Bitrates));

	if (MPEG_Version == MPEG2)
		return GetBitrateValueMPEG2(bitrate);
	else
		return GetBitrateValueMPEG1(bitrate);
}
const char * AEncodeProperties::GetPresetModeString(const int a_presetID) const
{
	assert(a_presetID < sizeof(the_Presets));

	switch (a_presetID) {
		case 1:
			return "r3mix";
		case 2:
			return "Normal";
		case 3:
			return "Low";
		case 4:
			return "High";
		case 5:
			return "Very High";
		case 6:
			return "Voice";
		case 7:
			return "Phone";
		case 8:
			return "SW";
		case 9:
			return "AM";
		case 10:
			return "FM";
		case 11:
			return "Voice";
		case 12:
			return "Radio";
		case 13:
			return "Tape";
		case 14:
			return "Hi-Fi";
		case 15:
			return "CD";
		case 16:
			return "Studio";
		default:
			return "None";
	}
}

const LAME_QUALTIY_PRESET AEncodeProperties::GetPresetModeValue() const
{
	assert(nPresetIndex < sizeof(the_Presets));

	return the_Presets[nPresetIndex];
}
#endif
bool AEncodeProperties::Config(const HINSTANCE Hinstance, const HWND HwndParent)
{
	//WM_INITDIALOG ?

	// remember the instance to retreive strings
//	hDllInstance = Hinstance;

	my_debug.OutPut("here");
	::DialogBoxParam(Hinstance, MAKEINTRESOURCE(IDD_CONFIG), HwndParent, ::ConfigProc, (LPARAM) this);
/*	if (ret == -1)
	{
		LPVOID lpMsgBuf;
		FormatMessage( 
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM | 
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf,
			0,
			NULL 
		);
		// Process any inserts in lpMsgBuf.
		// ...
		// Display the string.
		AOut::MyMessageBox( (LPCTSTR)lpMsgBuf, MB_OK | MB_ICONINFORMATION );
		// Free the buffer.
		LocalFree( lpMsgBuf );	
		return false;
	}
*/	
	return true;
}

bool AEncodeProperties::InitConfigDlg(HWND HwndDlg)
{
	// get all the required strings
//	TCHAR Version[5];
//	LoadString(hDllInstance, IDS_STRING_VERSION, Version, 5);

	int i;

	// Add required channel modes
	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_RESETCONTENT , NULL, NULL);
	for (i=0;i<GetChannelLentgh();i++)
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO), CB_ADDSTRING, NULL, (LPARAM) GetChannelModeString(i));

	char tmp[sizeof "v" + ACM::VERSION_STRING_CHARS];
	snprintf(tmp, sizeof tmp, "v%s", ACM::GetVersionString());
	SetWindowText( GetDlgItem( HwndDlg, IDC_STATIC_CONFIG_VERSION), tmp);

	// Add required bitrates
/*	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_BITRATE), CB_RESETCONTENT , NULL, NULL);
	for (i=0;i<GetBitrateLentgh();i++)
	{
		GetBitrateString(tmp, 5, i);
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_BITRATE), CB_ADDSTRING, NULL, (LPARAM) tmp );
	}

	// Add bitrates to the VBR combo box too
	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_MAXBITRATE), CB_RESETCONTENT , NULL, NULL);
	for (i=0;i<GetBitrateLentgh();i++)
	{
		GetBitrateString(tmp, 5, i);
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_MAXBITRATE), CB_ADDSTRING, NULL, (LPARAM) tmp );
	}

	// Add VBR Quality Slider
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_SETRANGE, TRUE, MAKELONG(0,9));

	// Add presets
	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_PRESET), CB_RESETCONTENT , NULL, NULL);
	for (i=0;i<GetPresetLentgh();i++)
		SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_PRESET), CB_ADDSTRING, NULL, (LPARAM) GetPresetModeString(i));
*/

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
	// get all the required strings
//	TCHAR Version[5];
//	LoadString(hDllInstance, IDS_STRING_VERSION, Version, 5);

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
//	UpdateAbrSteps(AverageBitrate_Min, AverageBitrate_Max, AverageBitrate_Step);
/*
	

	// Add required bitrates
	for (i=0;i<GetBitrateLentgh();i++)
	{
		if (i == nMinBitrateIndex)
		{
			SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_BITRATE), CB_SETCURSEL, i, NULL);
			break;
		}
	}

	// Add bitrates to the VBR combo box too
	for (i=0;i<GetBitrateLentgh();i++)
	{
		if (i == nMaxBitrateIndex)
		{
			SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_MAXBITRATE), CB_SETCURSEL, i, NULL);
			break;
		}
	}

//	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_SETRANGE, TRUE, MAKELONG(0,9));

	char tmp[3];
	wsprintf(tmp,"%d",VbrQuality);
	SetWindowText(GetDlgItem( HwndDlg, IDC_CONFIG_QUALITY), tmp);
	SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_SETPOS, TRUE, VbrQuality);
	
	wsprintf(tmp,"%d",AverageBitrate);
	SetWindowText(GetDlgItem( HwndDlg, IDC_EDIT_AVERAGE), tmp);
	

	// Add presets
	for (i=0;i<GetPresetLentgh();i++)
	{
		if (i == nPresetIndex)
		{
			SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_PRESET), CB_SETCURSEL, i, NULL);
			break;
		}
	}

	// Add User configs
//	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_SETTINGS), CB_RESETCONTENT , NULL, NULL);
	::SetWindowText(::GetDlgItem( HwndDlg, IDC_EDIT_OUTPUTDIR), OutputDir.c_str());
*/
	/**
		\todo Select the right saved config
	*/

	return true;
}

bool AEncodeProperties::UpdateValueFromDlg(HWND HwndDlg)
{
	nChannelIndex      = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_ENC_STEREO),   CB_GETCURSEL, NULL, NULL);
//	nMinBitrateIndex   = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_BITRATE),    CB_GETCURSEL, NULL, NULL);
//	nMaxBitrateIndex   = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_MAXBITRATE), CB_GETCURSEL, NULL, NULL);
//	nPresetIndex       = SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_PRESET),     CB_GETCURSEL, NULL, NULL);
//	VbrQuality         = SendMessage(GetDlgItem( HwndDlg, IDC_SLIDER_QUALITY), TBM_GETPOS , NULL, NULL);

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

/*	char tmpPath[MAX_PATH];
	::GetWindowText( ::GetDlgItem( HwndDlg, IDC_EDIT_OUTPUTDIR), tmpPath, MAX_PATH);
	OutputDir = tmpPath;

	::GetWindowText( ::GetDlgItem( HwndDlg, IDC_EDIT_AVERAGE), tmpPath, MAX_PATH);
	AverageBitrate = atoi(tmpPath);
	if (AverageBitrate < 8)
		AverageBitrate = 8;
	if (AverageBitrate > 320)
		AverageBitrate = 320;
*/
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
	nMaxBitrateIndex = 4; // 160 kbps (works for both MPEGI and II)
	nPresetIndex = 0; // None
	VbrQuality = 1; // Quite High
//	AverageBitrate = 128; // a bit lame

//	OutputDir = "c:\\";

//	DllLocation = "plugins\\lame_enc.dll";

	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();
	if (CurrentNode != NULL)
	{
		std::string CurrentConfig = "";

		if (CurrentNode->Attribute("default") != NULL)
		{
			CurrentConfig = *CurrentNode->Attribute("default");
		}

/*		// output parameters
		TiXmlElement* iterateElmt = node->FirstChildElement("DLL");
		if (iterateElmt != NULL)
		{
			const std::string * tmpname = iterateElmt->Attribute("location");
			if (tmpname != NULL)
			{
				DllLocation = *tmpname;
			}
		}
*/
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
	SaveValuesToStringKey("Current");
}

AEncodeProperties::AEncodeProperties(HMODULE hModule)
 :my_debug(ADbg(DEBUG_LEVEL_CREATION)),
 my_hModule(hModule)
{
	FillBitrateTables();

	std::string path = "";
//	HMODULE htmp = LoadLibrary("out_lame.dll");
	if (hModule != NULL)
	{
		char output[MAX_PATH];
		::GetModuleFileName(hModule, output, MAX_PATH);
//		::FreeLibrary(htmp);

		path = output;
	}
	my_store_location = path.substr(0,path.find_last_of('\\')+1);
	my_store_location += "lame_acm.xml";

	my_debug.OutPut("store path = %s",my_store_location.c_str());
//#ifdef OLD
//	::OutputDebugString(my_store_location.c_str());

	// make sure the XML file is present
	HANDLE hFile = ::CreateFile(my_store_location.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_ARCHIVE, NULL );
	::CloseHandle(hFile);
//#endif // OLD
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
	if (!my_stored_data.LoadFile(my_store_location))
		return NULL;

	TiXmlNode * node = my_stored_data.FirstChild("lame_acm");

	if (node == NULL)
		return NULL;

	return node->FirstChildElement("encodings");
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
	TiXmlElement * elt = parent.FirstChildElement("config");

	while (elt != NULL)
	{
		const std::string * tmpname = elt->Attribute("name");
		if (tmpname != NULL && tmpname->compare(name) == 0)
			break;
		elt = elt->NextSiblingElement("config");
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

	TiXmlNode* node = my_stored_data.FirstChild("lame_acm");

	if (node == NULL)
	{
		node = my_stored_data.InsertEndChild(TiXmlElement("lame_acm"));

		if (node == NULL)
			return;
	}

	TiXmlElement* ConfigNode = node->FirstChildElement("encodings");

	if (ConfigNode == NULL)
	{
		TiXmlElement encodings("encodings");

		encodings.SetAttribute("default", config_name);

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
		TiXmlElement created("config");

		created.SetAttribute("name",config_name);

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
		tmpElt = iterateElmt->FirstChildElement("Smart");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bSmartOutput = (tmpname->compare("true") == 0);
			
			tmpname = tmpElt->Attribute("ratio");
			if (tmpname != NULL)
				SmartRatioMax = DoubleFromAttribute(*tmpname);
		}

		// Smart output parameter
		tmpElt = iterateElmt->FirstChildElement("ABR");
		if (tmpElt != NULL)
		{
			unsigned int abr_min = AverageBitrate_Min;
			unsigned int abr_max = AverageBitrate_Max;
			unsigned int abr_step = AverageBitrate_Step;

			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bAbrOutput = (tmpname->compare("true") == 0);

			tmpname = tmpElt->Attribute("min");
			if (tmpname != NULL)
				abr_min = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute("max");
			if (tmpname != NULL)
				abr_max = UnsignedFromAttribute(*tmpname);

			tmpname = tmpElt->Attribute("step");
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
		tmpElt = iterateElmt->FirstChildElement("Copyright");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bCopyright = (tmpname->compare("true") == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement("CRC");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bCRC = (tmpname->compare("true") == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement("Original");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bOriginal = (tmpname->compare("true") == 0);
		}

		// Copyright parameter
		tmpElt = iterateElmt->FirstChildElement("Private");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bPrivate = (tmpname->compare("true") == 0);
		}
		// Bit reservoir parameter
		tmpElt = iterateElmt->FirstChildElement("Bit_reservoir");
		if (tmpElt != NULL)
		{
			tmpname = tmpElt->Attribute("use");
			if (tmpname != NULL)
				bNoBitRes = !(tmpname->compare("true") == 0);
		}
/*
		// bitrates
		tmpElt = iterateElmt->FirstChildElement("bitrate");
		tmpname = tmpElt->Attribute("min");
		if (tmpname != NULL)
		{
			unsigned int uitmp = atoi(tmpname->c_str());
			for (int i=0;i<sizeof(the_Bitrates)/sizeof(unsigned int);i++)
			{
				if (the_Bitrates[i] == uitmp)
				{
					nMinBitrateIndex = i;
					break;
				}
			}
		}

		tmpname = tmpElt->Attribute("max");
		if (tmpname != NULL)
		{
			unsigned int uitmp = atoi(tmpname->c_str());
			for (int i=0;i<sizeof(the_Bitrates)/sizeof(unsigned int);i++)
			{
				if (the_Bitrates[i] == uitmp)
				{
					nMaxBitrateIndex = i;
					break;
				}
			}
		}
*/
/*
		// output parameters
		tmpElt = iterateElmt->FirstChildElement("output");
		if (tmpElt != NULL)
		{
			OutputDir = *tmpElt->Attribute("path");
		}
*/
//#ifdef OLD
		// Channel mode parameter
		tmpElt = iterateElmt->FirstChildElement("Channel");
		if (tmpElt != NULL)
		{
			const std::string * tmpStr = tmpElt->Attribute("mode");
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
			tmpname = tmpElt->Attribute("force");
			if (tmpname != NULL)
				bForceChannel = (tmpname->compare("true") == 0);
		}
//#endif // OLD

		// Preset parameter
/*
		tmpElt = iterateElmt->FirstChildElement("Preset");
		if (tmpElt != NULL)
		{
			const std::string * tmpStr = tmpElt->Attribute("type");
			for (int i=0;i<GetPresetLentgh();i++)
			{
				if (tmpStr->compare(GetPresetModeString(i)) == 0)
				{
					nPresetIndex = i;
					break;
				}
			}

		}
*/
	}
}

bool AEncodeProperties::operator !=(const AEncodeProperties & the_instance) const
{
/*
	::OutputDebugString(bCopyright != the_instance.bCopyright?"1":"-");
	::OutputDebugString(bCRC != the_instance.bCRC            ?"2":"-");
	::OutputDebugString(bOriginal != the_instance.bOriginal  ?"3":"-");
	::OutputDebugString(bPrivate != the_instance.bPrivate    ?"4":"-");
	::OutputDebugString(bNoBitRes != the_instance.bNoBitRes  ?"5":"-");
	::OutputDebugString(bForceChannel != the_instance.bForceChannel?"8":"-");
	::OutputDebugString(nChannelIndex != the_instance.nChannelIndex?"10":"-");
	::OutputDebugString(nMinBitrateIndex != the_instance.nMinBitrateIndex?"11":"-");
	::OutputDebugString(nMaxBitrateIndex != the_instance.nMaxBitrateIndex?"12":"-");
	::OutputDebugString(nPresetIndex != the_instance.nPresetIndex?"13":"-");
	::OutputDebugString(VbrQuality != the_instance.VbrQuality?"14":"-");
	::OutputDebugString(AverageBitrate != the_instance.AverageBitrate?"15":"-");
	::OutputDebugString(OutputDir.compare(the_instance.OutputDir) != 0?"17":"-");

	std::string tmp = "";
	char tmpI[10];
	_itoa(AverageBitrate,tmpI,10);
	tmp += tmpI;
	tmp += " != ";
	_itoa(the_instance.AverageBitrate,tmpI,10);
	tmp += tmpI;
	::OutputDebugString(tmp.c_str());
*/
	return ((bCopyright != the_instance.bCopyright)
		 || (bCRC != the_instance.bCRC)
		 || (bOriginal != the_instance.bOriginal)
		 || (bPrivate != the_instance.bPrivate)
		 || (bSmartOutput != the_instance.bSmartOutput)
		 || (SmartRatioMax != the_instance.SmartRatioMax)
		 || (bAbrOutput != the_instance.bAbrOutput)
		 || (AverageBitrate_Min != the_instance.AverageBitrate_Min)
		 || (AverageBitrate_Max != the_instance.AverageBitrate_Max)
		 || (AverageBitrate_Step != the_instance.AverageBitrate_Step)
		 || (bNoBitRes != the_instance.bNoBitRes)
		 || (bForceChannel != the_instance.bForceChannel)
		 || (nChannelIndex != the_instance.nChannelIndex)
		 || (nMinBitrateIndex != the_instance.nMinBitrateIndex)
		 || (nMaxBitrateIndex != the_instance.nMaxBitrateIndex)
		 || (nPresetIndex != the_instance.nPresetIndex)
		 || (VbrQuality != the_instance.VbrQuality)
//		 || (AverageBitrate != the_instance.AverageBitrate)
//		 || (OutputDir.compare(the_instance.OutputDir) != 0)
		);
}

void AEncodeProperties::SelectSavedParams(const std::string the_string)
{
	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();

	if (CurrentNode != NULL)
	{
		CurrentNode->SetAttribute("default",the_string);
		GetValuesFromKey(the_string, *CurrentNode);
		my_stored_data.SaveFile(my_store_location);
	}
}

inline void AEncodeProperties::SetAttributeBool(TiXmlElement * the_elt,const std::string & the_string, const bool the_value) const
{
	if (the_value == false)
		the_elt->SetAttribute(the_string, "false");
	else
		the_elt->SetAttribute(the_string, "true");
}

void AEncodeProperties::SaveValuesToElement(TiXmlElement * the_element) const
{
	// get all the parameters saved in this Element
	TiXmlElement * tmpElt;

	// Bit Reservoir parameter
	tmpElt = the_element->FirstChildElement("Bit_reservoir");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Bit_reservoir");
		SetAttributeBool(tmpElt, "use", !bNoBitRes);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool(tmpElt, "use", !bNoBitRes);
	}
	// Copyright parameter
	tmpElt = the_element->FirstChildElement("Copyright");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Copyright");
		SetAttributeBool( tmpElt, "use", bCopyright);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bCopyright);
	}

	// Smart Output parameter
	tmpElt = the_element->FirstChildElement("Smart");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Smart");
		SetAttributeBool( tmpElt, "use", bSmartOutput);
		SetAttributeDouble( tmpElt, "ratio", SmartRatioMax);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bSmartOutput);
		SetAttributeDouble( tmpElt, "ratio", SmartRatioMax);
	}

	// Smart Output parameter
	tmpElt = the_element->FirstChildElement("ABR");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("ABR");
		SetAttributeBool( tmpElt, "use", bAbrOutput);
		tmpElt->SetAttribute("min", AverageBitrate_Min);
		tmpElt->SetAttribute("max", AverageBitrate_Max);
		tmpElt->SetAttribute("step", AverageBitrate_Step);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bAbrOutput);
		tmpElt->SetAttribute("min", AverageBitrate_Min);
		tmpElt->SetAttribute("max", AverageBitrate_Max);
		tmpElt->SetAttribute("step", AverageBitrate_Step);
	}

	// CRC parameter
	tmpElt = the_element->FirstChildElement("CRC");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("CRC");
		SetAttributeBool( tmpElt, "use", bCRC);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bCRC);
	}

	// Original parameter
	tmpElt = the_element->FirstChildElement("Original");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Original");
		SetAttributeBool( tmpElt, "use", bOriginal);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bOriginal);
	}

	// Private parameter
	tmpElt = the_element->FirstChildElement("Private");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Private");
		SetAttributeBool( tmpElt, "use", bPrivate);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		SetAttributeBool( tmpElt, "use", bPrivate);
	}

	// Channel Mode parameter
	tmpElt = the_element->FirstChildElement("Channel");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Channel");
		tmpElt->SetAttribute("mode", GetChannelModeString(nChannelIndex));
		SetAttributeBool( tmpElt, "force", bForceChannel);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		tmpElt->SetAttribute("mode", GetChannelModeString(nChannelIndex));
		SetAttributeBool( tmpElt, "force", bForceChannel);
	}
/*
	// Preset parameter
	tmpElt = the_element->FirstChildElement("Preset");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("Preset");
		tmpElt->SetAttribute("type", GetPresetModeString(nPresetIndex));
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		tmpElt->SetAttribute("type", GetPresetModeString(nPresetIndex));
	}

	// Bitrate parameter
	tmpElt = the_element->FirstChildElement("bitrate");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("bitrate");
		tmpElt->SetAttribute("min", the_Bitrates[nMinBitrateIndex]);
		tmpElt->SetAttribute("max", the_Bitrates[nMaxBitrateIndex]);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		tmpElt->SetAttribute("min", the_Bitrates[nMinBitrateIndex]);
		tmpElt->SetAttribute("max", the_Bitrates[nMaxBitrateIndex]);
	}

	// Output Directory parameter
	tmpElt = the_element->FirstChildElement("output");
	if (tmpElt == NULL)
	{
		tmpElt = new TiXmlElement("output");
		tmpElt->SetAttribute("path", OutputDir);
		the_element->InsertEndChild(*tmpElt);
	}
	else
	{
		tmpElt->SetAttribute("path", OutputDir);
	}
*/
}

bool AEncodeProperties::HandleDialogCommand(const HWND parentWnd, const WPARAM wParam, const LPARAM lParam)
{
	UINT command;
	command = GET_WM_COMMAND_ID(wParam, lParam);

	switch (command)
	{
	case IDOK :
	{
		bool bShouldEnd = true;

		// save parameters
		char string[MAX_PATH];
//		::GetWindowText(::GetDlgItem( parentWnd, IDC_COMBO_SETTINGS), string, MAX_PATH);

		wsprintf(string,"Current"); // only the Current config is supported at the moment
		
		my_debug.OutPut("my_hModule = 0x%08X",my_hModule);
#if 0
		AEncodeProperties tmpDlgProps(my_hModule);
		AEncodeProperties tmpSavedProps(my_hModule);
//#ifdef OLD
		tmpDlgProps.UpdateValueFromDlg(parentWnd);
		tmpSavedProps.SelectSavedParams(string);
		tmpSavedProps.ParamsRestore();
		// check if the values from the DLG are the same as the one saved in the config file
		// if yes, just do nothing
		if (tmpDlgProps != tmpSavedProps)
		{
			int save;

			if (strcmp(string,"Current") == 0)
			{
				// otherwise, prompt the user if he wants to overwrite the settings
				TCHAR tmpStr[250];
				::LoadString(AOut::GetInstance(),IDS_STRING_PROMPT_REPLACE_CURRENT,tmpStr,250);

				save = AOut::MyMessageBox( tmpStr, MB_OKCANCEL|MB_ICONQUESTION, parentWnd);
			}
			else
			{
				// otherwise, prompt the user if he wants to overwrite the settings
				TCHAR tmpStr[250];
				::LoadString(AOut::GetInstance(),IDS_STRING_PROMPT_REPLACE_SETING,tmpStr,250);
				TCHAR tmpDsp[500];
				wsprintf(tmpDsp,tmpStr,string);

				save = AOut::MyMessageBox( tmpDsp, MB_YESNOCANCEL|MB_ICONQUESTION, parentWnd);
			}

			if (save == IDCANCEL)
				bShouldEnd = false;
			else if (save == IDNO)
			{
				// save the values in 'current'
				UpdateValueFromDlg(parentWnd);
				SaveValuesToStringKey("Current");
				SelectSavedParams("Current");
			}
			else
			{
				// do so and save in XML
				UpdateValueFromDlg(parentWnd);
				SaveValuesToStringKey(string);
			}
		}
#endif
//#endif // OLD
my_debug.OutPut("before : nChannelIndex %d, bCRC %d, bCopyright %d, bOriginal %d, bPrivate %d",nChannelIndex, bCRC, bCopyright, bOriginal, bPrivate);

my_debug.OutPut("call UpdateValueFromDlg");

		UpdateValueFromDlg(parentWnd);

my_debug.OutPut("call ParamsSave");

		ParamsSave(); // only the Current config is supported now

//my_debug.OutPut("call SelectSavedParams");

//		SelectSavedParams(string);
//		UpdateDlgFromValue(parentWnd);

my_debug.OutPut("finished saving");

		if (bShouldEnd)
		{
			RemoveProp(parentWnd, "AEncodeProperties-Config");
		
			EndDialog(parentWnd, true);
		}
	}
	break;

	case IDCANCEL:
		RemoveProp(parentWnd, "AEncodeProperties-Config");
        EndDialog(parentWnd, false);
		break;

/*	case IDC_FIND_DLL:
	{
		OPENFILENAME file;
		char DllLocation[512];
		wsprintf(DllLocation,"%s",GetDllLocation());

		memset(&file, 0, sizeof(file));
		file.lStructSize = sizeof(file); 
		file.hwndOwner  = parentWnd;
		file.Flags = OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_ENABLEHOOK | OFN_EXPLORER ;
//				file.lpstrFile = AOut::the_AOut->DllLocation;
		file.lpstrFile = DllLocation;
		file.lpstrFilter = "Lame DLL (lame_enc.dll)\0LAME_ENC.DLL\0DLL (*.dll)\0*.DLL\0All (*.*)\0*.*\0";
		file.nFilterIndex = 1;
		file.nMaxFile  = sizeof(DllLocation);
		file.lpfnHook  = DLLFindCallback; // use to validate the DLL chosen

		GetOpenFileName(&file);

		SetDllLocation(DllLocation);
		// use this filename if necessary
	}
	break;
*/
/*	case IDC_BUTTON_OUTPUT:
	{
#ifndef SIMPLE_FOLDER
		BROWSEINFO info;
		memset(&info,0,sizeof(info));

		char FolderName[MAX_PATH];

		info.hwndOwner = parentWnd;
		info.pszDisplayName  = FolderName;
		info.lpfn = BrowseFolderCallbackroc;
		info.lParam = (LPARAM) this;

		// get the localised window title
		TCHAR output[250];
		::LoadString(AOut::GetInstance(),IDS_STRING_DIR_SELECT,output,250);
		info.lpszTitle = output;

#ifdef BIF_EDITBOX
		info.ulFlags |= BIF_EDITBOX;
#else // BIF_EDITBOX
		info.ulFlags |= 0x0010;
#endif // BIF_EDITBOX

#ifdef BIF_VALIDATE
		info.ulFlags |= BIF_VALIDATE;
#else // BIF_VALIDATE
		info.ulFlags |= 0x0020;
#endif // BIF_VALIDATE

#ifdef BIF_NEWDIALOGSTYLE
		info.ulFlags |= BIF_NEWDIALOGSTYLE;
#else // BIF_NEWDIALOGSTYLE
		info.ulFlags |= 0x0040;
#endif // BIF_NEWDIALOGSTYLE

		ITEMIDLIST *item = SHBrowseForFolder(&info);

    	if (item != NULL)
		{
			char tmpOutputDir[MAX_PATH];
			wsprintf(tmpOutputDir,"%s",GetOutputDirectory());

			SHGetPathFromIDList( item,tmpOutputDir );
			SetOutputDirectory( tmpOutputDir );
			::SetWindowText(GetDlgItem( parentWnd, IDC_EDIT_OUTPUTDIR), tmpOutputDir);
//					wsprintf(OutputDir,FolderName);
		}
#else // SIMPLE_FOLDER
		OPENFILENAME file;

		memset(&file, 0, sizeof(file));
		file.lStructSize = sizeof(file); 
		file.hwndOwner  = parentWnd;
		file.Flags = OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_ENABLEHOOK | OFN_EXPLORER ;
//				file.lpstrFile = GetDllLocation();
//				file.lpstrFile = GetOutputDirectory();
		file.lpstrInitialDir = GetOutputDirectory();
		file.lpstrFilter = "A Directory\0.*\0";
//				file.nFilterIndex = 1;
		file.nMaxFile  = MAX_PATH;
//				file.lpfnHook  = DLLFindCallback; // use to validate the DLL chosen
//				file.Flags = OFN_ENABLESIZING | OFN_NOREADONLYRETURN | OFN_HIDEREADONLY;
		file.Flags = OFN_NOREADONLYRETURN | OFN_HIDEREADONLY | OFN_EXPLORER;

		TCHAR output[250];
		::LoadString(AOut::GetInstance(),IDS_STRING_DIR_SELECT,output,250);
		file.lpstrTitle = output;

		GetSaveFileName(&file);
#endif // SIMPLE_FOLDER
	}
	break;
*/
		case IDC_CHECK_ENC_ABR:
			EnableAbrOptions(parentWnd, ::IsDlgButtonChecked( parentWnd, IDC_CHECK_ENC_ABR) == BST_CHECKED);
			break;
/*	case IDC_COMBO_SETTINGS:
//				if (CBN_SELCHANGE == GET_WM_COMMAND_CMD(wParam, lParam))
		if (CBN_SELENDOK == GET_WM_COMMAND_CMD(wParam, lParam))
		{
			char string[MAX_PATH];
			int nIdx = SendMessage(HWND(lParam), CB_GETCURSEL, NULL, NULL);
			SendMessage(HWND(lParam), CB_GETLBTEXT , nIdx, (LPARAM) string);

			// get the info corresponding to the new selected item
			SelectSavedParams(string);
			UpdateDlgFromValue(parentWnd);
		}
		break;
*/
/*	case IDC_BUTTON_CONFIG_SAVE:
	{
		// save the data in the current config
		char string[MAX_PATH];
		::GetWindowText(::GetDlgItem( parentWnd, IDC_COMBO_SETTINGS), string, MAX_PATH);

		UpdateValueFromDlg(parentWnd);
		SaveValuesToStringKey(string);
		SelectSavedParams(string);
		UpdateConfigs(parentWnd);
		UpdateDlgFromValue(parentWnd);
	}
	break;

	case IDC_BUTTON_CONFIG_RENAME:
	{
		char string[MAX_PATH];
		::GetWindowText(::GetDlgItem( parentWnd, IDC_COMBO_SETTINGS), string, MAX_PATH);

		if (RenameCurrentTo(string))
		{
			// Update the names displayed
			UpdateConfigs(parentWnd);
		}

	}
	break;

	case IDC_BUTTON_CONFIG_DELETE:
	{
		char string[MAX_PATH];
		::GetWindowText(::GetDlgItem( parentWnd, IDC_COMBO_SETTINGS), string, MAX_PATH);
		
		if (DeleteConfig(string))
		{
			// Update the names displayed
			UpdateConfigs(parentWnd);
			UpdateDlgFromValue(parentWnd);
		}
	}
	break;
*/
	}
	
    return FALSE;
}

bool AEncodeProperties::RenameCurrentTo(const std::string & new_config_name)
{
	bool bResult = false;

	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();

	if (CurrentNode != NULL)
	{
		if (CurrentNode->Attribute("default") != NULL)
		{
			std::string CurrentConfigName = *CurrentNode->Attribute("default");

			// no rename possible for Current
			if (CurrentConfigName == "")
			{
				bResult = true;
			}
			else if (CurrentConfigName != "Current")
			{
				// find the config that correspond to CurrentConfig
				TiXmlElement* iterateElmt = FindConfig(*CurrentNode, CurrentConfigName);
				if (iterateElmt != NULL)
				{
					iterateElmt->SetAttribute("name",new_config_name);
					bResult = true;
				}
			}

			if (bResult)
			{
				CurrentNode->SetAttribute("default",new_config_name);

				my_stored_data.SaveFile(my_store_location);
			}
		}
	}

	return bResult;
}

bool AEncodeProperties::DeleteConfig(const std::string & config_name)
{
	bool bResult = false;

	if (config_name != "Current")
	{
		// get the values from the saved file if possible
		TiXmlElement* CurrentNode = LoadEncodings();

		if (CurrentNode == NULL)
			return bResult;

		TiXmlElement* iterateElmt = FindConfig(*CurrentNode, config_name);
		if (iterateElmt != NULL)
		{
			CurrentNode->RemoveChild(iterateElmt);
			bResult = true;
		}

		if (bResult)
		{
			my_stored_data.SaveFile(my_store_location);

			// select a new default config : "Current"
			SelectSavedParams("Current");

		}
	}

	return bResult;
}

void AEncodeProperties::UpdateConfigs(const HWND HwndDlg)
{
	// Add User configs
//	SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_SETTINGS), CB_RESETCONTENT , NULL, NULL);

	// display all the names of the saved configs
	// get the values from the saved file if possible
	TiXmlElement* CurrentNode = LoadEncodings();

	if (CurrentNode != NULL)
	{
		std::string CurrentConfig = "";

		if (CurrentNode->Attribute("default") != NULL)
		{
			CurrentConfig = *CurrentNode->Attribute("default");
		}

		TiXmlElement* iterateElmt;

my_debug.OutPut("are we here ?");

		// find the config that correspond to CurrentConfig
		iterateElmt = CurrentNode->FirstChildElement("config");
		int Idx = 0;
		while (iterateElmt != NULL)
		{
			const std::string * tmpname = iterateElmt->Attribute("name");
			/**
				\todo support language names
			*/
			if (tmpname != NULL)
			{
//				SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_SETTINGS), CB_ADDSTRING, NULL, (LPARAM) tmpname->c_str());
				if (tmpname->compare(CurrentConfig) == 0)
				{
//					SendMessage(GetDlgItem( HwndDlg, IDC_COMBO_SETTINGS), CB_SETCURSEL, Idx, NULL);
					SelectSavedParams(*tmpname);
					UpdateDlgFromValue(HwndDlg);
				}
			}
my_debug.OutPut("Idx = %d",Idx);

			Idx++;
			// only Current config supported now
//			iterateElmt = iterateElmt->NextSiblingElement("config");
			iterateElmt = NULL;
my_debug.OutPut("iterateElmt = 0x%08X",iterateElmt);

		}
	}
}
/*
void AEncodeProperties::UpdateAbrSteps(unsigned int min, unsigned int max, unsigned int step) const
{
}
*/
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

	if (value_max<value_min)
	{
		SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_MAX), TBM_SETPOS, TRUE, value_min);
		UpdateDlgFromSlides(hwndDlg);
		return;
	}

	wsprintf(tmp,"%3d",value_min);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_MIN_VALUE), tmp);
	
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETRANGEMIN, TRUE, value_min);

	wsprintf(tmp,"%3d",value_max);
	::SetWindowText(GetDlgItem( hwndDlg, IDC_STATIC_AVERAGE_MAX_VALUE), tmp);
	
	SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_SAMPLE), TBM_SETRANGEMAX, TRUE, value_max);
	
	value_step = SendMessage(GetDlgItem( hwndDlg, IDC_SLIDER_AVERAGE_STEP), TBM_GETPOS, NULL, NULL);
	wsprintf(tmp,"%3d",value_step);
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
	wsprintf(tmp,"%3d",value);
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

