#pragma once

#include "Locator.h"

#include <afxwin.h>
#include <opencv2/core.hpp>

#include <vector>

constexpr UINT WM_IMAGE_SELECTION_CHANGED = WM_APP + 201;
constexpr UINT WM_IMAGE_CURSOR_POSITION = WM_APP + 204;

class CImageViewControl : public CWnd {
public:
    void setImage(const cv::Mat& image);
    void setResults(const std::vector<LocatorResult>& results, int modelWidth, int modelHeight);
    void setSelectedResult(int index);
    int selectedResult() const { return selectedResult_; }
    const std::vector<CRect>& resultLabelRects() const { return labelRects_; }
    void setSelectionEnabled(bool enabled);
    void setSelection(const cv::Rect& selection);
    void clearSelection();
    cv::Rect selectedRect() const;
    bool hasResults() const { return !results_.empty(); }
    void resetView();
    double zoom() const { return zoom_; }
    CPoint imageToClient(double x, double y) const { return imageToScreen(x, y); }
    CRect displayRect() const { return bitmapRect_; }
    CPoint cursorImagePosition() const { return cursorImage_; }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnMButtonDown(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();

private:
    bool pointToImage(CPoint point, int& x, int& y) const;
    CPoint imageToScreen(double x, double y) const;
    CPoint clampToBitmap(CPoint point) const;
    void updateSelection();
    void updateBitmapRect();
    void clampView(double scale, const CRect& client);
    void updateCursor(CPoint point);
    void notifyCursor();

    cv::Mat image_;
    std::vector<LocatorResult> results_;
    std::vector<CRect> labelRects_;
    int selectedResult_ = -1;
    int modelWidth_ = 0;
    int modelHeight_ = 0;
    CRect bitmapRect_;
    double zoom_ = 1.0;
    cv::Point2d origin_ = cv::Point2d(0.0, 0.0);
    CPoint cursorImage_ = CPoint(-1, -1);
    bool panning_ = false;
    CPoint panLast_;
    bool selectionEnabled_ = false;
    bool dragging_ = false;
    bool hasSelection_ = false;
    CPoint dragStart_;
    CPoint dragCurrent_;
    cv::Rect selection_;

    DECLARE_MESSAGE_MAP()
};
