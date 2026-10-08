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

#if !defined(_AENCODEPROPERTIES_H__INCLUDED_)
#define _AENCODEPROPERTIES_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <windows.h>
#include <string>

#include "ADbg/ADbg.h"
//#include "BladeMP3EncDLL.h"
#include "tinyxml/tinyxml.h"
//#include "AParameters/AParameters.h"

typedef const struct {
	UINT id;
	const char *tip;
} ToolTipItem;
/**
  \class AEncodeProperties
  \brief Handles all encoding settings of the ACM codec.
*/
class AEncodeProperties  
{
public:
	/**
		\brief Creates the settings with their default values.

		\param hModule the Windows module of the codec. The settings file
		lame_acm.xml is in the folder of this module.
	*/
	AEncodeProperties(HMODULE hModule);

	/**
		\brief Destroys the settings.
	*/
	virtual ~AEncodeProperties() {}

	/**
		\brief Handles the commands of the configuration dialog box.
	*/
	bool HandleDialogCommand(const HWND parentWnd, const WPARAM wParam, const LPARAM lParam);
	/**
		\brief Returns true if the two instances have different encoding parameters
	*/
	bool operator != (const AEncodeProperties & the_instance) const;

	/**
		\brief Returns true if the encoder sets the copyright bit.
	*/
	inline const bool GetCopyrightMode() const { return bCopyright; }
	/**
		\brief Returns true if the encoder writes CRC checksums.
	*/
	inline const bool GetCRCMode() const { return bCRC; }
	/**
		\brief Returns true if the encoder sets the original bit.
	*/
	inline const bool GetOriginalMode() const { return bOriginal; }
	/**
		\brief Returns true if the encoder sets the private bit.
	*/
	inline const bool GetPrivateMode() const { return bPrivate; }
	/**
		\brief Returns true if Smart Output is on. Then the codec does not offer
		output formats whose compression ratio is above GetSmartRatio().
	*/
	inline const bool GetSmartOutputMode() const { return bSmartOutput; }
	/**
		\brief Returns true if the codec offers ABR output formats.
	*/
	inline const bool GetAbrOutputMode() const { return bAbrOutput; }

	/**
		\brief Returns true if the settings switch off the bit reservoir.
	*/
	inline const bool GetNoBiResMode() const { return bNoBitRes; }

	/**
		\brief Returns true if the settings force the selected channel mode.

		Only Mono can be forced: then the codec encodes stereo input as mono.
		See OutputChannels().
	*/
	inline const bool GetForceChannelMode() const { return bForceChannel; }

	/**
		\brief Sets whether the encoder sets the copyright bit.
	*/
	inline void SetCopyrightMode(const bool bMode) { bCopyright = bMode; }
	/**
		\brief Sets whether the encoder writes CRC checksums.
	*/
	inline void SetCRCMode(const bool bMode) { bCRC = bMode; }
	/**
		\brief Sets whether the encoder sets the original bit.
	*/
	inline void SetOriginalMode(const bool bMode) { bOriginal = bMode; }
	/**
		\brief Sets whether the encoder sets the private bit.
	*/
	inline void SetPrivateMode(const bool bMode) { bPrivate = bMode; }

	/**
		\brief Switches Smart Output on or off, see GetSmartOutputMode().
	*/
	inline void SetSmartOutputMode(const bool bMode) { bSmartOutput = bMode; }
	/**
		\brief Sets whether the codec offers ABR output formats.
	*/
	inline void SetAbrOutputMode(const bool bMode) { bAbrOutput = bMode; }


	/**
		\brief Sets the setting that GetNoBiResMode() returns.
	*/
	inline void SetNoBiResMode(const bool bMode) { bNoBitRes = bMode; }
	
	/**
		\brief Sets the setting that GetForceChannelMode() returns.
	*/
	inline void SetForceChannelMode(const bool bMode) { bForceChannel = bMode; }
	
	/**
		\brief Returns the bitrate for CBR, or the minimum bitrate for VBR.
	*/
	const unsigned int GetBitrateValue() const;

	/**
		\brief Gets the current bitrate (the minimum bitrate for VBR) for the given
		       MPEG version.

		\param bitrate receives the bitrate
		\param MPEG_Version the MPEG version (MPEG-1 or MPEG-2)

		\return the result of GetBitrateValueMPEG1() or GetBitrateValueMPEG2()
	*/
	const int GetBitrateValue(DWORD & bitrate, const DWORD MPEG_Version) const;
	/**
		\brief Gets the current bitrate (the minimum bitrate for VBR) for MPEG-1.

		\param bitrate receives the bitrate

		\return 0 if the bitrate is in the MPEG-1 table. 1 if it is not; then
		        \a bitrate is the next higher MPEG-1 bitrate.
	*/
	const int GetBitrateValueMPEG1(DWORD & bitrate) const;
	/**
		\brief Gets the current bitrate (the minimum bitrate for VBR) for MPEG-2.

		\param bitrate receives the bitrate

		\return 0 if the bitrate is in the MPEG-2 table. -1 if it is not; then
		        \a bitrate is the next lower MPEG-2 bitrate.
	*/
	const int GetBitrateValueMPEG2(DWORD & bitrate) const;

	/**
		\brief Writes the current bitrate (the minimum bitrate for VBR) as text.

		\param string the buffer for the text
		\param string_size the size of the buffer

		\return the number of characters written. -1 if the bitrate is not found.
	*/
	inline const int GetBitrateString(char * string, int string_size) const {return GetBitrateString(string,string_size,nMinBitrateIndex); }

	/**
		\brief Writes the bitrate with the given index as text.

		\param string the buffer for the text
		\param string_size the size of the buffer
		\param a_bitrateID the index in the bitrate table

		\return the number of characters written. -1 if the bitrate is not found.
	*/
	const int GetBitrateString(char * string, int string_size, int a_bitrateID) const;

	/**
		\brief Returns the number of bitrates in the bitrate table.
	*/
	inline const int GetBitrateLentgh() const { return sizeof(the_Bitrates) / sizeof(unsigned int); }
	/**
		\brief Returns the highest compression ratio for Smart Output (default
		       15, for 1:15).
	*/
	inline double GetSmartRatio() const { return SmartRatioMax;}
	/**
		\brief Returns the lowest ABR bitrate that the codec offers.
	*/
	inline unsigned int GetAbrBitrateMin() const { return AverageBitrate_Min;}
	/**
		\brief Returns the highest ABR bitrate that the codec offers.
	*/
	inline unsigned int GetAbrBitrateMax() const { return AverageBitrate_Max;}
	/**
		\brief Returns the step between the ABR bitrates that the codec offers.
	*/
	inline unsigned int GetAbrBitrateStep() const { return AverageBitrate_Step;}

#if 0
//	const char * GetDllLocation() const { return DllLocation.c_str(); }
//	void SetDllLocation( const char * the_string ) { DllLocation = the_string; }

//	const char * GetOutputDirectory() const { return OutputDir.c_str(); }
//	void SetOutputDirectory( const char * the_string ) { OutputDir = the_string; }
#endif

	/**
		\brief Returns the channel mode to use.
	*/
	const unsigned int GetChannelModeValue() const;
	/**
		\brief Returns the number of channels that the encoded stream has.

		\param input_channels the number of channels of the input, 1 or 2
		\return 1 if the settings force Mono and the input is stereo.
		        Otherwise \a input_channels.
	*/
	const unsigned int OutputChannels(const unsigned int input_channels) const;
	/**
		\brief Returns the name of the current channel mode.
	*/
	inline const char * GetChannelModeString() const {return GetChannelModeString(nChannelIndex); }
	/**
		\brief Returns the name of the channel mode with the given index.

		\param a_channelID the channel mode index (see GetChannelLentgh())
	*/
	const char * GetChannelModeString(const int a_channelID) const;
	/**
		\brief Returns the number of channel modes.
	*/
	inline const int GetChannelLentgh() const { return sizeof(the_ChannelModes) / sizeof(the_ChannelModes[0]); }

//	const LAME_QUALTIY_PRESET GetPresetModeValue() const;
	/**
		\brief Returns the name of the preset with the given index.

		\param a_presetID the preset index
	*/
	const char * GetPresetModeString(const int a_presetID) const;
//	inline const int GetPresetLentgh() const { return sizeof(the_Presets) / sizeof(LAME_QUALTIY_PRESET); }

	/**
		\brief Shows the configuration dialog box (for the DRV_CONFIGURE message).
	*/
	bool Config(const HINSTANCE hInstance, const HWND HwndParent);

	/**
		\brief Fills the configuration dialog box with its texts and choices.
	*/
	bool InitConfigDlg(HWND hDialog);

	/**
		\brief Reads the settings from the configuration dialog box.
	*/
	bool UpdateValueFromDlg(HWND hDialog);
	/**
		\brief Shows the settings in the configuration dialog box.
	*/
	bool UpdateDlgFromValue(HWND hDialog);

	/**
		\brief Saves the current settings in the current configuration.
	*/
	void ParamsSave(void);

	/**
		\brief Loads the settings of the current configuration.
	*/
	void ParamsRestore(void);

	/**
		\brief Makes the configuration with the given name the default.
	*/
	void SelectSavedParams(const std::string config_name);
	/**
		\brief Saves the current settings under the given configuration name.
	*/
	void SaveValuesToStringKey(const std::string & config_name);
	/**
		\brief Renames the current configuration.
	*/
	bool RenameCurrentTo(const std::string & new_config_name);
	/**
		\brief Deletes the configuration with the given name from the saved
		       configurations.
	*/
	bool DeleteConfig(const std::string & config_name);

	ADbg              my_debug;

	/**
		\brief Updates the values shown next to the sliders, when a slider moves.
	*/
	void UpdateDlgFromSlides(HWND parent_window) const;

	static ToolTipItem Tooltips[15];
private:

	bool bCopyright;
	bool bCRC;
	bool bOriginal;
	bool bPrivate;
	bool bNoBitRes;
	bool bForceChannel;
	bool bSmartOutput;
	bool bAbrOutput;

	int VbrQuality;
	unsigned int AverageBitrate_Min;
	unsigned int AverageBitrate_Max;
	unsigned int AverageBitrate_Step;

	double SmartRatioMax;

	/** \brief Indexes into the_ChannelModes, in the order of its entries. */
	enum { CHANNEL_INDEX_STEREO, CHANNEL_INDEX_JOINT_STEREO, CHANNEL_INDEX_DUAL_CHANNEL,
	       CHANNEL_INDEX_MONO };
	static const unsigned int the_ChannelModes[4];
	int nChannelIndex;

	static unsigned int the_Bitrates[18];
	static unsigned int the_MPEG1_Bitrates[14];
	static unsigned int the_MPEG2_Bitrates[14];
	static void FillBitrateTables();
	int nMinBitrateIndex; // CBR and VBR
	int nMaxBitrateIndex; // only used in VBR mode

//	static const LAME_QUALTIY_PRESET the_Presets[17];
	int nPresetIndex;

//	char DllLocation[512];
//	std::string DllLocation;
//	char OutputDir[MAX_PATH];
//	std::string OutputDir;

//	AParameters my_base_parameters;
	TiXmlDocument my_stored_data;
	std::string my_store_location;
	std::string my_current_config;

//	HINSTANCE hDllInstance;

	void SaveValuesToElement(TiXmlElement * the_element) const;
	inline void SetAttributeBool(TiXmlElement * the_elt,const std::string & the_string, const bool the_value) const;
	void UpdateConfigs(const HWND HwndDlg);
	void EnableAbrOptions(HWND hDialog, bool enable);

	HMODULE my_hModule;

	/**
		\brief Reads the settings of a saved configuration from the XML data.

		\param config_name the name of the configuration
		\param parentNode the XML node that contains the configurations
	*/
	void GetValuesFromKey(const std::string & config_name, const TiXmlNode & parentNode);
};

#endif // !defined(_AENCODEPROPERTIES_H__INCLUDED_)
