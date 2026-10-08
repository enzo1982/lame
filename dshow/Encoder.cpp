/*
 *  LAME MP3 encoder for DirectShow
 *  LAME encoder wrapper
 *
 *  Copyright (c) 2000-2005 Marie Orlova, Peter Gubanov, Vitaly Ivanov, Elecard Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <streams.h>
#include "Encoder.h"
#include "lametag_scan.h"


/**
 * Creates the encoder wrapper and its output buffer. The LAME encoder itself is
 * created later, by Init().
 */
CEncoder::CEncoder() :
    pgf(NULL),
    m_bInpuTypeSet(FALSE),
    m_bOutpuTypeSet(FALSE),
    m_bFinished(FALSE),
    m_frameCount(0),
    m_outOffset(0),
    m_outReadOffset(0)
{
    m_outFrameBuf = new unsigned char[OUT_BUFFER_SIZE];
}

CEncoder::~CEncoder()
{
    Close(NULL);

    if (m_outFrameBuf)
        delete [] m_outFrameBuf;
}

/**
 * Checks whether the encoder supports an input format, and stores it.
 *
 * The encoder supports 16 bit PCM, mono or stereo, at 8000, 11025, 12000,
 * 16000, 22050, 24000, 32000, 44100 or 48000 Hz.
 *
 * @param lpwfex      the input format.
 * @param bJustCheck  true to only check the format, without storing it.
 * @return S_OK if the format is supported, E_INVALIDARG if not.
 */
HRESULT CEncoder::SetInputType(LPWAVEFORMATEX lpwfex, bool bJustCheck)
{
    CAutoLock l(&m_lock);

    if (lpwfex->wFormatTag == WAVE_FORMAT_PCM)
    {
        if (lpwfex->nChannels == 1 || lpwfex->nChannels == 2)
        {
            if (lpwfex->nSamplesPerSec  == 48000 ||
                lpwfex->nSamplesPerSec  == 44100 ||
                lpwfex->nSamplesPerSec  == 32000 ||
                lpwfex->nSamplesPerSec  == 24000 ||
                lpwfex->nSamplesPerSec  == 22050 ||
                lpwfex->nSamplesPerSec  == 16000 ||
                lpwfex->nSamplesPerSec  == 12000 ||
                lpwfex->nSamplesPerSec  == 11025 ||
                lpwfex->nSamplesPerSec  ==  8000)
            {
                if (lpwfex->wBitsPerSample == 16)
                {
                    if (!bJustCheck)
                    {
                        memcpy(&m_wfex, lpwfex, sizeof(WAVEFORMATEX));
                        m_bInpuTypeSet = true;
                    }

                    return S_OK;
                }
            }
        }
    }

    if (!bJustCheck)
        m_bInpuTypeSet = false;

    return E_INVALIDARG;
}

/**
 * Stores the encoder settings for the output. Init() applies them.
 */
HRESULT CEncoder::SetOutputType(MPEG_ENCODER_CONFIG &mabsi)
{
    CAutoLock l(&m_lock);

    m_mabsi = mabsi;
    m_bOutpuTypeSet = true;

    return S_OK;
}

/**
 * Creates and configures the LAME encoder from the stored input type and
 * output settings, if it does not exist yet, and resets the output buffer.
 *
 * @return S_OK on success. E_UNEXPECTED if the input type or the output
 *         settings are not set. E_FAIL if LAME cannot be initialized.
 */
HRESULT CEncoder::Init()
{
    CAutoLock l(&m_lock);

    m_outOffset     = 0;
    m_outReadOffset = 0;

    m_bFinished     = FALSE;

    m_frameCount    = 0;

    if (!pgf)
    {
        if (!m_bInpuTypeSet || !m_bOutpuTypeSet)
            return E_UNEXPECTED;

        // Init Lame library
        // note: newer, safer interface which doesn't 
        // allow or require direct access to 'gf' struct is being written
        // see the file 'API' included with LAME.
        if ((pgf = lame_init()) != NULL)
        {
            lame_set_num_channels(pgf, m_wfex.nChannels);
            lame_set_in_samplerate(pgf, m_wfex.nSamplesPerSec);
            lame_set_out_samplerate(pgf, m_mabsi.dwSampleRate);
            if ((lame_get_out_samplerate(pgf) >= 32000) && (m_mabsi.dwBitrate < 32))
                lame_set_brate(pgf, 32);
            else
                lame_set_brate(pgf, m_mabsi.dwBitrate);
            lame_set_VBR(pgf, m_mabsi.vmVariable);
            lame_set_VBR_min_bitrate_kbps(pgf, m_mabsi.dwVariableMin);
            lame_set_VBR_max_bitrate_kbps(pgf, m_mabsi.dwVariableMax);

            lame_set_copyright(pgf, m_mabsi.bCopyright);
            lame_set_original(pgf, m_mabsi.bOriginal);
            lame_set_error_protection(pgf, m_mabsi.bCRCProtect);

            lame_set_bWriteVbrTag(pgf, m_mabsi.dwXingTag);
            lame_set_strict_ISO(pgf, m_mabsi.dwStrictISO);
            lame_set_VBR_hard_min(pgf, m_mabsi.dwEnforceVBRmin);

            if (lame_get_num_channels(pgf) == 2 && !m_mabsi.bForceMono)
            {
                //int act_br = pgf->VBR ? pgf->VBR_min_bitrate_kbps + pgf->VBR_max_bitrate_kbps / 2 : pgf->brate;

                // Disabled. It's for user's consideration now
                //int rel = pgf->out_samplerate / (act_br + 1);
                //pgf->mode = rel < 200 ? m_mabsi.ChMode : JOINT_STEREO;

                lame_set_mode(pgf, m_mabsi.ChMode);
            }
            else
                lame_set_mode(pgf, MONO);

            if (lame_get_mode(pgf) == JOINT_STEREO)
                lame_set_force_ms(pgf, m_mabsi.dwForceMS);
            else
                lame_set_force_ms(pgf, 0);

//            pgf->mode_fixed = m_mabsi.dwModeFixed;

            if (m_mabsi.dwVoiceMode != 0)
            {
                lame_set_lowpassfreq(pgf,12000);
                //pgf->VBR_max_bitrate_kbps = 160;
            }

            if (m_mabsi.dwKeepAllFreq != 0)
            {
                lame_set_lowpassfreq(pgf, -1);
                lame_set_highpassfreq(pgf, -1);
            }

            lame_set_quality(pgf, m_mabsi.dwQuality);
            lame_set_VBR_q(pgf, m_mabsi.dwVBRq);

            if (lame_init_params(pgf) < 0)
            {
                lame_close(pgf);
                pgf = NULL;
                return E_FAIL;
            }

            // encoder delay compensation
            {
                int const nch = lame_get_num_channels(pgf);
                short * start_padd = (short *)calloc(48, nch * sizeof(short));

				int out_bytes = 0;

                if (nch == 2)
                    out_bytes = lame_encode_buffer_interleaved(pgf, start_padd, 48, m_outFrameBuf, OUT_BUFFER_SIZE);
                else
                    out_bytes = lame_encode_buffer(pgf, start_padd, start_padd, 48, m_outFrameBuf, OUT_BUFFER_SIZE);

				if (out_bytes > 0)
					m_outOffset += out_bytes;

                free(start_padd);
            }

            return S_OK;
        }

        return E_FAIL;
    }

    return S_OK;
}

/**
 * Closes the LAME encoder. If the encoder writes a LAME tag and @p pStream is
 * not NULL, the function first writes the final LAME tag into the stream.
 */
HRESULT CEncoder::Close(IStream* pStream)
{
	CAutoLock l(&m_lock);
    if (pgf)
    {
		if(lame_get_bWriteVbrTag(pgf) && pStream)
		{
			updateLameTagFrame(pStream);
		}

        lame_close(pgf);
        pgf = NULL;
    }

    return S_OK;
}

/**
 * Encodes PCM samples into the output buffer.
 *
 * @param pdata      the 16 bit PCM samples.
 * @param data_size  the size of @p pdata in bytes.
 * @return the number of bytes of @p pdata that were used. -1 on an error.
 */
int CEncoder::Encode(const short * pdata, int data_size)
{
    CAutoLock l(&m_lock);

    if (!pgf || !m_outFrameBuf || !pdata || data_size < 0 || (data_size & (sizeof(short) - 1)))
        return -1;

    // some data left in the buffer, shift to start
    if (m_outReadOffset > 0)
    {
        if (m_outOffset > m_outReadOffset)
            memmove(m_outFrameBuf, m_outFrameBuf + m_outReadOffset, m_outOffset - m_outReadOffset);

        m_outOffset -= m_outReadOffset;
    }

    m_outReadOffset = 0;



    m_bFinished = FALSE;

    int bytes_processed = 0;
    int const nch = lame_get_num_channels(pgf);

    while (1)
    {
        int nsamples = (data_size - bytes_processed) / (sizeof(short) * nch);

        if (nsamples <= 0)
            break;

        if (nsamples > 1152)
            nsamples = 1152;

        if (m_outOffset >= OUT_BUFFER_MAX)
            break;

        int out_bytes = 0;

        if (nch == 2)
            out_bytes = lame_encode_buffer_interleaved(
                                            pgf,
                                            (short *)(pdata + (bytes_processed / sizeof(short))),
                                            nsamples,
                                            m_outFrameBuf + m_outOffset,
                                            OUT_BUFFER_SIZE - m_outOffset);
        else
            out_bytes = lame_encode_buffer(
                                            pgf,
                                            pdata + (bytes_processed / sizeof(short)),
                                            pdata + (bytes_processed / sizeof(short)),
                                            nsamples,
                                            m_outFrameBuf + m_outOffset,
                                            OUT_BUFFER_SIZE - m_outOffset);

        if (out_bytes < 0)
            return -1;

        m_outOffset     += out_bytes;
        bytes_processed += nsamples * nch * sizeof(short);
    }

    return bytes_processed;
}

/**
 * Flushes the samples that the encoder still holds into the output buffer.
 */
HRESULT CEncoder::Finish()
{
    CAutoLock l(&m_lock);

    if (!pgf || !m_outFrameBuf || (m_outOffset >= OUT_BUFFER_MAX))
        return E_FAIL;

    m_outOffset += lame_encode_flush(pgf, m_outFrameBuf + m_outOffset, OUT_BUFFER_SIZE - m_outOffset);

    m_bFinished = TRUE;

    return S_OK;
}


/** \brief Values of the MPEG audio frame header fields that getFrameLength() tests. */
enum {
    MPEG_VERSION_RESERVED = 1,  /**< the version field value that is reserved */
    MPEG_VERSION_1 = 3,         /**< the version field value of MPEG-1 */
    LAYER_III = 1,              /**< the layer field value of Layer III */
    BITRATE_FREE = 0,           /**< the bitrate index of a free format stream */
    BITRATE_RESERVED = 15,      /**< the bitrate index that is reserved */
    SRATE_RESERVED = 3,         /**< the sample rate index that is reserved */
    EMPHASIS_RESERVED = 2       /**< the emphasis value that is reserved */
};

/** \brief Samples in one Layer III frame of MPEG-1, and of MPEG-2 and MPEG-2.5. */
enum { MPEG1_SAMPLES_PER_FRAME = 1152, MPEG2_SAMPLES_PER_FRAME = 576 };

/**
 * \brief The library's MPEG version for each value of the header's version
 *        field: 0 is MPEG-2.5, 1 is reserved, 2 is MPEG-2, 3 is MPEG-1.
 */
static const int lame_version[4] = { 2, -1, 0, 1 };

static int getFrameLength(const unsigned char * pdata)
{
    if (!pdata || pdata[0] != 0xff || (pdata[1] & 0xe0) != 0xe0)
        return -1;

    int version_id      = (pdata[1] & 0x18) >> 3;
    int layer           = (pdata[1] & 0x06) >> 1;
    int bitrate_id      = (pdata[2] & 0xF0) >> 4;
    int sample_rate_id  = (pdata[2] & 0x0C) >> 2;
    int padding         = (pdata[2] & 0x02) >> 1;
    int emphasis        =  pdata[3] & 0x03;

    if (version_id      != MPEG_VERSION_RESERVED &&
        layer           == LAYER_III &&
        bitrate_id      != BITRATE_FREE &&
        bitrate_id      != BITRATE_RESERVED &&
        sample_rate_id  != SRATE_RESERVED &&
        emphasis        != EMPHASIS_RESERVED)
    {
        int spf         = (version_id == MPEG_VERSION_1) ? MPEG1_SAMPLES_PER_FRAME : MPEG2_SAMPLES_PER_FRAME;
        int sample_rate = lame_get_samplerate(lame_version[version_id], sample_rate_id);
        int bitrate     = BitRateValue(version_id != MPEG_VERSION_1, bitrate_id - 1) * 1000;

        return (bitrate * spf) / (8 * sample_rate) + padding;
    }

    return -1;
}


int CEncoder::GetFrame(const unsigned char ** pframe)
{
    if (!pgf || !m_outFrameBuf || !pframe)
        return -1;

	while ((m_outOffset - m_outReadOffset) > 4)
    {
        int frame_length = getFrameLength(m_outFrameBuf + m_outReadOffset);

        if (frame_length < 0)
        {
            m_outReadOffset++;
        }
        else if (frame_length <= (m_outOffset - m_outReadOffset))
        {
            *pframe = m_outFrameBuf + m_outReadOffset;
            m_outReadOffset += frame_length;

            m_frameCount++;

            // don't deliver the first and the last frames
            if (m_frameCount != 1 && !(m_bFinished && (m_outOffset - m_outReadOffset) < 5))
                return frame_length;
        }
        else
            break;
    }

    return 0;
}

/**
 * Returns the encoded data that is ready, in a size that is a multiple of
 * @p cbAlign. After Finish(), it returns all remaining data, and rounds
 * @p *piBufferSize up to a multiple of @p cbAlign.
 *
 * @return the number of bytes in the block. -1 on an error.
 */
int CEncoder::GetBlockAligned(const unsigned char ** pblock, int* piBufferSize, const long& cbAlign)
{
	ASSERT(piBufferSize);
    if (!pgf || !m_outFrameBuf || !pblock)
        return -1;

	int iBlockLen = m_outOffset - m_outReadOffset;
	ASSERT(iBlockLen >= 0);
	
	if(!m_bFinished)
	{
		if(cbAlign > 0)
			iBlockLen-=iBlockLen%cbAlign;
		*piBufferSize = iBlockLen;
	}
	else
	{
		if(cbAlign && iBlockLen%cbAlign)
		{
			*piBufferSize = iBlockLen + cbAlign - iBlockLen%cbAlign;
		}
		else
		{
			*piBufferSize = iBlockLen;
		}
	}

	if(iBlockLen) {
		*pblock = m_outFrameBuf + m_outReadOffset;
		m_outReadOffset+=iBlockLen;
	}

	return iBlockLen;
}

HRESULT CEncoder::maybeSyncWord(IStream *pStream)
{
	HRESULT hr = S_OK;
    unsigned char mp3_frame_header[4];
	ULONG nbytes;
	if(FAILED(hr = pStream->Read(mp3_frame_header, sizeof(mp3_frame_header), &nbytes)))
		return hr;
	
    if ( nbytes != sizeof(mp3_frame_header) ) {
        return E_FAIL;
    }
    if ( !lametag_is_frame_sync(mp3_frame_header) ) {
        return S_FALSE; /* doesn't look like a sync word */
    }
    return S_OK;
}

HRESULT CEncoder::skipId3v2(IStream *pStream, size_t lametag_frame_size)
{
	HRESULT hr = S_OK;
    ULONG  nbytes;
    size_t  id3v2TagSize = 0;
    unsigned char id3v2Header[ID3V2_HEADER_BYTES];
	LARGE_INTEGER seekTo;

    /* seek to the beginning of the stream */
	seekTo.QuadPart = 0;
	if (FAILED(hr = pStream->Seek(seekTo,  STREAM_SEEK_SET, NULL))) {
        return hr;  /* not seekable, abort */
    }
    /* read 10 bytes in case there's an ID3 version 2 header here */
	hr = pStream->Read(id3v2Header, sizeof(id3v2Header), &nbytes);
    if (FAILED(hr))
		return hr;
	if(nbytes != sizeof(id3v2Header)) {
        return E_FAIL;  /* not readable, maybe opened Write-Only */
    }
    id3v2TagSize = (size_t) lametag_audio_offset(id3v2Header);
    /* Seek to the beginning of the audio stream */
	seekTo.QuadPart = id3v2TagSize;
	if (FAILED(hr = pStream->Seek(seekTo, STREAM_SEEK_SET, NULL))) {
        return hr;
    }
    if (S_OK != (hr = maybeSyncWord(pStream))) {
		return SUCCEEDED(hr)?E_FAIL:hr;
    }
	seekTo.QuadPart = id3v2TagSize+lametag_frame_size;
	if (FAILED(hr = pStream->Seek(seekTo, STREAM_SEEK_SET, NULL))) {
        return hr;
    }
    if (S_OK != (hr = maybeSyncWord(pStream))) {
        return SUCCEEDED(hr)?E_FAIL:hr;
    }
    /* OK, it seems we found our LAME-Tag/Xing frame again */
    /* Seek to the beginning of the audio stream */
	seekTo.QuadPart = id3v2TagSize;
	if (FAILED(hr = pStream->Seek(seekTo, STREAM_SEEK_SET, NULL))) {
        return hr;
    }
    return S_OK;
}

/**
 * Writes the final LAME tag frame over the first frame of the stream, after
 * any ID3v2 tag.
 */
HRESULT CEncoder::updateLameTagFrame(IStream* pStream)
{
	HRESULT hr = S_OK;
	size_t n = lame_get_lametag_frame( pgf, 0, 0 ); /* ask for bufer size */

    if ( n > 0 )
    {
        unsigned char* buffer = 0;
        ULONG m = n;

        if ( FAILED(hr = skipId3v2(pStream, n) )) 
        {
            /*DispErr( "Error updating LAME-tag frame:\n\n"
                     "can't locate old frame\n" );*/
            return hr;
        }

        buffer = (unsigned char*)malloc( n );

        if ( buffer == 0 ) 
        {
            /*DispErr( "Error updating LAME-tag frame:\n\n"
                     "can't allocate frame buffer\n" );*/
            return E_OUTOFMEMORY;
        }

        /* Put it all to disk again */
        n = lame_get_lametag_frame( pgf, buffer, n );
        if ( n > 0 ) 
        {
			hr = pStream->Write(buffer, n, &m);        
        }
        free( buffer );

        if ( m != n ) 
        {
            /*DispErr( "Error updating LAME-tag frame:\n\n"
                     "couldn't write frame into file\n" );*/
			return E_FAIL;
        }
    }
    return hr;
}
