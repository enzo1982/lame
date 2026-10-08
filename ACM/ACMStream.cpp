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
#endif // STRICT

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "adebug.h"

#include "ACMStream.h"

#include <lame.h>

// static methods

ACMStream * ACMStream::Create()
{
	ACMStream * Result;

	Result = new ACMStream;

	return Result;
}

const bool ACMStream::Erase(const ACMStream * a_ACMStream)
{
	delete a_ACMStream;
	return true;
}

// class methods

/**
	\brief Sends the debug output to the file that the registry names, if it
	names one.

	The file name is the string value DebugFile under
	HKEY_LOCAL_MACHINE\\SOFTWARE\\MUKOLI. A value that is not a string, or
	that does not fit into 512 bytes, leaves the output where it is.

	\param dbg  the debug output to configure.
*/
void ConfigureDebugFromRegistry(ADbg & dbg)
{
	unsigned char DebugFileName[512];
	HKEY OssKey;

	if (RegOpenKeyEx( HKEY_LOCAL_MACHINE, "SOFTWARE\\MUKOLI", 0, KEY_READ , &OssKey ) == ERROR_SUCCESS) {
		DWORD DataType;
		DWORD DebugFileNameSize = sizeof(DebugFileName) - 1;
		if (RegQueryValueEx( OssKey, "DebugFile", NULL, &DataType, DebugFileName, &DebugFileNameSize ) == ERROR_SUCCESS
		    && DataType == REG_SZ) {
			// A string value need not carry its terminator.
			DebugFileName[DebugFileNameSize] = '\0';
			dbg.setUseFile(true);
			dbg.setDebugFile((char *)DebugFileName);
			dbg.OutPut("Debug file is %s",(char *)DebugFileName);
		}
		RegCloseKey(OssKey);
	}
}

ACMStream::ACMStream() :
 m_WorkingBufferUseSize(0),
 gfp(NULL)
{
	 /// \todo get the debug level from the registry
my_debug = new ADbg(DEBUG_LEVEL_CREATION);
	if (my_debug != NULL) {
		my_debug->setPrefix("LAMEstream"); /// \todo get it from the registry
		my_debug->setIncludeTime(true);  /// \todo get it from the registry
		ConfigureDebugFromRegistry(*my_debug);
		my_debug->OutPut(DEBUG_LEVEL_FUNC_START, "ACMStream Creation (0X%08X)",this);
	}
	else {
		ADbg debug;
		debug.OutPut("ACMStream::ACMACMStream : Impossible to create my_debug");
	}

}

ACMStream::~ACMStream()
{
        // release memory - encoding is finished
	if (gfp) lame_close( gfp );

	if (my_debug != NULL)
	{
		my_debug->OutPut(DEBUG_LEVEL_FUNC_START, "ACMStream Deletion (0X%08X)",this);
		delete my_debug;
	}
}

/// The longest library report line that the debug log keeps whole, in bytes.
static const size_t REPORT_LINE_BYTES = 1000;

/// The debug log that acm_report() writes to. Set around the calls that
/// report, per thread, so two streams opened at once keep their own logs.
static thread_local const ADbg *acm_report_target = NULL;

/**
 * \brief Writes one report of the library into the debug log of the stream
 *        that is reporting. It has the type of a libmp3lame report function.
 *
 * The log ends each entry itself, so a line break at the end of the report is
 * dropped.
 *
 * \param format  the printf format.
 * \param ap      its arguments.
 */
static void
acm_report(const char *format, va_list ap)
{
	char line[REPORT_LINE_BYTES];
	size_t len;

	if (acm_report_target == NULL || vsnprintf(line, sizeof(line), format, ap) < 0)
		return;
	len = strnlen(line, sizeof(line));
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';
	acm_report_target->OutPut(DEBUG_LEVEL_FUNC_DEBUG, "%s", line);
}

bool ACMStream::init(const int nSamplesPerSec, const int nOutputSamplesPerSec, const int nChannels, const int nOutputChannels, const int nAvgBytesPerSec, const vbr_mode mode)
{
	bool bResult = false;

	my_SamplesPerSec  = nSamplesPerSec;
	my_OutSamplesPerSec = nOutputSamplesPerSec;
	my_Channels       = nChannels;
	my_OutChannels    = nOutputChannels;
	my_AvgBytesPerSec = nAvgBytesPerSec;
	my_VBRMode = mode;

	bResult = true;

	return bResult;

}

bool ACMStream::open(const AEncodeProperties & the_Properties)
{
	bool bResult = false;

	// Init the MP3 Stream
	// Init the global flags structure
	gfp = lame_init();

	// Set input sample frequency
	lame_set_in_samplerate( gfp, my_SamplesPerSec );

	// Set output sample frequency
	lame_set_out_samplerate( gfp, my_OutSamplesPerSec );

	lame_set_num_channels( gfp, my_Channels );
	// LAME mixes a stereo input down when the mode is MONO.
	if (my_OutChannels == 1)
		lame_set_mode( gfp, MONO );
	else if (the_Properties.GetChannelModeValue() == MONO)
		lame_set_mode( gfp, JOINT_STEREO ); // Mono without Force: a stereo stream stays stereo
	else
		lame_set_mode( gfp, (MPEG_mode_e)the_Properties.GetChannelModeValue()) ; /// \todo Get the mode from the default configuration

//	lame_set_VBR( gfp, vbr_off ); /// \note VBR not supported for the moment
	lame_set_VBR( gfp, my_VBRMode ); /// \note VBR not supported for the moment
	
	if (my_VBRMode == vbr_abr)
	{
		lame_set_VBR_q( gfp, 1 );

		lame_set_VBR_mean_bitrate_kbps( gfp, (my_AvgBytesPerSec * 8 + 500) / 1000 );

		if (24000 > lame_get_out_samplerate( gfp ))
		{
			// For MPEG-II
			lame_set_VBR_min_bitrate_kbps( gfp, 8);

			lame_set_VBR_max_bitrate_kbps( gfp, 160);
		}
		else
		{
			// For MPEG-I
			lame_set_VBR_min_bitrate_kbps( gfp, 32);

			lame_set_VBR_max_bitrate_kbps( gfp, 320);
		}
	}

	// Set bitrate
	lame_set_brate( gfp, my_AvgBytesPerSec * 8 / 1000 );

	/// \todo Get the mode from the default configuration
	// Set copyright flag?
	lame_set_copyright( gfp, the_Properties.GetCopyrightMode()?1:0 );
	// Do we have to tag  it as non original 
	lame_set_original( gfp, the_Properties.GetOriginalMode()?1:0 );
	// Add CRC?
	lame_set_error_protection( gfp, the_Properties.GetCRCMode()?1:0 );
	// Set private bit?
	lame_set_extension( gfp, the_Properties.GetPrivateMode()?1:0 );
	// Use the bit reservoir?
	lame_set_disable_reservoir( gfp, the_Properties.GetNoBiResMode()?1:0 );
	// INFO tag support not possible in ACM - it requires rewinding 
        // output stream to the beginning after encoding is finished.   
	lame_set_bWriteVbrTag( gfp, 0 );

	// The library's messages and the settings, into the debug log of this
	// stream; lame_init_params() takes the message function over
	acm_report_target = my_debug;
	lame_set_msgf( gfp, acm_report );
	if (0 == lame_init_params( gfp ))
	{
		// One frame of samples per call, for all channels
		my_SamplesPerBlock = lame_get_framesize( gfp ) * lame_get_num_channels( gfp );

		lame_print_config( gfp );
		lame_print_internals( gfp );
	}
	acm_report_target = NULL;

#ifdef FROM_DLL
beConfig.format.LHV1.dwReSampleRate		= my_OutSamplesPerSec;	  // force the user resampling
#endif // FROM_DLL

	bResult = true;

	return bResult;
}

bool ACMStream::close(LPBYTE pOutputBuffer, DWORD *pOutputSize)
{

bool bResult = false;

	int nOutputSamples = 0;

    /* the caller passes the destination buffer's size in *pOutputSize */
    nOutputSamples = lame_encode_flush( gfp, pOutputBuffer, (int) *pOutputSize );

	if ( nOutputSamples < 0 )
	{
		// BUFFER_TOO_SMALL
*pOutputSize = 0;
	}
	else
{
		*pOutputSize = nOutputSamples;

		bResult = true;
	}

	// lame will be closed in destructor
        //lame_close( gfp );

	return bResult;
}

DWORD ACMStream::GetOutputSizeForInput(const DWORD the_SrcLength) const
{
/*	double OutputInputRatio;

	if (my_VBRMode == vbr_off)
		OutputInputRatio = double(my_AvgBytesPerSec) / double(my_OutSamplesPerSec * 2);
	else // reserve the space for 320 kbps
		OutputInputRatio = 40000.0 / double(my_OutSamplesPerSec * 2);

	OutputInputRatio *= 1.15; // allow 15% more*/

    DWORD Result;

//	Result = DWORD(double(the_SrcLength) * OutputInputRatio);
    Result = DWORD(1.25*the_SrcLength + 7200);

my_debug->OutPut(DEBUG_LEVEL_FUNC_CODE, "Result = %d",Result);

	return Result;
}

bool ACMStream::ConvertBuffer(LPACMDRVSTREAMHEADER a_StreamHeader)
{
	bool result;

if (my_debug != NULL)
{
my_debug->OutPut(DEBUG_LEVEL_FUNC_DEBUG, "enter ACMStream::ConvertBuffer");
}

	DWORD InSize = a_StreamHeader->cbSrcLength / 2, OutSize = a_StreamHeader->cbDstLength; // 2 for 8<->16 bits

// Encode it
int dwSamples;
	int nOutputSamples = 0;

	dwSamples = InSize / lame_get_num_channels( gfp );

	if ( 1 == lame_get_num_channels( gfp ) )
	{
		nOutputSamples = lame_encode_buffer(gfp,(PSHORT)a_StreamHeader->pbSrc,(PSHORT)a_StreamHeader->pbSrc,dwSamples,a_StreamHeader->pbDst,a_StreamHeader->cbDstLength);
	}
	else
	{
		nOutputSamples = lame_encode_buffer_interleaved(gfp,(PSHORT)a_StreamHeader->pbSrc,dwSamples,a_StreamHeader->pbDst,a_StreamHeader->cbDstLength);
	}

	a_StreamHeader->cbSrcLengthUsed = a_StreamHeader->cbSrcLength;
	/* a negative answer is an error, not a byte count */
	a_StreamHeader->cbDstLengthUsed = nOutputSamples < 0 ? 0 : nOutputSamples;

	result = nOutputSamples >= 0 && a_StreamHeader->cbDstLengthUsed <= a_StreamHeader->cbDstLength;

	my_debug->OutPut(DEBUG_LEVEL_FUNC_CODE, "UsedSize = %d / EncodedSize = %d, result = %d (%d <= %d)", InSize, OutSize, result, a_StreamHeader->cbDstLengthUsed, a_StreamHeader->cbDstLength);

if (my_debug != NULL)
{
my_debug->OutPut(DEBUG_LEVEL_FUNC_DEBUG, "ACMStream::ConvertBuffer result = %d",result);
}

	return result;
}

/* map frequency to a valid MP3 sample frequency
 *
 * Robert Hegemann 2000-07-01
 *
 * The nine rates are the three sampling-frequency indices of each MPEG
 * version, which lame_get_samplerate() answers for.  The answer is the
 * lowest rate that still holds the requested one, or the highest there is
 * when none does.
 */
static int
map2MP3Frequency(int freq)
{
    int     version, index;
    int     smallest_fit = 0;
    int     highest = 0;

    for (version = 0; version <= 2; version++) {
        for (index = 0; index < 3; index++) {
            int const rate = lame_get_samplerate(version, index);
            if (rate <= 0)
                continue;
            if (rate > highest)
                highest = rate;
            if (rate >= freq && (smallest_fit == 0 || rate < smallest_fit))
                smallest_fit = rate;
        }
    }
    return smallest_fit != 0 ? smallest_fit : highest;
}


unsigned int ACMStream::GetOutputSampleRate(int samples_per_sec, int bitrate, int channels)
{
    if (bitrate==0)
        bitrate = (64000/8)*channels;

        /// \todo pass through the same LAME routine
	unsigned int OutputFrequency;
	// Both operands are ints, so the division is deliberately written on
	// doubles: done in integer arithmetic it floors the ratio before the
	// comparison below, which puts a ratio of 13.7 on the wrong side of the
	// boundary and offers a rate the caller then refuses to convert to. The
	// products are taken in double as well.
	double compression_ratio = double(samples_per_sec) * 16 * channels / (double(bitrate) * 8);
	// map2MP3Frequency() takes a frequency in Hz as an int, so the fractional
	// part of both expressions has always been discarded here.  The casts
	// write that down; they do not change which rate comes back.
	if (compression_ratio > 13.)
		OutputFrequency = map2MP3Frequency( (int) ((10. * bitrate * 8) / (16 * channels)));
	else
		OutputFrequency = map2MP3Frequency( (int) (0.97 * samples_per_sec) );

	return OutputFrequency;

}

