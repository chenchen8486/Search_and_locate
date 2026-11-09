#pragma once

#include "ImageViewControl.h"
#include "Locator.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>
#include <opencv2/core.hpp>

#include <functional>
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
    afx_msg void OnSelectRoi();
    afx_msg void OnClearRoi();
    afx_msg void OnReset();
    afx_msg void OnExecute();
    afx_msg void OnBatchBrowse();
    afx_msg void OnBatchRun();
    afx_msg void OnModeTabsChanged(NMHDR* header, LRESULT* result);
    afx_msg LRESULT OnImageSelectionChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSelfTest(WPARAM wParam, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem);
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);

private:
    struct SingleBaseline {
        cv::Mat search;
        cv::Mat model;
        CString searchPath;
        CString templatePath;
        LocatorConfig config;
        bool valid = false;
    };

    bool readConfig(LocatorConfig& config);
    void writeConfig(const LocatorConfig& config);
    void captureBaseline();
    void prepareResultList(bool batch);
    void resizeResultColumns();
    void initializeLayout();
    void applyLayout();
    void showInspectorPage(int page);
    void setBatchStats(const std::vector<CString>& values);
    void clearResultDisplay();
    void showError(LocatorError error);
    void savePreview(const wchar_t* path);
    void verifySelfTest(const std::string& directory, const cv::Mat& model,
                        const cv::Mat& search, LocatorBackend backend,
                        const wchar_t* previewPath);

    cv::Mat searchImage_;
    cv::Mat modelImage_;
    SingleBaseline baseline_;
    CImageViewControl searchView_;
    CImageViewControl templateView_;
    CListCtrl resultsList_;
    CListCtrl batchStats_;
    CComboBox backendCombo_;
    CComboBox subpixelCombo_;
    CTabCtrl modeTabs_;
    CFont titleFont_;
    CFont sectionFont_;
    CFont numericFont_;
    CBrush dialogBrush_;
    CBrush cardBrush_;
    CSize minimumWindowSize_ = CSize(0, 0);
    int dpiX_ = 96;
    int dpiY_ = 96;
    int activeInspectorPage_ = -1;
    bool batchResults_ = false;
    bool useRoi_ = false;
    LocatorRect roi_ = { 0, 0, 0, 0 };
    std::function<void()> selfTestAction_;
    std::string selfTestError_;

    DECLARE_MESSAGE_MAP()
};
