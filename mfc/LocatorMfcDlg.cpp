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

constexpr UINT WM_LOCATOR_SELF_TEST = WM_APP + 202;

int dividerPaintCount = 0;
int imagePaintCount = 0;

LRESULT CALLBACK countControlPaint(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_PAINT) {
        const int id = ::GetDlgCtrlID(window);
        if (id == IDC_DIVIDER_ANGLE) {
            ++dividerPaintCount;
        } else if (id == IDC_SEARCH_VIEW) {
            ++imagePaintCount;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

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
    ON_BN_CLICKED(IDC_SELECT_ROI, &CLocatorMfcDlg::OnSelectRoi)
    ON_BN_CLICKED(IDC_CLEAR_ROI, &CLocatorMfcDlg::OnClearRoi)
    ON_BN_CLICKED(IDC_RESET, &CLocatorMfcDlg::OnReset)
    ON_BN_CLICKED(IDC_EXECUTE, &CLocatorMfcDlg::OnExecute)
    ON_BN_CLICKED(IDC_BATCH_BROWSE, &CLocatorMfcDlg::OnBatchBrowse)
    ON_BN_CLICKED(IDC_BATCH_RUN, &CLocatorMfcDlg::OnBatchRun)
    ON_NOTIFY(TCN_SELCHANGE, IDC_MODE_TABS, &CLocatorMfcDlg::OnModeTabsChanged)
    ON_MESSAGE(WM_IMAGE_SELECTION_CHANGED, &CLocatorMfcDlg::OnImageSelectionChanged)
    ON_MESSAGE(WM_LOCATOR_SELF_TEST, &CLocatorMfcDlg::OnSelfTest)
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
    dialogBrush_.CreateSolidBrush(RGB(245, 247, 250));
    cardBrush_.CreateSolidBrush(RGB(255, 255, 255));
    SetWindowText(L"搜索匹配定位工具");
    const int panelIds[] = { IDC_PANEL_WORKSPACE, IDC_PANEL_PARAMETERS, IDC_PANEL_RESULTS };
    for (int id : panelIds) {
        GetDlgItem(id)->ShowWindow(SW_HIDE);
    }

    LOGFONT font = {};
    GetFont()->GetLogFont(&font);
    font.lfWeight = FW_SEMIBOLD;
    sectionFont_.CreateFontIndirectW(&font);
    font.lfHeight = static_cast<LONG>(font.lfHeight * 1.4);
    titleFont_.CreateFontIndirectW(&font);
    numericFont_.CreatePointFont(100, L"Consolas");
    GetDlgItem(IDC_APP_TITLE)->SetFont(&titleFont_);
    const int sectionIds[] = {
        IDC_SEARCH_TITLE, IDC_TEMPLATE_TITLE, IDC_RESULTS_TITLE,
        IDC_ANGLE_TITLE, IDC_SCALE_TITLE, IDC_RESULT_OPTIONS_TITLE,
        IDC_BATCH_DIRECTORY_TITLE, IDC_BATCH_SETTINGS_TITLE, IDC_BATCH_STATS_TITLE
    };
    for (int id : sectionIds) {
        GetDlgItem(id)->SetFont(&sectionFont_);
    }

    searchView_.SubclassDlgItem(IDC_SEARCH_VIEW, this);
    templateView_.SubclassDlgItem(IDC_TEMPLATE_VIEW, this);
    resultsList_.SubclassDlgItem(IDC_RESULT_LIST, this);
    resultsList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    resultsList_.SetFont(&numericFont_);
    batchStats_.SubclassDlgItem(IDC_BATCH_STATS, this);
    batchStats_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    batchStats_.SetFont(&numericFont_);
    batchStats_.InsertColumn(0, L"项目", LVCFMT_LEFT, 140);
    batchStats_.InsertColumn(1, L"值", LVCFMT_RIGHT, 180);
    setBatchStats(std::vector<CString>(7, CString(L"尚未运行")));
    backendCombo_.SubclassDlgItem(IDC_BACKEND, this);
    backendCombo_.AddString(L"CPU");
    backendCombo_.AddString(L"GPU");
    backendCombo_.SetCurSel(0);
    subpixelCombo_.SubclassDlgItem(IDC_SUBPIXEL, this);
    for (const wchar_t* value : { L"1", L"2", L"4", L"8" }) {
        subpixelCombo_.AddString(value);
    }
    subpixelCombo_.SetCurSel(3);
    const int numericIds[] = {
        IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP, IDC_SCALE_MIN, IDC_SCALE_MAX,
        IDC_SCALE_STEP, IDC_PYRAMID, IDC_MIN_SCORE, IDC_MAX_RESULTS,
        IDC_X_VICINITY, IDC_Y_VICINITY, IDC_BATCH_COUNT, IDC_BATCH_THREADS,
        IDC_BACKEND, IDC_SUBPIXEL
    };
    for (int id : numericIds) {
        GetDlgItem(id)->SetFont(&numericFont_);
    }
    modeTabs_.SubclassDlgItem(IDC_MODE_TABS, this);
    TCITEM tab = {};
    tab.mask = TCIF_TEXT;
    tab.pszText = const_cast<wchar_t*>(L"搜索参数");
    modeTabs_.InsertItem(0, &tab);
    tab.pszText = const_cast<wchar_t*>(L"批量评估");
    modeTabs_.InsertItem(1, &tab);
    prepareResultList(false);

    SetDlgItemText(IDC_ANGLE_MIN, L"0");
    SetDlgItemText(IDC_ANGLE_MAX, L"0");
    SetDlgItemText(IDC_ANGLE_STEP, L"1");
    SetDlgItemText(IDC_SCALE_MIN, L"1");
    SetDlgItemText(IDC_SCALE_MAX, L"1");
    SetDlgItemText(IDC_SCALE_STEP, L"0.01");
    SetDlgItemText(IDC_PYRAMID, L"1");
    SetDlgItemText(IDC_MIN_SCORE, L"0");
    SetDlgItemText(IDC_MAX_RESULTS, L"1");
    SetDlgItemText(IDC_X_VICINITY, L"0");
    SetDlgItemText(IDC_Y_VICINITY, L"0");
    SetDlgItemText(IDC_BATCH_COUNT, L"0");
    SetDlgItemText(IDC_BATCH_THREADS, L"20");
    SetDlgItemText(IDC_BATCH_PATH, L"");
    SetDlgItemText(IDC_SEARCH_PATH, L"未加载搜索图");
    SetDlgItemText(IDC_TEMPLATE_PATH, L"未加载模板");
    SetDlgItemText(IDC_STATUS_TEXT, L"就绪：请加载搜索图和模板");
    initializeLayout();
    showInspectorPage(0);
    if (selfTestAction_) {
        SetWindowPos(nullptr, -32000, -32000, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        PostMessage(WM_LOCATOR_SELF_TEST);
    }
    return TRUE;
}

void CLocatorMfcDlg::showInspectorPage(int page)
{
    const int searchIds[] = {
        IDC_PARAMETER_HINT, IDC_ANGLE_TITLE, IDC_ANGLE_HINT, IDC_ANGLE_MIN_LABEL,
        IDC_ANGLE_MAX_LABEL, IDC_ANGLE_STEP_LABEL, IDC_ANGLE_MIN, IDC_ANGLE_MAX,
        IDC_ANGLE_STEP, IDC_DIVIDER_ANGLE, IDC_SCALE_TITLE, IDC_SCALE_HINT,
        IDC_SCALE_MIN_LABEL, IDC_SCALE_MAX_LABEL, IDC_SCALE_STEP_LABEL, IDC_SCALE_MIN,
        IDC_SCALE_MAX, IDC_SCALE_STEP, IDC_DIVIDER_SCALE, IDC_RESULT_OPTIONS_TITLE,
        IDC_PRECISION_HINT, IDC_PYRAMID_LABEL, IDC_SUBPIXEL_LABEL, IDC_MIN_SCORE_LABEL,
        IDC_PYRAMID, IDC_SUBPIXEL, IDC_MIN_SCORE, IDC_MAX_RESULTS_LABEL,
        IDC_X_VICINITY_LABEL, IDC_Y_VICINITY_LABEL, IDC_MAX_RESULTS, IDC_X_VICINITY,
        IDC_Y_VICINITY, IDC_DIVIDER_RESULT_OPTIONS, IDC_EXECUTION_HINT, IDC_EXECUTE
    };
    const int batchIds[] = {
        IDC_BATCH_DIRECTORY_TITLE, IDC_BATCH_DIRECTORY_LABEL, IDC_BATCH_PATH,
        IDC_BATCH_BROWSE, IDC_BATCH_SETTINGS_TITLE, IDC_BATCH_SETTINGS_HINT,
        IDC_BATCH_COUNT_LABEL, IDC_BATCH_THREADS_LABEL, IDC_BATCH_COUNT,
        IDC_BATCH_THREADS, IDC_BATCH_RUN, IDC_BATCH_STATS_TITLE, IDC_BATCH_STATS
    };
    for (int id : searchIds) {
        GetDlgItem(id)->ShowWindow(page == 0 ? SW_SHOW : SW_HIDE);
    }
    for (int id : batchIds) {
        GetDlgItem(id)->ShowWindow(page == 1 ? SW_SHOW : SW_HIDE);
    }
    activeInspectorPage_ = page;
    if (modeTabs_.GetCurSel() != page) {
        modeTabs_.SetCurSel(page);
    }
    modeTabs_.RedrawWindow();
}

void CLocatorMfcDlg::OnModeTabsChanged(NMHDR* header, LRESULT* result)
{
    (void)header;
    showInspectorPage(modeTabs_.GetCurSel());
    *result = 0;
}

void CLocatorMfcDlg::setBatchStats(const std::vector<CString>& values)
{
    if (values.size() != 7) {
        throw std::invalid_argument("批量统计必须有七项");
    }
    const wchar_t* labels[] = {
        L"总任务数", L"成功 / 失败", L"预处理均值", L"搜索均值",
        L"整体均值", L"P99", L"总耗时"
    };
    batchStats_.DeleteAllItems();
    for (int row = 0; row < 7; ++row) {
        batchStats_.InsertItem(row, labels[row]);
        batchStats_.SetItemText(row, 1, values[row]);
    }
}

void CLocatorMfcDlg::initializeLayout()
{
    CClientDC dc(this);
    dpiX_ = dc.GetDeviceCaps(LOGPIXELSX);
    dpiY_ = dc.GetDeviceCaps(LOGPIXELSY);
    CRect client;
    CRect window;
    GetClientRect(&client);
    GetWindowRect(&window);
    minimumWindowSize_ = CSize(
        window.Width() - client.Width() + MulDiv(1180, dpiX_, 96),
        window.Height() - client.Height() + MulDiv(720, dpiY_, 96));
    applyLayout();
}

void CLocatorMfcDlg::applyLayout()
{
    const auto sx = [this](int value) { return MulDiv(value, dpiX_, 96); };
    const auto sy = [this](int value) { return MulDiv(value, dpiY_, 96); };
    CRect client;
    GetClientRect(&client);
    const int width = static_cast<int>(client.Width());
    const int height = static_cast<int>(client.Height());
    const int margin = sx(16);
    const int gap = sx(12);
    const int top = sy(56);
    const int bottom = height - sy(30);
    const int contentHeight = bottom - top;
    const int rightWidth = std::max(sx(390), std::min(sx(460), width * 31 / 100));
    const int leftWidth = width - margin * 2 - gap - rightWidth;
    const int left = margin;
    const int right = margin + leftWidth + gap;
    const int workspaceHeight = contentHeight * 62 / 100;
    const CRect workspace(left, top, left + leftWidth, top + workspaceHeight);
    const CRect results(left, workspace.bottom + gap, left + leftWidth, bottom);
    const CRect inspector(right, top, right + rightWidth, bottom);

    std::vector<std::pair<int, CRect>> placements;
    const auto place = [&placements](int id, const CRect& rect) {
        placements.push_back({ id, rect });
    };
    const auto box = [](int x, int y, int w, int h) {
        return CRect(x, y, x + w, y + h);
    };
    const int backendX = width - margin - sx(116);
    const int backendLabelX = backendX - sx(72);
    place(IDC_APP_TITLE, box(margin, sy(10), sx(160), sy(28)));
    place(IDC_APP_SUBTITLE, box(margin + sx(174), sy(18),
        backendLabelX - margin - sx(190), sy(18)));
    place(IDC_BACKEND_LABEL, box(backendLabelX, sy(18), sx(64), sy(20)));
    place(IDC_BACKEND, box(backendX, sy(14), sx(116), sy(150)));
    place(IDC_PANEL_WORKSPACE, workspace);
    place(IDC_PANEL_PARAMETERS, inspector);
    place(IDC_PANEL_RESULTS, results);

    const int innerLeft = left + sx(12);
    const int innerWidth = leftWidth - sx(24);
    const int toolbarY = top + sy(10);
    const int toolbarGap = sx(8);
    const int toolRows = innerWidth >= sx(760) ? 1 : 2;
    const int toolHeight = sy(32);
    const int toolIds[] = { IDC_LOAD_SEARCH, IDC_LOAD_TEMPLATE, IDC_SELECT_ROI,
                            IDC_CLEAR_ROI, IDC_RESET };
    for (int index = 0; index < 5; ++index) {
        const int row = toolRows == 2 ? index / 3 : 0;
        const int column = toolRows == 2 ? index % 3 : index;
        const int count = row == 1 ? 2 : (toolRows == 2 ? 3 : 5);
        const int buttonWidth = (innerWidth - toolbarGap * (count - 1)) / count;
        place(toolIds[index], box(innerLeft + column * (buttonWidth + toolbarGap),
            toolbarY + row * (toolHeight + toolbarGap), buttonWidth, toolHeight));
    }
    const int imageY = toolbarY + toolRows * toolHeight + (toolRows - 1) * sy(8) + sy(12);
    const int templateWidth = std::max(sx(172), std::min(sx(260), innerWidth / 4));
    const int searchWidth = innerWidth - gap - templateWidth;
    const int templateX = innerLeft + searchWidth + gap;
    const int pathY = workspace.bottom - sy(28);
    const int viewY = imageY + sy(24);
    place(IDC_SEARCH_TITLE, box(innerLeft, imageY, sx(80), sy(20)));
    place(IDC_IMAGE_META_SEARCH, box(innerLeft + sx(88), imageY + sy(2),
        searchWidth - sx(88), sy(18)));
    place(IDC_SEARCH_VIEW, box(innerLeft, viewY, searchWidth, pathY - viewY - sy(8)));
    place(IDC_SEARCH_PATH, box(innerLeft, pathY, searchWidth, sy(18)));
    place(IDC_DIVIDER_IMAGES, box(templateX - gap / 2, imageY, 1,
        workspace.bottom - imageY - sy(12)));
    place(IDC_TEMPLATE_TITLE, box(templateX, imageY, sx(60), sy(20)));
    place(IDC_IMAGE_META_TEMPLATE, box(templateX + sx(60), imageY + sy(2),
        templateWidth - sx(60), sy(18)));
    place(IDC_TEMPLATE_VIEW, box(templateX, viewY, templateWidth, pathY - viewY - sy(8)));
    place(IDC_TEMPLATE_PATH, box(templateX, pathY, templateWidth, sy(18)));

    place(IDC_RESULTS_TITLE, box(results.left + sx(12), results.top + sy(10), sx(90), sy(20)));
    place(IDC_RESULT_HINT, box(results.left + sx(112), results.top + sy(12),
        results.Width() - sx(124), sy(18)));
    place(IDC_RESULT_LIST, box(results.left + sx(12), results.top + sy(38),
        results.Width() - sx(24), results.Height() - sy(50)));

    modeTabs_.SetItemSize(CSize((rightWidth - sx(8)) / 2, sy(36)));
    place(IDC_MODE_TABS, box(right, top, rightWidth, sy(40)));
    const int bodyX = right + sx(16);
    const int bodyY = top + sy(52);
    const int bodyWidth = rightWidth - sx(32);
    const int sectionShift = std::max(0, std::min(sy(24),
        (static_cast<int>(inspector.Height()) - sy(640)) / 4));
    place(IDC_PARAMETER_HINT, box(bodyX, bodyY, bodyWidth, sy(18)));
    place(IDC_ANGLE_TITLE, box(bodyX, bodyY + sy(28) + sectionShift, bodyWidth, sy(18)));
    place(IDC_ANGLE_HINT, box(bodyX, bodyY + sy(48) + sectionShift, bodyWidth, sy(16)));
    place(IDC_DIVIDER_ANGLE, box(bodyX, bodyY + sy(140) + sectionShift, bodyWidth, 1));
    place(IDC_SCALE_TITLE, box(bodyX, bodyY + sy(160) + sectionShift * 2, bodyWidth, sy(18)));
    place(IDC_SCALE_HINT, box(bodyX, bodyY + sy(180) + sectionShift * 2, bodyWidth, sy(16)));
    place(IDC_DIVIDER_SCALE, box(bodyX, bodyY + sy(272) + sectionShift * 2, bodyWidth, 1));
    place(IDC_RESULT_OPTIONS_TITLE, box(bodyX, bodyY + sy(292) + sectionShift * 3, bodyWidth, sy(18)));
    place(IDC_PRECISION_HINT, box(bodyX, bodyY + sy(312) + sectionShift * 3, bodyWidth, sy(16)));
    place(IDC_DIVIDER_RESULT_OPTIONS, box(bodyX, bodyY + sy(468) + sectionShift * 3, bodyWidth, 1));
    const auto fieldRow = [&place, &box, &sx, &sy, bodyX, bodyY, bodyWidth](
        const int (&labels)[3], const int (&edits)[3], int labelY, int editY, int offset) {
        const int fieldWidth = (bodyWidth - sx(16)) / 3;
        for (int column = 0; column < 3; ++column) {
            const int x = bodyX + column * (fieldWidth + sx(8));
            place(labels[column], box(x, bodyY + sy(labelY) + offset, fieldWidth, sy(16)));
            place(edits[column], box(x, bodyY + sy(editY) + offset, fieldWidth, sy(32)));
        }
    };
    const int angleLabels[] = { IDC_ANGLE_MIN_LABEL, IDC_ANGLE_MAX_LABEL, IDC_ANGLE_STEP_LABEL };
    const int angleEdits[] = { IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP };
    const int scaleLabels[] = { IDC_SCALE_MIN_LABEL, IDC_SCALE_MAX_LABEL, IDC_SCALE_STEP_LABEL };
    const int scaleEdits[] = { IDC_SCALE_MIN, IDC_SCALE_MAX, IDC_SCALE_STEP };
    const int precisionLabels[] = { IDC_PYRAMID_LABEL, IDC_SUBPIXEL_LABEL, IDC_MIN_SCORE_LABEL };
    const int precisionEdits[] = { IDC_PYRAMID, IDC_SUBPIXEL, IDC_MIN_SCORE };
    const int resultLabels[] = { IDC_MAX_RESULTS_LABEL, IDC_X_VICINITY_LABEL, IDC_Y_VICINITY_LABEL };
    const int resultEdits[] = { IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY };
    fieldRow(angleLabels, angleEdits, 74, 92, sectionShift);
    fieldRow(scaleLabels, scaleEdits, 206, 224, sectionShift * 2);
    fieldRow(precisionLabels, precisionEdits, 340, 358, sectionShift * 3);
    fieldRow(resultLabels, resultEdits, 400, 418, sectionShift * 3);
    place(IDC_EXECUTION_HINT, box(bodyX, inspector.bottom - sy(76), bodyWidth, sy(18)));
    place(IDC_EXECUTE, box(bodyX, inspector.bottom - sy(52), bodyWidth, sy(36)));

    place(IDC_BATCH_DIRECTORY_TITLE, box(bodyX, bodyY, bodyWidth, sy(18)));
    place(IDC_BATCH_DIRECTORY_LABEL, box(bodyX, bodyY + sy(26), bodyWidth, sy(16)));
    const int browseWidth = std::max(sx(80), bodyWidth / 5);
    place(IDC_BATCH_PATH, box(bodyX, bodyY + sy(52),
        bodyWidth - browseWidth - sx(8), sy(32)));
    place(IDC_BATCH_BROWSE, box(bodyX + bodyWidth - browseWidth,
        bodyY + sy(52), browseWidth, sy(32)));
    place(IDC_BATCH_SETTINGS_TITLE, box(bodyX, bodyY + sy(124), bodyWidth, sy(18)));
    place(IDC_BATCH_SETTINGS_HINT, box(bodyX, bodyY + sy(148), bodyWidth, sy(16)));
    const int batchFieldWidth = (bodyWidth - sx(8)) / 2;
    place(IDC_BATCH_COUNT_LABEL, box(bodyX, bodyY + sy(174), batchFieldWidth, sy(16)));
    place(IDC_BATCH_THREADS_LABEL, box(bodyX + batchFieldWidth + sx(8),
        bodyY + sy(174), batchFieldWidth, sy(16)));
    place(IDC_BATCH_COUNT, box(bodyX, bodyY + sy(192), batchFieldWidth, sy(32)));
    place(IDC_BATCH_THREADS, box(bodyX + batchFieldWidth + sx(8),
        bodyY + sy(192), batchFieldWidth, sy(32)));
    place(IDC_BATCH_RUN, box(bodyX, bodyY + sy(240), bodyWidth, sy(36)));
    place(IDC_BATCH_STATS_TITLE, box(bodyX, bodyY + sy(296), bodyWidth, sy(18)));
    place(IDC_BATCH_STATS, box(bodyX, bodyY + sy(320), bodyWidth,
        inspector.bottom - bodyY - sy(336)));
    place(IDC_STATUS_TEXT, box(margin, height - sy(25), width - margin * 2, sy(19)));

    HDWP deferred = BeginDeferWindowPos(static_cast<int>(placements.size()));
    if (deferred == nullptr) {
        return;
    }
    for (const auto& entry : placements) {
        CWnd* control = GetDlgItem(entry.first);
        if (control == nullptr) {
            continue;
        }
        HDWP next = DeferWindowPos(deferred, control->GetSafeHwnd(), nullptr,
            entry.second.left, entry.second.top, entry.second.Width(), entry.second.Height(),
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (next == nullptr) {
            EndDeferWindowPos(deferred);
            return;
        }
        deferred = next;
    }
    EndDeferWindowPos(deferred);
    resizeResultColumns();
    CRect statsClient;
    batchStats_.GetClientRect(&statsClient);
    batchStats_.SetColumnWidth(0, statsClient.Width() * 45 / 100);
    batchStats_.SetColumnWidth(1, statsClient.Width() * 55 / 100 - 2);
    RedrawWindow(nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

void CLocatorMfcDlg::OnSize(UINT type, int width, int height)
{
    CDialogEx::OnSize(type, width, height);
    if (type != SIZE_MINIMIZED && minimumWindowSize_.cx > 0) {
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
        CPen border(PS_SOLID, 1, RGB(212, 222, 230));
        CBrush fill(RGB(255, 255, 255));
        CPen* oldPen = dc.SelectObject(&border);
        CBrush* oldBrush = dc.SelectObject(&fill);
        dc.RoundRect(&rect, CPoint(8, 8));
        dc.SelectObject(oldBrush);
        dc.SelectObject(oldPen);
    };
    drawCard(IDC_PANEL_WORKSPACE);
    drawCard(IDC_PANEL_PARAMETERS);
    drawCard(IDC_PANEL_RESULTS);
    CPen separator(PS_SOLID, 1, RGB(212, 222, 230));
    CPen* oldPen = dc.SelectObject(&separator);
    CRect anchor;
    GetDlgItem(IDC_PANEL_WORKSPACE)->GetWindowRect(&anchor);
    ScreenToClient(&anchor);
    dc.MoveTo(anchor.left, anchor.top - 6);
    dc.LineTo(anchor.right, anchor.top - 6);
    GetDlgItem(IDC_STATUS_TEXT)->GetWindowRect(&anchor);
    ScreenToClient(&anchor);
    dc.MoveTo(anchor.left, anchor.top - 7);
    dc.LineTo(anchor.right, anchor.top - 7);
    dc.SelectObject(oldPen);
}

HBRUSH CLocatorMfcDlg::OnCtlColor(CDC* dc, CWnd* wnd, UINT ctlColor)
{
    if ((ctlColor == CTLCOLOR_STATIC || ctlColor == CTLCOLOR_BTN) && wnd != nullptr) {
        const int id = wnd->GetDlgCtrlID();
        dc->SetBkMode(TRANSPARENT);
        if (id == IDC_STATUS_TEXT) {
            dc->SetTextColor(RGB(22, 114, 135));
        } else if (id == IDC_APP_SUBTITLE || id == IDC_RESULT_HINT ||
                   id == IDC_PRECISION_HINT || id == IDC_SEARCH_PATH ||
                   id == IDC_TEMPLATE_PATH || id == IDC_IMAGE_META_SEARCH ||
                   id == IDC_IMAGE_META_TEMPLATE || id == IDC_PARAMETER_HINT ||
                   id == IDC_ANGLE_HINT || id == IDC_SCALE_HINT ||
                   id == IDC_BATCH_DIRECTORY_LABEL || id == IDC_BATCH_SETTINGS_HINT ||
                   id == IDC_EXECUTION_HINT) {
            dc->SetTextColor(RGB(99, 116, 135));
        } else {
            dc->SetTextColor(RGB(24, 37, 52));
        }
        const bool onDialog = id == IDC_APP_TITLE || id == IDC_APP_SUBTITLE ||
            id == IDC_BACKEND_LABEL || id == IDC_STATUS_TEXT;
        return onDialog ? static_cast<HBRUSH>(dialogBrush_.GetSafeHandle())
                        : static_cast<HBRUSH>(cardBrush_.GetSafeHandle());
    }
    return CDialogEx::OnCtlColor(dc, wnd, ctlColor);
}

void CLocatorMfcDlg::OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem)
{
    if (controlId == IDC_MODE_TABS && drawItem->CtlType == ODT_TAB) {
        const bool selected = static_cast<int>(drawItem->itemID) == modeTabs_.GetCurSel();
        CDC dc;
        dc.Attach(drawItem->hDC);
        dc.FillSolidRect(&drawItem->rcItem,
                         selected ? RGB(22, 114, 135) : RGB(233, 239, 243));
        wchar_t text[64] = {};
        TCITEM item = {};
        item.mask = TCIF_TEXT;
        item.pszText = text;
        item.cchTextMax = 64;
        if (modeTabs_.GetItem(static_cast<int>(drawItem->itemID), &item)) {
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(selected ? RGB(255, 255, 255) : RGB(65, 80, 96));
            CFont* oldFont = dc.SelectObject(selected ? &sectionFont_ : GetFont());
            CRect textRect = drawItem->rcItem;
            dc.DrawText(text, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(oldFont);
        }
        if ((drawItem->itemState & ODS_FOCUS) != 0) {
            CRect focus = drawItem->rcItem;
            focus.DeflateRect(4, 4);
            dc.DrawFocusRect(&focus);
        }
        dc.Detach();
        return;
    }
    if (drawItem->CtlType != ODT_BUTTON) {
        CDialogEx::OnDrawItem(controlId, drawItem);
        return;
    }
    const bool primary = controlId == IDC_EXECUTE || controlId == IDC_BATCH_RUN;
    const bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    const bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = disabled ? RGB(229, 231, 235) :
        primary ? (pressed ? RGB(18, 95, 112) : RGB(22, 114, 135)) :
        (pressed ? RGB(233, 239, 243) : RGB(249, 251, 252));
    const COLORREF border = primary ? fill : RGB(212, 222, 230);

    CDC dc;
    dc.Attach(drawItem->hDC);
    CPen outline(PS_SOLID, 1, border);
    CBrush background(fill);
    CPen* oldPen = dc.SelectObject(&outline);
    CBrush* oldBrush = dc.SelectObject(&background);
    dc.RoundRect(&drawItem->rcItem, CPoint(6, 6));
    dc.SelectObject(oldBrush);
    dc.SelectObject(oldPen);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(disabled ? RGB(156, 163, 175) :
                    primary ? RGB(255, 255, 255) : RGB(24, 37, 52));
    CFont* oldFont = dc.SelectObject(primary ? &sectionFont_ : GetFont());
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
    CString dimensions;
    dimensions.Format(L"%d × %d", image.cols, image.rows);
    SetDlgItemText(IDC_IMAGE_META_SEARCH, dimensions);
    SetDlgItemText(IDC_SEARCH_PATH, path);
    SetDlgItemText(IDC_STATUS_TEXT, L"搜索图已加载");
    captureBaseline();
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
    CString dimensions;
    dimensions.Format(L"%d × %d", image.cols, image.rows);
    SetDlgItemText(IDC_IMAGE_META_TEMPLATE, dimensions);
    SetDlgItemText(IDC_TEMPLATE_PATH, path);
    SetDlgItemText(IDC_STATUS_TEXT, L"模板已加载");
    captureBaseline();
}

void CLocatorMfcDlg::OnSelectRoi()
{
    if (searchImage_.empty()) {
        AfxMessageBox(L"请先加载搜索图");
        return;
    }
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
    roi_ = { selection.x, selection.y, selection.width, selection.height };
    useRoi_ = true;
    clearResultDisplay();
    SetDlgItemText(IDC_STATUS_TEXT, CString(L"ROI 已设置"));
    searchView_.setSelectionEnabled(false);
    return 0;
}

void CLocatorMfcDlg::writeConfig(const LocatorConfig& config)
{
    const auto number = [this](int id, double value) {
        CString text;
        text.Format(L"%.12g", value);
        SetDlgItemText(id, text);
    };
    number(IDC_ANGLE_MIN, config.angleMinDeg);
    number(IDC_ANGLE_MAX, config.angleMaxDeg);
    number(IDC_ANGLE_STEP, config.angleStepDeg);
    number(IDC_SCALE_MIN, config.scaleMin);
    number(IDC_SCALE_MAX, config.scaleMax);
    number(IDC_SCALE_STEP, config.scaleStep);
    SetDlgItemInt(IDC_PYRAMID, config.pyramidLevels);
    number(IDC_MIN_SCORE, config.minScore);
    SetDlgItemInt(IDC_MAX_RESULTS, config.maxResults);
    SetDlgItemInt(IDC_X_VICINITY, config.xVicinity);
    SetDlgItemInt(IDC_Y_VICINITY, config.yVicinity);
    backendCombo_.SetCurSel(config.backend == LocatorBackend::Gpu ? 1 : 0);
    const int resolutions[] = { 1, 2, 4, 8 };
    for (int index = 0; index < 4; ++index) {
        if (resolutions[index] == config.subPixelResolution) {
            subpixelCombo_.SetCurSel(index);
            break;
        }
    }
}

void CLocatorMfcDlg::captureBaseline()
{
    baseline_.valid = !searchImage_.empty() && !modelImage_.empty() &&
                      readConfig(baseline_.config);
    if (baseline_.valid) {
        baseline_.search = searchImage_.clone();
        baseline_.model = modelImage_.clone();
        GetDlgItemText(IDC_SEARCH_PATH, baseline_.searchPath);
        GetDlgItemText(IDC_TEMPLATE_PATH, baseline_.templatePath);
    }
    GetDlgItem(IDC_RESET)->EnableWindow(baseline_.valid);
}

void CLocatorMfcDlg::OnReset()
{
    if (!baseline_.valid) {
        return;
    }
    searchImage_ = baseline_.search.clone();
    modelImage_ = baseline_.model.clone();
    searchView_.setImage(searchImage_);
    templateView_.setImage(modelImage_);
    writeConfig(baseline_.config);
    useRoi_ = false;
    roi_ = { 0, 0, 0, 0 };
    searchView_.clearSelection();
    searchView_.setSelectionEnabled(false);
    clearResultDisplay();
    SetDlgItemText(IDC_SEARCH_PATH, baseline_.searchPath);
    SetDlgItemText(IDC_TEMPLATE_PATH, baseline_.templatePath);
    CString dimensions;
    dimensions.Format(L"%d × %d", searchImage_.cols, searchImage_.rows);
    SetDlgItemText(IDC_IMAGE_META_SEARCH, dimensions);
    dimensions.Format(L"%d × %d", modelImage_.cols, modelImage_.rows);
    SetDlgItemText(IDC_IMAGE_META_TEMPLATE, dimensions);
    SetDlgItemText(IDC_STATUS_TEXT, L"已复位到当前图片对加载完成时的状态，请重新执行搜索");
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
        { IDC_MAX_RESULTS, &config.maxResults },
        { IDC_X_VICINITY, &config.xVicinity },
        { IDC_Y_VICINITY, &config.yVicinity }
    };
    for (const auto& field : intFields) {
        if (!readInt(this, field.first, *field.second)) {
            return false;
        }
    }
    const int resolutions[] = { 1, 2, 4, 8 };
    const int selected = subpixelCombo_.GetCurSel();
    if (selected < 0) {
        return false;
    }
    config.subPixelResolution = resolutions[selected];
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
    const wchar_t* batchTitles[] = { L"任务编号", L"失败原因", L"预处理 ms", L"搜索 ms", L"整体 ms" };
    const wchar_t* singleTitles[] = {
        L"中心 x（px）", L"中心 y（px）", L"角度（°）", L"尺度（倍率）", L"分数（%）"
    };
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
        std::vector<CString> statistics;
        CString value;
        value.Format(L"%d", static_cast<int>(results.size()));
        statistics.push_back(value);
        value.Format(L"%d / %d", succeeded, static_cast<int>(results.size()) - succeeded);
        statistics.push_back(value);
        value.Format(L"%.3f ms", preprocessSum / countValue);
        statistics.push_back(value);
        value.Format(L"%.3f ms", searchSum / countValue);
        statistics.push_back(value);
        value.Format(L"%.3f ms", totalSum / countValue);
        statistics.push_back(value);
        value.Format(L"%.3f ms", totals[p99Index]);
        statistics.push_back(value);
        value.Format(L"%.3f ms", wallMs);
        statistics.push_back(value);
        setBatchStats(statistics);
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
        throw std::runtime_error("无法创建窗口截图位图");
    }
    SetWindowPos(nullptr, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    CDC* source = GetDC();
    CDC* target = CDC::FromHandle(image.GetDC());
    const BOOL copied = target->BitBlt(0, 0, client.Width(), client.Height(), source, 0, 0, SRCCOPY);
    image.ReleaseDC();
    ReleaseDC(source);
    ShowWindow(SW_HIDE);
    if (!copied || FAILED(image.Save(path))) {
        throw std::runtime_error("无法保存真实窗口截图");
    }
}

void CLocatorMfcDlg::runSelfTest(const std::string& directory, const cv::Mat& model,
                                 const cv::Mat& search, LocatorBackend backend,
                                 const wchar_t* previewPath)
{
    selfTestAction_ = [this, &directory, &model, &search, backend, previewPath] {
        verifySelfTest(directory, model, search, backend, previewPath);
    };
    selfTestError_.clear();
    const INT_PTR result = DoModal();
    selfTestAction_ = nullptr;
    if (!selfTestError_.empty()) {
        const std::string message = selfTestError_;
        selfTestError_.clear();
        throw std::runtime_error(message);
    }
    if (result != 0) {
        throw std::runtime_error("模态对话框自测未正常完成");
    }
}

LRESULT CLocatorMfcDlg::OnSelfTest(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;
    try {
        selfTestAction_();
    } catch (const std::exception& error) {
        selfTestError_ = error.what();
    } catch (...) {
        selfTestError_ = "自测发生未知异常";
    }
    EndDialog(selfTestError_.empty() ? 0 : 1);
    return 0;
}

void CLocatorMfcDlg::verifySelfTest(const std::string& directory, const cv::Mat& model,
                                    const cv::Mat& search, LocatorBackend backend,
                                    const wchar_t* previewPath)
{
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    ShowWindow(SW_HIDE);
    require(GetDlgItem(IDC_SELECT_TEMPLATE) == nullptr,
            "模板只能通过单独加载图片指定，不应保留框选模板控件");
    require(GetDlgItem(IDC_RESET) != nullptr && !GetDlgItem(IDC_RESET)->IsWindowEnabled(),
            "没有完整图片基准时复位按钮应禁用");
    searchImage_ = search;
    modelImage_ = model;
    searchView_.setImage(searchImage_);
    templateView_.setImage(modelImage_);
    clearResultDisplay();
    CString imageDescription;
    imageDescription.Format(L"已加载搜索图（%d × %d）", search.cols, search.rows);
    SetDlgItemText(IDC_SEARCH_PATH, imageDescription);
    imageDescription.Format(L"%d × %d", search.cols, search.rows);
    SetDlgItemText(IDC_IMAGE_META_SEARCH, imageDescription);
    imageDescription.Format(L"已加载模板（%d × %d）", model.cols, model.rows);
    SetDlgItemText(IDC_TEMPLATE_PATH, imageDescription);
    imageDescription.Format(L"%d × %d", model.cols, model.rows);
    SetDlgItemText(IDC_IMAGE_META_TEMPLATE, imageDescription);
    captureBaseline();
    require(GetDlgItem(IDC_RESET)->IsWindowEnabled(), "完整图片对加载后复位按钮未启用");

    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    CRect roiClient;
    searchView_.GetClientRect(&roiClient);
    const int roiWidth = std::min(roiClient.Width(), roiClient.Height());
    const CRect roiBitmap(roiClient.left + (roiClient.Width() - roiWidth) / 2,
                          roiClient.top + (roiClient.Height() - roiWidth) / 2,
                          roiClient.left + (roiClient.Width() + roiWidth) / 2,
                          roiClient.top + (roiClient.Height() + roiWidth) / 2);
    const CPoint roiStart = roiBitmap.CenterPoint();
    const CPoint outside[] = {
        CPoint(roiBitmap.left - 10, roiStart.y), CPoint(roiStart.x, roiBitmap.top - 10),
        CPoint(roiBitmap.right + 10, roiStart.y), CPoint(roiStart.x, roiBitmap.bottom + 10)
    };
    searchView_.setSelectionEnabled(true);
    for (const CPoint& end : outside) {
        searchView_.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(roiStart.x, roiStart.y));
        searchView_.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
        searchView_.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
        const cv::Rect selected = searchView_.selectedRect();
        const std::string roiDebug = "显示=" + std::to_string(roiBitmap.left) + "," +
            std::to_string(roiBitmap.top) + "," + std::to_string(roiBitmap.right) + "," +
            std::to_string(roiBitmap.bottom) + "，起点=" + std::to_string(roiStart.x) + "," +
            std::to_string(roiStart.y) + "，终点=" + std::to_string(end.x) + "," +
            std::to_string(end.y) + "，选区=" + std::to_string(selected.x) + "," +
            std::to_string(selected.y) + "," + std::to_string(selected.width) + "," +
            std::to_string(selected.height);
        require(selected.x >= 0 && selected.y >= 0 && selected.width > 0 && selected.height > 0 &&
                selected.x + selected.width <= search.cols &&
                selected.y + selected.height <= search.rows,
                (std::string("ROI 在图像边界外释放后没有保留有效选区，") + roiDebug).c_str());
    }
    searchView_.clearSelection();
    searchView_.setSelectionEnabled(false);
    ShowWindow(SW_HIDE);

    const auto controlRect = [this](int id) {
        CRect rect;
        GetDlgItem(id)->GetWindowRect(&rect);
        ScreenToClient(&rect);
        return rect;
    };
    if (previewPath != nullptr && *previewPath != L'\0') {
        const auto requireClearedText = [this, &require, &controlRect, previewPath](
            int id, const wchar_t* text, COLORREF background, const wchar_t* suffix) {
            for (int repeat = 0; repeat < 3; ++repeat) {
                SetDlgItemText(id, text);
                SetDlgItemText(id, L"");
                const std::wstring path = std::wstring(previewPath) + suffix;
                savePreview(path.c_str());
                CImage capture;
                require(SUCCEEDED(capture.Load(path.c_str())), "无法读取真实窗口截图");
                const CRect rect = controlRect(id);
                for (int y = rect.top; y < rect.bottom; ++y) {
                    for (int x = rect.left; x < rect.right; ++x) {
                        require(capture.GetPixel(x, y) == background,
                                (std::string("文字清除后仍有旧像素，ID=") +
                                 std::to_string(id)).c_str());
                    }
                }
            }
        };
        requireClearedText(IDC_STATUS_TEXT,
            L"预处理 0.123 ms，搜索 4.567 ms，整体 4.890 ms，共 1 个结果",
            RGB(245, 247, 250), L".cleared-status.png");
        requireClearedText(IDC_TEMPLATE_PATH,
            L"D:\\project\\c++\\sapera\\test_data\\Search\\Thick_196_Kernel.bmp",
            RGB(255, 255, 255), L".cleared-template-path.png");
        requireClearedText(IDC_SEARCH_PATH,
            L"D:\\project\\c++\\sapera\\test_data\\Search\\Thick_196.bmp",
            RGB(255, 255, 255), L".cleared-search-path.png");
    }
    const auto requireSameRow = [&controlRect, &require](int first, int second, int third,
                                                        const char* message) {
        const CRect a = controlRect(first);
        const CRect b = controlRect(second);
        const CRect c = controlRect(third);
        require(a.top == b.top && b.top == c.top, message);
    };
    CWnd* tabs = GetDlgItem(IDC_MODE_TABS);
    require(tabs != nullptr && tabs->SendMessage(TCM_GETITEMCOUNT) == 2,
            "右侧缺少搜索参数与批量评估原生页签");
    require(tabs->SendMessage(TCM_GETCURSEL) == 0, "默认页签不是搜索参数");
    SetWindowSubclass(GetDlgItem(IDC_DIVIDER_ANGLE)->GetSafeHwnd(), countControlPaint, 1, 0);
    SetWindowSubclass(GetDlgItem(IDC_SEARCH_VIEW)->GetSafeHwnd(), countControlPaint, 1, 0);
    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    dividerPaintCount = 0;
    imagePaintCount = 0;
    applyLayout();
    require(dividerPaintCount > 0 && imagePaintCount > 0,
            (std::string("布局后分割线和图像控件没有统一重绘，次数=") +
             std::to_string(dividerPaintCount) + "/" + std::to_string(imagePaintCount)).c_str());
    CRect directView;
    searchView_.GetWindowRect(&directView);
    imagePaintCount = 0;
    searchView_.SetWindowPos(nullptr, 0, 0, directView.Width() - 20, directView.Height(),
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(imagePaintCount > 0, "图像控件自身尺寸变化后没有重绘");
    applyLayout();
    ShowWindow(SW_HIDE);
    const int searchControls[] = { IDC_ANGLE_MIN, IDC_SCALE_MIN, IDC_SUBPIXEL, IDC_EXECUTE };
    const int batchControls[] = { IDC_BATCH_PATH, IDC_BATCH_RUN, IDC_BATCH_STATS };
    const char* pageStage = "初始化";
    const auto requirePage = [this, &searchControls, &batchControls, &require, &pageStage](int page) {
        for (int id : searchControls) {
            const bool visible = (GetDlgItem(id)->GetStyle() & WS_VISIBLE) != 0;
            require(visible == (page == 0),
                (std::string("搜索控件显隐错误，ID=") + std::to_string(id) +
                 "，实际=" + std::to_string(visible) + "，目标页=" + std::to_string(page) +
                 "，记录页=" + std::to_string(activeInspectorPage_) + "，阶段=" + pageStage).c_str());
        }
        for (int id : batchControls) {
            const bool visible = (GetDlgItem(id)->GetStyle() & WS_VISIBLE) != 0;
            require(visible == (page == 1),
                (std::string("批量控件显隐错误，ID=") + std::to_string(id) +
                 "，实际=" + std::to_string(visible) + "，目标页=" + std::to_string(page) +
                 "，记录页=" + std::to_string(activeInspectorPage_) + "，阶段=" + pageStage).c_str());
        }
    };
    requirePage(0);
    SetDlgItemText(IDC_BATCH_PATH, L"页签切换保留目录");
    SetDlgItemText(IDC_ANGLE_MIN, L"0");
    tabs->SendMessage(TCM_SETCURSEL, 1, 0);
    NMHDR change = {};
    change.hwndFrom = tabs->GetSafeHwnd();
    change.idFrom = IDC_MODE_TABS;
    change.code = TCN_SELCHANGE;
    SendMessage(WM_NOTIFY, IDC_MODE_TABS, reinterpret_cast<LPARAM>(&change));
    pageStage = "鼠标通知";
    requirePage(1);
    CString retainedText;
    GetDlgItemText(IDC_BATCH_PATH, retainedText);
    require(retainedText == L"页签切换保留目录", "切换页签清空了批量目录");
    tabs->SetFocus();
    tabs->SendMessage(TCM_SETCURFOCUS, 0, 0);
    tabs->SendMessage(WM_KEYDOWN, VK_RIGHT, 0);
    require(tabs->SendMessage(TCM_GETCURSEL) == 1, "原生页签不支持方向键切换");
    pageStage = "方向键进入批量";
    requirePage(1);
    tabs->SendMessage(WM_KEYDOWN, VK_LEFT, 0);
    require(tabs->SendMessage(TCM_GETCURSEL) == 0, "页签不能通过左方向键返回");
    pageStage = "方向键返回搜索";
    requirePage(0);
    GetDlgItemText(IDC_ANGLE_MIN, retainedText);
    require(retainedText == L"0", "页签键盘切换清空了搜索参数");

    CString caption;
    GetDlgItemText(IDC_ANGLE_TITLE, caption);
    require(caption == L"角度范围", "角度分区标题不符合确认稿");
    GetDlgItemText(IDC_ANGLE_HINT, caption);
    require(caption.Find(L"度") >= 0 && caption.Find(L"顺时针") >= 0,
            "角度分区没有说明单位与旋转方向");
    GetDlgItemText(IDC_SCALE_TITLE, caption);
    require(caption == L"尺度范围", "尺度分区标题不符合确认稿");
    GetDlgItemText(IDC_SCALE_HINT, caption);
    require(caption.Find(L"倍率") >= 0 && caption.Find(L"1.0") >= 0,
            "尺度分区没有说明倍率与原始尺寸");
    GetDlgItemText(IDC_PRECISION_HINT, caption);
    require(caption.Find(L"1/2/4/8") >= 0 && caption.Find(L"去重") >= 0,
            "亚像素与去重参数没有取值注释");
    GetDlgItemText(IDC_BATCH_COUNT_LABEL, caption);
    require(caption.Find(L"0") >= 0 && caption.Find(L"全部") >= 0,
            "批量任务数没有说明零值含义");
    const int dividerIds[] = { IDC_DIVIDER_ANGLE, IDC_DIVIDER_SCALE, IDC_DIVIDER_RESULT_OPTIONS };
    for (int id : dividerIds) {
        CWnd* divider = GetDlgItem(id);
        require(divider != nullptr && (divider->GetStyle() & SS_TYPEMASK) == SS_ETCHEDHORZ,
                "参数分区缺少细分隔线");
    }
    const auto requireWithin = [&controlRect, &require](int childId, int panelId) {
        const CRect child = controlRect(childId);
        const CRect panel = controlRect(panelId);
        require(child.left >= panel.left && child.top >= panel.top &&
                child.right <= panel.right && child.bottom <= panel.bottom,
                "功能控件超出所属工作区");
    };
    requireWithin(IDC_SEARCH_VIEW, IDC_PANEL_WORKSPACE);
    requireWithin(IDC_TEMPLATE_VIEW, IDC_PANEL_WORKSPACE);
    requireWithin(IDC_RESULT_LIST, IDC_PANEL_RESULTS);
    requireWithin(IDC_MODE_TABS, IDC_PANEL_PARAMETERS);
    requireWithin(IDC_ANGLE_MIN, IDC_PANEL_PARAMETERS);
    requireWithin(IDC_BATCH_PATH, IDC_PANEL_PARAMETERS);
    requireWithin(IDC_BATCH_STATS, IDC_PANEL_PARAMETERS);
    LOGFONT numericFont = {};
    GetDlgItem(IDC_ANGLE_MIN)->GetFont()->GetLogFont(&numericFont);
    require(std::wcscmp(numericFont.lfFaceName, L"Consolas") == 0,
            "数字输入没有使用等宽字体");
    CListCtrl* statsList = static_cast<CListCtrl*>(GetDlgItem(IDC_BATCH_STATS));
    require(statsList->GetHeaderCtrl() != nullptr &&
            statsList->GetHeaderCtrl()->GetItemCount() == 2 &&
            statsList->GetItemText(0, 0) == L"总任务数",
            "批量统计不是标签和值两列结构");
    CRect tabClient;
    CRect lastTab;
    tabs->GetClientRect(&tabClient);
    tabs->SendMessage(TCM_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&lastTab));
    require(tabs->SendMessage(TCM_GETROWCOUNT) == 1 && lastTab.right <= tabClient.right,
            "两个页签没有完整利用检查器宽度，出现了滚动箭头");
    require((tabs->GetStyle() & TCS_OWNERDRAWFIXED) != 0,
            "页签没有明确的选中项高亮绘制");
    require(controlRect(IDC_EXECUTION_HINT).top -
            controlRect(IDC_DIVIDER_RESULT_OPTIONS).bottom <= MulDiv(120, dpiY_, 96),
            "默认窗口的参数与执行按钮之间存在过大空白");
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
    const int toolIds[] = { IDC_LOAD_SEARCH, IDC_LOAD_TEMPLATE,
                            IDC_SELECT_ROI, IDC_CLEAR_ROI };
    const int firstToolWidth = controlRect(IDC_LOAD_SEARCH).Width();
    for (int id : toolIds) {
        require(std::abs(controlRect(id).Width() - firstToolWidth) <= 1,
                "图像工具按钮没有均分可用宽度");
    }
    const DWORD windowStyle = GetStyle();
    require((windowStyle & WS_THICKFRAME) != 0 &&
            (windowStyle & WS_MAXIMIZEBOX) != 0,
            "主窗口缺少可调整边框或最大化按钮");
    CRect frame;
    GetWindowRect(&frame);
    const LRESULT leftHit = SendMessage(WM_NCHITTEST, 0,
        MAKELPARAM(frame.left + 2, frame.top + frame.Height() / 2));
    const LRESULT rightHit = SendMessage(WM_NCHITTEST, 0,
        MAKELPARAM(frame.right - 3, frame.top + frame.Height() / 2));
    const LRESULT bottomHit = SendMessage(WM_NCHITTEST, 0,
        MAKELPARAM(frame.left + frame.Width() / 2, frame.bottom - 3));
    require(leftHit == HTLEFT && rightHit == HTRIGHT && bottomHit == HTBOTTOM,
            "实际窗口边框没有返回缩放命中区域");
    MINMAXINFO limits = {};
    SendMessage(WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    require(limits.ptMinTrackSize.x > 0 && limits.ptMinTrackSize.y > 0 &&
            limits.ptMinTrackSize.x < frame.Width() &&
            limits.ptMinTrackSize.y < frame.Height(),
            "最小窗口必须允许缩小，不能等于默认尺寸");

    CRect client;
    GetClientRect(&client);
    require(client.Width() <= 1800 && client.Height() <= 1000,
            "默认对话框尺寸过大，不适合 1080p 工作区");
    const auto validateVisibleLayout = [this, &require](bool allowEllipsis) {
        CRect bounds;
        GetClientRect(&bounds);
        CClientDC dc(this);
        std::vector<CRect> visibleRects;
        for (CWnd* child = GetWindow(GW_CHILD); child != nullptr;
             child = child->GetNextWindow()) {
            if ((child->GetStyle() & WS_VISIBLE) == 0) {
                continue;
            }
            CRect rect;
            child->GetWindowRect(&rect);
            ScreenToClient(&rect);
            if (child->GetDlgCtrlID() == IDC_BACKEND ||
                child->GetDlgCtrlID() == IDC_SUBPIXEL) {
                COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
                if (::GetComboBoxInfo(child->GetSafeHwnd(), &info)) {
                    rect.bottom = rect.top + info.rcItem.bottom + 4;
                }
            }
            require(rect.left >= bounds.left && rect.top >= bounds.top &&
                    rect.right <= bounds.right && rect.bottom <= bounds.bottom,
                    "可见控件超出对话框边界");
            for (const CRect& previous : visibleRects) {
                CRect overlap;
                require(!overlap.IntersectRect(rect, previous),
                        "可见控件之间存在重叠");
            }
            visibleRects.push_back(rect);
            const int id = child->GetDlgCtrlID();
            const bool variableText = id == IDC_SEARCH_PATH || id == IDC_TEMPLATE_PATH ||
                id == IDC_BATCH_PATH || id == IDC_BATCH_STATS || id == IDC_STATUS_TEXT;
            CString text;
            child->GetWindowText(text);
            if (text.IsEmpty() || (allowEllipsis && variableText)) {
                continue;
            }
            CFont* oldFont = dc.SelectObject(child->GetFont());
            int lineCount = 1;
            int textWidth = 0;
            int start = 0;
            while (start <= text.GetLength()) {
                const int end = text.Find(L'\n', start);
                CString line = text.Mid(start, end < 0 ? text.GetLength() : end - start);
                line.Remove(L'\r');
                textWidth = std::max(textWidth, static_cast<int>(dc.GetTextExtent(line).cx));
                if (end < 0) {
                    break;
                }
                ++lineCount;
                start = end + 1;
            }
            const int textHeight = dc.GetTextExtent(L"定位").cy * lineCount;
            dc.SelectObject(oldFont);
            require(textWidth <= rect.Width() - 6 && textHeight <= rect.Height(),
                    "说明或按钮文字被裁切");
        }
    };
    validateVisibleLayout(false);
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
    subpixelCombo_.SetCurSel(2);
    SetDlgItemText(IDC_MIN_SCORE, L"85");
    SetDlgItemText(IDC_MAX_RESULTS, L"2");
    SetDlgItemText(IDC_X_VICINITY, L"2");
    SetDlgItemText(IDC_Y_VICINITY, L"3");
    useRoi_ = true;
    roi_ = { 10, 10, 100, 100 };

    LocatorConfig config;
    require(readConfig(config), "对话框参数格式读取失败");
    CString actualConfig;
    actualConfig.Format(L"后端 %d，角度 %.2f/%.2f/%.2f，尺度 %.2f/%.2f/%.2f，"
                        L"金字塔 %d，亚像素 %d，最低分 %.2f，Top-N %d，去重 %d/%d",
        static_cast<int>(config.backend), config.angleMinDeg, config.angleMaxDeg,
        config.angleStepDeg, config.scaleMin, config.scaleMax, config.scaleStep,
        config.pyramidLevels, config.subPixelResolution, config.minScore,
        config.maxResults, config.xVicinity, config.yVicinity);
    const std::string actualConfigText(CT2A(actualConfig.GetString(), CP_UTF8));
    require(config.backend == backend && config.angleMinDeg == -2.0 &&
            config.angleMaxDeg == 1.0 && config.angleStepDeg == 1.0 &&
            config.scaleMin == 0.9 && config.scaleMax == 1.05 &&
            config.scaleStep == 0.05 && config.pyramidLevels == 2 &&
            config.subPixelResolution == 4 && config.minScore == 85.0 &&
            config.maxResults == 2 && config.xVicinity == 2 && config.yVicinity == 3,
            (std::string("对话框控件到 LocatorConfig 的映射错误，") + actualConfigText).c_str());
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
    const int flexibleIds[] = {
        IDC_LOAD_SEARCH, IDC_LOAD_TEMPLATE, IDC_SELECT_ROI,
        IDC_CLEAR_ROI, IDC_SEARCH_PATH, IDC_TEMPLATE_PATH, IDC_STATUS_TEXT,
        IDC_TEMPLATE_VIEW, IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP,
        IDC_SCALE_MIN, IDC_SCALE_MAX, IDC_SCALE_STEP, IDC_PYRAMID, IDC_SUBPIXEL,
        IDC_MIN_SCORE, IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY, IDC_EXECUTE,
        IDC_BATCH_PATH, IDC_BATCH_BROWSE, IDC_BATCH_COUNT, IDC_BATCH_THREADS,
        IDC_BATCH_RUN, IDC_BATCH_STATS
    };
    std::vector<std::pair<int, CRect>> originalFlexibleRects;
    for (int id : flexibleIds) {
        originalFlexibleRects.push_back({ id, controlRect(id) });
    }
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
            controlRect(IDC_PANEL_PARAMETERS).Width() > originalParameters.Width() &&
            controlRect(IDC_EXECUTE).top > originalExecute.top,
            "窗口放大后参数面板没有扩展或跟随窗口");
    for (const auto& entry : originalFlexibleRects) {
        require(controlRect(entry.first).Width() > entry.second.Width(),
                (std::string("窗口放大后仍有控件保持固定宽度，ID=") +
                 std::to_string(entry.first)).c_str());
    }
    validateVisibleLayout(true);
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

    SetWindowPos(nullptr, -32000, -32000, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(SW_MAXIMIZE);
    require(IsZoomed() != FALSE, "主窗口不能最大化");
    validateVisibleLayout(true);
    ShowWindow(SW_RESTORE);
    ShowWindow(SW_HIDE);
    SetWindowPos(nullptr, 0, 0, originalWindow.Width(), originalWindow.Height(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(controlRect(IDC_SEARCH_VIEW) == originalSearch &&
            controlRect(IDC_RESULT_LIST) == originalResults,
            "窗口还原后布局没有恢复");

    SetWindowPos(nullptr, 0, 0, limits.ptMinTrackSize.x, limits.ptMinTrackSize.y,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(&client);
    require(client.Width() < originalWindow.Width() &&
            client.Height() < originalWindow.Height(),
            "窗口不能缩小到比默认尺寸更小的可读布局");
    require(controlRect(IDC_LOAD_SEARCH).top != controlRect(IDC_CLEAR_ROI).top,
            "窄窗口工具按钮没有换行");
    requireSameRow(IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP,
                   "缩小后角度参数不再并排");
    requireWithin(IDC_SEARCH_VIEW, IDC_PANEL_WORKSPACE);
    requireWithin(IDC_SUBPIXEL, IDC_PANEL_PARAMETERS);
    requireWithin(IDC_BATCH_STATS, IDC_PANEL_PARAMETERS);
    validateVisibleLayout(true);
    require(resultsList_.GetItemCount() == resultCount &&
            resultsList_.GetItemText(0, 0) == firstResult && searchView_.hasResults(),
            "缩小或最大化后已有结果被清除");
    require(tabs->SendMessage(TCM_GETROWCOUNT) == 1,
            "最小窗口中原生页签被挤成两行，选中项超出单行控件");
    tabs->GetClientRect(&tabClient);
    for (int index = 0; index < 2; ++index) {
        CRect item;
        tabs->SendMessage(TCM_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item));
        require(item.top >= tabClient.top && item.bottom <= tabClient.bottom &&
                item.right <= tabClient.right,
                "最小窗口中页签项超出实际客户区");
    }
    if (previewPath != nullptr && *previewPath != L'\0') {
        cv::Mat probe(37, 93, CV_8UC1, cv::Scalar(0));
        probe(cv::Rect(8, 4, 40, 10)).setTo(220);
        probe(cv::Rect(8, 14, 12, 14)).setTo(100);
        templateView_.setImage(probe);
        const std::wstring shapePreview = std::wstring(previewPath) + L".template-shape.png";
        savePreview(shapePreview.c_str());
        CImage capture;
        require(SUCCEEDED(capture.Load(shapePreview.c_str())), "无法读取模板探针截图");
        CRect viewClient;
        templateView_.GetClientRect(&viewClient);
        const double imageScale = std::min(viewClient.Width() / 93.0, viewClient.Height() / 37.0);
        const int drawnWidth = static_cast<int>(93 * imageScale);
        const int drawnHeight = static_cast<int>(37 * imageScale);
        const CRect view = controlRect(IDC_TEMPLATE_VIEW);
        const CRect drawn(view.left + (viewClient.Width() - drawnWidth) / 2,
                          view.top + (viewClient.Height() - drawnHeight) / 2,
                          view.left + (viewClient.Width() - drawnWidth) / 2 + drawnWidth,
                          view.top + (viewClient.Height() - drawnHeight) / 2 + drawnHeight);
        const auto sample = [&drawn, &capture](int x, int y) {
            return capture.GetPixel(drawn.left + static_cast<int>((x + 0.5) * drawn.Width() / 93.0),
                                    drawn.top + static_cast<int>((y + 0.5) * drawn.Height() / 37.0));
        };
        const COLORREF bright = sample(35, 8);
        require(GetRValue(bright) == 220 && GetGValue(bright) == 220 && GetBValue(bright) == 220,
                (std::string("非 4 字节对齐模板发生错位，预期 220，实际=") +
                 std::to_string(GetRValue(bright))).c_str());
        const COLORREF lowerLeft = sample(12, 18);
        require(GetRValue(lowerLeft) == 100 && GetGValue(lowerLeft) == 100 &&
                GetBValue(lowerLeft) == 100, "模板方向或左下区域显示错误");
        require(sample(70, 20) == RGB(0, 0, 0), "模板右侧空白区域显示错误");
        templateView_.setImage(modelImage_);
        for (int cycle = 0; cycle < 3; ++cycle) {
            showInspectorPage(1);
            pageStage = "最小窗口反复切换至批量";
            requirePage(1);
            validateVisibleLayout(true);
            const std::wstring batchCycle = std::wstring(previewPath) +
                L".cycle" + std::to_wstring(cycle) + L"-batch.png";
            savePreview(batchCycle.c_str());
            showInspectorPage(0);
            pageStage = "最小窗口反复切换至搜索";
            requirePage(0);
            validateVisibleLayout(true);
            const std::wstring searchCycle = std::wstring(previewPath) +
                L".cycle" + std::to_wstring(cycle) + L"-search.png";
            savePreview(searchCycle.c_str());
        }
        const std::wstring compactPreview = std::wstring(previewPath) + L".compact.png";
        savePreview(compactPreview.c_str());
    }
    SetWindowPos(nullptr, 0, 0, originalWindow.Width(), originalWindow.Height(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(controlRect(IDC_SEARCH_VIEW) == originalSearch &&
            controlRect(IDC_RESULT_LIST) == originalResults,
            "最小尺寸恢复默认布局失败");

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
    require(batchStats_.GetItemText(0, 1) == L"197" &&
            batchStats_.GetItemText(1, 1) == L"197 / 0",
            "对话框批量评估未通过 197 对数据");
    if (previewPath != nullptr && *previewPath != L'\0') {
        showInspectorPage(1);
        const std::wstring batchPreview = std::wstring(previewPath) + L".batch.png";
        savePreview(batchPreview.c_str());
        showInspectorPage(0);
    }

    const CString batchCountBeforeReset = batchStats_.GetItemText(0, 1);
    CString batchPathBeforeReset;
    GetDlgItemText(IDC_BATCH_PATH, batchPathBeforeReset);
    SetDlgItemText(IDC_BATCH_COUNT, L"37");
    SetDlgItemText(IDC_BATCH_THREADS, L"3");
    SendMessage(WM_COMMAND, MAKEWPARAM(IDC_RESET, BN_CLICKED),
                reinterpret_cast<LPARAM>(GetDlgItem(IDC_RESET)->GetSafeHwnd()));
    LocatorConfig restored;
    LocatorConfig defaults;
    require(readConfig(restored) && restored.backend == defaults.backend &&
            restored.angleMinDeg == defaults.angleMinDeg &&
            restored.angleMaxDeg == defaults.angleMaxDeg &&
            restored.scaleMin == defaults.scaleMin && restored.scaleMax == defaults.scaleMax &&
            restored.subPixelResolution == defaults.subPixelResolution,
            "复位没有恢复初始参数和后端");
    require(!useRoi_ && searchView_.selectedRect().empty() &&
            resultsList_.GetItemCount() == 0 && !searchView_.hasResults(),
            "复位没有清空 ROI、结果及叠加内容");
    require(cv::countNonZero(searchImage_ != search) == 0 &&
            cv::countNonZero(modelImage_ != model) == 0,
            "复位没有恢复当前图片基准");
    CString status;
    GetDlgItemText(IDC_STATUS_TEXT, status);
    require(status.Find(L"复位") >= 0 && resultsList_.GetItemCount() == 0,
            "复位后不应自动搜索且应有明确状态");
    GetDlgItemText(IDC_BATCH_COUNT, status);
    require(status == L"37", "复位修改了批量任务数");
    GetDlgItemText(IDC_BATCH_THREADS, status);
    require(status == L"3", "复位修改了批量线程数");
    GetDlgItemText(IDC_BATCH_PATH, status);
    require(status == batchPathBeforeReset && batchStats_.GetItemText(0, 1) == batchCountBeforeReset,
            "复位修改了批量目录或统计");

    modelImage_ = model.clone();
    modelImage_(cv::Rect(0, 0, 1, 1)).setTo(255 - model.at<uchar>(0, 0));
    templateView_.setImage(modelImage_);
    captureBaseline();
    modelImage_ = model.clone();
    OnReset();
    require(modelImage_.size() == model.size() &&
            modelImage_.at<uchar>(0, 0) != model.at<uchar>(0, 0),
            "重新加载图片对后复位仍使用旧模板基准");
}
