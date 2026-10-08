/*
 *  LAME MP3 encoder for DirectShow
 *  Registry calls handling class
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

#include <Windows.h>

#include "reg.h"

namespace Lame
{

CRegKey::CRegKey(void)
{
    m_hRootKey = NULL;
    m_hKey = NULL;
}


CRegKey::CRegKey(HKEY rt, LPCTSTR pName)
{
    m_hRootKey = rt;
    m_hKey = NULL;
    if(pName)
        Open(m_hRootKey, pName);
}


CRegKey::~CRegKey(void)
{
    Close();
}



BOOL    CRegKey::Open(HKEY rootKey, LPCTSTR pName)
{
    if(m_hKey)
        Close();

    m_hRootKey = rootKey;
    if(pName) 
    {
        if(RegOpenKeyEx(m_hRootKey, pName, 0, KEY_READ, &m_hKey) != ERROR_SUCCESS) 
        {
            m_hKey = NULL;
            return FALSE;
        }
    }
    else 
    {
        m_hKey = m_hRootKey;
    }

    return TRUE;
}


BOOL    CRegKey::Create(HKEY rootKey, LPCTSTR pName)
{
    if(m_hKey)
        Close();

    m_hRootKey = rootKey;
    if(pName) 
    {
        if(RegCreateKeyEx(m_hRootKey, pName, NULL,
                TEXT(""), REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
                &m_hKey, NULL) != ERROR_SUCCESS) 
        {

            m_hKey = NULL;
            return FALSE;
        }
    }
    m_hRootKey = m_hKey;

    return TRUE;
}


BOOL    CRegKey::Close(void)
{
    if(m_hKey && m_hKey != m_hRootKey)
        RegCloseKey(m_hKey);

    m_hKey = 0;
    return TRUE;
}

DWORD   CRegKey::getDWORD(LPCTSTR valuename, DWORD bDefault)
{
    DWORD dwData;
    DWORD cbData;
    DWORD   dwType;

    if(!m_hKey)
        return bDefault;

    cbData = sizeof(dwData);
    if(RegQueryValueEx(m_hKey, valuename, NULL, &dwType, (PBYTE)&dwData, &cbData) == ERROR_SUCCESS) {
        if(dwType == REG_DWORD) 
        {
            return (UINT)dwData;
        }
    }

    return bDefault;
}


void    CRegKey::setDWORD(LPCTSTR valuename, DWORD dwValue)
{
    DWORD dwData = dwValue;
    RegSetValueEx(m_hKey, valuename, 0, REG_DWORD, (PBYTE)&dwData, sizeof(dwData));
}


} // namespace Lame
