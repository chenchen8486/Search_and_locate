#include "ImageViewControl.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

BEGIN_MESSAGE_MAP(CImageViewControl, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

void CImageViewControl::setImage(const cv::Mat& image)
{
    if (image.empty()) {
        image_.release();
    } else {
        image_ = image.clone();
    }
    results_.clear();
    resetView();
    clearSelection();
    Invalidate();
}

void CImageViewControl::setResults(const std::vector<LocatorResult>& results,
                                   int modelWidth, int modelHeight)
{
    results_ = results;
    modelWidth_ = modelWidth;
    modelHeight_ = modelHeight;
    selectedResult_ = -1;
    labelRects_.clear();
    Invalidate();
}

void CImageViewControl::setSelectedResult(int index)
{
    selectedResult_ = index >= 0 && index < static_cast<int>(results_.size()) ? index : -1;
    Invalidate();
    UpdateWindow();
}

void CImageViewControl::setSelectionEnabled(bool enabled)
{
    selectionEnabled_ = enabled;
    if (!enabled) {
        dragging_ = false;
    }
}

void CImageViewControl::setSelection(const cv::Rect& selection)
{
    selection_ = selection;
    hasSelection_ = !selection.empty();
    Invalidate();
}

void CImageViewControl::clearSelection()
{
    hasSelection_ = false;
    selection_ = cv::Rect();
    Invalidate();
}

cv::Rect CImageViewControl::selectedRect() const
{
    return selection_;
}

void CImageViewControl::clampView(double scale, const CRect& client)
{
    if (image_.empty() || scale <= 0.0) {
        return;
    }
    const double maxX = std::max(0.0, image_.cols - client.Width() / scale);
    const double maxY = std::max(0.0, image_.rows - client.Height() / scale);
    origin_.x = std::max(0.0, std::min(origin_.x, maxX));
    origin_.y = std::max(0.0, std::min(origin_.y, maxY));
}

void CImageViewControl::updateBitmapRect()
{
    if (image_.empty()) {
        bitmapRect_.SetRectEmpty();
        return;
    }
    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        bitmapRect_.SetRectEmpty();
        return;
    }
    const double fit = std::min(static_cast<double>(client.Width()) / image_.cols,
                                static_cast<double>(client.Height()) / image_.rows);
    const double scale = fit * zoom_;
    if (zoom_ <= 1.0) {
        origin_ = cv::Point2d(0.0, 0.0);
        const int width = std::max(1, static_cast<int>(image_.cols * scale));
        const int height = std::max(1, static_cast<int>(image_.rows * scale));
        bitmapRect_ = CRect(client.left + (client.Width() - width) / 2,
                            client.top + (client.Height() - height) / 2,
                            client.left + (client.Width() - width) / 2 + width,
                            client.top + (client.Height() - height) / 2 + height);
        return;
    }
    clampView(scale, client);
    const int left = client.left + static_cast<int>(std::lround(-origin_.x * scale));
    const int top = client.top + static_cast<int>(std::lround(-origin_.y * scale));
    bitmapRect_ = CRect(left, top,
                        left + std::max(1, static_cast<int>(std::lround(image_.cols * scale))),
                        top + std::max(1, static_cast<int>(std::lround(image_.rows * scale))));
}

void CImageViewControl::resetView()
{
    zoom_ = 1.0;
    origin_ = cv::Point2d(0.0, 0.0);
    panning_ = false;
    cursorImage_ = CPoint(-1, -1);
    updateBitmapRect();
    Invalidate();
}

void CImageViewControl::notifyCursor()
{
    if (GetParent() != nullptr) {
        GetParent()->PostMessage(WM_IMAGE_CURSOR_POSITION, 0, 0);
    }
}

void CImageViewControl::updateCursor(CPoint point)
{
    CPoint current(-1, -1);
    int x = 0;
    int y = 0;
    if (pointToImage(point, x, y)) {
        current = CPoint(x, y);
    }
    cursorImage_ = current;
    notifyCursor();
    TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, GetSafeHwnd(), 0 };
    TrackMouseEvent(&tracking);
}

BOOL CImageViewControl::OnEraseBkgnd(CDC* dc)
{
    (void)dc;
    return TRUE;
}

void CImageViewControl::OnSize(UINT type, int width, int height)
{
    CWnd::OnSize(type, width, height);
    if (type != SIZE_MINIMIZED) {
        updateBitmapRect();
        Invalidate();
        UpdateWindow();
    }
}

bool CImageViewControl::pointToImage(CPoint point, int& x, int& y) const
{
    if (image_.empty() || !bitmapRect_.PtInRect(point)) {
        return false;
    }
    x = (point.x - bitmapRect_.left) * image_.cols / bitmapRect_.Width();
    y = (point.y - bitmapRect_.top) * image_.rows / bitmapRect_.Height();
    x = std::max(0, std::min(image_.cols - 1, x));
    y = std::max(0, std::min(image_.rows - 1, y));
    return true;
}

CPoint CImageViewControl::imageToScreen(double x, double y) const
{
    const double scaleX = static_cast<double>(bitmapRect_.Width()) / image_.cols;
    const double scaleY = static_cast<double>(bitmapRect_.Height()) / image_.rows;
    return CPoint(bitmapRect_.left + static_cast<LONG>(std::lround(x * scaleX)),
                  bitmapRect_.top + static_cast<LONG>(std::lround(y * scaleY)));
}

CPoint CImageViewControl::clampToBitmap(CPoint point) const
{
    CRect client;
    GetClientRect(&client);
    CRect visible;
    if (!visible.IntersectRect(&bitmapRect_, &client)) {
        return point;
    }
    point.x = std::max(visible.left, std::min(point.x, visible.right - 1));
    point.y = std::max(visible.top, std::min(point.y, visible.bottom - 1));
    return point;
}

void CImageViewControl::updateSelection()
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!pointToImage(dragStart_, x0, y0) || !pointToImage(dragCurrent_, x1, y1)) {
        hasSelection_ = false;
        return;
    }
    const int left = std::min(x0, x1);
    const int top = std::min(y0, y1);
    selection_ = cv::Rect(left, top, std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1);
    hasSelection_ = selection_.width > 0 && selection_.height > 0;
}

void CImageViewControl::OnLButtonDown(UINT flags, CPoint point)
{
    int x = 0;
    int y = 0;
    if (selectionEnabled_ && pointToImage(point, x, y)) {
        SetCapture();
        dragging_ = true;
        hasSelection_ = true;
        dragStart_ = point;
        dragCurrent_ = point;
        updateSelection();
        Invalidate();
    }
    CWnd::OnLButtonDown(flags, point);
}

void CImageViewControl::OnMouseMove(UINT flags, CPoint point)
{
    if (panning_) {
        CRect client;
        GetClientRect(&client);
        const double fit = std::min(static_cast<double>(client.Width()) / image_.cols,
                                    static_cast<double>(client.Height()) / image_.rows);
        const double scale = fit * zoom_;
        origin_.x -= (point.x - panLast_.x) / scale;
        origin_.y -= (point.y - panLast_.y) / scale;
        panLast_ = point;
        updateBitmapRect();
        Invalidate();
    } else {
        if (dragging_) {
            dragCurrent_ = clampToBitmap(point);
            updateSelection();
            Invalidate();
        }
        updateCursor(point);
    }
    CWnd::OnMouseMove(flags, point);
}

void CImageViewControl::OnLButtonUp(UINT flags, CPoint point)
{
    if (dragging_) {
        dragCurrent_ = clampToBitmap(point);
        dragging_ = false;
        ReleaseCapture();
        updateSelection();
        Invalidate();
        if (hasSelection_ && GetParent() != nullptr) {
            GetParent()->PostMessage(WM_IMAGE_SELECTION_CHANGED, 0, 0);
        }
    }
    CWnd::OnLButtonUp(flags, point);
}

BOOL CImageViewControl::OnMouseWheel(UINT flags, short delta, CPoint point)
{
    (void)flags;
    if (image_.empty() || bitmapRect_.Width() <= 0) {
        return TRUE;
    }
    ScreenToClient(&point);
    const double oldZoom = zoom_;
    const double newZoom = std::max(1.0, std::min(64.0, oldZoom * (delta > 0 ? 1.25 : 0.8)));
    if (newZoom == oldZoom) {
        return TRUE;
    }
    const double worldX = (point.x - bitmapRect_.left) * image_.cols / bitmapRect_.Width();
    const double worldY = (point.y - bitmapRect_.top) * image_.rows / bitmapRect_.Height();
    CRect client;
    GetClientRect(&client);
    const double fit = std::min(static_cast<double>(client.Width()) / image_.cols,
                                static_cast<double>(client.Height()) / image_.rows);
    zoom_ = newZoom;
    origin_ = cv::Point2d(worldX - point.x / (fit * zoom_),
                          worldY - point.y / (fit * zoom_));
    updateBitmapRect();
    updateCursor(point);
    Invalidate();
    return TRUE;
}

void CImageViewControl::OnMButtonDown(UINT flags, CPoint point)
{
    if (zoom_ > 1.0 && !image_.empty()) {
        SetCapture();
        panning_ = true;
        panLast_ = point;
    }
    CWnd::OnMButtonDown(flags, point);
}

void CImageViewControl::OnMButtonUp(UINT flags, CPoint point)
{
    if (panning_) {
        panning_ = false;
        ReleaseCapture();
        updateCursor(point);
    }
    CWnd::OnMButtonUp(flags, point);
}

void CImageViewControl::OnMouseLeave()
{
    cursorImage_ = CPoint(-1, -1);
    notifyCursor();
}

void CImageViewControl::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(24, 26, 28));
    dc.IntersectClipRect(client);
    updateBitmapRect();
    if (image_.empty()) {
        labelRects_.clear();
        dc.SetTextColor(RGB(180, 180, 180));
        dc.SetBkMode(TRANSPARENT);
        dc.TextOutW(12, 12, L"暂无图像");
        return;
    }

    struct GrayBitmapInfo {
        BITMAPINFOHEADER header;
        RGBQUAD colors[256];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = image_.cols;
    info.header.biHeight = -image_.rows;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biCompression = BI_RGB;
    const int stride = (image_.cols + 3) & ~3;
    std::vector<BYTE> packed(static_cast<size_t>(stride) * image_.rows);
    for (int row = 0; row < image_.rows; ++row) {
        std::copy(image_.ptr<uchar>(row), image_.ptr<uchar>(row) + image_.cols,
                  packed.begin() + static_cast<size_t>(row) * stride);
    }
    info.header.biSizeImage = static_cast<DWORD>(packed.size());
    for (int i = 0; i < 256; ++i) {
        info.colors[i].rgbRed = static_cast<BYTE>(i);
        info.colors[i].rgbGreen = static_cast<BYTE>(i);
        info.colors[i].rgbBlue = static_cast<BYTE>(i);
    }
    ::StretchDIBits(dc.GetSafeHdc(), bitmapRect_.left, bitmapRect_.top,
                    bitmapRect_.Width(), bitmapRect_.Height(),
                    0, 0, image_.cols, image_.rows, packed.data(),
                    reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, SRCCOPY);

    CPen resultPen(PS_SOLID, 2, RGB(0, 220, 100));
    CPen selectedPen(PS_SOLID, 3, RGB(0, 200, 255));
    CPen* oldPen = dc.SelectObject(&resultPen);
    CBrush* oldBrush = static_cast<CBrush*>(dc.SelectStockObject(HOLLOW_BRUSH));
    dc.SetBkMode(TRANSPARENT);
    labelRects_.clear();
    for (int resultIndex = 0; resultIndex < static_cast<int>(results_.size()); ++resultIndex) {
        const LocatorResult& result = results_[resultIndex];
        const bool selected = resultIndex == selectedResult_;
        dc.SelectObject(selected ? &selectedPen : &resultPen);
        dc.SetTextColor(selected ? RGB(0, 220, 255) : RGB(0, 255, 120));
        const double halfWidth = modelWidth_ * result.scale / 2.0;
        const double halfHeight = modelHeight_ * result.scale / 2.0;
        const double cosine = std::cos(result.angle * kPi / 180.0);
        const double sine = std::sin(result.angle * kPi / 180.0);
        const int signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        CPoint corners[4];
        for (int i = 0; i < 4; ++i) {
            const double dx = signs[i][0] * halfWidth;
            const double dy = signs[i][1] * halfHeight;
            corners[i] = imageToScreen(result.x + dx * cosine - dy * sine,
                                       result.y + dx * sine + dy * cosine);
        }
        dc.Polygon(corners, 4);

        const CPoint center = imageToScreen(result.x, result.y);
        dc.MoveTo(center.x - 6, center.y);
        dc.LineTo(center.x + 6, center.y);
        dc.MoveTo(center.x, center.y - 6);
        dc.LineTo(center.x, center.y + 6);
        CString label;
        label.Format(L"#%d  角度 %.2f，尺度 %.3f，分数 %.2f",
                     resultIndex + 1, result.angle, result.scale, result.score);
        const CSize textSize = dc.GetTextExtent(label);
        const int offsets[4][2] = {
            { 8, 8 }, { -textSize.cx - 8, 8 },
            { 8, -textSize.cy - 8 }, { -textSize.cx - 8, -textSize.cy - 8 }
        };
        CRect placedLabel;
        for (const auto& offset : offsets) {
            const int left = std::max(client.left, std::min(center.x + offset[0],
                                                           client.right - textSize.cx));
            const int top = std::max(client.top, std::min(center.y + offset[1],
                                                         client.bottom - textSize.cy));
            const CRect candidate(left, top, left + textSize.cx, top + textSize.cy);
            bool overlaps = false;
            for (const CRect& previous : labelRects_) {
                if (previous.Width() > 0 && CRect().IntersectRect(candidate, previous)) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                placedLabel = candidate;
                break;
            }
        }
        if (placedLabel.Width() > 0) {
            dc.TextOutW(placedLabel.left, placedLabel.top, label);
        }
        labelRects_.push_back(placedLabel);
    }
    dc.SelectObject(oldBrush);
    dc.SelectObject(oldPen);

    if (hasSelection_) {
        const CPoint topLeft = imageToScreen(selection_.x, selection_.y);
        const CPoint bottomRight = imageToScreen(selection_.x + selection_.width,
                                                 selection_.y + selection_.height);
        CPen selectionPen(PS_SOLID, 2, RGB(255, 80, 60));
        CPen* previousPen = dc.SelectObject(&selectionPen);
        CBrush* previousBrush = static_cast<CBrush*>(dc.SelectStockObject(HOLLOW_BRUSH));
        dc.Rectangle(CRect(topLeft, bottomRight));
        dc.SelectObject(previousBrush);
        dc.SelectObject(previousPen);
    }
}
