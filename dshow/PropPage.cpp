/*
 *  LAME MP3 encoder for DirectShow
 *  Basic property page
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
#include <commctrl.h>
#include "iaudioprops.h"
#include "mpegac.h"
#include "resource.h"
#include "../ACM/SettingText.h"
#include "SystemFontPage.h"
#include "PropPage.h"
#include "Reg.h"

// strings to appear in comboboxes
const char * szBitRateString[2][14] = {
    {
        "32 kbps","40 kbps","48 kbps","56 kbps",
        "64 kbps","80 kbps","96 kbps","112 kbps",
        "128 kbps","160 kbps","192 kbps","224 kbps",
        "256 kbps","320 kbps"
    },
    {
        "8 kbps","16 kbps","24 kbps","32 kbps",
        "40 kbps","48 kbps","56 kbps","64 kbps",
        "80 kbps","96 kbps","112 kbps","128 kbps",
        "144 kbps","160 kbps"
    }
};

struct SSampleRate {
    DWORD dwSampleRate;
    LPCSTR lpSampleRate;
};

SSampleRate srRates[9] = {
    // MPEG-1
    {48000, "48 kHz"},
    {44100, "44.1 kHz"},
    {32000, "32 kHz"},

    // MPEG-2
    {24000, "24 kHz"},
    {22050, "22.05 kHz"},
    {16000, "16 kHz"},

    // MPEG-2.5
    {12000, "12 kHz"},
    {11025, "11.025 kHz"},
    { 8000, "8 kHz"}
};

/**
 * Creates the property page. The class factory of the filter calls it.
 */
CUnknown * WINAPI CMpegAudEncPropertyPage::CreateInstance( LPUNKNOWN punk, HRESULT *phr )
{
    CMpegAudEncPropertyPage *pNewObject
        = new CMpegAudEncPropertyPage( punk, phr );

    if( pNewObject == NULL )
        *phr = E_OUTOFMEMORY;

    return pNewObject;
}

/**
 * Creates the property page for the basic encoder settings.
 */
CMpegAudEncPropertyPage::CMpegAudEncPropertyPage(LPUNKNOWN punk, HRESULT *phr)
 : CSystemFontPropertyPage(NAME("Encoder Property Page"), 
                      punk, IDD_AUDIOENCPROPS, IDS_AUDIO_PROPS_TITLE)                      
    , m_pAEProps(NULL)
{
    ASSERT(phr);

    m_srIdx = 0;

    InitCommonControls();
}

/**
 * Gets the IAudioEncoderProperties interface of the filter, and reads the
 * current settings.
 */
HRESULT CMpegAudEncPropertyPage::OnConnect(IUnknown *pUnknown)
{
    ASSERT(m_pAEProps == NULL);

    // Ask the filter for it's control interface

    HRESULT hr = pUnknown->QueryInterface(IID_IAudioEncoderProperties2,(void **)&m_pAEProps);
    if (FAILED(hr))
        return E_NOINTERFACE;

    ASSERT(m_pAEProps);

    // Get current filter state
    m_pAEProps->get_Bitrate(&m_dwBitrate);
    m_pAEProps->get_Variable(&m_dwVariable);
    m_pAEProps->get_VariableMin(&m_dwMin);
    m_pAEProps->get_VariableMax(&m_dwMax);
    m_pAEProps->get_Quality(&m_dwQuality);
    m_pAEProps->get_VariableQ(&m_dwVBRq);
    m_pAEProps->get_SampleRate(&m_dwSampleRate);
    m_pAEProps->get_CRCFlag(&m_dwCRC);
    m_pAEProps->get_ForceMono(&m_dwForceMono);
    m_pAEProps->get_CopyrightFlag(&m_dwCopyright);
    m_pAEProps->get_OriginalFlag(&m_dwOriginal);
    m_pAEProps->get_Average(&m_dwAverage);
    m_pAEProps->get_AverageBitrate(&m_dwAverageBitrate);
    m_pAEProps->get_PrivateFlag(&m_dwPrivate);
    m_pAEProps->get_BitReservoir(&m_dwReservoir);

    return NOERROR;
}

/**
 * Writes the settings back to the filter, saves them in the registry, and
 * releases the interface.
 */
HRESULT CMpegAudEncPropertyPage::OnDisconnect()
{
    // Release the interface
    if (m_pAEProps == NULL)
        return E_UNEXPECTED;

    m_pAEProps->set_Bitrate(m_dwBitrate);
    m_pAEProps->set_Variable(m_dwVariable);
    m_pAEProps->set_VariableMin(m_dwMin);
    m_pAEProps->set_VariableMax(m_dwMax);
    m_pAEProps->set_Quality(m_dwQuality);
    m_pAEProps->set_VariableQ(m_dwVBRq);
    m_pAEProps->set_SampleRate(m_dwSampleRate);
    m_pAEProps->set_CRCFlag(m_dwCRC);
    m_pAEProps->set_ForceMono(m_dwForceMono);
    m_pAEProps->set_CopyrightFlag(m_dwCopyright);
    m_pAEProps->set_OriginalFlag(m_dwOriginal);
    m_pAEProps->set_Average(m_dwAverage);
    m_pAEProps->set_AverageBitrate(m_dwAverageBitrate);
    m_pAEProps->set_PrivateFlag(m_dwPrivate);
    m_pAEProps->set_BitReservoir(m_dwReservoir);
    m_pAEProps->SaveAudioEncoderPropertiesToRegistry();

    m_pAEProps->Release();
    m_pAEProps = NULL;

    return NOERROR;
}

/**
 * Called when the dialog is created. Fills the dialog controls.
 */
HRESULT CMpegAudEncPropertyPage::OnActivate(void)
{
    InitPropertiesDialog(m_hwnd);

    return NOERROR;
}

/**
 * Called when the dialog is destroyed.
 */
HRESULT CMpegAudEncPropertyPage::OnDeactivate(void)
{
    return NOERROR;
}

/**
 * Handles the messages of the dialog.
 */
INT_PTR CMpegAudEncPropertyPage::OnReceiveMessage(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_HSCROLL:
        if ((HWND)lParam == m_hwndQuality)
        {
            int pos = SendMessage(m_hwndQuality, TBM_GETPOS, 0, 0);
            if (pos >= 0 && (unsigned int) pos < ENCODING_QUALITY_LEVELS)
            {
                SetDlgItemText(hwnd,IDC_TEXT_QUALITY,EncodingQualityText(pos));
                m_pAEProps->set_Quality(pos);
                SetDirty();
            }
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_COMBO_CBR:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nBitrate = SendDlgItemMessage(hwnd, IDC_COMBO_CBR, CB_GETCURSEL, 0, 0L);
                DWORD dwSampleRate;
                m_pAEProps->get_SampleRate(&dwSampleRate);
                DWORD dwBitrate;

                if (dwSampleRate >= 32000)
                {
                    // Consider MPEG-1
                    dwBitrate = BitRateValue(0, nBitrate);
                }
                else
                {
                    // Consider MPEG-2/2.5
                    dwBitrate = BitRateValue(1, nBitrate);
                }

                m_pAEProps->set_Bitrate(dwBitrate);

                SetDirty();
            }
            break;

        case IDC_COMBO_VBRMIN:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nVariableMin = SendDlgItemMessage(hwnd, IDC_COMBO_VBRMIN, CB_GETCURSEL, 0, 0L);
                DWORD dwSampleRate;
                m_pAEProps->get_SampleRate(&dwSampleRate);
                DWORD dwMin;

                if (dwSampleRate >= 32000)
                {
                    // Consider MPEG-1
                    dwMin = BitRateValue(0, nVariableMin);
                }
                else
                {
                    // Consider MPEG-2/2.5
                    dwMin = BitRateValue(1, nVariableMin);
                }

                m_pAEProps->set_VariableMin(dwMin);

                SetDirty();
            }
            break;

        case IDC_COMBO_VBRMAX:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nVariableMax = SendDlgItemMessage(hwnd, IDC_COMBO_VBRMAX, CB_GETCURSEL, 0, 0L);
                DWORD dwSampleRate;
                m_pAEProps->get_SampleRate(&dwSampleRate);
                DWORD dwMax;

                if (dwSampleRate >= 32000)
                {
                    // Consider MPEG-1
                    dwMax = BitRateValue(0, nVariableMax);
                }
                else
                {
                    // Consider MPEG-2/2.5
                    dwMax = BitRateValue(1, nVariableMax);
                }

                m_pAEProps->set_VariableMax(dwMax);

                SetDirty();
            }
            break;

        case IDC_COMBO_SAMPLE_RATE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nSampleRate = SendDlgItemMessage(hwnd, IDC_COMBO_SAMPLE_RATE, CB_GETCURSEL, 0, 0L);

                if (nSampleRate < 0)
                    nSampleRate = 0;
                else if (nSampleRate > 2)
                    nSampleRate = 2;

                DWORD dwSampleRate = srRates[nSampleRate * 3 + m_srIdx].dwSampleRate;

                m_pAEProps->set_SampleRate(dwSampleRate);
                InitPropertiesDialog(hwnd);
                SetDirty();
            }
            break;

        case IDC_COMBO_VBRq:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nVBRq = SendDlgItemMessage(hwnd, IDC_COMBO_VBRq, CB_GETCURSEL, 0, 0L);
                if (nVBRq >=0 && nVBRq <=9) 
                    m_pAEProps->set_VariableQ(nVBRq);
                SetDirty();
            }
            break;

        case IDC_RADIO_CBR:
        case IDC_RADIO_VBR:
            // set_Variable() ends ABR too: 0 gives CBR, 1 VBR
            m_pAEProps->set_Variable(LOWORD(wParam) == IDC_RADIO_VBR);
            SetDirty();
            break;

        case IDC_RADIO_ABR:
            m_pAEProps->set_Average(TRUE);
            SetDirty();
            break;

        case IDC_COMBO_ABR:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int nAverage = SendDlgItemMessage(hwnd, IDC_COMBO_ABR, CB_GETCURSEL, 0, 0L);
                DWORD dwSampleRate;
                m_pAEProps->get_SampleRate(&dwSampleRate);
                if (nAverage >= 0 && nAverage < 14)
                    m_pAEProps->set_AverageBitrate(BitRateValue(dwSampleRate >= 32000 ? 0 : 1, nAverage));
                SetDirty();
            }
            break;

        case IDC_CHECK_PRIVATE:
            m_pAEProps->set_PrivateFlag(IsDlgButtonChecked(hwnd, IDC_CHECK_PRIVATE));
            SetDirty();
            break;

        case IDC_CHECK_RESERVOIR:
            m_pAEProps->set_BitReservoir(IsDlgButtonChecked(hwnd, IDC_CHECK_RESERVOIR));
            SetDirty();
            break;

        case IDC_CHECK_PES:
            m_pAEProps->set_PESOutputEnabled(IsDlgButtonChecked(hwnd, IDC_CHECK_PES));
            SetDirty();
            break;

        case IDC_CHECK_COPYRIGHT:
            m_pAEProps->set_CopyrightFlag(IsDlgButtonChecked(hwnd, IDC_CHECK_COPYRIGHT));
            SetDirty();
            break;

        case IDC_CHECK_ORIGINAL:
            m_pAEProps->set_OriginalFlag(IsDlgButtonChecked(hwnd, IDC_CHECK_ORIGINAL));
            SetDirty();
            break;

        case IDC_CHECK_CRC:
            m_pAEProps->set_CRCFlag(IsDlgButtonChecked(hwnd, IDC_CHECK_CRC));
            SetDirty();
            break;

        case IDC_FORCE_MONO:
            m_pAEProps->set_ForceMono(IsDlgButtonChecked(hwnd, IDC_FORCE_MONO));
            SetDirty();
            break;
        }
        return TRUE;

    case WM_DESTROY:
        return TRUE;

    default:
        return FALSE;
    }

    return TRUE;
}

/**
 * Called when the user presses Apply. Stores the current settings, saves them
 * in the registry, and applies them to the filter.
 */
HRESULT CMpegAudEncPropertyPage::OnApplyChanges()
{
    m_pAEProps->get_Bitrate(&m_dwBitrate);
    m_pAEProps->get_Variable(&m_dwVariable);
    m_pAEProps->get_VariableMin(&m_dwMin);
    m_pAEProps->get_VariableMax(&m_dwMax);
    m_pAEProps->get_Quality(&m_dwQuality);
    m_pAEProps->get_VariableQ(&m_dwVBRq);
    m_pAEProps->get_SampleRate(&m_dwSampleRate);
    m_pAEProps->get_CRCFlag(&m_dwCRC);
    m_pAEProps->get_ForceMono(&m_dwForceMono);
    m_pAEProps->get_CopyrightFlag(&m_dwCopyright);
    m_pAEProps->get_OriginalFlag(&m_dwOriginal);
    m_pAEProps->get_Average(&m_dwAverage);
    m_pAEProps->get_AverageBitrate(&m_dwAverageBitrate);
    m_pAEProps->get_PrivateFlag(&m_dwPrivate);
    m_pAEProps->get_BitReservoir(&m_dwReservoir);
    m_pAEProps->SaveAudioEncoderPropertiesToRegistry();

    m_pAEProps->ApplyChanges();

    return S_OK;
}

/**
 * Fills the dialog controls with the current settings.
 */
void CMpegAudEncPropertyPage::InitPropertiesDialog(HWND hwndParent)
{
    EnableControls(hwndParent, TRUE);

    m_hwndQuality = GetDlgItem(hwndParent,IDC_SLIDER_QUALITY);
    DWORD dwQuality;
    m_pAEProps->get_Quality(&dwQuality);
    SendDlgItemMessage(hwndParent, IDC_SLIDER_QUALITY, TBM_SETRANGE, 1, MAKELONG (0, ENCODING_QUALITY_LEVELS - 1));
    SendDlgItemMessage(hwndParent, IDC_SLIDER_QUALITY, TBM_SETPOS, 1, dwQuality);
    if (dwQuality < ENCODING_QUALITY_LEVELS)
        SetDlgItemText(hwndParent,IDC_TEXT_QUALITY,EncodingQualityText(dwQuality));

    //
    // initialize sample rate selection
    //
    DWORD dwSourceSampleRate;
    m_pAEProps->get_SourceSampleRate(&dwSourceSampleRate);

    SendDlgItemMessage(hwndParent, IDC_COMBO_SAMPLE_RATE, CB_RESETCONTENT, 0, 0L);

    switch (dwSourceSampleRate)
    {
    case 48000:
    case 24000:
    case 12000:
        m_srIdx = 0;
        break;

    case 32000:
    case 16000:
    case  8000:
        m_srIdx = 2;
        break;

    case 44100:
    case 22050:
    case 11025:
    default:
        m_srIdx = 1;
    }

    for (int i = 0; i < 3; i++)
        SendDlgItemMessage(hwndParent, IDC_COMBO_SAMPLE_RATE, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)srRates[i * 3 + m_srIdx].lpSampleRate);

    DWORD dwSampleRate;
    m_pAEProps->get_SampleRate(&dwSampleRate);
    m_pAEProps->set_SampleRate(dwSampleRate);

    int nSR = 0;
    while (nSR < 3 && dwSampleRate != srRates[nSR * 3 + m_srIdx].dwSampleRate)
    {
        nSR++;
    }

    if (nSR >= 3)
        nSR = 0;

    SendDlgItemMessage(hwndParent, IDC_COMBO_SAMPLE_RATE, CB_SETCURSEL, nSR, 0);

    DWORD dwChannels;
    m_pAEProps->get_SourceChannels(&dwChannels);

    //
    //initialize VBRq combo box
    //
    unsigned int k;
    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRq, CB_RESETCONTENT, 0, 0);
    for (k = 0; k < VBR_QUALITY_LEVELS; k++)
    {
        char level_text[sizeof "9 (about 320 kbps)"];

        VbrQualityText(k, level_text, sizeof level_text);
        SendDlgItemMessage(hwndParent, IDC_COMBO_VBRq, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)level_text);
    }
    DWORD dwVBRq;
    m_pAEProps->get_VariableQ(&dwVBRq);
    if (dwVBRq >= VBR_QUALITY_LEVELS)
        dwVBRq = VBR_QUALITY_LEVELS - 1;
    m_pAEProps->set_VariableQ(dwVBRq);
    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRq, CB_SETCURSEL, dwVBRq, 0);

//////////////////////////////////////
// initialize CBR selection
//////////////////////////////////////
    int nSt;

    SendDlgItemMessage(hwndParent, IDC_COMBO_CBR, CB_RESETCONTENT, 0, 0);
    if (dwSampleRate >= 32000)
    {
        // If target sampling rate is less than 32000, consider
        // MPEG 1 audio
        nSt = 0;
        for (int i = 0; i < 14; i++)
            SendDlgItemMessage(hwndParent, IDC_COMBO_CBR, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[0][i]);
    }
    else
    {
        // Consider MPEG 2 / 2.5 audio
        nSt = 1;
        for (int i = 0; i < 14 ; i++)
            SendDlgItemMessage(hwndParent, IDC_COMBO_CBR, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[1][i]);
    }

    DWORD dwBitrate;
    m_pAEProps->get_Bitrate(&dwBitrate);

    int nBitrateSel = 0;
    // BitRateValue() is in ascending order
    // We use this fact. We also know there are 14 bitrate values available.
    // We are going to use the closest possible, so we can limit loop with 13
    while (nBitrateSel < 13 && BitRateValue(nSt, nBitrateSel) < dwBitrate)
        nBitrateSel++;
    SendDlgItemMessage(hwndParent, IDC_COMBO_CBR, CB_SETCURSEL, nBitrateSel, 0);

    // check if the specified bitrate is found exactly and correct if not
    if (BitRateValue(nSt, nBitrateSel) != dwBitrate)
    {
        dwBitrate = BitRateValue(nSt, nBitrateSel);
        // we can change it, because it is independent of any other parameters
        // (but depends on some of them!)
        m_pAEProps->set_Bitrate(dwBitrate);
    }

    //
    // Check VBR/CBR radio button
    //
    DWORD dwVariable, dwAverage;
    m_pAEProps->get_Variable(&dwVariable);
    m_pAEProps->get_Average(&dwAverage);
    CheckDlgButton(hwndParent, IDC_RADIO_CBR, !dwVariable && !dwAverage ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndParent, IDC_RADIO_VBR, dwVariable ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndParent, IDC_RADIO_ABR, dwAverage ? BST_CHECKED : BST_UNCHECKED);

    //
    // The ABR target, from the bitrates of the MPEG version of the sample rate
    //
    DWORD dwAverageBitrate;
    int nAverageSel = 0;
    SendDlgItemMessage(hwndParent, IDC_COMBO_ABR, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 14; i++)
        SendDlgItemMessage(hwndParent, IDC_COMBO_ABR, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[nSt][i]);
    m_pAEProps->get_AverageBitrate(&dwAverageBitrate);
    while (nAverageSel < 13 && BitRateValue(nSt, nAverageSel) < dwAverageBitrate)
        nAverageSel++;
    SendDlgItemMessage(hwndParent, IDC_COMBO_ABR, CB_SETCURSEL, nAverageSel, 0);
    if (BitRateValue(nSt, nAverageSel) != dwAverageBitrate)
        m_pAEProps->set_AverageBitrate(BitRateValue(nSt, nAverageSel));

//////////////////////////////////////////////////
// initialize VBR selection
//////////////////////////////////////////////////
    //VBRMIN, VBRMAX
    int j, nST;

    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMIN, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMAX, CB_RESETCONTENT, 0, 0);

    if (dwSampleRate >= 32000)
    {
            nST = 0;
            for (j=0; j<14 ;j++) {
                SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMIN, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[0][j]);
                SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMAX, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[0][j]);
            }
    }
    else
    {
            nST = 1;
            for (j = 0; j < 14; j++)
            {
                SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMIN, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[1][j]);
                SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMAX, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)szBitRateString[1][j]);
            }
    }

    DWORD dwMin,dwMax;
    m_pAEProps->get_VariableMin(&dwMin);
    m_pAEProps->get_VariableMax(&dwMax);

    int nVariableMinSel = 0;
    int nVariableMaxSel = 0;
    
    // BitRateValue() is in ascending order
    // We use this fact. We also know there are 14 bitrate values available.
    // We are going to use the closest possible, so we can limit loop with 13
    while (nVariableMinSel<13 && BitRateValue(nST, nVariableMinSel) < dwMin)
        nVariableMinSel++;
    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMIN, CB_SETCURSEL, nVariableMinSel, 0);

    while (nVariableMaxSel<13 && BitRateValue(nST, nVariableMaxSel) < dwMax)
        nVariableMaxSel++;
    SendDlgItemMessage(hwndParent, IDC_COMBO_VBRMAX, CB_SETCURSEL, nVariableMaxSel, 0);

    
    // check if the specified bitrate is found exactly and correct if not
    if (BitRateValue(nST, nVariableMinSel) != dwMin)
    {
        dwMin = BitRateValue(nST, nVariableMinSel);
        // we can change it, because it is independent of any other parameters
        // (but depends on some of them!)
        m_pAEProps->set_VariableMin(dwMin);
    }

    // check if the specified bitrate is found exactly and correct if not
    if (BitRateValue(nST, nVariableMaxSel) != dwMax)
    {
        dwMax = BitRateValue(nST, nVariableMaxSel);
        // we can change it, because it is independent of any other parameters
        // (but depends on some of them!)
        m_pAEProps->set_VariableMax(dwMax);
    }

    //
    // initialize checkboxes
    //
    DWORD dwPES;
    m_pAEProps->get_PESOutputEnabled(&dwPES);

    dwPES = 0;
    CheckDlgButton(hwndParent, IDC_CHECK_PES, dwPES ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwCRC;
    m_pAEProps->get_CRCFlag(&dwCRC);
    CheckDlgButton(hwndParent, IDC_CHECK_CRC, dwCRC ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwForceMono;
    m_pAEProps->get_ForceMono(&dwForceMono);
    CheckDlgButton(hwndParent, IDC_FORCE_MONO, dwForceMono ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwPrivate;
    m_pAEProps->get_PrivateFlag(&dwPrivate);
    CheckDlgButton(hwndParent, IDC_CHECK_PRIVATE, dwPrivate ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwReservoir;
    m_pAEProps->get_BitReservoir(&dwReservoir);
    CheckDlgButton(hwndParent, IDC_CHECK_RESERVOIR, dwReservoir ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwCopyright;
    m_pAEProps->get_CopyrightFlag(&dwCopyright);
    CheckDlgButton(hwndParent, IDC_CHECK_COPYRIGHT, dwCopyright ? BST_CHECKED : BST_UNCHECKED);

    DWORD dwOriginal;
    m_pAEProps->get_OriginalFlag(&dwOriginal);
    CheckDlgButton(hwndParent, IDC_CHECK_ORIGINAL, dwOriginal ? BST_CHECKED : BST_UNCHECKED);
}


/**
 * Enables or disables the dialog controls.
 */
void CMpegAudEncPropertyPage::EnableControls(HWND hwndParent, bool bEnable)
{
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_PES), false);//bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_RADIO_CBR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_COMBO_CBR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_RADIO_VBR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_RADIO_ABR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_COMBO_ABR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_PRIVATE), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_RESERVOIR), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_COMBO_VBRMIN), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_COMBO_VBRMAX), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_COPYRIGHT), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_ORIGINAL), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_CHECK_CRC), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_FORCE_MONO), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_SLIDER_QUALITY), bEnable);
    EnableWindow(GetDlgItem(hwndParent, IDC_COMBO_SAMPLE_RATE), bEnable);
}

/**
 * Marks the page as changed, and tells the property page site.
 */
void CMpegAudEncPropertyPage::SetDirty()
{
    m_bDirty = TRUE;
    if (m_pPageSite)
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
}

