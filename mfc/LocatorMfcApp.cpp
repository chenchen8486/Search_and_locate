#include "LocatorMfcApp.h"
#include "LocatorMfcDlg.h"

#include <afxcmn.h>

CLocatorMfcApp theApp;

BOOL CLocatorMfcApp::InitInstance()
{
    INITCOMMONCONTROLSEX controls = { sizeof(INITCOMMONCONTROLSEX),
                                      ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    CWinApp::InitInstance();

    CLocatorMfcDlg dialog;
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
