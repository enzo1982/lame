/**
 * @file
 * @brief The implementation of SettingText.h.
 */

#include <stddef.h>
#include <stdio.h>

#include "SettingText.h"

/**
 * Returns the text of an encoding quality level: the level, and for the
 * levels that lame.h describes, what it gives. 0 is the best and slowest, 9
 * the fastest, and 3 is LAME's default.
 *
 * @param level  the level, 0 to ENCODING_QUALITY_LEVELS - 1.
 * @return the text, or NULL for a level out of range.
 */
const char * EncodingQualityText(unsigned int level)
{
	static const char *const texts[ENCODING_QUALITY_LEVELS] = {
		"0 (best, slowest)", "1", "2 (near best)", "3 (default)", "4",
		"5 (good, fast)", "6", "7 (very fast)", "8", "9 (fastest)"
	};

	return (level < ENCODING_QUALITY_LEVELS) ? texts[level] : NULL;
}

/// The typical bitrate of each VBR quality level, in kbit/s, that a VBR
/// format reports as its average. Measured on music: the median over the
/// tracks, rounded to the nearest CBR bitrate of the MPEG version.
static const struct {
	unsigned int frequency;
	unsigned int channels;
	unsigned int kbps[VBR_QUALITY_LEVELS];
} vbr_typical_kbps[] = {
	{ 48000, 1, { 128, 112,  96,  96,  80,  80,  64,  56,  56,  40 } },
	{ 48000, 2, { 256, 224, 192, 192, 160, 128, 128, 112,  96,  80 } },
	{ 44100, 1, { 128, 112,  96,  96,  80,  80,  64,  56,  56,  40 } },
	{ 44100, 2, { 256, 224, 192, 160, 160, 128, 112, 112,  96,  80 } },
	{ 32000, 1, { 112,  96,  80,  80,  64,  64,  56,  48,  48,  32 } },
	{ 32000, 2, { 224, 192, 160, 160, 128, 112,  96,  96,  80,  64 } },
	{ 24000, 1, {  80,  80,  64,  64,  56,  48,  40,  40,  40,  32 } },
	{ 24000, 2, { 160, 144, 128, 112,  96,  96,  80,  80,  64,  56 } },
	{ 22050, 1, {  80,  64,  64,  56,  48,  48,  40,  40,  40,  32 } },
	{ 22050, 2, { 144, 128, 112, 112,  96,  80,  80,  64,  64,  56 } },
	{ 16000, 1, {  56,  48,  48,  40,  40,  32,  32,  32,  32,  24 } },
	{ 16000, 2, { 112,  96,  80,  80,  80,  64,  56,  56,  48,  48 } },
	{ 12000, 1, {  40,  40,  32,  32,  32,  24,  24,  24,  24,  24 } },
	{ 12000, 2, {  64,  64,  64,  56,  56,  48,  40,  40,  40,  40 } },
	{ 11025, 1, {  40,  40,  32,  32,  24,  24,  24,  24,  24,  16 } },
	{ 11025, 2, {  64,  64,  56,  56,  48,  48,  40,  40,  40,  40 } },
	{  8000, 1, {  32,  32,  24,  24,  24,  16,  16,  16,  16,  16 } },
	{  8000, 2, {  56,  56,  48,  48,  40,  32,  32,  32,  32,  24 } },
};

/**
 * Looks up the typical bitrate of a VBR quality level in vbr_typical_kbps.
 *
 * @param the_Frequency  the sample rate in Hz.
 * @param the_Channels   the number of channels, 1 or 2.
 * @param the_Quality    the VBR quality level, 0 to VBR_QUALITY_LEVELS - 1.
 * @return the bitrate in kbit/s, or 0 for a sample rate the table does not
 *         have.
 */
unsigned int VbrTypicalBitrate(unsigned int the_Frequency, unsigned int the_Channels, unsigned int the_Quality)
{
	for (size_t i = 0; i < sizeof vbr_typical_kbps / sizeof vbr_typical_kbps[0]; i++)
	{
		if (vbr_typical_kbps[i].frequency == the_Frequency && vbr_typical_kbps[i].channels == the_Channels
		    && the_Quality < VBR_QUALITY_LEVELS)
			return vbr_typical_kbps[i].kbps[the_Quality];
	}
	return 0;
}

/**
 * Writes the text of a VBR quality level: the level and its typical bitrate
 * for music at 44.1 kHz stereo, for example "4 (about 160 kbps)".
 *
 * @param level  the level, 0 to VBR_QUALITY_LEVELS - 1.
 * @param text   receives the text.
 * @param size   the size of @p text in bytes.
 * @return the result of snprintf(), or -1 for a level out of range.
 */
int VbrQualityText(unsigned int level, char *text, size_t size)
{
	if (level >= VBR_QUALITY_LEVELS)
		return -1;
	return snprintf(text, size, "%u (about %u kbps)", level,
	                VbrTypicalBitrate(VBR_TEXT_SAMPLE_RATE, VBR_TEXT_CHANNELS, level));
}

/**
 * LAME's licence notice, as COPYING and the sources of the library state it:
 * the GNU Library General Public License, version 2 or later. The about boxes
 * show it.
 */
const char LICENSE_NOTICE[] =
	"This library is free software; you can redistribute it and/or modify it "
	"under the terms of the GNU Library General Public License as published by "
	"the Free Software Foundation; either version 2 of the License, or (at "
	"your option) any later version.\r\n\r\n"
	"This library is distributed in the hope that it will be useful, but "
	"WITHOUT ANY WARRANTY; without even the implied warranty of "
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Library "
	"General Public License for more details.\r\n\r\n"
	"You should have received a copy of the GNU Library General Public "
	"License along with this library; if not, write to the Free Software "
	"Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.";

/** The authors of the ACM codec, of the DirectShow filter, and LAME's. */
const char ABOUT_CREDITS[] =
	"Steve Lhomme (ACM codec), Elecard Ltd. with Marie Orlova, Peter Gubanov and "
	"Vitaly Ivanov (DirectShow filter), and the LAME developers";

/** The author of the icon. */
const char ABOUT_ICON_CREDIT[] = "Icon: Lucas Granito";
