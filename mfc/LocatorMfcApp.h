#pragma once

#include <afxwin.h>

class CLocatorMfcApp : public CWinApp {
public:
    BOOL InitInstance() override;
};

extern CLocatorMfcApp theApp;
