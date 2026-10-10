/**
 * @file
 * @brief The implementation of SystemFontPage.h.
 */

#include <streams.h>
#include <olectl.h>

#include <vector>

#include "../ACM/DialogFont.h"
#include "SystemFontPage.h"

/**
 * Creates the page.
 *
 * @param pName     the name of the object, for debugging.
 * @param pUnk      the outer object, for aggregation.
 * @param DialogId  the resource ID of the page's dialog. It must be a DIALOGEX
 *                  resource with a FONT statement.
 * @param TitleId   the resource ID of the page's title.
 */
CSystemFontPropertyPage::CSystemFontPropertyPage(LPCTSTR pName, LPUNKNOWN pUnk, int DialogId, int TitleId)
    : CBasePropertyPage(pName, pUnk, DialogId, TitleId),
      m_bHasObject(FALSE)
{
}

/**
 * Creates the dialog of the page in the font of the desktop. If that font or
 * the template copy is not available, it creates the dialog as the resource
 * describes it.
 *
 * @param hwndParent  the parent window.
 * @param lParam      the value that WM_INITDIALOG passes to DialogProc().
 * @return the dialog, or NULL if it could not be created.
 */
HWND CSystemFontPropertyPage::CreatePage(HWND hwndParent, LPARAM lParam)
{
    LOGFONTW font;
    std::vector<BYTE> dialog_template;

    if (SystemDialogFont(font) && DialogTemplateWithFont(g_hInst, (WORD) m_DialogId, font, dialog_template))
        return CreateDialogIndirectParam(g_hInst, (LPCDLGTEMPLATE) &dialog_template[0], hwndParent, DialogProc, lParam);
    return CreateDialogParam(g_hInst, MAKEINTRESOURCE(m_DialogId), hwndParent, DialogProc, lParam);
}

/**
 * Reports the title of the page and the size of its dialog in the font of
 * the desktop, so that the property frame makes room for it.
 *
 * @param pPageInfo  receives the title and the size.
 * @return NOERROR, or the error of CBasePropertyPage::GetPageInfo().
 */
STDMETHODIMP CSystemFontPropertyPage::GetPageInfo(LPPROPPAGEINFO pPageInfo)
{
    HRESULT const hr = CBasePropertyPage::GetPageInfo(pPageInfo);
    HWND page;
    RECT rect;

    if (FAILED(hr))
        return hr;
    page = CreatePage(GetDesktopWindow(), 0);
    if (page != NULL) {
        GetWindowRect(page, &rect);
        pPageInfo->size.cx = rect.right - rect.left;
        pPageInfo->size.cy = rect.bottom - rect.top;
        DestroyWindow(page);
    }
    return NOERROR;
}

/**
 * Passes the objects on to CBasePropertyPage::SetObjects(), and notes whether
 * the page now has one, which Activate() needs.
 *
 * @param cObjects  the number of objects, 0 or 1.
 * @param ppUnk     the objects.
 * @return the result of CBasePropertyPage::SetObjects().
 */
STDMETHODIMP CSystemFontPropertyPage::SetObjects(ULONG cObjects, LPUNKNOWN *ppUnk)
{
    HRESULT const hr = CBasePropertyPage::SetObjects(cObjects, ppUnk);

    if (cObjects == 1 && ppUnk != NULL && *ppUnk != NULL)
        m_bHasObject = TRUE;
    else if (cObjects == 0)
        m_bHasObject = FALSE;
    return hr;
}

/**
 * Creates and shows the page through CreatePage(), where
 * CBasePropertyPage::Activate() creates it from the resource.
 *
 * @param hwndParent  the window of the property frame.
 * @param pRect       where the page goes.
 * @param fModal      whether the frame is modal. Not used.
 * @return NOERROR, E_POINTER without a rectangle, E_UNEXPECTED without an
 *         object or when the page is already active, or E_OUTOFMEMORY if the
 *         dialog could not be created.
 */
STDMETHODIMP CSystemFontPropertyPage::Activate(HWND hwndParent, LPCRECT pRect, BOOL fModal)
{
    UNREFERENCED_PARAMETER(fModal);
    CheckPointer(pRect, E_POINTER);
    if (!m_bHasObject || m_hwnd != NULL)
        return E_UNEXPECTED;

    m_hwnd = CreatePage(hwndParent, (LPARAM) this);
    if (m_hwnd == NULL)
        return E_OUTOFMEMORY;

    OnActivate();
    Move(pRect);
    return Show(SW_SHOWNORMAL);
}
