#pragma once

#include "ImageViewControl.h"
#include "Locator.h"
#include "LocatorWorkflow.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>
#include <opencv2/core.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
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
    afx_msg void OnApplyRoi();
    afx_msg void OnReset();
    afx_msg void OnParameterChanged(UINT id);
    afx_msg void OnChoiceChanged();
    afx_msg void OnExecute();
    afx_msg void OnBatchBrowse();
    afx_msg void OnBatchRun();
    afx_msg void OnBatchCancel();
    afx_msg void OnModeTabsChanged(NMHDR* header, LRESULT* result);
    afx_msg void OnResultItemChanged(NMHDR* header, LRESULT* result);
    afx_msg LRESULT OnImageSelectionChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnImageCursorPosition(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSingleComplete(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnBatchProgress(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnBatchComplete(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSelfTest(WPARAM wParam, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor);
    afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem);
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg void OnDestroy();

private:
    struct SingleBaseline {
        cv::Mat search;
        cv::Mat model;
        CString searchPath;
        CString templatePath;
        LocatorConfig config;
        bool valid = false;
    };

    bool readConfig(LocatorConfig& config, CString* diagnosis = nullptr, int* invalidControl = nullptr);
    void updateExecutionHint();
    void updateBatchHint();
    void updateRoiFields();
    void writeConfig(const LocatorConfig& config);
    void captureBaseline();
    void setSingleControlsEnabled(bool enabled);
    void setBatchControlsEnabled(bool enabled);
    void showSingleResult(const LocatorWorkflow::SingleResult& run);
    void clearBatchResultDisplay();
    void showBatchResult(const LocatorWorkflow::BatchRun& run);
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
    std::thread singleWorker_;
    bool singleRunning_ = false;
    unsigned singleGeneration_ = 0;
    std::thread batchWorker_;
    bool batchRunning_ = false;
    std::atomic<bool> batchCancelRequested_ = false;
    unsigned batchGeneration_ = 0;
    std::function<void()> selfTestAction_;
    std::string selfTestError_;

    DECLARE_MESSAGE_MAP()
};
