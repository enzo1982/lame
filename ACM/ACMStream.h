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


void ConfigureDebugFromRegistry(ADbg & dbg);

class ACMStream
{
public:
	ACMStream( );
	virtual ~ACMStream( );

	static ACMStream * Create();
	static bool Erase(const ACMStream * a_ACMStream);

	bool init(const int nSamplesPerSec, const int nOutputSamplesPerSec, const int nChannels, const int nOutputChannels, const int nAvgBytesPerSec, const vbr_mode mode);
	bool open(const AEncodeProperties & the_Properties);

	DWORD GetOutputSizeForInput(const DWORD the_SrcLength) const;
	bool  ConvertBuffer(LPACMDRVSTREAMHEADER a_StreamHeader);

	static unsigned int GetOutputSampleRate(int samples_per_sec, int bitrate, int channels);

protected:
	void read_settings(const AEncodeProperties & the_Properties);
	bool restart();
	bool start();

	lame_global_flags * gfp;
	// The last conversion ended the MP3 stream; the next one starts a new one
	bool my_Ended;
	// A conversion of this MP3 stream asked for whole blocks, so the first
	// one that does not is the last one
	bool my_Aligned;
	// The codec's module, whose folder holds the settings file
	HMODULE my_Module;

	ADbg my_debug;
	int my_SamplesPerSec;     // of the input
	int my_OutSamplesPerSec;  // of the encoded stream
	int my_Channels;     // of the input
	int my_OutChannels;  // of the encoded stream
	int my_AvgBytesPerSec;
	vbr_mode my_VBRMode;

	// The settings that open() and restart() read, for start()
	MPEG_mode my_Mode;
	bool my_Copyright;
	bool my_Original;
	bool my_CRC;
	bool my_Private;
	bool my_NoBitRes;
};

#endif // !defined(_ACMSTREAM_H__INCLUDED_)

