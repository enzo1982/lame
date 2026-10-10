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
 my_FrameInput(0.0),
 my_LargestFrame(0),
 my_HeldFrames(0),
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

/// The debug log that acm_report_at() writes to. Set around the calls that
/// report, per thread, so two streams opened at once keep their own logs.
static thread_local const ADbg *acm_report_target = NULL;

/**
 * \brief Writes one report of the library into the debug log of the stream
 *        that is reporting.
 *
 * The log ends each entry itself, so a line break at the end of the report is
 * dropped. acm_report() and acm_report_error(), the report functions that the
 * stream gives libmp3lame, call it.
 *
 * \param level   the debug level of the entry.
 * \param format  the printf format.
 * \param ap      its arguments.
 */
static void
acm_report_at(int level, const char *format, va_list ap)
{
	char line[REPORT_LINE_BYTES];
	size_t len;

	if (acm_report_target == NULL || vsnprintf(line, sizeof(line), format, ap) < 0)
		return;
	len = strnlen(line, sizeof(line));
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';
	acm_report_target->OutPut(level, "%s", line);
}

/**
 * \brief The report function for messages and debug reports, at the debug
 *        level.
 *
 * \param format  the printf format.
 * \param ap      its arguments.
 */
static void
acm_report(const char *format, va_list ap)
{
	acm_report_at(DEBUG_LEVEL_FUNC_DEBUG, format, ap);
}

/**
 * \brief The report function for errors, at the level of the log's own
 *        messages.
 *
 * \param format  the printf format.
 * \param ap      its arguments.
 */
static void
acm_report_error(const char *format, va_list ap)
{
	acm_report_at(DEBUG_LEVEL_MSG, format, ap);
}

/**
 * \brief Writes a setting that LAME rejected into the debug log of a stream.
 *
 * \param log     the debug log of the stream.
 * \param result  what the setter returned.
 * \param setter  the name of the setter.
 * \param value   the value the setter was given.
 * \return true if the setter took the value.
 */
static bool
setting_taken(const ADbg & log, int result, const char *setter, int value)
{
	if (result == 0)
		return true;
	log.OutPut(DEBUG_LEVEL_FUNC_CODE, "%s(%d) failed (%d)", setter, value, result);
	return false;
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

	\return false if lame_init() fails, if LAME rejects a setting, or if
	        lame_init_params() fails. The stream then has no encoder.
*/
bool ACMStream::start()
{
	int init_result;
	bool taken;

	if (gfp != NULL)
		lame_close( gfp );
	my_Ended = false;
	my_Aligned = false;
	my_Kept.clear();

	// Init the MP3 Stream
	// Init the global flags structure
	gfp = lame_init();
	if (gfp == NULL)
		return false;

	// A setting that LAME rejects fails the start: LAME would encode with
	// another value than the formats of the stream say
	taken = setting_taken(my_debug, lame_set_in_samplerate( gfp, my_SamplesPerSec ),
	                      "lame_set_in_samplerate", my_SamplesPerSec)
		&& setting_taken(my_debug, lame_set_out_samplerate( gfp, my_OutSamplesPerSec ),
		                 "lame_set_out_samplerate", my_OutSamplesPerSec)
		&& setting_taken(my_debug, lame_set_num_channels( gfp, my_Channels ),
		                 "lame_set_num_channels", my_Channels)
		&& setting_taken(my_debug, lame_set_mode( gfp, my_Mode ), "lame_set_mode", my_Mode)
		&& setting_taken(my_debug, lame_set_VBR( gfp, my_VBRMode ), "lame_set_VBR", my_VBRMode);

	if (taken && my_VBRMode == vbr_abr)
	{
		int const mean_kbps = (my_AvgBytesPerSec * 8 + 500) / 1000;
		// The bitrate range of MPEG-II below 24000 Hz, else of MPEG-I
		int const min_kbps = 24000 > my_OutSamplesPerSec ? 8 : 32;
		int const max_kbps = 24000 > my_OutSamplesPerSec ? 160 : 320;

		taken = setting_taken(my_debug, lame_set_VBR_q( gfp, 1 ), "lame_set_VBR_q", 1)
			&& setting_taken(my_debug, lame_set_VBR_mean_bitrate_kbps( gfp, mean_kbps ),
			                 "lame_set_VBR_mean_bitrate_kbps", mean_kbps)
			&& setting_taken(my_debug, lame_set_VBR_min_bitrate_kbps( gfp, min_kbps ),
			                 "lame_set_VBR_min_bitrate_kbps", min_kbps)
			&& setting_taken(my_debug, lame_set_VBR_max_bitrate_kbps( gfp, max_kbps ),
			                 "lame_set_VBR_max_bitrate_kbps", max_kbps);
	}

	/// \todo Get the mode from the default configuration
	// The bitrate, the header bits and the bit reservoir. No INFO tag: it
	// needs the start of the output again after the encoding, and an ACM
	// stream cannot go back.
	taken = taken
		&& setting_taken(my_debug, lame_set_brate( gfp, my_AvgBytesPerSec * 8 / 1000 ),
		                 "lame_set_brate", my_AvgBytesPerSec * 8 / 1000)
		&& setting_taken(my_debug, lame_set_copyright( gfp, my_Copyright?1:0 ),
		                 "lame_set_copyright", my_Copyright?1:0)
		&& setting_taken(my_debug, lame_set_original( gfp, my_Original?1:0 ),
		                 "lame_set_original", my_Original?1:0)
		&& setting_taken(my_debug, lame_set_error_protection( gfp, my_CRC?1:0 ),
		                 "lame_set_error_protection", my_CRC?1:0)
		&& setting_taken(my_debug, lame_set_extension( gfp, my_Private?1:0 ),
		                 "lame_set_extension", my_Private?1:0)
		&& setting_taken(my_debug, lame_set_disable_reservoir( gfp, my_NoBitRes?1:0 ),
		                 "lame_set_disable_reservoir", my_NoBitRes?1:0)
		&& setting_taken(my_debug, lame_set_bWriteVbrTag( gfp, 0 ), "lame_set_bWriteVbrTag", 0);

	if (!taken)
	{
		lame_close( gfp );
		gfp = NULL;
		return false;
	}

	// The library's messages, errors and the settings, into the debug log of
	// this stream and not onto the stderr of the host; lame_init_params()
	// takes the report functions over
	acm_report_target = &my_debug;
	lame_set_msgf( gfp, acm_report );
	lame_set_debugf( gfp, acm_report );
	lame_set_errorf( gfp, acm_report_error );
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

	read_bounds();
	return true;
}

/// Bytes per second of one kbit/s.
static const int BYTES_PER_KBPS = 125;
/// The padding byte that a Layer III frame can carry.
static const int PADDING_BYTES = 1;
/// The input samples of a Layer III frame of MPEG-1; MPEG-2 and MPEG-2.5
/// frames have half as many.
static const int MPEG1_FRAME_SAMPLES = 1152;
/// The frames that the samples the encoder holds and the end of the MP3
/// stream can add to a conversion, for frames of MPEG-1.
static const DWORD HELD_FRAMES_MPEG1 = 5;
/// The same, for the shorter frames of MPEG-2 and MPEG-2.5.
static const DWORD HELD_FRAMES_MPEG2 = 6;
/// How far back the bit reservoir reaches in MPEG-1: the largest
/// main_data_begin, 9 bits (ISO/IEC 11172-3, 2.4.1.7 and 2.4.2.7).
static const int RESERVOIR_BYTES_MPEG1 = 511;
/// The same in MPEG-2 and MPEG-2.5: 8 bits (ISO/IEC 13818-3, 2.4.1.2).
static const int RESERVOIR_BYTES_MPEG2 = 255;
/// The index of the lowest bitrate in the tables of lame_get_bitrate().
static const int LOWEST_BITRATE_INDEX = 1;

/**
	\brief Takes the bounds of the MP3 stream from the started encoder: the
	input of one MP3 frame, the largest frame, and the held frames.

	The largest frame is one at the highest bitrate of the MP3 stream, its
	bitrate for CBR, the highest one of ABR, with its padding byte. The held
	frames are those of the MPEG version and those that the bit reservoir
	holds back: LAME returns a frame only once the frames whose main data
	begin in it are encoded. They are counted in frames of the lowest bitrate
	the stream can have, its bitrate for CBR, the lowest of the MPEG version
	otherwise, as silent frames of VBR and ABR can have it.
*/
void ACMStream::read_bounds()
{
	int const frame_samples = lame_get_framesize( gfp );
	int const kbps = lame_get_VBR( gfp ) == vbr_off
		? lame_get_brate( gfp ) : lame_get_VBR_max_bitrate_kbps( gfp );
	int const lowest_kbps = lame_get_VBR( gfp ) == vbr_off
		? lame_get_brate( gfp ) : lame_get_bitrate( lame_get_version( gfp ), LOWEST_BITRATE_INDEX );
	int const smallest_frame = frame_samples * lowest_kbps * BYTES_PER_KBPS / lame_get_out_samplerate( gfp );
	int const reservoir = frame_samples == MPEG1_FRAME_SAMPLES ? RESERVOIR_BYTES_MPEG1 : RESERVOIR_BYTES_MPEG2;
	DWORD const largest_frame = DWORD(frame_samples * kbps * BYTES_PER_KBPS
		/ lame_get_out_samplerate( gfp )) + PADDING_BYTES;
	DWORD const held_frames = (frame_samples == MPEG1_FRAME_SAMPLES ? HELD_FRAMES_MPEG1 : HELD_FRAMES_MPEG2)
		+ DWORD((reservoir + smallest_frame - 1) / smallest_frame);

	// A frame holds frame_samples samples of the output rate
	my_FrameInput = double(frame_samples) * my_SamplesPerSec / lame_get_out_samplerate( gfp );
	my_LargestFrame = largest_frame;
	my_HeldFrames = held_frames;
	my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "%d kbit/s at most: %u bytes per frame, %u frames held",
	                kbps, (unsigned) largest_frame, (unsigned) held_frames);
}

/**
	\brief Returns the most MP3 bytes that a conversion of the given input can
	return: the whole frames that its sample frames begin and the held frames,
	each of the largest size.

	\param the_Frames the input sample frames.
	\return the bytes, at most MAXDWORD.
*/
DWORD ACMStream::MostBytesFor(const DWORD the_Frames) const
{
	double const bytes = (ceil(the_Frames / my_FrameInput) + my_HeldFrames) * my_LargestFrame;

	return bytes < double(MAXDWORD) ? DWORD(bytes) : MAXDWORD;
}

/// What lame_encode_buffer() returns at most per input sample of one channel
/// (lame.h).
static const double LAME_BYTES_PER_SAMPLE = 1.25;
/// What lame_encode_buffer() returns at most beyond that, and what
/// lame_encode_flush() returns at most, at the standard bitrates (lame.h).
static const DWORD LAME_MARGIN_BYTES = 7200;

/**
	\brief Returns the size of a destination buffer that holds everything a
	conversion of the given source returns, the end of the MP3 stream
	included.

	\param the_SrcLength the size of the source buffer, in bytes.
	\return the size of the destination buffer, in bytes.
*/
DWORD ACMStream::GetOutputSizeForInput(const DWORD the_SrcLength) const
{
	DWORD const frame_bytes = (DWORD) my_Channels * sizeof(short);
	DWORD const Result = MostBytesFor(the_SrcLength / frame_bytes);

	my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "Result = %u", (unsigned) Result);

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
	DWORD const whole_frames = the_DstLength / my_LargestFrame;
	double const most_frames = double(MAXDWORD / frame_bytes);
	double fitting = 0.0;
	DWORD frames;

	if (whole_frames > my_HeldFrames)
		fitting = floor((whole_frames - my_HeldFrames) * my_FrameInput);
	frames = DWORD(fitting < most_frames ? fitting : most_frames);
	// MostBytesFor() rounds up
	while (frames > 0 && MostBytesFor(frames) > the_DstLength)
		--frames;

	my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "Result = %u", (unsigned) (frames * frame_bytes));

	return frames * frame_bytes;
}

/**
	\brief Copies the MP3 data that earlier conversions kept to the start of
	the destination buffer of a conversion, as much as fits, and keeps the
	rest.

	\param a_StreamHeader the conversion.
	\return the bytes copied.
*/
DWORD ACMStream::ReturnKept(LPACMDRVSTREAMHEADER a_StreamHeader)
{
	DWORD const copied = my_Kept.size() < a_StreamHeader->cbDstLength
		? (DWORD) my_Kept.size() : a_StreamHeader->cbDstLength;

	if (copied > 0)
	{
		memcpy(a_StreamHeader->pbDst, &my_Kept[0], copied);
		my_Kept.erase(my_Kept.begin(), my_Kept.begin() + copied);
	}
	return copied;
}

/**
	\brief Copies what LAME encoded for a conversion into its destination
	buffer, after the bytes already there. What does not fit goes to the kept
	data.

	\param a_StreamHeader the conversion.
	\param a_Returned the bytes already in the destination buffer.
	\param a_Encoded the bytes that LAME encoded.
	\return the bytes in the destination buffer now.
*/
DWORD ACMStream::ReturnEncoded(LPACMDRVSTREAMHEADER a_StreamHeader, DWORD a_Returned, DWORD a_Encoded)
{
	DWORD const room = a_StreamHeader->cbDstLength - a_Returned;
	DWORD const copied = a_Encoded < room ? a_Encoded : room;

	if (copied > 0)
		memcpy(a_StreamHeader->pbDst + a_Returned, &my_Encoded[0], copied);
	if (copied < a_Encoded)
		my_Kept.insert(my_Kept.end(), my_Encoded.begin() + copied, my_Encoded.begin() + a_Encoded);
	return a_Returned + copied;
}

/**
	\brief Encodes the source buffer of a conversion into its destination
	buffer.

	The MP3 data that earlier conversions kept comes first. What does not fit
	into the destination buffer is kept for the next conversion.

	With ACM_STREAMCONVERTF_START in the conversion flags, a new MP3 stream
	starts, with the settings the settings file holds now: the samples that
	the encoder holds and the MP3 data kept from earlier conversions are
	dropped. With ACM_STREAMCONVERTF_END, the encoder is flushed after the
	source buffer, and the end of the MP3 stream follows the encoded data in
	the destination buffer. The first conversion without
	ACM_STREAMCONVERTF_BLOCKALIGN after one with it does the same: it is the
	last one of the data, and a client that never sends END marks the end so.
	A conversion with BLOCKALIGN that does not end the MP3 stream leaves the
	last sample frame of its source unused, so that such a client always has
	source data left for that last conversion. The conversion after the end
	starts a new MP3 stream, with or without ACM_STREAMCONVERTF_START.

	\param a_StreamHeader the buffers and the conversion flags. The function
	       sets the bytes used of both buffers.
	\return false if the encoder cannot start or fails, or if the destination
	        buffer of a conversion that ends the MP3 stream is too small for
	        everything the conversion returns. That conversion converts
	        nothing.
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

	DWORD const channels = (DWORD) lame_get_num_channels( gfp );
	DWORD frames = InSize / channels;

	if (aligned && !ending && frames > 0)
		--frames;

	// A conversion that ends the MP3 stream returns everything in its
	// destination buffer. With less room, nothing is converted, so the client
	// can try again with a larger buffer.
	if (ending)
	{
		DWORD const most = MostBytesFor(frames);

		if (most > a_StreamHeader->cbDstLength || my_Kept.size() > a_StreamHeader->cbDstLength - most)
		{
			my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "the end of the stream needs %u + %u bytes, the buffer has %u",
			                (unsigned) my_Kept.size(), (unsigned) most, (unsigned) a_StreamHeader->cbDstLength);
			a_StreamHeader->cbSrcLengthUsed = 0;
			a_StreamHeader->cbDstLengthUsed = 0;
			return false;
		}
	}

	DWORD returned = ReturnKept(a_StreamHeader);

	// Room for LAME's bounds of the encode and of the flush (lame.h)
	double const out_per_in = my_OutSamplesPerSec > my_SamplesPerSec
		? double(my_OutSamplesPerSec) / double(my_SamplesPerSec) : 1.0;
	size_t const room = size_t(ceil(LAME_BYTES_PER_SAMPLE * frames * out_per_in)) + 2 * LAME_MARGIN_BYTES;
	int encoded;

	my_Encoded.resize(room);
	// The encode and flush calls report too
	acm_report_target = &my_debug;
	if (channels == 1)
		encoded = lame_encode_buffer(gfp, (PSHORT) a_StreamHeader->pbSrc, (PSHORT) a_StreamHeader->pbSrc,
		                             (int) frames, &my_Encoded[0], (int) room);
	else
		encoded = lame_encode_buffer_interleaved(gfp, (PSHORT) a_StreamHeader->pbSrc, (int) frames,
		                                         &my_Encoded[0], (int) room);

	if (encoded >= 0 && ending)
	{
		size_t const flush_room = room - (size_t) encoded;
		int flushed = -1;

		// lame_encode_flush() takes a size of 0 as no limit
		if (flush_room > 0)
			flushed = lame_encode_flush( gfp, &my_Encoded[0] + encoded, (int) flush_room );
		encoded = flushed < 0 ? flushed : encoded + flushed;
		my_Ended = true;
	}
	acm_report_target = NULL;

	// A partial sample frame at the end is not encoded
	a_StreamHeader->cbSrcLengthUsed = frames * channels * sizeof(short);
	if (encoded >= 0)
		returned = ReturnEncoded(a_StreamHeader, returned, (DWORD) encoded);
	a_StreamHeader->cbDstLengthUsed = returned;
	if (my_Ended && !my_Kept.empty())
		my_debug.OutPut(DEBUG_LEVEL_FUNC_CODE, "%u bytes of the end of the stream do not fit",
		                (unsigned) my_Kept.size());

	result = encoded >= 0;

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

