#include "ImageViewControl.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

BEGIN_MESSAGE_MAP(CImageViewControl, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

void CImageViewControl::setImage(const cv::Mat& image)
{
    if (image.empty()) {
        image_.release();
    } else {
        image_ = image.clone();
    }
    results_.clear();
    clearSelection();
    Invalidate();
}

void CImageViewControl::setResults(const std::vector<LocatorResult>& results,
                                   int modelWidth, int modelHeight)
{
    results_ = results;
    modelWidth_ = modelWidth;
    modelHeight_ = modelHeight;
    Invalidate();
}

void CImageViewControl::setSelectionEnabled(bool enabled)
{
    selectionEnabled_ = enabled;
    if (!enabled) {
        dragging_ = false;
    }
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

BOOL CImageViewControl::OnEraseBkgnd(CDC* dc)
{
    (void)dc;
    return TRUE;
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
    if (dragging_) {
        point.x = std::max(bitmapRect_.left, std::min(point.x, bitmapRect_.right - 1));
        point.y = std::max(bitmapRect_.top, std::min(point.y, bitmapRect_.bottom - 1));
        dragCurrent_ = point;
        updateSelection();
        Invalidate();
    }
    CWnd::OnMouseMove(flags, point);
}

void CImageViewControl::OnLButtonUp(UINT flags, CPoint point)
{
    if (dragging_) {
        dragCurrent_ = point;
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

void CImageViewControl::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(24, 26, 28));
    bitmapRect_.SetRectEmpty();
    if (image_.empty()) {
        dc.SetTextColor(RGB(180, 180, 180));
        dc.SetBkMode(TRANSPARENT);
        dc.TextOutW(12, 12, L"暂无图像");
        return;
    }

    const double scale = std::min(static_cast<double>(client.Width()) / image_.cols,
                                  static_cast<double>(client.Height()) / image_.rows);
    const int width = std::max(1, static_cast<int>(image_.cols * scale));
    const int height = std::max(1, static_cast<int>(image_.rows * scale));
    bitmapRect_ = CRect(client.left + (client.Width() - width) / 2,
                        client.top + (client.Height() - height) / 2,
                        client.left + (client.Width() - width) / 2 + width,
                        client.top + (client.Height() - height) / 2 + height);

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
    CPen* oldPen = dc.SelectObject(&resultPen);
    CBrush* oldBrush = static_cast<CBrush*>(dc.SelectStockObject(HOLLOW_BRUSH));
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(0, 255, 120));
    for (const LocatorResult& result : results_) {
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
        label.Format(L"角度 %.2f，尺度 %.3f，分数 %.2f", result.angle, result.scale, result.score);
        dc.TextOutW(center.x + 8, center.y + 8, label);
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
