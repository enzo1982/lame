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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include <windows.h>

#include "adebug.h"

#include "ACMStream.h"

#include <lame.h>

// static methods

ACMStream * ACMStream::Create()
{
	ACMStream * Result;

	Result = new (std::nothrow) ACMStream;

	return Result;
}

bool ACMStream::Erase(const ACMStream * a_ACMStream)
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
 gfp(NULL),
 my_Ended(false),
 my_Aligned(false),
 my_Module(NULL),
 my_debug(DEBUG_LEVEL_CREATION)
{
	 /// \todo get the debug level from the registry
	my_debug.setPrefix("LAMEstream"); /// \todo get it from the registry
	my_debug.setIncludeTime(true);  /// \todo get it from the registry
	ConfigureDebugFromRegistry(my_debug);
	my_debug.OutPut(DEBUG_LEVEL_FUNC_START, "ACMStream Creation (0X%08X)",this);
}

ACMStream::~ACMStream()
{
        // release memory - encoding is finished
	if (gfp) lame_close( gfp );

	my_debug.OutPut(DEBUG_LEVEL_FUNC_START, "ACMStream Deletion (0X%08X)",this);
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

/**
	\brief Reads the encoder settings and starts the encoder of the stream.

	Each new MP3 stream on this stream reads the settings again, from the
	settings file (restart()).

	\param the_Properties the settings of the codec.
	\return false if LAME rejects the settings. The stream then has no
	        encoder.
*/
bool ACMStream::open(const AEncodeProperties & the_Properties)
{
	my_Module = the_Properties.GetModule();
	read_settings(the_Properties);
	return start();
}

/**
	\brief Starts a new MP3 stream with the settings that the settings file
	holds now, so that a change made in the configuration dialog since the
	stream opened applies. The formats of the stream stay as they were
	opened.

	\return false if LAME rejects the settings. The stream then has no
	        encoder.
*/
bool ACMStream::restart()
{
	AEncodeProperties now(my_Module);

	now.ParamsRestore();
	read_settings(now);
	return start();
}

/**
	\brief Takes the settings that start() passes to LAME from the settings
	of the codec, for the formats the stream was opened with.

	\param the_Properties the settings of the codec.
*/
void ACMStream::read_settings(const AEncodeProperties & the_Properties)
{
	// LAME mixes a stereo input down when the mode is MONO.
	if (my_OutChannels == 1)
		my_Mode = MONO;
	else if (the_Properties.GetChannelModeValue() == MONO)
		my_Mode = JOINT_STEREO; // Mono without Force: a stereo stream stays stereo
	else
		my_Mode = (MPEG_mode_e)the_Properties.GetChannelModeValue(); /// \todo Get the mode from the default configuration

	my_Copyright = the_Properties.GetCopyrightMode();
	my_Original  = the_Properties.GetOriginalMode();
	my_CRC       = the_Properties.GetCRCMode();
	my_Private   = the_Properties.GetPrivateMode();
	my_NoBitRes  = the_Properties.GetNoBiResMode();
}

/**
	\brief Starts a new encoder with the settings that open() or restart()
	read. The encoder that runs, if any, is closed first.

	\return false if lame_init() or lame_init_params() fails. The stream
	        then has no encoder.
*/
bool ACMStream::start()
{
	int init_result;

	if (gfp != NULL)
		lame_close( gfp );
	my_Ended = false;
	my_Aligned = false;

	// Init the MP3 Stream
	// Init the global flags structure
	gfp = lame_init();
	if (gfp == NULL)
		return false;

	// Set input sample frequency
	lame_set_in_samplerate( gfp, my_SamplesPerSec );

	// Set output sample frequency
	lame_set_out_samplerate( gfp, my_OutSamplesPerSec );

	lame_set_num_channels( gfp, my_Channels );
	lame_set_mode( gfp, my_Mode );

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
	lame_set_copyright( gfp, my_Copyright?1:0 );
	// Do we have to tag  it as non original 
	lame_set_original( gfp, my_Original?1:0 );
	// Add CRC?
	lame_set_error_protection( gfp, my_CRC?1:0 );
	// Set private bit?
	lame_set_extension( gfp, my_Private?1:0 );
	// Use the bit reservoir?
	lame_set_disable_reservoir( gfp, my_NoBitRes?1:0 );
	// INFO tag support not possible in ACM - it requires rewinding 
        // output stream to the beginning after encoding is finished.   
	lame_set_bWriteVbrTag( gfp, 0 );

	// The library's messages and the settings, into the debug log of this
	// stream; lame_init_params() takes the message function over
	acm_report_target = &my_debug;
	lame_set_msgf( gfp, acm_report );
	init_result = lame_init_params( gfp );
	if (init_result == 0)
	{
		lame_print_config( gfp );
		lame_print_internals( gfp );
	}
	acm_report_target = NULL;

	if (init_result != 0)
	{
		my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "lame_init_params() failed (%d)", init_result);
		lame_close( gfp );
		gfp = NULL;
		return false;
	}

	return true;
}

/// Destination bytes per source byte, in GetOutputSizeForInput().
static const double OUTPUT_BYTES_PER_INPUT_BYTE = 1.25;
/// What lame_encode_buffer() returns at most per input sample of one channel
/// (lame.h).
static const double LAME_BYTES_PER_SAMPLE = 1.25;
/// What lame_encode_buffer() returns at most beyond that, and what
/// lame_encode_flush() returns at most, at the standard bitrates (lame.h).
static const DWORD LAME_MARGIN_BYTES = 7200;
/// The margin of GetOutputSizeForInput(), in bytes: room for the encode and
/// for the flush of a conversion with ACM_STREAMCONVERTF_END.
static const DWORD OUTPUT_MARGIN_BYTES = 2 * LAME_MARGIN_BYTES;

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
    Result = DWORD(OUTPUT_BYTES_PER_INPUT_BYTE*the_SrcLength + OUTPUT_MARGIN_BYTES);

my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "Result = %d",Result);

	return Result;
}

/**
	\brief Returns the largest source size whose output fits into a
	destination buffer of the given size, in whole sample frames.

	GetOutputSizeForInput() of the result is not larger than
	\a the_DstLength.

	\param the_DstLength the size of the destination buffer, in bytes.
	\return the source size, in bytes. 0 if not even one sample frame fits.
*/
DWORD ACMStream::GetInputSizeForOutput(const DWORD the_DstLength) const
{
	DWORD const frame_bytes = (DWORD) my_Channels * sizeof(short);
	DWORD Result = 0;

	if (the_DstLength > OUTPUT_MARGIN_BYTES)
		Result = DWORD((the_DstLength - OUTPUT_MARGIN_BYTES) / OUTPUT_BYTES_PER_INPUT_BYTE);
	Result -= Result % frame_bytes;

	my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "Result = %d",Result);

	return Result;
}

/**
	\brief Encodes the source buffer of a conversion into its destination
	buffer.

	With ACM_STREAMCONVERTF_START in the conversion flags, a new MP3 stream
	starts, with the settings the settings file holds now: the samples that
	the encoder holds from earlier conversions are dropped. With
	ACM_STREAMCONVERTF_END, the encoder is flushed after the source buffer,
	and the end of the MP3 stream follows the encoded data in the destination
	buffer. The first conversion without ACM_STREAMCONVERTF_BLOCKALIGN after
	one with it does the same: it is the last one of the data, and a client
	that never sends END marks the end so. The conversion after either starts
	a new MP3 stream, with or without ACM_STREAMCONVERTF_START.

	\param a_StreamHeader the buffers and the conversion flags. The function
	       sets the bytes used of both buffers.
	\return false if the encoder cannot start, or if the destination buffer
	        is too small. A conversion that ends the MP3 stream into a buffer
	        too small for the flush converts nothing.
*/
bool ACMStream::ConvertBuffer(LPACMDRVSTREAMHEADER a_StreamHeader)
{
	bool result;

my_debug.OutPut(DEBUG_LEVEL_FUNC_DEBUG, "enter ACMStream::ConvertBuffer");

	DWORD InSize = a_StreamHeader->cbSrcLength / 2, OutSize = a_StreamHeader->cbDstLength; // 2 for 8<->16 bits
	bool const aligned = (a_StreamHeader->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN) != 0;

	if ((a_StreamHeader->fdwConvert & ACM_STREAMCONVERTF_START) != 0 || my_Ended)
		restart();

	// END, or the last conversion of a client that does not send it
	bool const ending = (a_StreamHeader->fdwConvert & ACM_STREAMCONVERTF_END) != 0
		|| (my_Aligned && !aligned);
	if (aligned)
		my_Aligned = true;
	if (gfp == NULL)
	{
		my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "the stream has no encoder");
		a_StreamHeader->cbSrcLengthUsed = 0;
		a_StreamHeader->cbDstLengthUsed = 0;
		return false;
	}

// Encode it
int dwSamples;
	int nOutputSamples = 0;

	dwSamples = InSize / lame_get_num_channels( gfp );

	// A conversion that ends the MP3 stream needs room for the encoded data
	// and for the flush. With less, nothing is converted, so the client can
	// try again with a larger buffer.
	if (ending)
	{
		double const out_per_in = my_OutSamplesPerSec > my_SamplesPerSec
			? double(my_OutSamplesPerSec) / double(my_SamplesPerSec) : 1.0;
		DWORD const needed = DWORD(ceil(LAME_BYTES_PER_SAMPLE * dwSamples * out_per_in))
			+ 2 * LAME_MARGIN_BYTES;

		if (a_StreamHeader->cbDstLength < needed)
		{
			my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "the end of the stream needs %u bytes, the buffer has %u",
			                (unsigned) needed, (unsigned) a_StreamHeader->cbDstLength);
			a_StreamHeader->cbSrcLengthUsed = 0;
			a_StreamHeader->cbDstLengthUsed = 0;
			return false;
		}
	}

	if ( 1 == lame_get_num_channels( gfp ) )
	{
		nOutputSamples = lame_encode_buffer(gfp,(PSHORT)a_StreamHeader->pbSrc,(PSHORT)a_StreamHeader->pbSrc,dwSamples,a_StreamHeader->pbDst,a_StreamHeader->cbDstLength);
	}
	else
	{
		nOutputSamples = lame_encode_buffer_interleaved(gfp,(PSHORT)a_StreamHeader->pbSrc,dwSamples,a_StreamHeader->pbDst,a_StreamHeader->cbDstLength);
	}

	if (nOutputSamples >= 0 && ending)
	{
		DWORD const room = a_StreamHeader->cbDstLength - (DWORD) nOutputSamples;
		int flushed = -1;

		// lame_encode_flush() takes a size of 0 as no limit
		if (room > 0)
			flushed = lame_encode_flush( gfp, a_StreamHeader->pbDst + nOutputSamples, (int) room );
		nOutputSamples = flushed < 0 ? flushed : nOutputSamples + flushed;
		my_Ended = true;
	}

	// A partial sample frame at the end is not encoded
	a_StreamHeader->cbSrcLengthUsed = (DWORD) dwSamples * lame_get_num_channels( gfp ) * sizeof(short);
	/* a negative answer is an error, not a byte count */
	a_StreamHeader->cbDstLengthUsed = nOutputSamples < 0 ? 0 : nOutputSamples;

	result = nOutputSamples >= 0 && a_StreamHeader->cbDstLengthUsed <= a_StreamHeader->cbDstLength;

	my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "UsedSize = %d / EncodedSize = %d, result = %d (%d <= %d)", InSize, OutSize, result, a_StreamHeader->cbDstLengthUsed, a_StreamHeader->cbDstLength);

my_debug.OutPut(DEBUG_LEVEL_FUNC_DEBUG, "ACMStream::ConvertBuffer result = %d",result);

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

