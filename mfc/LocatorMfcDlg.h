#pragma once

#include "ImageViewControl.h"
#include "Locator.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>
#include <opencv2/core.hpp>

#include <string>
#include <utility>
#include <vector>

class CLocatorMfcDlg : public CDialogEx {
public:
    explicit CLocatorMfcDlg(CWnd* parent = nullptr);
    void runSelfTest(const std::string& directory, const cv::Mat& model,
                     const cv::Mat& search, LocatorBackend backend,
                     const wchar_t* previewPath = nullptr);

protected:
    BOOL OnInitDialog() override;
    afx_msg void OnLoadSearch();
    afx_msg void OnLoadTemplate();
    afx_msg void OnSelectTemplate();
    afx_msg void OnSelectRoi();
    afx_msg void OnClearRoi();
    afx_msg void OnExecute();
    afx_msg void OnBatchBrowse();
    afx_msg void OnBatchRun();
    afx_msg LRESULT OnImageSelectionChanged(WPARAM wParam, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem);
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);

private:
    bool readConfig(LocatorConfig& config);
    void prepareResultList(bool batch);
    void resizeResultColumns();
    void captureInitialLayout();
    void applyLayout();
    void clearResultDisplay();
    void showError(LocatorError error);
    void savePreview(const wchar_t* path);

    cv::Mat searchImage_;
    cv::Mat modelImage_;
    CImageViewControl searchView_;
    CImageViewControl templateView_;
    CListCtrl resultsList_;
    CComboBox backendCombo_;
    CFont titleFont_;
    CFont sectionFont_;
    std::vector<std::pair<HWND, CRect>> initialControls_;
    CSize initialClientSize_ = CSize(0, 0);
    CSize minimumWindowSize_ = CSize(0, 0);
    bool batchResults_ = false;
    bool selectionForRoi_ = false;
    bool useRoi_ = false;
    LocatorRect roi_ = { 0, 0, 0, 0 };

    DECLARE_MESSAGE_MAP()
};
