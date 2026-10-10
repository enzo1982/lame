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
#include "SettingText.h"
#include "tinyxml/tinyxml.h"

typedef const struct {
	UINT id;
	const char *tip;
} ToolTipItem;
/**
  \class AEncodeProperties
  \brief Handles all encoding settings of the ACM codec.
*/
/**
	\brief What decides the formats that the codec lists: the families it
	offers, their ranges, and the Smart filter.
*/
struct FormatListSettings
{
	bool cbr;                  ///< CBR formats are offered
	bool abr;                  ///< ABR formats are offered
	bool vbr;                  ///< VBR formats are offered
	bool smart;                ///< formats above smart_ratio are left out
	double smart_ratio;        ///< the highest compression ratio with smart
	unsigned int cbr_min;      ///< the lowest CBR bitrate, in kbit/s
	unsigned int cbr_max;      ///< the highest CBR bitrate, in kbit/s
	unsigned int abr_min;      ///< the lowest ABR bitrate, in kbit/s
	unsigned int abr_max;      ///< the highest ABR bitrate, in kbit/s
	unsigned int abr_step;     ///< the step between ABR bitrates, in kbit/s
	unsigned int vbr_best;     ///< the best VBR quality level offered
	unsigned int vbr_worst;    ///< the worst VBR quality level offered
	unsigned int vbr_min;      ///< the VBR minimum bitrate, or VBR_BITRATE_NO_LIMIT
	unsigned int vbr_max;      ///< the VBR maximum bitrate, or VBR_BITRATE_NO_LIMIT
};

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
		\brief Returns true if the codec offers VBR output formats.
	*/
	inline bool GetVbrOutputMode() const { return bVbrOutput; }
	/**
		\brief Returns true if the codec offers CBR output formats.
	*/
	inline bool GetCbrOutputMode() const { return bCbrOutput; }
	/**
		\brief Returns the lowest CBR bitrate that the codec offers, in kbit/s.
	*/
	inline unsigned int GetCbrBitrateMin() const { return CbrBitrate_Min; }
	/**
		\brief Returns the other end of the CBR range, at or above
		       GetCbrBitrateMin().
	*/
	inline unsigned int GetCbrBitrateMax() const { return CbrBitrate_Max; }

	/**
		\brief Returns true if the settings switch off the bit reservoir.
	*/
	inline bool GetNoBiResMode() const { return bNoBitRes; }
	/**
		\brief Returns the encoding quality, 0 (the best, slowest) to 9.
	*/
	inline unsigned int GetQuality() const { return nQuality; }
	/**
		\brief Returns true if LAME keeps all frequencies: no lowpass and no
		       highpass filter.
	*/
	inline bool GetKeepAllFrequencies() const { return bKeepAllFrequencies; }
	/**
		\brief Returns true if the bit reservoir stays within the limit of the
		       ISO standard.
	*/
	inline bool GetStrictISO() const { return bStrictISO; }
	/**
		\brief Returns true if a joint stereo stream codes every frame as mid
		       and side.
	*/
	inline bool GetForceMS() const { return bForceMS; }

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
		\brief Sets whether the codec offers VBR output formats.
	*/
	inline void SetVbrOutputMode(const bool bMode) { bVbrOutput = bMode; }


	/**
		\brief Sets the setting that GetNoBiResMode() returns.
	*/
	inline void SetNoBiResMode(const bool bMode) { bNoBitRes = bMode; }
	
	/**
		\brief Sets the setting that GetForceChannelMode() returns.
	*/
	inline void SetForceChannelMode(const bool bMode) { bForceChannel = bMode; }
	
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
	/** \brief The worst VBR quality level of LAME; 0 is the best. */
	static const unsigned int VBR_QUALITY_WORST = VBR_QUALITY_LEVELS - 1;
	/**
		\brief Returns the best VBR quality level that the codec offers, 0 to
		       9 (0 is the best).
	*/
	inline unsigned int GetVbrQualityBest() const { return VbrQuality_Best;}
	/**
		\brief Returns the other end of the VBR range: a level from
		       GetVbrQualityBest() to VBR_QUALITY_WORST.
	*/
	inline unsigned int GetVbrQualityWorst() const { return VbrQuality_Worst;}
	/** \brief The VBR bitrate limit that leaves the limit to LAME. */
	static const unsigned int VBR_BITRATE_NO_LIMIT = 0;
	/**
		\brief Returns the lowest bitrate of a VBR stream, in kbit/s, or
		       VBR_BITRATE_NO_LIMIT.
	*/
	inline unsigned int GetVbrBitrateMin() const { return VbrBitrate_Min;}
	/**
		\brief Returns the other limit, the highest bitrate, as
		       GetVbrBitrateMin() returns the lowest.
	*/
	inline unsigned int GetVbrBitrateMax() const { return VbrBitrate_Max;}
	/**
		\brief Returns true if every frame of a VBR stream uses at least
		       GetVbrBitrateMin(), silent frames included.
	*/
	inline bool GetVbrEnforceMin() const { return bVbrEnforceMin;}
	FormatListSettings GetFormatListSettings() const;
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
		\brief Returns the name of the channel mode with the given index, as
		       the configuration dialog shows it and the settings file
		       stores it.

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

	/** \brief Room for the text of UpdateAbrList(), terminator included. */
	static const size_t ABR_TEXT_CHARS = 64;
	/** \brief The tabs of the settings dialog, in their order. */
	enum { TAB_FORMATS, TAB_ENCODING, SETTINGS_TABS };
	static void ShowSettingsTab(HWND hDialog, int tab);
	static void KeepRangeInOrder(HWND hDialog, int lower, int upper, int moved);
	static void KeepVbrLimitsInOrder(HWND hDialog, int moved);
	static void KeepOneFamily(HWND hDialog, int clicked);
	static void EnableFamilyControls(HWND hDialog);
	static void UpdateAbrList(HWND hDialog);
	static bool ShowsJointStereo(HWND hSettings);
	FormatListSettings FormatListFromDlg(HWND hDialog) const;
	void UpdateFormatCount(HWND hDialog) const;

	/** \brief The controls with a tooltip. */
	static const int TOOLTIP_COUNT = 26;
	static ToolTipItem Tooltips[TOOLTIP_COUNT];
private:

	bool bCopyright;
	bool bCRC;
	bool bOriginal;
	bool bPrivate;
	bool bNoBitRes;
	unsigned int nQuality; ///< the encoding quality of lame_set_quality()
	bool bKeepAllFrequencies;
	bool bStrictISO;
	bool bForceMS;
	bool bForceChannel;
	bool bSmartOutput;
	bool bAbrOutput;
	bool bVbrOutput;
	bool bCbrOutput;
	unsigned int CbrBitrate_Min;
	unsigned int CbrBitrate_Max;

	unsigned int VbrQuality_Best;
	unsigned int VbrQuality_Worst;
	unsigned int VbrBitrate_Min;
	unsigned int VbrBitrate_Max;
	bool bVbrEnforceMin;

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
	static bool IsListBitrate(unsigned int kbps);
	static bool IsVbrBitrateLimit(unsigned int kbps);
	static void FillBitrateList(HWND combo, bool with_no_limit);

	TiXmlDocument my_stored_data;
	std::string my_store_location;
	std::string my_current_config;

	void SaveValuesToElement(TiXmlElement * the_element) const;
	static inline void SetAttributeBool(TiXmlElement * the_elt,const std::string & the_string, const bool the_value);
	void UpdateConfigs(const HWND HwndDlg);

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
