#pragma once

#include "Locator.h"

#include <afxwin.h>
#include <opencv2/core.hpp>

#include <vector>

constexpr UINT WM_IMAGE_SELECTION_CHANGED = WM_APP + 201;

class CImageViewControl : public CWnd {
public:
    void setImage(const cv::Mat& image);
    void setResults(const std::vector<LocatorResult>& results, int modelWidth, int modelHeight);
    void setSelectionEnabled(bool enabled);
    void clearSelection();
    cv::Rect selectedRect() const;
    bool hasResults() const { return !results_.empty(); }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);

private:
    bool pointToImage(CPoint point, int& x, int& y) const;
    CPoint imageToScreen(double x, double y) const;
    void updateSelection();

    cv::Mat image_;
    std::vector<LocatorResult> results_;
    int modelWidth_ = 0;
    int modelHeight_ = 0;
    CRect bitmapRect_;
    bool selectionEnabled_ = false;
    bool dragging_ = false;
    bool hasSelection_ = false;
    CPoint dragStart_;
    CPoint dragCurrent_;
    cv::Rect selection_;

    DECLARE_MESSAGE_MAP()
};
