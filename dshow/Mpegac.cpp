/*
 *  LAME MP3 encoder for DirectShow
 *  DirectShow filter implementation
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
#include <olectl.h>
#include <initguid.h>
//#include <olectlid.h>
#include "uids.h"
#include "iaudioprops.h"
#include "mpegac.h"
#include "resource.h"

#include "PropPage.h"
#include "PropPage_adv.h"
#include "aboutprp.h"

#include "Encoder.h"
#include "Reg.h"

#ifndef _INC_MMREG
#include <mmreg.h>
#endif

// default parameters
#define         DEFAULT_STEREO_MODE         JOINT_STEREO
#define         DEFAULT_FORCE_MS            0
#define         DEFAULT_MODE_FIXED          0
#define         DEFAULT_ENFORCE_MIN         0
#define         DEFAULT_VOICE               0
#define         DEFAULT_KEEP_ALL_FREQ       0
#define         DEFAULT_STRICT_ISO          0
#define         DEFAULT_DISABLE_SHORT_BLOCK 0
#define         DEFAULT_XING_TAG            0
#define         DEFAULT_SAMPLE_RATE         44100
#define         DEFAULT_BITRATE             128
#define         DEFAULT_VARIABLE            0
#define         DEFAULT_CRC                 0
#define         DEFAULT_FORCE_MONO          0
#define         DEFAULT_SET_DURATION        1
#define         DEFAULT_SAMPLE_OVERLAP      1
#define         DEFAULT_COPYRIGHT           0
#define         DEFAULT_ORIGINAL            0
#define         DEFAULT_VARIABLEMIN         80
#define         DEFAULT_VARIABLEMAX         160
#define         DEFAULT_ENCODING_QUALITY    5
#define         DEFAULT_VBR_QUALITY         4
#define         DEFAULT_PES                 0

#define         DEFAULT_FILTER_MERIT        MERIT_DO_NOT_USE                // Standard compressor merit value

#define GET_DATARATE(kbps) (kbps * 1000 / 8)
#define GET_FRAMELENGTH(bitrate, sample_rate) ((WORD)(((sample_rate < 32000 ? 72000 : 144000) * (bitrate))/(sample_rate)))
#define DECLARE_PTR(type, ptr, expr) type* ptr = (type*)(expr);

/*  Registration setup stuff */
//  Setup data


AMOVIESETUP_MEDIATYPE sudMpgInputType[] =
{
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_PCM }
};
AMOVIESETUP_MEDIATYPE sudMpgOutputType[] =
{
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_MPEG1AudioPayload },
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_MPEG2_AUDIO },
    { &MEDIATYPE_Audio, &MEDIASUBTYPE_MP3 },
    { &MEDIATYPE_Stream, &MEDIASUBTYPE_MPEG1Audio }
};

AMOVIESETUP_PIN sudMpgPins[] =
{
    { L"PCM Input",
      FALSE,                               // bRendered
      FALSE,                               // bOutput
      FALSE,                               // bZero
      FALSE,                               // bMany
      &CLSID_NULL,                         // clsConnectsToFilter
      NULL,                                // ConnectsToPin
      NUMELMS(sudMpgInputType),            // Number of media types
      sudMpgInputType
    },
    { L"MPEG Output",
      FALSE,                               // bRendered
      TRUE,                                // bOutput
      FALSE,                               // bZero
      FALSE,                               // bMany
      &CLSID_NULL,                         // clsConnectsToFilter
      NULL,                                // ConnectsToPin
      NUMELMS(sudMpgOutputType),           // Number of media types
      sudMpgOutputType
    }
};

AMOVIESETUP_FILTER sudMpgAEnc =
{
    &CLSID_LAMEDShowFilter,
    L"LAME Audio Encoder",
    DEFAULT_FILTER_MERIT,                  // Standard compressor merit value
    NUMELMS(sudMpgPins),                   // 2 pins
    sudMpgPins
};

/*****************************************************************************/
// COM Global table of objects in this dll
static WCHAR g_wszName[] = L"LAME Audio Encoder";
CFactoryTemplate g_Templates[] = 
{
  { g_wszName, &CLSID_LAMEDShowFilter, CMpegAudEnc::CreateInstance, NULL, &sudMpgAEnc },
  { L"LAME Audio Encoder Property Page", &CLSID_LAMEDShow_PropertyPage, CMpegAudEncPropertyPage::CreateInstance},
  { L"LAME Audio Encoder Property Page", &CLSID_LAMEDShow_PropertyPageAdv, CMpegAudEncPropertyPageAdv::CreateInstance},
  { L"LAME Audio Encoder About", &CLSID_LAMEDShow_About, CMAEAbout::CreateInstance}
};
// Count of objects listed in g_cTemplates
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);



////////////////////////////////////////////
// Declare the DirectShow filter information.

// Used by IFilterMapper2() in the call to DllRegisterServer()
// to register the filter in the CLSID_AudioCompressorCategory.
REGFILTER2 rf2FilterReg = {
    1,                     // Version number.
    DEFAULT_FILTER_MERIT,  // Merit. This should match the merit specified in the AMOVIESETUP_FILTER definition
    NUMELMS(sudMpgPins),   // Number of pins.
    sudMpgPins             // Pointer to pin information.
};

STDAPI DllRegisterServer(void)
{
    HRESULT hr = AMovieDllRegisterServer2(TRUE);
    if (FAILED(hr)) {
        return hr;
    }

    IFilterMapper2 *pFM2 = NULL;
    hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void **)&pFM2);
    if (SUCCEEDED(hr)) {
        hr = pFM2->RegisterFilter(
            CLSID_LAMEDShowFilter,           // Filter CLSID. 
            g_wszName,                       // Filter name.
            NULL,                            // Device moniker. 
            &CLSID_AudioCompressorCategory,  // Audio compressor category.
            g_wszName,                       // Instance data.
            &rf2FilterReg                    // Filter information.
            );
        pFM2->Release();
    }
    return hr;
}

STDAPI DllUnregisterServer()
{
    HRESULT hr = AMovieDllRegisterServer2(FALSE);
    if (FAILED(hr)) {
        return hr;
    }

    IFilterMapper2 *pFM2 = NULL;
    hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void **)&pFM2);
    if (SUCCEEDED(hr)) {
        hr = pFM2->UnregisterFilter(&CLSID_AudioCompressorCategory, g_wszName, CLSID_LAMEDShowFilter);
        pFM2->Release();
    }
    return hr;
}


//  The base classes provide DllEntryPoint(), which sets up the class factory
//  table, but no DllMain().  The C runtime supplies a do-nothing DllMain() of
//  its own when a DLL declares none, so without this forwarder the filter
//  links and DllEntryPoint() is simply never called.  Letting the runtime pick
//  the entry point is what makes the runtime initialise at all, so the
//  arrangement has to be this way round: the runtime first, the filter second.

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE hModule, ULONG dwReason, LPVOID lpReserved);

BOOL WINAPI
DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
    return DllEntryPoint(hModule, dwReason, lpReserved);
}


CUnknown *CMpegAudEnc::CreateInstance(LPUNKNOWN lpunk, HRESULT *phr) 
{
    CMpegAudEnc *punk = new CMpegAudEnc(lpunk, phr);
    if (punk == NULL) 
        *phr = E_OUTOFMEMORY;
    return punk;
}

CMpegAudEnc::CMpegAudEnc(LPUNKNOWN lpunk, HRESULT *phr)
 :  CTransformFilter(NAME("LAME Audio Encoder"), lpunk, CLSID_LAMEDShowFilter),
    CPersistStream(lpunk, phr)
{
    // ENCODER OUTPUT PIN
    // Override the output pin with our own which will implement the IAMStreamConfig Interface
    CTransformOutputPin *pOut = new CMpegAudEncOutPin( this, phr );
    if (pOut == NULL) {
        *phr = E_OUTOFMEMORY;
        return;
    }
    else if (FAILED(*phr)) {             // A failed return code should delete the object
        delete pOut;
        return;
    }
    m_pOutput = pOut;

    // ENCODER INPUT PIN
    // Since we've created our own output pin we must also create
    // the input pin ourselves because the CTransformFilter base class 
    // will create an extra output pin if the input pin wasn't created.        
    CTransformInputPin *pIn = new CTransformInputPin(NAME("LameEncoderInputPin"),
                                                     this,              // Owner filter
                                                     phr,               // Result code
                                                     L"Input");         // Pin name

    if (pIn == NULL) {
        *phr = E_OUTOFMEMORY;
        return;
    }
    else if (FAILED(*phr)) {             // A failed return code should delete the object
        delete pIn;
        return;
    }
    m_pInput = pIn;


    MPEG_ENCODER_CONFIG mec;
    ReadPresetSettings(&mec);
    m_Encoder.SetOutputType(mec);

    m_CapsNum = 0;
    m_hasFinished = TRUE;
    m_bStreamOutput = FALSE;
    m_currentMediaTypeIndex = 0;
}

CMpegAudEnc::~CMpegAudEnc(void)
{
}

LPAMOVIESETUP_FILTER CMpegAudEnc::GetSetupData()
{
    return &sudMpgAEnc;
}


HRESULT CMpegAudEnc::Receive(IMediaSample * pSample)
{
    CAutoLock lock(&m_cs);

    if (!pSample)
        return S_OK;

    BYTE * pSourceBuffer = NULL;

    if (pSample->GetPointer(&pSourceBuffer) != S_OK || !pSourceBuffer)
        return S_OK;

    long sample_size = pSample->GetActualDataLength();

    REFERENCE_TIME rtStart, rtStop;
    BOOL gotValidTime = (pSample->GetTime(&rtStart, &rtStop) != VFW_E_SAMPLE_TIME_NOT_SET);

    if (sample_size <= 0 || pSourceBuffer == NULL || m_hasFinished || (gotValidTime && rtStart < 0))
        return S_OK;

    if (gotValidTime)
    {
        if (m_rtStreamTime < 0)
        {
            m_rtStreamTime = rtStart;
            m_rtEstimated = rtStart;
        }
        else
        {
            resync_point_t * sync = m_sync + m_sync_in_idx;

            if (sync->applied)
            {
                REFERENCE_TIME rtGap = rtStart - m_rtEstimated;

                // if old sync data is applied and gap is greater than 1 ms
                // then make a new synchronization point
                if (rtGap > 10000 || (m_allowOverlap && rtGap < -10000))
                {
                    sync->sample    = m_samplesIn;
                    sync->delta     = rtGap;
                    sync->applied   = FALSE;

                    m_rtEstimated  += sync->delta;

                    if (m_sync_in_idx < (RESYNC_COUNT - 1))
                        m_sync_in_idx++;
                    else
                        m_sync_in_idx = 0;
                }
            }
        }
    }

    m_rtEstimated   += (LONGLONG)(m_bytesToDuration * sample_size);
    m_samplesIn     += sample_size / m_bytesPerSample;

    while (sample_size > 0)
    {
        int bytes_processed = m_Encoder.Encode((short *)pSourceBuffer, sample_size);

        if (bytes_processed <= 0)
            return S_OK;

        FlushEncodedSamples();

        sample_size     -= bytes_processed;
        pSourceBuffer   += bytes_processed;
    }

    return S_OK;
}




HRESULT CMpegAudEnc::FlushEncodedSamples()
{
    IMediaSample * pOutSample = NULL;
    BYTE * pDst = NULL;

    if(m_bStreamOutput)
    {
        HRESULT hr = S_OK;
        const unsigned char *   pblock      = NULL;
        int iBufferSize;
        int iBlockLength = m_Encoder.GetBlockAligned(&pblock, &iBufferSize, m_cbStreamAlignment);

        if(!iBlockLength)
            return S_OK;

        hr = m_pOutput->GetDeliveryBuffer(&pOutSample, NULL, NULL, 0);
        if (hr == S_OK && pOutSample)
        {
            hr = pOutSample->GetPointer(&pDst);
            if (hr == S_OK && pDst)
            {
                CopyMemory(pDst, pblock, iBlockLength);
                if (iBufferSize > pOutSample->GetSize())
                    iBufferSize = pOutSample->GetSize();
                if (iBufferSize > iBlockLength)
                    ZeroMemory(pDst + iBlockLength, iBufferSize - iBlockLength);
                REFERENCE_TIME rtEndPos = m_rtBytePos + iBufferSize;
                EXECUTE_ASSERT(S_OK == pOutSample->SetTime(&m_rtBytePos, &rtEndPos));
                pOutSample->SetActualDataLength(iBufferSize);
                m_rtBytePos += iBlockLength;
                m_pOutput->Deliver(pOutSample);
            }
            pOutSample->Release();
        }
        return S_OK;
    }

    if (m_rtStreamTime < 0)
        m_rtStreamTime = 0;

    while (1)
    {
        const unsigned char *   pframe      = NULL;
        int                     frame_size  = m_Encoder.GetFrame(&pframe);

        if (frame_size <= 0 || !pframe)
            break;

        if (!m_sync[m_sync_out_idx].applied && m_sync[m_sync_out_idx].sample <= m_samplesOut)
        {
            m_rtStreamTime += m_sync[m_sync_out_idx].delta;
            m_sync[m_sync_out_idx].applied = TRUE;

            if (m_sync_out_idx < (RESYNC_COUNT - 1))
                m_sync_out_idx++;
            else
                m_sync_out_idx = 0;
        }

        REFERENCE_TIME rtStart = m_rtStreamTime;
        REFERENCE_TIME rtStop = rtStart + m_rtFrameTime;

        HRESULT hr = m_pOutput->GetDeliveryBuffer(&pOutSample, NULL, NULL, 0);
        if (hr == S_OK && pOutSample)
        {
            hr = pOutSample->GetPointer(&pDst);
            if (hr == S_OK && pDst)
            {
                CopyMemory(pDst, pframe, frame_size);
                pOutSample->SetActualDataLength(frame_size);
                pOutSample->SetSyncPoint(TRUE);
                pOutSample->SetTime(&rtStart, m_setDuration ? &rtStop : NULL);
                m_pOutput->Deliver(pOutSample);
            }
            pOutSample->Release();
        }
        m_samplesOut += m_samplesPerFrame;
        m_rtStreamTime = rtStop;
    }

    return S_OK;
}


/**
 * Prepares the filter for new data: computes the frame size and frame time for
 * the output sample rate, resets the time stamps, and initializes the encoder.
 */
HRESULT CMpegAudEnc::StartStreaming()
{
    WAVEFORMATEX * pwfxIn  = (WAVEFORMATEX *) m_pInput->CurrentMediaType().Format();

    m_bytesPerSample    = pwfxIn->nChannels * sizeof(short);
    DWORD dwOutSampleRate;
    if(MEDIATYPE_Stream == m_pOutput->CurrentMediaType().majortype)
    {
        MPEG_ENCODER_CONFIG mcfg;
        if(FAILED(m_Encoder.GetOutputType(&mcfg)))
            return E_FAIL;
        dwOutSampleRate = mcfg.dwSampleRate;
    }
    else
    {
        dwOutSampleRate = ((WAVEFORMATEX *) m_pOutput->CurrentMediaType().Format())->nSamplesPerSec;
    }
    m_samplesPerFrame   = (dwOutSampleRate >= 32000) ? 1152 : 576;
    m_rtFrameTime = MulDiv(10000000, m_samplesPerFrame, dwOutSampleRate);
    m_samplesIn = m_samplesOut = 0;
    m_rtStreamTime = -1;
    m_rtBytePos = 0;

    // initialize encoder
    HRESULT hr = m_Encoder.Init();
    if (FAILED(hr))
        return hr;

    m_hasFinished   = FALSE;

    for (int i = 0; i < RESYNC_COUNT; i++)
    {
        m_sync[i].sample   = 0;
        m_sync[i].delta    = 0;
        m_sync[i].applied  = TRUE;
    }

    m_sync_in_idx = 0;
    m_sync_out_idx = 0;

    get_SetDuration(&m_setDuration);
    get_SampleOverlap(&m_allowOverlap);

    return S_OK;
}


HRESULT CMpegAudEnc::StopStreaming()
{
  IStream *pStream = NULL;
    if(m_bStreamOutput && m_pOutput->IsConnected() != FALSE)
    {
        IPin * pDwnstrmInputPin = m_pOutput->GetConnected();
        if(pDwnstrmInputPin && FAILED(pDwnstrmInputPin->QueryInterface(IID_IStream, (LPVOID*)(&pStream))))
        {
            pStream = NULL;
        }
    }
    

    m_Encoder.Close(pStream);

    if(pStream)
        pStream->Release();

    return S_OK;
}


/**
 * Ends the stream: flushes the encoder, delivers the remaining encoded data,
 * and closes the encoder. For stream output, it also sets the size of the
 * output stream, and the encoder writes the final LAME tag.
 */
HRESULT CMpegAudEnc::EndOfStream()
{
    CAutoLock lock(&m_cs);

    // Flush data
    m_Encoder.Finish();
    FlushEncodedSamples();

    IStream *pStream = NULL;
    if(m_bStreamOutput && m_pOutput->IsConnected() != FALSE)
    {
        IPin * pDwnstrmInputPin = m_pOutput->GetConnected();
        if(pDwnstrmInputPin)
        {
            if(FAILED(pDwnstrmInputPin->QueryInterface(IID_IStream, (LPVOID*)(&pStream))))
            {
                pStream = NULL;	
            }
        }
    }

    if(pStream)
    {
        ULARGE_INTEGER size;
        size.QuadPart = m_rtBytePos;
        pStream->SetSize(size);	
    }

    m_Encoder.Close(pStream);

    if(pStream)
        pStream->Release();

    m_hasFinished = TRUE;

    return CTransformFilter::EndOfStream();
}


/**
 * Starts a flush. The encoded data that is left is sent downstream. For stream
 * output, the size of the output stream is set. Then the stream time and the
 * byte position start again at the beginning.
 */
HRESULT CMpegAudEnc::BeginFlush()
{
    HRESULT hr = CTransformFilter::BeginFlush();

    if (SUCCEEDED(hr))
    {
        CAutoLock lock(&m_cs);

        // Flush data
        m_Encoder.Finish();
        FlushEncodedSamples();

        IStream *pStream = NULL;
        if(m_bStreamOutput && m_pOutput->IsConnected() != FALSE)
        {
            IPin * pDwnstrmInputPin = m_pOutput->GetConnected();
            if(pDwnstrmInputPin && SUCCEEDED(pDwnstrmInputPin->QueryInterface(IID_IStream, (LPVOID*)(&pStream))))
            {
                ULARGE_INTEGER size;
                size.QuadPart = m_rtBytePos;
                pStream->SetSize(size);	
                pStream->Release();
            }
        }
        m_rtStreamTime = -1;
        m_rtBytePos = 0;
    }

    return hr;
}



/**
 * Called when a pin connects with a media type.
 *
 * For the input pin, the function passes the input format to the encoder and
 * lists the output formats that this input supports. For the output pin, it
 * reconnects the input pin if the current input type does not fit the new
 * output type.
 */
HRESULT CMpegAudEnc::SetMediaType(PIN_DIRECTION direction, const CMediaType * pmt)
{
    if (pmt == NULL)
        return E_POINTER;

    HRESULT hr = S_OK;

    if (direction == PINDIR_INPUT)
    {
        if (*pmt->FormatType() != FORMAT_WaveFormatEx)
        return VFW_E_INVALIDMEDIATYPE;

        if (pmt->FormatLength() < sizeof(WAVEFORMATEX))
            return VFW_E_INVALIDMEDIATYPE;

        DbgLog((LOG_TRACE,1,TEXT("CMpegAudEnc::SetMediaType(), direction = PINDIR_INPUT")));

        // Pass input media type to encoder
        m_Encoder.SetInputType((LPWAVEFORMATEX)pmt->Format());

        WAVEFORMATEX * pwfx = (WAVEFORMATEX *)pmt->Format();
        DWORD sample_rate = 44100;

        if (pwfx) {
            sample_rate = pwfx->nSamplesPerSec;
            m_bytesToDuration = (float)1.e7 / (float)(pwfx->nChannels * sizeof(short) * pwfx->nSamplesPerSec);
        } else {
            m_bytesToDuration = 0.0;
        }
        // Parse the encoder output capabilities into the subset of capabilities that are supported 
        // for the current input format. This listing will be utilized by the IAMStreamConfig Interface.
        LoadOutputCapabilities(sample_rate);

        Reconnect();
    }
    else if (direction == PINDIR_OUTPUT)
    {
        // Before we set the output type, we might need to reconnect 
        // the input pin with a new type.
        if (m_pInput && m_pInput->IsConnected()) 
        {
            // Check if the current input type is compatible.
            hr = CheckTransform(&m_pInput->CurrentMediaType(), &m_pOutput->CurrentMediaType());
            if (FAILED(hr)) {
                // We need to reconnect the input pin. 
                // Note: The CheckMediaType method has already called QueryAccept on the upstream filter. 
                hr = m_pGraph->Reconnect(m_pInput);
                return hr;
            }
        }

//        WAVEFORMATEX wfIn;
//        m_Encoder.GetInputType(&wfIn);

//        if (wfIn.nSamplesPerSec %
//            ((LPWAVEFORMATEX)pmt->Format())->nSamplesPerSec != 0)
//            return VFW_E_TYPE_NOT_ACCEPTED;
    }

    return hr;
}

/**
 * Checks whether the filter accepts an input media type: uncompressed audio in
 * a WAVEFORMATEX format that the encoder supports.
 */
HRESULT CMpegAudEnc::CheckInputType(const CMediaType* mtIn)
{
    if (*mtIn->Type() == MEDIATYPE_Audio && *mtIn->FormatType() == FORMAT_WaveFormatEx)
        if (mtIn->FormatLength() >= sizeof(WAVEFORMATEX))
            if (mtIn->IsTemporalCompressed() == FALSE)
                return m_Encoder.SetInputType((LPWAVEFORMATEX)mtIn->Format(), true);

    return E_INVALIDARG;
}

/**
 * Checks whether the filter can convert the input media type into the output
 * media type.
 */
HRESULT CMpegAudEnc::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut)
{
    if(MEDIATYPE_Stream != mtOut->majortype)
    {
        if (*mtOut->FormatType() != FORMAT_WaveFormatEx)
            return VFW_E_INVALIDMEDIATYPE;

        if (mtOut->FormatLength() < sizeof(WAVEFORMATEX))
            return VFW_E_INVALIDMEDIATYPE;

        MPEG_ENCODER_CONFIG	mec;
        if(FAILED(m_Encoder.GetOutputType(&mec)))
            return S_OK;

        if (mec.dwSampleRate == 0 ||
            ((LPWAVEFORMATEX)mtIn->Format())->nSamplesPerSec % mec.dwSampleRate != 0)
            return S_OK;

        if (mec.dwSampleRate != ((LPWAVEFORMATEX)mtOut->Format())->nSamplesPerSec)
            return VFW_E_TYPE_NOT_ACCEPTED;

        return S_OK;
    }
    else if(mtOut->subtype == MEDIASUBTYPE_MPEG1Audio)
        return S_OK;

    return VFW_E_TYPE_NOT_ACCEPTED;
}

/**
 * Sets the number and the size of the output buffers.
 */
HRESULT CMpegAudEnc::DecideBufferSize(
                        IMemAllocator*		  pAllocator,
                        ALLOCATOR_PROPERTIES* pProperties)
{
    HRESULT hr = S_OK;

    if(m_bStreamOutput)
    {
        // A block the encoder's buffer never fills would never be delivered.
        if (pProperties->cbAlign > OUT_BUFFER_MAX)
            return VFW_E_BADALIGN;
        m_cbStreamAlignment = pProperties->cbAlign;
    }

    if (pProperties->cBuffers == 0) pProperties->cBuffers = 1;  // If downstream filter didn't suggest a buffer count then default to 1
    pProperties->cbBuffer = OUT_BUFFER_SIZE;
    //
    
    ASSERT(pProperties->cbBuffer);
    
    ALLOCATOR_PROPERTIES Actual;
    hr = pAllocator->SetProperties(pProperties,&Actual);
    if(FAILED(hr))
        return hr;

    if (Actual.cbBuffer < pProperties->cbBuffer ||
        Actual.cBuffers < pProperties->cBuffers) 
    {// can't use this allocator
        return E_INVALIDARG;
    }
    return S_OK;
}

/**
 * Calls CMpegAudEncOutPin::GetMediaType().
 */
HRESULT CMpegAudEnc::GetMediaType(int iPosition, CMediaType *pMediaType)
{
    DbgLog((LOG_TRACE,1,TEXT("CMpegAudEnc::GetMediaType()")));

    return m_pOutput->GetMediaType(iPosition, pMediaType);
}

/**
 * Updates the output media type after the encoder settings were changed by
 * hand, and reconnects the output pin if the type changed. It works only while
 * the filter is stopped and the output pin is connected.
 */
HRESULT CMpegAudEnc::Reconnect()
{
    HRESULT hr = S_FALSE;

    if (m_pOutput && m_pOutput->IsConnected() && m_State == State_Stopped)
    {
        MPEG_ENCODER_CONFIG mec;
        hr = m_Encoder.GetOutputType(&mec);

        if ((hr = m_Encoder.SetOutputType(mec)) == S_OK)
        {
            // Create an updated output MediaType using the current encoder settings
            CMediaType cmt;
            cmt.InitMediaType();
            m_pOutput->GetMediaType(m_currentMediaTypeIndex, &cmt);

            // If the updated MediaType matches the current output MediaType no reconnect is needed
            if (m_pOutput->CurrentMediaType() == cmt) return S_OK;

            // Attempt to reconnect the output pin using the updated MediaType
            if (S_OK == (hr = m_pOutput->GetConnected()->QueryAccept(&cmt))) {
                hr = m_pOutput->SetMediaType(&cmt);
                if ( FAILED(hr) ) { return(hr); }

                hr = m_pGraph->Reconnect(m_pOutput);
            }
            else
                hr = m_pOutput->SetMediaType(&cmt);
        }
    }

    return hr;
}

/**
 * Lists the CBR output formats that the given input sample rate supports. The
 * IAMStreamConfig interface offers this list.
 */
void CMpegAudEnc::LoadOutputCapabilities(DWORD sample_rate)
{
    m_CapsNum = 0;

    // Clear out any existing output capabilities
    ZeroMemory(OutputCaps, sizeof(OutputCaps));

    // The rates of the standard whose multiple the input rate is, from the
    // library's tables: the 48, 44.1 and 32 kHz families in turn, each as
    // MPEG-1, MPEG-2 and MPEG-2.5, every bitrate of the version, highest
    // first. The MPEG-2.5 bitrates stop at 64 kbit/s.
    const int rate_index[3] = { 1, 0, 2 };   // 48000, 44100, 32000 and their halves
    const int version[3] = { 1, 0, 2 };      // MPEG-1, MPEG-2, MPEG-2.5
    for (int r = 0; r < 3; r++) {
        for (int v = 0; v < 3; v++) {
            int const rate = lame_get_samplerate(version[v], rate_index[r]);
            if (rate <= 0 || 0 != sample_rate % rate)
                continue;
            for (int i = 14; i >= 1; i--) {
                int const kbps = lame_get_bitrate(version[v], i);
                if (kbps <= 0)
                    continue;
                // Don't overrun the capabilities array: the last writable
                // slot is MAX_IAMSTREAMCONFIG_CAPS - 1.
                if (m_CapsNum >= (int)MAX_IAMSTREAMCONFIG_CAPS)
                    return;
                OutputCaps[m_CapsNum].nSampleRate = (DWORD) rate;
                OutputCaps[m_CapsNum].nBitRate = (DWORD) kbps;
                m_CapsNum++;
            }
        }
    }
}


/**
 * The fields of the encoder configuration that the registry keeps as they
 * are, with the name of their registry value and their default. The VBR
 * switch and the channel mode are read and written on their own.
 */
static const struct {
    LPCTSTR name;
    DWORD MPEG_ENCODER_CONFIG::*field;
    DWORD dflt;
} registry_fields[] = {
    { VALUE_BITRATE, &MPEG_ENCODER_CONFIG::dwBitrate, DEFAULT_BITRATE },
    { VALUE_VARIABLEMIN, &MPEG_ENCODER_CONFIG::dwVariableMin, DEFAULT_VARIABLEMIN },
    { VALUE_VARIABLEMAX, &MPEG_ENCODER_CONFIG::dwVariableMax, DEFAULT_VARIABLEMAX },
    { VALUE_QUALITY, &MPEG_ENCODER_CONFIG::dwQuality, DEFAULT_ENCODING_QUALITY },
    { VALUE_VBR_QUALITY, &MPEG_ENCODER_CONFIG::dwVBRq, DEFAULT_VBR_QUALITY },
    { VALUE_CRC, &MPEG_ENCODER_CONFIG::bCRCProtect, DEFAULT_CRC },
    { VALUE_FORCE_MONO, &MPEG_ENCODER_CONFIG::bForceMono, DEFAULT_FORCE_MONO },
    { VALUE_SET_DURATION, &MPEG_ENCODER_CONFIG::bSetDuration, DEFAULT_SET_DURATION },
    { VALUE_SAMPLE_OVERLAP, &MPEG_ENCODER_CONFIG::bSampleOverlap, DEFAULT_SAMPLE_OVERLAP },
    { VALUE_COPYRIGHT, &MPEG_ENCODER_CONFIG::bCopyright, DEFAULT_COPYRIGHT },
    { VALUE_ORIGINAL, &MPEG_ENCODER_CONFIG::bOriginal, DEFAULT_ORIGINAL },
    { VALUE_SAMPLE_RATE, &MPEG_ENCODER_CONFIG::dwSampleRate, DEFAULT_SAMPLE_RATE },
    { VALUE_PES, &MPEG_ENCODER_CONFIG::dwPES, DEFAULT_PES },
    { VALUE_FORCE_MS, &MPEG_ENCODER_CONFIG::dwForceMS, DEFAULT_FORCE_MS },
    { VALUE_ENFORCE_MIN, &MPEG_ENCODER_CONFIG::dwEnforceVBRmin, DEFAULT_ENFORCE_MIN },
    { VALUE_VOICE, &MPEG_ENCODER_CONFIG::dwVoiceMode, DEFAULT_VOICE },
    { VALUE_KEEP_ALL_FREQ, &MPEG_ENCODER_CONFIG::dwKeepAllFreq, DEFAULT_KEEP_ALL_FREQ },
    { VALUE_STRICT_ISO, &MPEG_ENCODER_CONFIG::dwStrictISO, DEFAULT_STRICT_ISO },
    { VALUE_DISABLE_SHORT_BLOCK, &MPEG_ENCODER_CONFIG::dwNoShortBlock, DEFAULT_DISABLE_SHORT_BLOCK },
    { VALUE_XING_TAG, &MPEG_ENCODER_CONFIG::dwXingTag, DEFAULT_XING_TAG },
    { VALUE_MODE_FIXED, &MPEG_ENCODER_CONFIG::dwModeFixed, DEFAULT_MODE_FIXED },
};

/**
 * Reads the saved encoder settings from the registry.
 */
void CMpegAudEnc::ReadPresetSettings(MPEG_ENCODER_CONFIG * pmec)
{
    DbgLog((LOG_TRACE,1,TEXT("CMpegAudEnc::ReadPresetSettings()")));

    Lame::CRegKey rk(HKEY_CURRENT_USER, KEY_LAME_ENCODER);

    for (size_t i = 0; i < sizeof(registry_fields) / sizeof(registry_fields[0]); i++)
        pmec->*registry_fields[i].field = rk.getDWORD((PTSTR) registry_fields[i].name, registry_fields[i].dflt);
    pmec->vmVariable        = rk.getDWORD(VALUE_VARIABLE, DEFAULT_VARIABLE) ? vbr_rh : vbr_off;
    pmec->ChMode            = (MPEG_mode)rk.getDWORD(VALUE_STEREO_MODE, DEFAULT_STEREO_MODE);

    rk.Close();
}

/**
 * Returns the class IDs of the three property pages of the filter.
 */
HRESULT CMpegAudEnc::GetPages(CAUUID *pcauuid) 
{
    GUID *pguid;

    pcauuid->cElems = 3;
    pcauuid->pElems = pguid = (GUID *) CoTaskMemAlloc(sizeof(GUID) * pcauuid->cElems);

    if (pcauuid->pElems == NULL)
        return E_OUTOFMEMORY;

    pguid[0] = CLSID_LAMEDShow_PropertyPage;
    pguid[1] = CLSID_LAMEDShow_PropertyPageAdv;
    pguid[2] = CLSID_LAMEDShow_About;

    return S_OK;
}

STDMETHODIMP CMpegAudEnc::NonDelegatingQueryInterface(REFIID riid, void ** ppv) 
{

    if (riid == IID_ISpecifyPropertyPages)
        return GetInterface((ISpecifyPropertyPages *) this, ppv);
    else if(riid == IID_IPersistStream)
        return GetInterface((IPersistStream *)this, ppv);
//    else if (riid == IID_IVAudioEncSettings)
//        return GetInterface((IVAudioEncSettings*) this, ppv);
    else if (riid == IID_IAudioEncoderProperties)
        return GetInterface((IAudioEncoderProperties*) this, ppv);

    return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

/**
 * Reads one field of the encoder configuration.
 *
 * \param field  the field.
 * \param value  receives its value.
 * \param name   the name of the accessor, for the debug log.
 * \return S_OK.
 */
HRESULT CMpegAudEnc::GetConfigField(DWORD MPEG_ENCODER_CONFIG::*field, DWORD *value, LPCTSTR name)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);
    *value = mec.*field;
    UNREFERENCED_PARAMETER(name);   // the log is compiled into debug builds only
    DbgLog((LOG_TRACE, 1, TEXT("%s -> %d"), name, *value));
    return S_OK;
}

/**
 * Sets one field of the encoder configuration.
 *
 * \param field  the field.
 * \param value  its new value.
 * \param name   the name of the accessor, for the debug log.
 * \return S_OK.
 */
HRESULT CMpegAudEnc::SetConfigField(DWORD MPEG_ENCODER_CONFIG::*field, DWORD value, LPCTSTR name)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);
    mec.*field = value;
    m_Encoder.SetOutputType(mec);
    UNREFERENCED_PARAMETER(name);   // the log is compiled into debug builds only
    DbgLog((LOG_TRACE, 1, TEXT("%s(%d)"), name, value));
    return S_OK;
}

////////////////////////////////////////////////////////////////
//IVAudioEncSettings interface methods
////////////////////////////////////////////////////////////////

//
// IAudioEncoderProperties
//
STDMETHODIMP CMpegAudEnc::get_PESOutputEnabled(DWORD *dwEnabled)
{
    *dwEnabled = (DWORD)m_Encoder.IsPES();
    DbgLog((LOG_TRACE, 1, TEXT("get_PESOutputEnabled -> %d"), *dwEnabled));

    return S_OK;
}

STDMETHODIMP CMpegAudEnc::set_PESOutputEnabled(DWORD dwEnabled)
{
    m_Encoder.SetPES((BOOL)!!dwEnabled);
    DbgLog((LOG_TRACE, 1, TEXT("set_PESOutputEnabled(%d)"), !!dwEnabled));

    return S_OK;
}

STDMETHODIMP CMpegAudEnc::get_Bitrate(DWORD *dwBitrate)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwBitrate, dwBitrate, TEXT("get_Bitrate"));
}

STDMETHODIMP CMpegAudEnc::set_Bitrate(DWORD dwBitrate)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwBitrate, dwBitrate, TEXT("set_Bitrate"));
}

STDMETHODIMP CMpegAudEnc::get_Variable(DWORD *dwVariable)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);
    *dwVariable = (DWORD)(mec.vmVariable == vbr_off ? 0 : 1);
    DbgLog((LOG_TRACE, 1, TEXT("get_Variable -> %d"), *dwVariable));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::set_Variable(DWORD dwVariable)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);

    mec.vmVariable = dwVariable ? vbr_rh : vbr_off;
    m_Encoder.SetOutputType(mec);
    DbgLog((LOG_TRACE, 1, TEXT("set_Variable(%d)"), dwVariable));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::get_VariableMin(DWORD *dwMin)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwVariableMin, dwMin, TEXT("get_Variablemin"));
}

STDMETHODIMP CMpegAudEnc::set_VariableMin(DWORD dwMin)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwVariableMin, dwMin, TEXT("set_Variablemin"));
}

STDMETHODIMP CMpegAudEnc::get_VariableMax(DWORD *dwMax)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwVariableMax, dwMax, TEXT("get_Variablemax"));
}

STDMETHODIMP CMpegAudEnc::set_VariableMax(DWORD dwMax)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwVariableMax, dwMax, TEXT("set_Variablemax"));
}

STDMETHODIMP CMpegAudEnc::get_Quality(DWORD *dwQuality)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwQuality, dwQuality, TEXT("get_Quality"));
}

STDMETHODIMP CMpegAudEnc::set_Quality(DWORD dwQuality)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwQuality, dwQuality, TEXT("set_Quality"));
}
STDMETHODIMP CMpegAudEnc::get_VariableQ(DWORD *dwVBRq)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwVBRq, dwVBRq, TEXT("get_VariableQ"));
}

STDMETHODIMP CMpegAudEnc::set_VariableQ(DWORD dwVBRq)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwVBRq, dwVBRq, TEXT("set_VariableQ"));
}


STDMETHODIMP CMpegAudEnc::get_SourceSampleRate(DWORD *dwSampleRate)
{
    *dwSampleRate = 0;

    WAVEFORMATEX wf;
    if(FAILED(m_Encoder.GetInputType(&wf)))
        return E_FAIL;

    *dwSampleRate = wf.nSamplesPerSec;
    DbgLog((LOG_TRACE, 1, TEXT("get_SourceSampleRate -> %d"), *dwSampleRate));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::get_SourceChannels(DWORD *dwChannels)
{
    WAVEFORMATEX wf;
    if(FAILED(m_Encoder.GetInputType(&wf)))
        return E_FAIL;

    *dwChannels = wf.nChannels;
    DbgLog((LOG_TRACE, 1, TEXT("get_SourceChannels -> %d"), *dwChannels));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::get_SampleRate(DWORD *dwSampleRate)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwSampleRate, dwSampleRate, TEXT("get_SampleRate"));
}

STDMETHODIMP CMpegAudEnc::set_SampleRate(DWORD dwSampleRate)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwSampleRate, dwSampleRate, TEXT("set_SampleRate"));
}

STDMETHODIMP CMpegAudEnc::get_ChannelMode(DWORD *dwChannelMode)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);
    *dwChannelMode = mec.ChMode;
    DbgLog((LOG_TRACE, 1, TEXT("get_ChannelMode -> %d"), *dwChannelMode));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::set_ChannelMode(DWORD dwChannelMode)
{
    MPEG_ENCODER_CONFIG mec;
    m_Encoder.GetOutputType(&mec);
    mec.ChMode = (MPEG_mode)dwChannelMode;
    m_Encoder.SetOutputType(mec);
    DbgLog((LOG_TRACE, 1, TEXT("set_ChannelMode(%d)"), dwChannelMode));
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::get_ForceMS(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwForceMS, dwFlag, TEXT("get_ForceMS"));
}

STDMETHODIMP CMpegAudEnc::set_ForceMS(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwForceMS, dwFlag, TEXT("set_ForceMS"));
}


STDMETHODIMP CMpegAudEnc::get_CRCFlag(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bCRCProtect, dwFlag, TEXT("get_CRCFlag"));
}

STDMETHODIMP CMpegAudEnc::get_ForceMono(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bForceMono, dwFlag, TEXT("get_ForceMono"));
}

STDMETHODIMP CMpegAudEnc::get_SetDuration(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bSetDuration, dwFlag, TEXT("get_SetDuration"));
}

STDMETHODIMP CMpegAudEnc::get_SampleOverlap(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bSampleOverlap, dwFlag, TEXT("get_SampleOverlap"));
}

STDMETHODIMP CMpegAudEnc::set_CRCFlag(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bCRCProtect, dwFlag, TEXT("set_CRCFlag"));
}

STDMETHODIMP CMpegAudEnc::set_ForceMono(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bForceMono, dwFlag, TEXT("set_ForceMono"));
}

STDMETHODIMP CMpegAudEnc::set_SetDuration(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bSetDuration, dwFlag, TEXT("set_SetDuration"));
}

STDMETHODIMP CMpegAudEnc::set_SampleOverlap(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bSampleOverlap, dwFlag, TEXT("set_SampleOverlap"));
}

STDMETHODIMP CMpegAudEnc::get_EnforceVBRmin(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwEnforceVBRmin, dwFlag, TEXT("get_EnforceVBRmin"));
}

STDMETHODIMP CMpegAudEnc::set_EnforceVBRmin(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwEnforceVBRmin, dwFlag, TEXT("set_EnforceVBRmin"));
}

STDMETHODIMP CMpegAudEnc::get_VoiceMode(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwVoiceMode, dwFlag, TEXT("get_VoiceMode"));
}

STDMETHODIMP CMpegAudEnc::set_VoiceMode(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwVoiceMode, dwFlag, TEXT("set_VoiceMode"));
}

STDMETHODIMP CMpegAudEnc::get_KeepAllFreq(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwKeepAllFreq, dwFlag, TEXT("get_KeepAllFreq"));
}

STDMETHODIMP CMpegAudEnc::set_KeepAllFreq(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwKeepAllFreq, dwFlag, TEXT("set_KeepAllFreq"));
}

STDMETHODIMP CMpegAudEnc::get_StrictISO(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwStrictISO, dwFlag, TEXT("get_StrictISO"));
}

STDMETHODIMP CMpegAudEnc::set_StrictISO(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwStrictISO, dwFlag, TEXT("set_StrictISO"));
}

STDMETHODIMP CMpegAudEnc::get_NoShortBlock(DWORD *dwNoShortBlock)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwNoShortBlock, dwNoShortBlock, TEXT("get_NoShortBlock"));
}

STDMETHODIMP CMpegAudEnc::set_NoShortBlock(DWORD dwNoShortBlock)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwNoShortBlock, dwNoShortBlock, TEXT("set_NoShortBlock"));
}

STDMETHODIMP CMpegAudEnc::get_XingTag(DWORD *dwXingTag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwXingTag, dwXingTag, TEXT("get_XingTag"));
}

STDMETHODIMP CMpegAudEnc::set_XingTag(DWORD dwXingTag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwXingTag, dwXingTag, TEXT("set_XingTag"));
}



STDMETHODIMP CMpegAudEnc::get_OriginalFlag(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bOriginal, dwFlag, TEXT("get_OriginalFlag"));
}

STDMETHODIMP CMpegAudEnc::set_OriginalFlag(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bOriginal, dwFlag, TEXT("set_OriginalFlag"));
}

STDMETHODIMP CMpegAudEnc::get_CopyrightFlag(DWORD *dwFlag)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::bCopyright, dwFlag, TEXT("get_CopyrightFlag"));
}

STDMETHODIMP CMpegAudEnc::set_CopyrightFlag(DWORD dwFlag)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::bCopyright, dwFlag, TEXT("set_CopyrightFlag"));
}

STDMETHODIMP CMpegAudEnc::get_ModeFixed(DWORD *dwModeFixed)
{
    return GetConfigField(&MPEG_ENCODER_CONFIG::dwModeFixed, dwModeFixed, TEXT("get_ModeFixed"));
}

STDMETHODIMP CMpegAudEnc::set_ModeFixed(DWORD dwModeFixed)
{
    return SetConfigField(&MPEG_ENCODER_CONFIG::dwModeFixed, dwModeFixed, TEXT("set_ModeFixed"));
}

STDMETHODIMP CMpegAudEnc::get_ParameterBlockSize(BYTE *pcBlock, DWORD *pdwSize)
{
    if (pcBlock != NULL && pdwSize != NULL) {
        DbgLog((LOG_TRACE, 1, TEXT("get_ParameterBlockSize -> %d%d"), *pcBlock, *pdwSize));
        if (*pdwSize >= sizeof(MPEG_ENCODER_CONFIG)) {
            m_Encoder.GetOutputType((MPEG_ENCODER_CONFIG*)pcBlock);
            return S_OK;
        }
        else {
            *pdwSize = sizeof(MPEG_ENCODER_CONFIG);
            return E_FAIL;
        }
    }
    else if (pdwSize != NULL) {
        *pdwSize = sizeof(MPEG_ENCODER_CONFIG);
        return S_OK;
    }

    return E_FAIL;
}

STDMETHODIMP CMpegAudEnc::set_ParameterBlockSize(BYTE *pcBlock, DWORD dwSize)
{
    if (pcBlock != NULL) {
        DbgLog((LOG_TRACE, 1, TEXT("get_ParameterBlockSize(%d, %d)"), *pcBlock, dwSize));
        if (sizeof(MPEG_ENCODER_CONFIG) == dwSize){
            m_Encoder.SetOutputType(*(MPEG_ENCODER_CONFIG*)pcBlock);
            return S_OK;
        }
    }
    return E_FAIL; 
}


STDMETHODIMP CMpegAudEnc::DefaultAudioEncoderProperties()
{
    DbgLog((LOG_TRACE, 1, TEXT("DefaultAudioEncoderProperties()")));

    HRESULT hr = InputTypeDefined();
    if (FAILED(hr))
        return hr;

    DWORD dwSourceSampleRate;
    get_SourceSampleRate(&dwSourceSampleRate);

    set_PESOutputEnabled(DEFAULT_PES);

    set_Bitrate(DEFAULT_BITRATE);
    set_Variable(FALSE);
    set_VariableMin(DEFAULT_VARIABLEMIN);
    set_VariableMax(DEFAULT_VARIABLEMAX);
    set_Quality(DEFAULT_ENCODING_QUALITY);
    set_VariableQ(DEFAULT_VBR_QUALITY);

    set_SampleRate(dwSourceSampleRate);
    set_CRCFlag(DEFAULT_CRC);
    set_ForceMono(DEFAULT_FORCE_MONO);
    set_SetDuration(DEFAULT_SET_DURATION);
    set_SampleOverlap(DEFAULT_SAMPLE_OVERLAP);
    set_OriginalFlag(DEFAULT_ORIGINAL);
    set_CopyrightFlag(DEFAULT_COPYRIGHT);

    set_EnforceVBRmin(DEFAULT_ENFORCE_MIN);
    set_VoiceMode(DEFAULT_VOICE);
    set_KeepAllFreq(DEFAULT_KEEP_ALL_FREQ);
    set_StrictISO(DEFAULT_STRICT_ISO);
    set_NoShortBlock(DEFAULT_DISABLE_SHORT_BLOCK);
    set_XingTag(DEFAULT_XING_TAG);
    set_ForceMS(DEFAULT_FORCE_MS);
    set_ChannelMode(DEFAULT_STEREO_MODE);
    set_ModeFixed(DEFAULT_MODE_FIXED);

    return S_OK;
}

STDMETHODIMP CMpegAudEnc::LoadAudioEncoderPropertiesFromRegistry()
{
    DbgLog((LOG_TRACE, 1, TEXT("LoadAudioEncoderPropertiesFromRegistry()")));

    MPEG_ENCODER_CONFIG mec;
    ReadPresetSettings(&mec);
    if(m_Encoder.SetOutputType(mec) == S_FALSE)
        return S_FALSE;
    return S_OK;
}

STDMETHODIMP CMpegAudEnc::SaveAudioEncoderPropertiesToRegistry()
{
    DbgLog((LOG_TRACE, 1, TEXT("SaveAudioEncoderPropertiesToRegistry()")));
    Lame::CRegKey rk;

    MPEG_ENCODER_CONFIG mec;
    if(m_Encoder.GetOutputType(&mec) == S_FALSE)
        return E_FAIL;

    if(rk.Create(HKEY_CURRENT_USER, KEY_LAME_ENCODER))
    {
        for (size_t i = 0; i < sizeof(registry_fields) / sizeof(registry_fields[0]); i++)
            rk.setDWORD((PTSTR) registry_fields[i].name, mec.*registry_fields[i].field);
        rk.setDWORD(VALUE_VARIABLE, mec.vmVariable);
        rk.setDWORD(VALUE_STEREO_MODE, mec.ChMode);

        rk.Close();
    }

    // Reconnect filter graph
    Reconnect();

    return S_OK;
}

STDMETHODIMP CMpegAudEnc::InputTypeDefined()
{
    WAVEFORMATEX wf;
    if(FAILED(m_Encoder.GetInputType(&wf)))
    {
        DbgLog((LOG_TRACE, 1, TEXT("!InputTypeDefined()")));
        return E_FAIL;
    }

    DbgLog((LOG_TRACE, 1, TEXT("InputTypeDefined()")));
    return S_OK;
}


STDMETHODIMP CMpegAudEnc::ApplyChanges()
{
    return Reconnect();
}

//
// CPersistStream stuff
//

/**
 * Returns the class ID of the filter.
 */
STDMETHODIMP CMpegAudEnc::GetClassID(CLSID *pClsid)
{
    CheckPointer(pClsid, E_POINTER);
    *pClsid = CLSID_LAMEDShowFilter;
    return S_OK;
}

HRESULT CMpegAudEnc::WriteToStream(IStream *pStream)
{
    DbgLog((LOG_TRACE,1,TEXT("WriteToStream()")));

    MPEG_ENCODER_CONFIG mec;

    if(m_Encoder.GetOutputType(&mec) == S_FALSE)
        return E_FAIL;

    return pStream->Write(&mec, sizeof(mec), 0);
}


/**
 * Reads the encoder settings from a saved filter graph (.GRF file).
 */
HRESULT CMpegAudEnc::ReadFromStream(IStream *pStream)
{
    MPEG_ENCODER_CONFIG mec;

    HRESULT hr = pStream->Read(&mec, sizeof(mec), 0);
    if(FAILED(hr))
        return hr;

    if(m_Encoder.SetOutputType(mec) == S_FALSE)
        return S_FALSE;

    DbgLog((LOG_TRACE,1,TEXT("ReadFromStream() succeeded")));

    hr = S_OK;
    return hr;
}


/**
 * Returns the size of the settings that WriteToStream() writes.
 */
int CMpegAudEnc::SizeMax()
{
    return sizeof(MPEG_ENCODER_CONFIG);
}





/**
 * Creates the output pin. CMpegAudEnc has only this one output pin.
 */
CMpegAudEncOutPin::CMpegAudEncOutPin( CMpegAudEnc * pFilter, HRESULT * pHr ) :
        CTransformOutputPin( NAME("LameEncoderOutputPin"), pFilter, pHr, L"Output\0" ),
        m_pFilter(pFilter)
{
    m_SetFormat = FALSE;
}

CMpegAudEncOutPin::~CMpegAudEncOutPin()
{
} 

STDMETHODIMP CMpegAudEncOutPin::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
    if(riid == IID_IAMStreamConfig) {
        CheckPointer(ppv, E_POINTER);
        return GetInterface((IAMStreamConfig*)(this), ppv);
    }
    return CTransformOutputPin::NonDelegatingQueryInterface(riid, ppv);
}


/**
 * Called after the output format is negotiated. Changes the encoder settings
 * to match the media type.
 */
HRESULT CMpegAudEncOutPin::SetMediaType(const CMediaType *pmt)
{
    // Retrieve the current LAME encoder configuration
    MPEG_ENCODER_CONFIG mec;
    m_pFilter->m_Encoder.GetOutputType(&mec);

    // Annotate if we are using the MEDIATYPE_Stream output type
    m_pFilter->m_bStreamOutput = (pmt->majortype == MEDIATYPE_Stream);

    if (pmt->majortype == MEDIATYPE_Stream) {
        // Update the encoder configuration using the settings that were
        // cached in the CMpegAudEncOutPin::GetMediaType() call
        mec.dwSampleRate = m_CurrentOutputFormat.nSampleRate;
        mec.dwBitrate = m_CurrentOutputFormat.nBitRate;
        mec.ChMode = m_CurrentOutputFormat.ChMode;
    }
    else {
        // Update the encoder configuration directly using the values
        // passed via the CMediaType structure.  
        MPEGLAYER3WAVEFORMAT *pfmt = (MPEGLAYER3WAVEFORMAT*) pmt->Format();
        mec.dwSampleRate = pfmt->wfx.nSamplesPerSec;
        mec.dwBitrate = pfmt->wfx.nAvgBytesPerSec * 8 / 1000;

        if (pfmt->wfx.nChannels == 1) { mec.ChMode = MONO; }
        else if (pfmt->wfx.nChannels == 2 && mec.ChMode == MONO && !mec.bForceMono) { mec.ChMode = STEREO; }
    }
    m_pFilter->m_Encoder.SetOutputType(mec);

    // Now configure this MediaType on the output pin
    HRESULT hr = CTransformOutputPin::SetMediaType(pmt);
    return hr;
}


/**
 * Returns the output media type at a position in the list of supported
 * formats. Position 0 is always the current media type.
 */
HRESULT CMpegAudEncOutPin::GetMediaType(int iPosition, CMediaType *pmt)
{
    if (iPosition < 0) return E_INVALIDARG;

    // If iPosition equals zero then we always return the currently configured MediaType 
    if (iPosition == 0) {
        *pmt = m_mt;
        return S_OK;
    }

    switch (iPosition)
    {
        case 1:
        {
            pmt->SetType(&MEDIATYPE_Audio);
            pmt->SetSubtype(&MEDIASUBTYPE_MP3);
            break;
        }
        case 2:
        {
            pmt->SetType(&MEDIATYPE_Stream);
            pmt->SetSubtype(&MEDIASUBTYPE_MPEG1Audio);
            pmt->SetFormatType(&GUID_NULL);
            break;
        }
        case 3:
        {   // The last case that we evaluate is the MPEG2_PES format, but if the 
            // encoder isn't configured for it then just return VFW_S_NO_MORE_ITEMS
            if ( !m_pFilter->m_Encoder.IsPES() ) { return VFW_S_NO_MORE_ITEMS; }

            pmt->SetType(&MEDIATYPE_MPEG2_PES);
            pmt->SetSubtype(&MEDIASUBTYPE_MPEG2_AUDIO);
            break;
        }
        default:
            return VFW_S_NO_MORE_ITEMS;
    }


    // Output capabilities are dependent on the input so insure it is connected
    if ( !m_pFilter->m_pInput->IsConnected() ) {
        pmt->SetFormatType(&FORMAT_None);
        return NOERROR;
    }


    // Annotate the current MediaType index for recall in CMpegAudEnc::Reconnect()
    m_pFilter->m_currentMediaTypeIndex = iPosition;

    // Configure the remaining AM_MEDIA_TYPE parameters using the cached encoder settings.
    // Since MEDIATYPE_Stream doesn't have a format block the current settings 
    // for CHANNEL MODE, BITRATE and SAMPLERATE are cached in m_CurrentOutputFormat for use
    // when we setup the LAME encoder in the call to CMpegAudEncOutPin::SetMediaType()
    MPEG_ENCODER_CONFIG mec;
    m_pFilter->m_Encoder.GetOutputType(&mec);           // Retrieve the current encoder config

    WAVEFORMATEX wf;                                    // Retrieve the input configuration
    m_pFilter->m_Encoder.GetInputType(&wf);

    // Use the current encoder sample rate unless it isn't a modulus of the input rate
    if (mec.dwSampleRate != 0 && (wf.nSamplesPerSec % mec.dwSampleRate) == 0) {
        m_CurrentOutputFormat.nSampleRate = mec.dwSampleRate;
    }
    else {
        m_CurrentOutputFormat.nSampleRate = wf.nSamplesPerSec;
    }

    // Select the output channel config based on the encoder config and input channel count
    m_CurrentOutputFormat.ChMode = mec.ChMode;
    switch (wf.nChannels)                    // Determine if we need to alter ChMode based upon the channel count and ForceMono flag 
    {
        case 1:
        {
            m_CurrentOutputFormat.ChMode = MONO;
            break;
        }
        case 2:
        {
            if (mec.ChMode == MONO && !mec.bForceMono) { m_CurrentOutputFormat.ChMode = STEREO; }
            else if ( mec.bForceMono ) { m_CurrentOutputFormat.ChMode = MONO; }
            break;
        }
    }

    // Select the encoder bit rate. In VBR mode we set the data rate parameter
    // of the WAVE_FORMAT_MPEGLAYER3 structure to the minimum VBR value
    m_CurrentOutputFormat.nBitRate = (mec.vmVariable == vbr_off) ? mec.dwBitrate : mec.dwVariableMin;

    if (pmt->majortype == MEDIATYPE_Stream) return NOERROR;     // No further config required for MEDIATYPE_Stream


    // Now configure the remainder of the WAVE_FORMAT_MPEGLAYER3 format block
    // and its parent AM_MEDIA_TYPE structure
    DECLARE_PTR(MPEGLAYER3WAVEFORMAT, p_mp3wvfmt, pmt->AllocFormatBuffer(sizeof(MPEGLAYER3WAVEFORMAT)));
    ZeroMemory(p_mp3wvfmt, sizeof(MPEGLAYER3WAVEFORMAT));

    p_mp3wvfmt->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    p_mp3wvfmt->wfx.nChannels = (m_CurrentOutputFormat.ChMode == MONO) ? 1 : 2;
    p_mp3wvfmt->wfx.nSamplesPerSec = m_CurrentOutputFormat.nSampleRate;
    p_mp3wvfmt->wfx.nAvgBytesPerSec = GET_DATARATE(m_CurrentOutputFormat.nBitRate);
    p_mp3wvfmt->wfx.nBlockAlign = 1;
    p_mp3wvfmt->wfx.wBitsPerSample = 0;
    p_mp3wvfmt->wfx.cbSize = sizeof(MPEGLAYER3WAVEFORMAT) - sizeof(WAVEFORMATEX);

    p_mp3wvfmt->wID = MPEGLAYER3_ID_MPEG;
    p_mp3wvfmt->fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO;
    p_mp3wvfmt->nBlockSize = GET_FRAMELENGTH(m_CurrentOutputFormat.nBitRate, p_mp3wvfmt->wfx.nSamplesPerSec);
    p_mp3wvfmt->nFramesPerBlock = 1;
    p_mp3wvfmt->nCodecDelay = 0;

    pmt->SetTemporalCompression(FALSE);
    pmt->SetSampleSize(OUT_BUFFER_SIZE);
    pmt->SetFormat((LPBYTE)p_mp3wvfmt, sizeof(MPEGLAYER3WAVEFORMAT));
    pmt->SetFormatType(&FORMAT_WaveFormatEx);

    return NOERROR;
}


/**
 * Checks whether the output pin supports an output media type.
 */
HRESULT CMpegAudEncOutPin::CheckMediaType(const CMediaType *pmtOut)
{
    // Fail if the input pin is not connected.
    if (!m_pFilter->m_pInput->IsConnected()) {
        return VFW_E_NOT_CONNECTED;
    }

    // Reject any media types that we know in advance our 
    // filter cannot use.
    if (pmtOut->majortype != MEDIATYPE_Audio && pmtOut->majortype != MEDIATYPE_Stream) { return S_FALSE; }

    // If SetFormat was previously called, check whether pmtOut exactly 
    // matches the format that was specified in SetFormat.
    // Return S_OK if they match, or VFW_E_INVALIDMEDIATYPE otherwise.)
    if ( m_SetFormat ) {
        if (*pmtOut != m_mt) { return VFW_E_INVALIDMEDIATYPE; }
        else { return S_OK; }
    }

    // Now do the normal check for this media type.
    HRESULT hr;
    hr = m_pFilter->CheckTransform (&m_pFilter->m_pInput->CurrentMediaType(),  // The input type.
                                    pmtOut);                                   // The proposed output type.

    if (hr == S_OK) {
        return S_OK;           // This format is compatible with the current input type.
    }
 
    // This format is not compatible with the current input type. 
    // Maybe we can reconnect the input pin with a new input type.
    
    // Enumerate the upstream filter's preferred output types, and 
    // see if one of them will work.
    CMediaType *pmtEnum;
    BOOL fFound = FALSE;
    IEnumMediaTypes *pEnum;
    hr = m_pFilter->m_pInput->GetConnected()->EnumMediaTypes(&pEnum);
    if (hr != S_OK) {
        return E_FAIL;
    }

    while (hr = pEnum->Next(1, (AM_MEDIA_TYPE **)&pmtEnum, NULL), hr == S_OK)
    {
        // Check this input type against the proposed output type.
        hr = m_pFilter->CheckTransform(pmtEnum, pmtOut);
        if (hr != S_OK) {
            DeleteMediaType(pmtEnum);
            continue; // Try the next one.
        }

        // This input type is a possible candidate. But, we have to make
        // sure that the upstream filter can switch to this type. 
        hr = m_pFilter->m_pInput->GetConnected()->QueryAccept(pmtEnum);
        if (hr != S_OK) {
            // The upstream filter will not switch to this type.
            DeleteMediaType(pmtEnum);
            continue; // Try the next one.
        }
        fFound = TRUE;
        DeleteMediaType(pmtEnum);
        break;
    }
    pEnum->Release();

    if (fFound) {
        // This output type is OK, but if we are asked to use it, we will
        // need to reconnect our input pin. (See SetFormat, below.)
        return S_OK;
    }
    else {
        return VFW_E_INVALIDMEDIATYPE;
    }
}



//////////////////////////////////////////////////////////////////////////
//  IAMStreamConfig
//////////////////////////////////////////////////////////////////////////

HRESULT STDMETHODCALLTYPE CMpegAudEncOutPin::SetFormat(AM_MEDIA_TYPE *pmt)
{
    CheckPointer(pmt, E_POINTER);
    HRESULT hr;

    // Hold the filter state lock, to make sure that streaming isn't 
    // in the middle of starting or stopping:
    CAutoLock cObjectLock(&m_pFilter->m_csFilter);

    // Cannot set the format unless the filter is stopped.
    if (m_pFilter->m_State != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }

    // The set of possible output formats depends on the input format,
    // so if the input pin is not connected, return a failure code.
    if (!m_pFilter->m_pInput->IsConnected()) {
        return VFW_E_NOT_CONNECTED;
    }

    // If the pin is already using this format, there's nothing to do.
    if (IsConnected() && CurrentMediaType() == *pmt) {
        if ( m_SetFormat ) return S_OK;
    }

    // See if this media type is acceptable.
    if ((hr = CheckMediaType((CMediaType *)pmt)) != S_OK) {
        return hr;
    }

    // If we're connected to a downstream filter, we have to make
    // sure that the downstream filter accepts this media type.
    if (IsConnected()) {
        hr = GetConnected()->QueryAccept(pmt);
        if (hr != S_OK) {
            return VFW_E_INVALIDMEDIATYPE;
        }
    }

    // Now make a note that from now on, this is the only format allowed,
    // and refuse anything but this in the CheckMediaType() code above.
    m_SetFormat = TRUE;
    m_mt = *pmt;

    // Changing the format means reconnecting if necessary.
    if (IsConnected()) {
        m_pFilter->m_pGraph->Reconnect(this);
    }

    return NOERROR;
}

HRESULT STDMETHODCALLTYPE CMpegAudEncOutPin::GetFormat(AM_MEDIA_TYPE **ppmt)
{
    *ppmt = CreateMediaType(&m_mt);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMpegAudEncOutPin::GetNumberOfCapabilities(int *piCount, int *piSize)
{
    // The set of possible output formats depends on the input format,
    // so if the input pin is not connected, return a failure code.
    if (!m_pFilter->m_pInput->IsConnected()) {
        return VFW_E_NOT_CONNECTED;
    }

    // Retrieve the current encoder configuration
    MPEG_ENCODER_CONFIG mec;
    m_pFilter->m_Encoder.GetOutputType(&mec);

    // If the encoder is in VBR mode GetStreamCaps() isn't implemented
    if (mec.vmVariable != vbr_off) { *piCount = 0; }
    else { *piCount = m_pFilter->m_CapsNum; }

    *piSize = sizeof(AUDIO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMpegAudEncOutPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC)
{
    // The set of possible output formats depends on the input format,
    // so if the input pin is not connected, return a failure code.
    if (!m_pFilter->m_pInput->IsConnected()) {
        return VFW_E_NOT_CONNECTED;
    }

    // If we don't have a capabilities array GetStreamCaps() isn't implemented
    if (m_pFilter->m_CapsNum == 0) return E_NOTIMPL;

    // If the encoder is in VBR mode GetStreamCaps() isn't implemented
    MPEG_ENCODER_CONFIG mec;
    m_pFilter->m_Encoder.GetOutputType(&mec);
    if (mec.vmVariable != vbr_off) return E_NOTIMPL;

    if (iIndex < 0) return E_INVALIDARG;
    // The table holds m_CapsNum entries, so m_CapsNum itself is one past the
    // end: reading it gives a zeroed entry whose sample rate is 0, which the
    // frame length below divides by.
    if (iIndex >= m_pFilter->m_CapsNum) return S_FALSE;

    // Load the MPEG Layer3 WaveFormatEx structure with the appropriate entries
    // for this IAMStreamConfig index element.
    //
    // The entry describes what the encoder can produce, so it is built here
    // rather than from this pin's own media type: that type carries no format
    // block before the pin connects, and none afterwards either where the pin
    // has connected as a stream - which is what a file writer downstream
    // asks for.
    CMediaType mt;

    DECLARE_PTR(MPEGLAYER3WAVEFORMAT, p_mp3wvfmt,
                mt.AllocFormatBuffer(sizeof(MPEGLAYER3WAVEFORMAT)));
    if (p_mp3wvfmt == NULL) return E_OUTOFMEMORY;
    ZeroMemory(p_mp3wvfmt, sizeof(MPEGLAYER3WAVEFORMAT));

    mt.SetType(&MEDIATYPE_Audio);
    mt.SetSubtype(&MEDIASUBTYPE_MP3);
    mt.SetTemporalCompression(FALSE);
    mt.SetSampleSize(OUT_BUFFER_SIZE);
    mt.SetFormatType(&FORMAT_WaveFormatEx);

    p_mp3wvfmt->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    p_mp3wvfmt->wfx.nChannels = 2;
    p_mp3wvfmt->wfx.nSamplesPerSec = m_pFilter->OutputCaps[iIndex].nSampleRate;
    p_mp3wvfmt->wfx.nAvgBytesPerSec = GET_DATARATE(m_pFilter->OutputCaps[iIndex].nBitRate);
    p_mp3wvfmt->wfx.nBlockAlign = 1;
    p_mp3wvfmt->wfx.wBitsPerSample = 0;
    p_mp3wvfmt->wfx.cbSize = sizeof(MPEGLAYER3WAVEFORMAT) - sizeof(WAVEFORMATEX);

    p_mp3wvfmt->wID = MPEGLAYER3_ID_MPEG;
    p_mp3wvfmt->fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO;
    p_mp3wvfmt->nBlockSize = GET_FRAMELENGTH(m_pFilter->OutputCaps[iIndex].nBitRate, m_pFilter->OutputCaps[iIndex].nSampleRate);
    p_mp3wvfmt->nFramesPerBlock = 1;
    p_mp3wvfmt->nCodecDelay = 0;

    *pmt = CreateMediaType(&mt);
    if (*pmt == NULL) return E_OUTOFMEMORY;

    // Set up the companion AUDIO_STREAM_CONFIG_CAPS structure
    // We are only using the CHANNELS element of the structure
    DECLARE_PTR(AUDIO_STREAM_CONFIG_CAPS, pascc, pSCC);

    ZeroMemory(pascc, sizeof(AUDIO_STREAM_CONFIG_CAPS));
    pascc->guid = MEDIATYPE_Audio;

    pascc->MinimumChannels = 1;
    pascc->MaximumChannels = 2;
    pascc->ChannelsGranularity = 1;

    pascc->MinimumSampleFrequency = p_mp3wvfmt->wfx.nSamplesPerSec;
    pascc->MaximumSampleFrequency = p_mp3wvfmt->wfx.nSamplesPerSec;
    pascc->SampleFrequencyGranularity = 0;

    pascc->MinimumBitsPerSample = p_mp3wvfmt->wfx.wBitsPerSample;
    pascc->MaximumBitsPerSample = p_mp3wvfmt->wfx.wBitsPerSample;
    pascc->BitsPerSampleGranularity = 0;

    return S_OK;
}

