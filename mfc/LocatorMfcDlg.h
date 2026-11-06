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
                     const cv::Mat& search, LocatorBackend backend);

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

private:
    bool readConfig(LocatorConfig& config);
    void prepareResultList(bool batch);
    void clearResultDisplay();
    void showError(LocatorError error);

    cv::Mat searchImage_;
    cv::Mat modelImage_;
    CImageViewControl searchView_;
    CImageViewControl templateView_;
    CListCtrl resultsList_;
    CComboBox backendCombo_;
    bool selectionForRoi_ = false;
    bool useRoi_ = false;
    LocatorRect roi_ = { 0, 0, 0, 0 };

    DECLARE_MESSAGE_MAP()
};
