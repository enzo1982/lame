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

#if !defined(_AENCODEPROPERTIES_H__INCLUDED_)
#define _AENCODEPROPERTIES_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <windows.h>
#include <string>
#include <vector>

#include "ADbg/ADbg.h"
#include "tinyxml/tinyxml.h"

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
		\brief Returns true if the encoder sets the copyright bit.
	*/
	inline bool GetCopyrightMode() const { return bCopyright; }
	/**
		\brief Returns true if the encoder writes CRC checksums.
	*/
	inline bool GetCRCMode() const { return bCRC; }
	/**
		\brief Returns true if the encoder sets the original bit.
	*/
	inline bool GetOriginalMode() const { return bOriginal; }
	/**
		\brief Returns true if the encoder sets the private bit.
	*/
	inline bool GetPrivateMode() const { return bPrivate; }
	/**
		\brief Returns true if Smart Output is on. Then the codec does not offer
		output formats whose compression ratio is above GetSmartRatio().
	*/
	inline bool GetSmartOutputMode() const { return bSmartOutput; }
	/**
		\brief Returns true if the codec offers ABR output formats.
	*/
	inline bool GetAbrOutputMode() const { return bAbrOutput; }

	/**
		\brief Returns true if the settings switch off the bit reservoir.
	*/
	inline bool GetNoBiResMode() const { return bNoBitRes; }

	/**
		\brief Returns the Windows module of the codec, whose folder holds the
		settings file.
	*/
	inline HMODULE GetModule() const { return my_hModule; }

	/**
		\brief Returns true if the settings force the selected channel mode.

		Only Mono can be forced: then the codec encodes stereo input as mono.
		See OutputChannels().
	*/
	inline bool GetForceChannelMode() const { return bForceChannel; }

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
		\brief Writes the current bitrate (the minimum bitrate for VBR) as text.

		\param string the buffer for the text
		\param string_size the size of the buffer

		\return the number of characters written. -1 if the bitrate is not found.
	*/
	inline int GetBitrateString(char * string, int string_size) const {return GetBitrateString(string,string_size,nMinBitrateIndex); }

	/**
		\brief Writes the bitrate with the given index as text.

		\param string the buffer for the text
		\param string_size the size of the buffer
		\param a_bitrateID the index in the bitrate table

		\return the number of characters written. -1 if the bitrate is not found.
	*/
	int GetBitrateString(char * string, int string_size, int a_bitrateID) const;

	/**
		\brief Returns the number of bitrates in the bitrate table.
	*/
	inline int GetBitrateLentgh() const { return sizeof(the_Bitrates) / sizeof(unsigned int); }
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
	static std::vector<unsigned int> AbrLadder(unsigned int min, unsigned int max, unsigned int step);

	/**
		\brief Returns the channel mode to use.
	*/
	unsigned int GetChannelModeValue() const;
	/**
		\brief Returns the number of channels that the encoded stream has.

		\param input_channels the number of channels of the input, 1 or 2
		\return 1 if the settings force Mono and the input is stereo.
		        Otherwise \a input_channels.
	*/
	unsigned int OutputChannels(const unsigned int input_channels) const;
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
	inline int GetChannelLentgh() const { return sizeof(the_ChannelModes) / sizeof(the_ChannelModes[0]); }

	/**
		\brief Shows the configuration dialog box (for the DRV_CONFIGURE message).

		\return true after OK. false after Cancel, or if the dialog box
		        cannot be shown.
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

		\param config_name the name. It is a copy: the function reloads the
		       settings file, which frees the strings of the document that the
		       caller read the name from.
	*/
	void SelectSavedParams(const std::string config_name);
	/**
		\brief Saves the current settings under the given configuration name.
	*/
	void SaveValuesToStringKey(const std::string & config_name);

	ADbg              my_debug;

	/**
		\brief Updates the values shown next to the sliders, when a slider moves.
	*/
	void UpdateDlgFromSlides(HWND parent_window) const;

	static ToolTipItem Tooltips[14];
private:

	bool bCopyright;
	bool bCRC;
	bool bOriginal;
	bool bPrivate;
	bool bNoBitRes;
	bool bForceChannel;
	bool bSmartOutput;
	bool bAbrOutput;

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

	TiXmlDocument my_stored_data;
	std::string my_store_location;
	std::string my_current_config;

	void SaveValuesToElement(TiXmlElement * the_element) const;
	static inline void SetAttributeBool(TiXmlElement * the_elt,const std::string & the_string, const bool the_value);
	void UpdateConfigs(const HWND HwndDlg);
	static void EnableAbrOptions(HWND hDialog, bool enable);

	HMODULE my_hModule;

	/**
		\brief Reads the settings of a saved configuration from the XML data.

		\param config_name the name of the configuration
		\param parentNode the XML node that contains the configurations
	*/
	void GetValuesFromKey(const std::string & config_name, const TiXmlNode & parentNode);
	TiXmlElement * LoadEncodings();
	static TiXmlElement * FindConfig(const TiXmlNode & parent, const std::string & name);
	static TiXmlElement * ChildElement(TiXmlElement & parent, const char * name);
};

#endif // !defined(_AENCODEPROPERTIES_H__INCLUDED_)
