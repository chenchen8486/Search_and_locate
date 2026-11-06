#pragma once

#include "ImageViewControl.h"
#include "Locator.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>
#include <opencv2/core.hpp>

#include <string>

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

private:
    bool readConfig(LocatorConfig& config);
    void prepareResultList(bool batch);
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
    bool selectionForRoi_ = false;
    bool useRoi_ = false;
    LocatorRect roi_ = { 0, 0, 0, 0 };

    DECLARE_MESSAGE_MAP()
};
