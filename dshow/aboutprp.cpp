/*
 *  LAME MP3 encoder for DirectShow
 *  About property page
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

#include <windows.h>
#include <streams.h>
#include <olectl.h>
#include <commctrl.h>
#include "iaudioprops.h"
#include "../ACM/SettingText.h"
#include "SystemFontPage.h"
#include "aboutprp.h"
#include "mpegac.h"
#include "resource.h"
#include "Reg.h"
#include <stdio.h>

// -------------------------------------------------------------------------
// CMAEAbout
// -------------------------------------------------------------------------



/**
 * Creates the About page. The class factory of the filter calls it.
 */
CUnknown * WINAPI CMAEAbout::CreateInstance(LPUNKNOWN lpunk, HRESULT *phr)
{
    CUnknown *punk = new CMAEAbout(lpunk, phr);
    if (punk == NULL) {
        *phr = E_OUTOFMEMORY;
    }

    return punk;
}


/**
 * Creates the About page.
 */
CMAEAbout::CMAEAbout(LPUNKNOWN lpunk, HRESULT *phr)
    : CSystemFontPropertyPage(NAME("About LAME Ain't MP3 Encoder"), lpunk,
        IDD_ABOUT,IDS_ABOUT)
    , m_fWindowInactive(TRUE)
{
    ASSERT(phr);

//    InitCommonControls();
}

/**
 * Does nothing. The About page does not use the filter.
 */
HRESULT CMAEAbout::OnConnect(IUnknown *pUnknown)
{
    return NOERROR;
}


/**
 * Does nothing, like OnConnect().
 */
HRESULT CMAEAbout::OnDisconnect()
{
    // Release the interface

    return NOERROR;
}


/**
 * Called when the dialog is created. Shows the license text, the LAME version
 * and the address of the LAME home page.
 */
HRESULT CMAEAbout::OnActivate(void)
{
    // Add text to the window.
    m_fWindowInactive = FALSE;
    SendDlgItemMessage(m_hwnd, IDC_LAME_LA, WM_SETTEXT, 0, (LPARAM)LICENSE_NOTICE);


    CHAR strbuf[250];
    snprintf(strbuf, sizeof strbuf, "LAME Audio Encoder v%s", get_lame_version());
    SendDlgItemMessage(m_hwnd, IDC_LAME_VER, WM_SETTEXT, 0, (LPARAM)strbuf);
    SendDlgItemMessage(m_hwnd, IDC_LAME_URL, WM_SETTEXT, 0, (LPARAM)get_lame_url());
    SendDlgItemMessage(m_hwnd, IDC_LAME_CREDITS, WM_SETTEXT, 0, (LPARAM)ABOUT_CREDITS);
    SendDlgItemMessage(m_hwnd, IDC_LAME_ICON, WM_SETTEXT, 0, (LPARAM)ABOUT_ICON_CREDIT);
    return NOERROR;
}

/**
 * Called when the dialog is destroyed.
 */
HRESULT CMAEAbout::OnDeactivate(void)
{
    m_fWindowInactive = TRUE;
    return NOERROR;
}


/**
 * Does nothing. The About page has no settings.
 */
HRESULT CMAEAbout::OnApplyChanges(void)
{
    return NOERROR;
}


/**
 * Handles the messages of the dialog.
 */
INT_PTR CMAEAbout::OnReceiveMessage( HWND hwnd
                                , UINT uMsg
                                , WPARAM wParam
                                , LPARAM lParam)
{
    if (m_fWindowInactive)
        return FALSE;

    switch (uMsg)
    {
    case WM_DESTROY:
        return TRUE;

    default:
        return FALSE;
    }
}

/**
 * Marks the page as changed, and tells the property page site.
 */
void CMAEAbout::SetDirty()
{
    m_bDirty = TRUE;

    if (m_pPageSite)
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
}

