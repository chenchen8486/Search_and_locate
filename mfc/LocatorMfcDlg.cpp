#include "LocatorMfcDlg.h"

#include "BatchData.h"
#include "BatchLocator.h"
#include "LocatorWorkflow.h"
#include "resource.h"

#include <afxdlgs.h>
#include <atlconv.h>
#include <atlimage.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

bool readDouble(CWnd* window, int id, double& value)
{
    CString text;
    window->GetDlgItemText(id, text);
    text.Trim();
    if (text.IsEmpty()) {
        return false;
    }
    wchar_t* end = nullptr;
    value = std::wcstod(text.GetString(), &end);
    return end != text.GetString() && *end == L'\0' && std::isfinite(value);
}

bool readInt(CWnd* window, int id, int& value)
{
    CString text;
    window->GetDlgItemText(id, text);
    text.Trim();
    if (text.IsEmpty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(text.GetString(), &end, 10);
    if (end == text.GetString() || *end != L'\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

const wchar_t* errorText(LocatorError error)
{
    switch (error) {
    case LocatorError::Ok: return L"成功";
    case LocatorError::InvalidImage: return L"图像为空、格式无效或模板大于搜索区域";
    case LocatorError::InvalidConfig: return L"配置参数无效";
    case LocatorError::InvalidRoi: return L"ROI 为空、越界或小于模板";
    case LocatorError::NotInitialized: return L"定位器尚未初始化";
    case LocatorError::BackendUnavailable: return L"所选计算后端不可用";
    case LocatorError::InternalError: return L"核心算法内部错误";
    }
    return L"未知错误";
}

} // namespace

CLocatorMfcDlg::CLocatorMfcDlg(CWnd* parent)
    : CDialogEx(IDD_LOCATOR_MFC, parent)
{
}

BEGIN_MESSAGE_MAP(CLocatorMfcDlg, CDialogEx)
    ON_BN_CLICKED(IDC_LOAD_SEARCH, &CLocatorMfcDlg::OnLoadSearch)
    ON_BN_CLICKED(IDC_LOAD_TEMPLATE, &CLocatorMfcDlg::OnLoadTemplate)
    ON_BN_CLICKED(IDC_SELECT_TEMPLATE, &CLocatorMfcDlg::OnSelectTemplate)
    ON_BN_CLICKED(IDC_SELECT_ROI, &CLocatorMfcDlg::OnSelectRoi)
    ON_BN_CLICKED(IDC_CLEAR_ROI, &CLocatorMfcDlg::OnClearRoi)
    ON_BN_CLICKED(IDC_EXECUTE, &CLocatorMfcDlg::OnExecute)
    ON_BN_CLICKED(IDC_BATCH_BROWSE, &CLocatorMfcDlg::OnBatchBrowse)
    ON_BN_CLICKED(IDC_BATCH_RUN, &CLocatorMfcDlg::OnBatchRun)
    ON_MESSAGE(WM_IMAGE_SELECTION_CHANGED, &CLocatorMfcDlg::OnImageSelectionChanged)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_WM_DRAWITEM()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

BOOL CLocatorMfcDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(L"搜索匹配定位工具");
    const int panelIds[] = {
        IDC_PANEL_WORKSPACE, IDC_PANEL_PARAMETERS, IDC_PANEL_RESULTS, IDC_PANEL_BATCH
    };
    for (int id : panelIds) {
        GetDlgItem(id)->ShowWindow(SW_HIDE);
    }

    LOGFONT font = {};
    GetFont()->GetLogFont(&font);
    font.lfWeight = FW_SEMIBOLD;
    sectionFont_.CreateFontIndirectW(&font);
    font.lfHeight = static_cast<LONG>(font.lfHeight * 1.65);
    titleFont_.CreateFontIndirectW(&font);
    GetDlgItem(IDC_APP_TITLE)->SetFont(&titleFont_);
    const int sectionIds[] = {
        IDC_SECTION_WORKSPACE, IDC_SECTION_PARAMETERS, IDC_SECTION_RESULTS, IDC_SECTION_BATCH
    };
    for (int id : sectionIds) {
        GetDlgItem(id)->SetFont(&sectionFont_);
    }

    searchView_.SubclassDlgItem(IDC_SEARCH_VIEW, this);
    templateView_.SubclassDlgItem(IDC_TEMPLATE_VIEW, this);
    resultsList_.SubclassDlgItem(IDC_RESULT_LIST, this);
    resultsList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    backendCombo_.SubclassDlgItem(IDC_BACKEND, this);
    backendCombo_.AddString(L"CPU");
    backendCombo_.AddString(L"GPU");
    backendCombo_.SetCurSel(0);
    prepareResultList(false);

    SetDlgItemText(IDC_ANGLE_MIN, L"0");
    SetDlgItemText(IDC_ANGLE_MAX, L"0");
    SetDlgItemText(IDC_ANGLE_STEP, L"1");
    SetDlgItemText(IDC_SCALE_MIN, L"1");
    SetDlgItemText(IDC_SCALE_MAX, L"1");
    SetDlgItemText(IDC_SCALE_STEP, L"0.01");
    SetDlgItemText(IDC_PYRAMID, L"1");
    SetDlgItemText(IDC_SUBPIXEL, L"8");
    SetDlgItemText(IDC_MIN_SCORE, L"0");
    SetDlgItemText(IDC_MAX_RESULTS, L"1");
    SetDlgItemText(IDC_X_VICINITY, L"0");
    SetDlgItemText(IDC_Y_VICINITY, L"0");
    SetDlgItemText(IDC_BATCH_COUNT, L"0");
    SetDlgItemText(IDC_BATCH_THREADS, L"20");
    SetDlgItemText(IDC_BATCH_PATH, L"");
    SetDlgItemText(IDC_BATCH_STATS, L"尚未运行批量评估\r\n结果列表仅显示失败任务");
    SetDlgItemText(IDC_SEARCH_PATH, L"未加载搜索图");
    SetDlgItemText(IDC_TEMPLATE_PATH, L"未加载模板");
    SetDlgItemText(IDC_STATUS_TEXT, L"就绪：请加载搜索图和模板");
    captureInitialLayout();
    return TRUE;
}

void CLocatorMfcDlg::captureInitialLayout()
{
    CRect client;
    CRect window;
    GetClientRect(&client);
    GetWindowRect(&window);
    initialClientSize_ = CSize(client.Width(), client.Height());
    minimumWindowSize_ = CSize(window.Width(), window.Height());
    initialControls_.clear();
    for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow()) {
        CRect rect;
        child->GetWindowRect(&rect);
        ScreenToClient(&rect);
        initialControls_.push_back({ child->GetSafeHwnd(), rect });
    }
}

void CLocatorMfcDlg::applyLayout()
{
    const auto originalRect = [this](int id) {
        const HWND hwnd = GetDlgItem(id)->GetSafeHwnd();
        for (const auto& entry : initialControls_) {
            if (entry.first == hwnd) {
                return entry.second;
            }
        }
        throw std::runtime_error("找不到控件的初始布局");
    };
    const CRect workspacePanel = originalRect(IDC_PANEL_WORKSPACE);
    const CRect parametersPanel = originalRect(IDC_PANEL_PARAMETERS);
    const CRect resultsPanel = originalRect(IDC_PANEL_RESULTS);
    const CRect batchPanel = originalRect(IDC_PANEL_BATCH);
    const CRect templateView = originalRect(IDC_TEMPLATE_VIEW);

    CRect client;
    GetClientRect(&client);
    const int dx = client.Width() - initialClientSize_.cx;
    const int dy = client.Height() - initialClientSize_.cy;
    const int workspaceDelta = dy * 3 / 5;
    HDWP deferred = BeginDeferWindowPos(static_cast<int>(initialControls_.size()));
    if (deferred == nullptr) {
        return;
    }
    for (const auto& entry : initialControls_) {
        CRect rect = entry.second;
        const int id = ::GetDlgCtrlID(entry.first);
        const bool rightSide = rect.left >= parametersPanel.left;
        const bool templateColumn = !rightSide && rect.left >= templateView.left &&
            rect.top < workspacePanel.bottom;
        const bool batch = rightSide && rect.top >= batchPanel.top;
        const bool parameters = rightSide && !batch;
        const bool results = !rightSide && rect.top >= resultsPanel.top;
        const bool workspace = !rightSide && rect.top >= workspacePanel.top &&
            rect.top < resultsPanel.top;

        if (rightSide || templateColumn) {
            rect.OffsetRect(dx, 0);
        } else if (id == IDC_PANEL_WORKSPACE || id == IDC_SEARCH_VIEW ||
                   id == IDC_SEARCH_PATH || id == IDC_STATUS_TEXT ||
                   id == IDC_PANEL_RESULTS || id == IDC_RESULT_LIST) {
            rect.right += dx;
        }

        if (batch) {
            rect.OffsetRect(0, dy);
        } else if (parameters) {
            if (id == IDC_PANEL_PARAMETERS) {
                rect.bottom += dy;
            } else if (id == IDC_EXECUTE) {
                rect.OffsetRect(0, dy);
            }
        } else if (results) {
            rect.top += workspaceDelta;
            if (id == IDC_PANEL_RESULTS || id == IDC_RESULT_LIST) {
                rect.bottom += dy;
            } else {
                rect.bottom += workspaceDelta;
            }
        } else if (workspace) {
            if (id == IDC_PANEL_WORKSPACE) {
                rect.bottom += workspaceDelta;
            } else if (id == IDC_SEARCH_VIEW || id == IDC_TEMPLATE_VIEW) {
                rect.bottom += workspaceDelta;
            } else if (id == IDC_SEARCH_PATH || id == IDC_TEMPLATE_PATH ||
                       id == IDC_STATUS_TEXT) {
                rect.OffsetRect(0, workspaceDelta);
            }
        }
        HDWP next = DeferWindowPos(deferred, entry.first, nullptr,
                                   rect.left, rect.top, rect.Width(), rect.Height(),
                                   SWP_NOZORDER | SWP_NOACTIVATE);
        if (next == nullptr) {
            EndDeferWindowPos(deferred);
            return;
        }
        deferred = next;
    }
    EndDeferWindowPos(deferred);
    resizeResultColumns();
}

void CLocatorMfcDlg::OnSize(UINT type, int width, int height)
{
    CDialogEx::OnSize(type, width, height);
    if (type != SIZE_MINIMIZED && !initialControls_.empty()) {
        applyLayout();
    }
}

void CLocatorMfcDlg::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CDialogEx::OnGetMinMaxInfo(info);
    if (minimumWindowSize_.cx > 0 && minimumWindowSize_.cy > 0) {
        info->ptMinTrackSize.x = minimumWindowSize_.cx;
        info->ptMinTrackSize.y = minimumWindowSize_.cy;
    }
}

BOOL CLocatorMfcDlg::OnEraseBkgnd(CDC* dc)
{
    CRect client;
    GetClientRect(&client);
    dc->FillSolidRect(&client, RGB(245, 247, 250));
    return TRUE;
}

void CLocatorMfcDlg::OnPaint()
{
    CPaintDC dc(this);
    const auto drawCard = [this, &dc](int id) {
        CRect rect;
        GetDlgItem(id)->GetWindowRect(&rect);
        ScreenToClient(&rect);
        CPen border(PS_SOLID, 1, RGB(224, 229, 236));
        CBrush fill(RGB(255, 255, 255));
        CPen* oldPen = dc.SelectObject(&border);
        CBrush* oldBrush = dc.SelectObject(&fill);
        dc.RoundRect(&rect, CPoint(12, 12));
        dc.SelectObject(oldBrush);
        dc.SelectObject(oldPen);
    };
    drawCard(IDC_PANEL_WORKSPACE);
    drawCard(IDC_PANEL_PARAMETERS);
    drawCard(IDC_PANEL_RESULTS);
    drawCard(IDC_PANEL_BATCH);
}

HBRUSH CLocatorMfcDlg::OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor)
{
    if (ctlColor == CTLCOLOR_STATIC && wnd != nullptr) {
        const int id = wnd->GetDlgCtrlID();
        dc->SetBkMode(TRANSPARENT);
        if (id == IDC_STATUS_TEXT) {
            dc->SetTextColor(RGB(37, 99, 235));
        } else if (id == IDC_APP_SUBTITLE || id == IDC_WORKSPACE_HINT ||
                   id == IDC_RESULT_HINT || id == IDC_SEARCH_PATH || id == IDC_TEMPLATE_PATH) {
            dc->SetTextColor(RGB(107, 114, 128));
        } else {
            dc->SetTextColor(RGB(31, 41, 55));
        }
        return static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH));
    }
    return CDialogEx::OnCtlColor(dc, wnd, ctlColor);
}

void CLocatorMfcDlg::OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem)
{
    if (drawItem->CtlType != ODT_BUTTON) {
        CDialogEx::OnDrawItem(controlId, drawItem);
        return;
    }
    const bool primary = controlId == IDC_EXECUTE || controlId == IDC_BATCH_RUN;
    const bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    const bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = disabled ? RGB(229, 231, 235) :
        primary ? (pressed ? RGB(29, 78, 216) : RGB(37, 99, 235)) :
        (pressed ? RGB(243, 244, 246) : RGB(255, 255, 255));
    const COLORREF border = primary ? fill : RGB(209, 213, 219);

    CDC dc;
    dc.Attach(drawItem->hDC);
    CPen outline(PS_SOLID, 1, border);
    CBrush background(fill);
    CPen* oldPen = dc.SelectObject(&outline);
    CBrush* oldBrush = dc.SelectObject(&background);
    dc.RoundRect(&drawItem->rcItem, CPoint(8, 8));
    dc.SelectObject(oldBrush);
    dc.SelectObject(oldPen);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(disabled ? RGB(156, 163, 175) :
                    primary ? RGB(255, 255, 255) : RGB(55, 65, 81));
    CFont* oldFont = dc.SelectObject(&sectionFont_);
    CString text;
    GetDlgItemText(controlId, text);
    dc.DrawText(text, &drawItem->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((drawItem->itemState & ODS_FOCUS) != 0) {
        CRect focus = drawItem->rcItem;
        focus.DeflateRect(4, 4);
        dc.DrawFocusRect(&focus);
    }
    dc.SelectObject(oldFont);
    dc.Detach();
}

void CLocatorMfcDlg::OnLoadSearch()
{
    CFileDialog dialog(TRUE, L"bmp", nullptr, OFN_FILEMUSTEXIST,
                       L"BMP 文件 (*.bmp)|*.bmp|所有文件 (*.*)|*.*||", this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    const CString path = dialog.GetPathName();
    cv::Mat image;
    if (!loadGrayImage(std::string(CT2A(path.GetString(), CP_ACP)), image)) {
        AfxMessageBox(L"搜索图读取失败，请选择有效的 8 位灰度 BMP 文件");
        return;
    }
    searchImage_ = image;
    useRoi_ = false;
    searchView_.setImage(searchImage_);
    resultsList_.DeleteAllItems();
    SetDlgItemText(IDC_SEARCH_PATH, path);
    SetDlgItemText(IDC_STATUS_TEXT, L"搜索图已加载");
}

void CLocatorMfcDlg::OnLoadTemplate()
{
    CFileDialog dialog(TRUE, L"bmp", nullptr, OFN_FILEMUSTEXIST,
                       L"BMP 文件 (*.bmp)|*.bmp|所有文件 (*.*)|*.*||", this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    const CString path = dialog.GetPathName();
    cv::Mat image;
    if (!loadGrayImage(std::string(CT2A(path.GetString(), CP_ACP)), image)) {
        AfxMessageBox(L"模板读取失败，请选择有效的 8 位灰度 BMP 文件");
        return;
    }
    modelImage_ = image;
    templateView_.setImage(modelImage_);
    clearResultDisplay();
    SetDlgItemText(IDC_TEMPLATE_PATH, path);
    SetDlgItemText(IDC_STATUS_TEXT, L"模板已加载");
}

void CLocatorMfcDlg::OnSelectTemplate()
{
    if (searchImage_.empty()) {
        AfxMessageBox(L"请先加载用于框选模板的大图");
        return;
    }
    selectionForRoi_ = false;
    searchView_.setSelectionEnabled(true);
    SetDlgItemText(IDC_STATUS_TEXT, L"在搜索图上拖动鼠标框选模板");
}

void CLocatorMfcDlg::OnSelectRoi()
{
    if (searchImage_.empty()) {
        AfxMessageBox(L"请先加载搜索图");
        return;
    }
    selectionForRoi_ = true;
    searchView_.setSelectionEnabled(true);
    SetDlgItemText(IDC_STATUS_TEXT, L"在搜索图上拖动鼠标框选 ROI");
}

void CLocatorMfcDlg::OnClearRoi()
{
    useRoi_ = false;
    roi_ = { 0, 0, 0, 0 };
    searchView_.clearSelection();
    searchView_.setSelectionEnabled(false);
    clearResultDisplay();
    SetDlgItemText(IDC_STATUS_TEXT, L"已清除 ROI，搜索范围为完整图像");
}

LRESULT CLocatorMfcDlg::OnImageSelectionChanged(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;
    const cv::Rect selection = searchView_.selectedRect();
    if (selection.empty()) {
        return 0;
    }

    if (selectionForRoi_) {
        roi_ = { selection.x, selection.y, selection.width, selection.height };
        useRoi_ = true;
        clearResultDisplay();
        SetDlgItemText(IDC_STATUS_TEXT, CString(L"ROI 已设置"));
    } else {
        searchImage_(selection).copyTo(modelImage_);
        templateView_.setImage(modelImage_);
        clearResultDisplay();
        SetDlgItemText(IDC_TEMPLATE_PATH, L"来自搜索图框选");
        SetDlgItemText(IDC_STATUS_TEXT, L"已从大图生成模板");
        searchView_.clearSelection();
    }
    searchView_.setSelectionEnabled(false);
    return 0;
}

bool CLocatorMfcDlg::readConfig(LocatorConfig& config)
{
    config.backend = backendCombo_.GetCurSel() == 1 ? LocatorBackend::Gpu : LocatorBackend::Cpu;
    const std::pair<int, double*> doubleFields[] = {
        { IDC_ANGLE_MIN, &config.angleMinDeg },
        { IDC_ANGLE_MAX, &config.angleMaxDeg },
        { IDC_ANGLE_STEP, &config.angleStepDeg },
        { IDC_SCALE_MIN, &config.scaleMin },
        { IDC_SCALE_MAX, &config.scaleMax },
        { IDC_SCALE_STEP, &config.scaleStep },
        { IDC_MIN_SCORE, &config.minScore }
    };
    for (const auto& field : doubleFields) {
        if (!readDouble(this, field.first, *field.second)) {
            return false;
        }
    }

    const std::pair<int, int*> intFields[] = {
        { IDC_PYRAMID, &config.pyramidLevels },
        { IDC_SUBPIXEL, &config.subPixelResolution },
        { IDC_MAX_RESULTS, &config.maxResults },
        { IDC_X_VICINITY, &config.xVicinity },
        { IDC_Y_VICINITY, &config.yVicinity }
    };
    for (const auto& field : intFields) {
        if (!readInt(this, field.first, *field.second)) {
            return false;
        }
    }
    return true;
}

void CLocatorMfcDlg::showError(LocatorError error)
{
    const CString message = errorText(error);
    SetDlgItemText(IDC_STATUS_TEXT, message);
    AfxMessageBox(message);
}

void CLocatorMfcDlg::OnExecute()
{
    if (searchImage_.empty() || modelImage_.empty()) {
        AfxMessageBox(L"请先加载搜索图和模板");
        return;
    }

    LocatorConfig config;
    if (!readConfig(config)) {
        AfxMessageBox(L"参数格式无效，请检查所有数值输入");
        return;
    }
    config.useRoi = useRoi_;
    if (useRoi_) {
        config.roi = roi_;
    }

    const LocatorWorkflow::SingleResult run =
        LocatorWorkflow::runSingle(modelImage_, searchImage_, config);
    if (run.error != LocatorError::Ok) {
        showError(run.error);
        return;
    }
    const std::vector<LocatorResult>& results = run.matches;

    prepareResultList(false);
    for (size_t i = 0; i < results.size(); ++i) {
        const LocatorResult& result = results[i];
        CString value;
        value.Format(L"%.4f", result.x);
        const int row = resultsList_.InsertItem(static_cast<int>(i), value);
        value.Format(L"%.4f", result.y);
        resultsList_.SetItemText(row, 1, value);
        value.Format(L"%.4f", result.angle);
        resultsList_.SetItemText(row, 2, value);
        value.Format(L"%.4f", result.scale);
        resultsList_.SetItemText(row, 3, value);
        value.Format(L"%.2f", result.score);
        resultsList_.SetItemText(row, 4, value);
    }
    searchView_.setResults(results, modelImage_.cols, modelImage_.rows);

    const double preprocessMs = run.preprocessMs;
    const double searchMs = run.searchMs;
    const double totalMs = run.totalMs;
    CString status;
    status.Format(L"预处理 %.3f ms，搜索 %.3f ms，整体 %.3f ms，共 %d 个结果",
                  preprocessMs, searchMs, totalMs, static_cast<int>(results.size()));
    SetDlgItemText(IDC_STATUS_TEXT, status);
}


void CLocatorMfcDlg::prepareResultList(bool batch)
{
    resultsList_.DeleteAllItems();
    CHeaderCtrl* header = resultsList_.GetHeaderCtrl();
    while (header != nullptr && header->GetItemCount() > 0) {
        resultsList_.DeleteColumn(0);
    }
    const wchar_t* batchTitles[] = { L"任务", L"失败原因", L"预处理 ms", L"搜索 ms", L"整体 ms" };
    const wchar_t* singleTitles[] = { L"x", L"y", L"角度", L"尺度", L"分数" };
    const wchar_t** titles = batch ? batchTitles : singleTitles;
    for (int column = 0; column < 5; ++column) {
        resultsList_.InsertColumn(column, titles[column], LVCFMT_LEFT, 100);
    }
    batchResults_ = batch;
    resizeResultColumns();
}

void CLocatorMfcDlg::resizeResultColumns()
{
    CHeaderCtrl* header = resultsList_.GetHeaderCtrl();
    if (header == nullptr || header->GetItemCount() != 5) {
        return;
    }
    CRect client;
    resultsList_.GetClientRect(&client);
    const int width = client.Width();
    const int batchWidths[] = { 12, 36, 17, 17, 18 };
    const int singleWidths[] = { 20, 20, 18, 18, 24 };
    const int* percentages = batchResults_ ? batchWidths : singleWidths;
    for (int column = 0; column < 5; ++column) {
        resultsList_.SetColumnWidth(column, width * percentages[column] / 100);
    }
}

void CLocatorMfcDlg::OnBatchBrowse()
{
    CFileDialog dialog(TRUE, L"bmp", nullptr, OFN_FILEMUSTEXIST,
                       L"BMP 文件 (*.bmp)|*.bmp|所有文件 (*.*)|*.*||", this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const CString path = dialog.GetPathName();
    const int separator = path.ReverseFind(L'\\');
    if (separator > 0) {
        SetDlgItemText(IDC_BATCH_PATH, path.Left(separator));
    }
}

void CLocatorMfcDlg::OnBatchRun()
{
    CString directory;
    GetDlgItemText(IDC_BATCH_PATH, directory);
    directory.Trim();
    int count = 0;
    int threads = 0;
    LocatorConfig config;
    if (directory.IsEmpty()) {
        AfxMessageBox(L"请输入或选择 Thick 数据目录");
        return;
    }
    if (!readInt(this, IDC_BATCH_COUNT, count) || count < 0 ||
        !readInt(this, IDC_BATCH_THREADS, threads) || !readConfig(config)) {
        AfxMessageBox(L"批量任务数、线程数或搜索参数无效");
        return;
    }
    config.useRoi = useRoi_;
    if (useRoi_) {
        config.roi = roi_;
    }

    CWaitCursor wait;
    try {
        const std::vector<ThickPair> pairs = findThickPairs(
            std::string(CT2A(directory.GetString(), CP_ACP)));
        const int actualCount = count == 0 ? static_cast<int>(pairs.size()) : count;
        BatchDataSet data = makeBenchmarkTasks(pairs, actualCount);
        for (BatchTask& task : data.tasks) {
            task.config = config;
        }
        std::vector<BatchResult> results;
        const double wallMs = LocatorWorkflow::runBatch(data.tasks, threads, results);

        prepareResultList(true);
        double preprocessSum = 0.0;
        double searchSum = 0.0;
        double totalSum = 0.0;
        int succeeded = 0;
        int failureRow = 0;
        std::vector<double> totals;
        for (const BatchResult& result : results) {
            preprocessSum += result.preprocessMs;
            searchSum += result.searchMs;
            totalSum += result.totalMs;
            totals.push_back(result.totalMs);
            const bool passed = result.error == LocatorError::Ok && !result.matches.empty();
            if (passed) {
                ++succeeded;
                continue;
            }
            CString value;
            value.Format(L"%d", result.id);
            const int row = resultsList_.InsertItem(failureRow++, value);
            resultsList_.SetItemText(row, 1,
                result.error == LocatorError::Ok ? CString(L"无匹配") : CString(errorText(result.error)));
            value.Format(L"%.3f", result.preprocessMs);
            resultsList_.SetItemText(row, 2, value);
            value.Format(L"%.3f", result.searchMs);
            resultsList_.SetItemText(row, 3, value);
            value.Format(L"%.3f", result.totalMs);
            resultsList_.SetItemText(row, 4, value);
        }
        std::sort(totals.begin(), totals.end());
        const double countValue = static_cast<double>(results.size());
        const size_t p99Index = static_cast<size_t>(std::ceil(countValue * 0.99)) - 1;
        CString statistics;
        statistics.Format(
            L"总数 %d，成功 %d，失败 %d\r\n均值：预处理 %.3f ms，搜索 %.3f ms，整体 %.3f ms\r\nP99 %.3f ms，批量总耗时 %.3f ms",
            static_cast<int>(results.size()), succeeded,
            static_cast<int>(results.size()) - succeeded,
            preprocessSum / countValue, searchSum / countValue,
            totalSum / countValue, totals[p99Index], wallMs);
        SetDlgItemText(IDC_BATCH_STATS, statistics);
        SetDlgItemText(IDC_STATUS_TEXT, L"批量评估完成，下方列表仅显示失败或无匹配任务");
    } catch (const std::exception& error) {
        const CString message(CA2W(error.what(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, message);
        AfxMessageBox(message);
    }
}

void CLocatorMfcDlg::clearResultDisplay()
{
    resultsList_.DeleteAllItems();
    searchView_.setResults({}, modelImage_.cols, modelImage_.rows);
}

void CLocatorMfcDlg::savePreview(const wchar_t* path)
{
    CRect client;
    GetClientRect(&client);
    CImage image;
    if (image.Create(client.Width(), client.Height(), 32) == 0) {
        throw std::runtime_error("无法创建离屏截图位图");
    }
    SetWindowPos(nullptr, -32000, -32000, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    CDC* target = CDC::FromHandle(image.GetDC());
    const BOOL printed = PrintWindow(target, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    image.ReleaseDC();
    ShowWindow(SW_HIDE);
    if (!printed || FAILED(image.Save(path))) {
        throw std::runtime_error("无法保存离屏截图");
    }
}

void CLocatorMfcDlg::runSelfTest(const std::string& directory, const cv::Mat& model,
                                 const cv::Mat& search, LocatorBackend backend,
                                 const wchar_t* previewPath)
{
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    require(Create(IDD_LOCATOR_MFC) != FALSE, "无法创建无界面对话框");
    ShowWindow(SW_HIDE);
    searchImage_ = search;
    modelImage_ = model;
    searchView_.setImage(searchImage_);
    templateView_.setImage(modelImage_);
    clearResultDisplay();

    const auto controlRect = [this](int id) {
        CRect rect;
        GetDlgItem(id)->GetWindowRect(&rect);
        ScreenToClient(&rect);
        return rect;
    };
    const auto requireSameRow = [&controlRect, &require](int first, int second, int third,
                                                        const char* message) {
        const CRect a = controlRect(first);
        const CRect b = controlRect(second);
        const CRect c = controlRect(third);
        require(a.top == b.top && b.top == c.top, message);
    };
    requireSameRow(IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP,
                   "角度最小值、最大值和步进必须并排");
    requireSameRow(IDC_SCALE_MIN, IDC_SCALE_MAX, IDC_SCALE_STEP,
                   "尺度最小值、最大值和步进必须并排");
    requireSameRow(IDC_PYRAMID, IDC_SUBPIXEL, IDC_MIN_SCORE,
                   "金字塔、亚像素和最低分数必须并排");
    requireSameRow(IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY,
                   "Top-N 和邻近半径必须并排");
    require(controlRect(IDC_BATCH_COUNT).top == controlRect(IDC_BATCH_THREADS).top,
            "批量任务数和线程数必须并排");
    require((GetDlgItem(IDC_EXECUTE)->GetStyle() & BS_TYPEMASK) == BS_OWNERDRAW &&
            (GetDlgItem(IDC_BATCH_RUN)->GetStyle() & BS_TYPEMASK) == BS_OWNERDRAW,
            "主要操作按钮缺少统一的自绘样式");
    const DWORD windowStyle = GetStyle();
    require((windowStyle & WS_THICKFRAME) != 0 &&
            (windowStyle & WS_MAXIMIZEBOX) != 0,
            "主窗口缺少可调整边框或最大化按钮");
    MINMAXINFO limits = {};
    SendMessage(WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    require(limits.ptMinTrackSize.x > 0 && limits.ptMinTrackSize.y > 0,
            "主窗口没有设置最小布局尺寸");

    CRect client;
    GetClientRect(&client);
    require(client.Width() <= 1800 && client.Height() <= 1000,
            "默认对话框尺寸过大，不适合 1080p 工作区");
    std::vector<CRect> visibleRects;
    for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow()) {
        if (!child->IsWindowVisible()) {
            continue;
        }
        CRect rect;
        child->GetWindowRect(&rect);
        ScreenToClient(&rect);
        require(rect.left >= client.left && rect.top >= client.top &&
                rect.right <= client.right && rect.bottom <= client.bottom,
                "可见控件超出对话框边界");
        for (const CRect& previous : visibleRects) {
            CRect overlap;
            require(!overlap.IntersectRect(rect, previous), "可见控件之间存在重叠");
        }
        visibleRects.push_back(rect);
    }
    CRect listClient;
    resultsList_.GetClientRect(&listClient);
    int columnWidth = 0;
    for (int column = 0; column < 5; ++column) {
        columnWidth += resultsList_.GetColumnWidth(column);
    }
    require(columnWidth >= listClient.Width() * 9 / 10,
            "结果列没有利用列表的可用宽度");

    backendCombo_.SetCurSel(backend == LocatorBackend::Gpu ? 1 : 0);
    SetDlgItemText(IDC_ANGLE_MIN, L"-2");
    SetDlgItemText(IDC_ANGLE_MAX, L"1");
    SetDlgItemText(IDC_ANGLE_STEP, L"1");
    SetDlgItemText(IDC_SCALE_MIN, L"0.9");
    SetDlgItemText(IDC_SCALE_MAX, L"1.05");
    SetDlgItemText(IDC_SCALE_STEP, L"0.05");
    SetDlgItemText(IDC_PYRAMID, L"2");
    SetDlgItemText(IDC_SUBPIXEL, L"4");
    SetDlgItemText(IDC_MIN_SCORE, L"85");
    SetDlgItemText(IDC_MAX_RESULTS, L"2");
    SetDlgItemText(IDC_X_VICINITY, L"2");
    SetDlgItemText(IDC_Y_VICINITY, L"3");
    useRoi_ = true;
    roi_ = { 10, 10, 100, 100 };

    LocatorConfig config;
    require(readConfig(config), "对话框参数格式读取失败");
    require(config.backend == backend && config.angleMinDeg == -2.0 &&
            config.angleMaxDeg == 1.0 && config.angleStepDeg == 1.0 &&
            config.scaleMin == 0.9 && config.scaleMax == 1.05 &&
            config.scaleStep == 0.05 && config.pyramidLevels == 2 &&
            config.subPixelResolution == 4 && config.minScore == 85.0 &&
            config.maxResults == 2 && config.xVicinity == 2 && config.yVicinity == 3,
            "对话框控件到 LocatorConfig 的映射错误");
    config.useRoi = true;
    config.roi = roi_;
    const LocatorWorkflow::SingleResult expected =
        LocatorWorkflow::runSingle(modelImage_, searchImage_, config);
    require(expected.error == LocatorError::Ok && !expected.matches.empty(),
            "同配置核心工作流没有返回有效结果");
    CString expectedFields[5];
    expectedFields[0].Format(L"%.4f", expected.matches[0].x);
    expectedFields[1].Format(L"%.4f", expected.matches[0].y);
    expectedFields[2].Format(L"%.4f", expected.matches[0].angle);
    expectedFields[3].Format(L"%.4f", expected.matches[0].scale);
    expectedFields[4].Format(L"%.2f", expected.matches[0].score);
    OnExecute();
    require(resultsList_.GetItemCount() == static_cast<int>(expected.matches.size()) &&
            searchView_.hasResults(), "对话框单次结果数量或叠加数据与核心不一致");
    if (resultsList_.GetItemText(0, 0) != expectedFields[0] ||
        resultsList_.GetItemText(0, 1) != expectedFields[1] ||
        resultsList_.GetItemText(0, 2) != expectedFields[2] ||
        resultsList_.GetItemText(0, 3) != expectedFields[3] ||
        resultsList_.GetItemText(0, 4) != expectedFields[4]) {
        const CString actual = resultsList_.GetItemText(0, 0) + L"，" +
            resultsList_.GetItemText(0, 1) + L"，" + resultsList_.GetItemText(0, 2) + L"，" +
            resultsList_.GetItemText(0, 3) + L"，" + resultsList_.GetItemText(0, 4);
        throw std::runtime_error("对话框单次结果实际为：" +
                                 std::string(CT2A(actual.GetString(), CP_UTF8)));
    }

    CRect originalWindow;
    GetWindowRect(&originalWindow);
    const CRect originalSearch = controlRect(IDC_SEARCH_VIEW);
    const CRect originalResults = controlRect(IDC_RESULT_LIST);
    const CRect originalParameters = controlRect(IDC_PANEL_PARAMETERS);
    const CRect originalExecute = controlRect(IDC_EXECUTE);
    const int resultCount = resultsList_.GetItemCount();
    const CString firstResult = resultsList_.GetItemText(0, 0);
    const int rightMargin = client.right - originalParameters.right;
    SetWindowPos(nullptr, 0, 0, originalWindow.Width() + 240, originalWindow.Height() + 120,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(&client);
    require(controlRect(IDC_SEARCH_VIEW).Width() > originalSearch.Width() &&
            controlRect(IDC_SEARCH_VIEW).Height() > originalSearch.Height(),
            "窗口放大后搜索图像区没有扩大");
    require(controlRect(IDC_RESULT_LIST).Width() > originalResults.Width() &&
            controlRect(IDC_RESULT_LIST).Height() > originalResults.Height(),
            "窗口放大后结果列表没有扩大");
    require(controlRect(IDC_PANEL_PARAMETERS).right == client.right - rightMargin &&
            controlRect(IDC_EXECUTE).top > originalExecute.top,
            "右侧参数卡片或执行按钮没有跟随窗口");
    require(resultsList_.GetItemCount() == resultCount &&
            resultsList_.GetItemText(0, 0) == firstResult && searchView_.hasResults(),
            "窗口缩放后匹配结果被清除");
    resultsList_.GetClientRect(&listClient);
    columnWidth = 0;
    for (int column = 0; column < 5; ++column) {
        columnWidth += resultsList_.GetColumnWidth(column);
    }
    require(columnWidth >= listClient.Width() * 9 / 10,
            "窗口缩放后结果列没有跟随列表宽度");
    SetWindowPos(nullptr, 0, 0, originalWindow.Width(), originalWindow.Height(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(controlRect(IDC_SEARCH_VIEW) == originalSearch &&
            controlRect(IDC_RESULT_LIST) == originalResults,
            "窗口还原后布局没有恢复");

    if (previewPath != nullptr && *previewPath != L'\0') {
        savePreview(previewPath);
    }
    clearResultDisplay();
    require(resultsList_.GetItemCount() == 0 && !searchView_.hasResults(),
            "图像或 ROI 变化后仍保留旧结果");

    SetDlgItemText(IDC_BATCH_PATH, CString(CA2W(directory.c_str(), CP_ACP)));
    SetDlgItemText(IDC_BATCH_COUNT, L"0");
    SetDlgItemText(IDC_BATCH_THREADS, L"20");
    OnBatchRun();
    CString statistics;
    GetDlgItemText(IDC_BATCH_STATS, statistics);
    require(statistics.Find(L"总数 197，成功 197，失败 0") >= 0,
            "对话框批量评估未通过 197 对数据");
    DestroyWindow();
}
