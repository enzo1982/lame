/**
 * @file
 * @brief A property page in the font of the desktop.
 *
 * CBasePropertyPage creates its dialog from the resource, in the font that
 * the resource names. CSystemFontPropertyPage creates it in the font that the
 * desktop uses for dialogs (SystemDialogFont()), and reports the size of that
 * dialog to the property frame. The pages of the filter derive from it.
 */

#if !defined(LAME_DSHOW_SYSTEMFONTPAGE_H)
#define LAME_DSHOW_SYSTEMFONTPAGE_H

class CSystemFontPropertyPage : public CBasePropertyPage
{
public:
    CSystemFontPropertyPage(LPCTSTR pName, LPUNKNOWN pUnk, int DialogId, int TitleId);

    STDMETHODIMP GetPageInfo(LPPROPPAGEINFO pPageInfo);
    STDMETHODIMP SetObjects(ULONG cObjects, LPUNKNOWN *ppUnk);
    STDMETHODIMP Activate(HWND hwndParent, LPCRECT pRect, BOOL fModal);

private:
    HWND CreatePage(HWND hwndParent, LPARAM lParam);

    /** TRUE while the page has an object, as SetObjects() was last told. */
    BOOL m_bHasObject;
};

#endif // !defined(LAME_DSHOW_SYSTEMFONTPAGE_H)
