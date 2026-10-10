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

#if !defined(_ACMSTREAM_H__INCLUDED_)
#define _ACMSTREAM_H__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>
#include <msacmdrv.h>

#include "ADbg/ADbg.h"

#include "AEncodeProperties.h"

#include <lame.h>

#include <vector>


void ConfigureDebugFromRegistry(ADbg & dbg);

class ACMStream
{
public:
	ACMStream( );
	virtual ~ACMStream( );

	static ACMStream * Create();
	static bool Erase(const ACMStream * a_ACMStream);

	bool init(const int nSamplesPerSec, const int nOutputSamplesPerSec, const int nChannels, const int nOutputChannels, const int nAvgBytesPerSec, const vbr_mode mode, const unsigned int vbrQuality = 0);
	bool open(const AEncodeProperties & the_Properties);

	DWORD GetOutputSizeForInput(const DWORD the_SrcLength) const;
	DWORD GetInputSizeForOutput(const DWORD the_DstLength) const;
	bool  ConvertBuffer(LPACMDRVSTREAMHEADER a_StreamHeader);

	static unsigned int GetOutputSampleRate(int samples_per_sec, int bitrate, int channels);
	static bool VbrBitrateBounds(int the_SampleRate, int the_Channels, unsigned int the_Min, unsigned int the_Max,
	                             unsigned int & the_Lowest, unsigned int & the_Highest);

protected:
	void read_settings(const AEncodeProperties & the_Properties);
	bool restart();
	bool start();
	void read_bounds();
	DWORD MostBytesFor(const DWORD the_Frames) const;
	DWORD ReturnKept(LPACMDRVSTREAMHEADER a_StreamHeader);
	DWORD ReturnEncoded(LPACMDRVSTREAMHEADER a_StreamHeader, DWORD a_Returned, DWORD a_Encoded);

	lame_global_flags * gfp;
	// The last conversion ended the MP3 stream; the next one starts a new one
	bool my_Ended;
	// A conversion of this MP3 stream asked for whole blocks, so the first
	// one that does not is the last one
	bool my_Aligned;
	// The codec's module, whose folder holds the settings file
	HMODULE my_Module;
	// The input sample frames of one MP3 frame
	double my_FrameInput;
	// The largest MP3 frame of the stream, in bytes, at its highest bitrate
	DWORD my_LargestFrame;
	// HELD_FRAMES_MPEG1 or HELD_FRAMES_MPEG2, for the MPEG version of the stream
	DWORD my_HeldFrames;
	// MP3 data that did not fit into a destination buffer; the next
	// conversion returns it first
	std::vector<unsigned char> my_Kept;
	// Where LAME encodes a conversion
	std::vector<unsigned char> my_Encoded;

	ADbg my_debug;
	int my_SamplesPerSec;     // of the input
	int my_OutSamplesPerSec;  // of the encoded stream
	int my_Channels;     // of the input
	int my_OutChannels;  // of the encoded stream
	int my_AvgBytesPerSec;
	vbr_mode my_VBRMode;
	unsigned int my_VBRQuality;  // the VBR quality level of a vbr_mtrh stream

	// The settings that open() and restart() read, for start()
	MPEG_mode my_Mode;
	bool my_Copyright;
	bool my_Original;
	bool my_CRC;
	bool my_Private;
	bool my_NoBitRes;
	unsigned int my_Quality;  // the encoding quality of lame_set_quality()
	unsigned int my_VbrBitrateMin;  // kbit/s, 0 for none
	unsigned int my_VbrBitrateMax;  // kbit/s, 0 for none
	bool my_VbrEnforceMin;
	bool my_KeepAllFrequencies;
	bool my_StrictISO;
	bool my_ForceMS;  // of a joint stereo stream
};

#endif // !defined(_ACMSTREAM_H__INCLUDED_)

