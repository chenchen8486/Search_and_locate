#include "LocatorMfcDlg.h"

#include "BatchData.h"
#include "BatchLocator.h"
#include "LocatorWorkflow.h"
#include "resource.h"

#include <afxdlgs.h>
#include <atlconv.h>
#include <atlimage.h>
#include <ole2.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

struct BatchReviewData {
    std::vector<ThickPair> pairs;
    BatchDataSet data;
};

namespace {

constexpr UINT WM_LOCATOR_SELF_TEST = WM_APP + 202;
constexpr UINT WM_LOCATOR_SINGLE_COMPLETE = WM_APP + 203;

struct SingleCompletion {
    unsigned generation = 0;
    LocatorWorkflow::SingleResult run;
};

constexpr UINT WM_LOCATOR_BATCH_PROGRESS = WM_APP + 205;
constexpr UINT WM_LOCATOR_BATCH_COMPLETE = WM_APP + 206;
constexpr UINT WM_LOCATOR_DIRECTORY_SCANNED = WM_APP + 207;

struct BatchProgressData {
    int completed = 0;
    int total = 0;
};

struct BatchCompletion {
    unsigned generation = 0;
    LocatorWorkflow::BatchRun run;
    std::shared_ptr<BatchReviewData> review;
};

int groupPaintCount = 0;
int groupEraseCount = 0;
int imagePaintCount = 0;

LRESULT CALLBACK countControlPaint(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_ERASEBKGND && ::GetDlgCtrlID(window) == IDC_ANGLE_TITLE) {
        ++groupEraseCount;
    }
    if (message == WM_PAINT) {
        const int id = ::GetDlgCtrlID(window);
        if (id == IDC_ANGLE_TITLE) {
            ++groupPaintCount;
        } else if (id == IDC_SEARCH_VIEW) {
            ++imagePaintCount;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK groupBackgroundProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_ERASEBKGND) {
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client,
                 static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        return TRUE;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK editFrameProc(HWND window, UINT message, WPARAM wParam,
                               LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_NCPAINT) {
        RECT rect = {};
        GetWindowRect(window, &rect);
        OffsetRect(&rect, -rect.left, -rect.top);
        HDC dc = GetWindowDC(window);
        if (dc != nullptr) {
            HBRUSH border = CreateSolidBrush(RGB(143, 155, 168));
            FrameRect(dc, &rect, border);
            DeleteObject(border);
            ReleaseDC(window, dc);
        }
    }
    return result;
}

void redrawInputFrames(CWnd* dialog)
{
    for (CWnd* child = dialog->GetWindow(GW_CHILD); child != nullptr;
         child = child->GetNextWindow()) {
        wchar_t className[16] = {};
        GetClassNameW(child->GetSafeHwnd(), className, 16);
        if (std::wcscmp(className, L"Edit") == 0) {
            child->RedrawWindow(nullptr, nullptr,
                                RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
        }
    }
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
    ON_BN_CLICKED(IDC_BATCH_CANCEL, &CLocatorMfcDlg::OnBatchCancel)
    ON_CBN_SELCHANGE(IDC_BATCH_FILTER, &CLocatorMfcDlg::OnBatchFilterChanged)
    ON_CONTROL_RANGE(EN_CHANGE, IDC_ANGLE_MIN, IDC_Y_VICINITY, &CLocatorMfcDlg::OnParameterChanged)
    ON_CONTROL_RANGE(EN_CHANGE, IDC_BATCH_PATH, IDC_BATCH_THREADS, &CLocatorMfcDlg::OnParameterChanged)
    ON_CBN_SELCHANGE(IDC_BACKEND, &CLocatorMfcDlg::OnChoiceChanged)
    ON_CBN_SELCHANGE(IDC_SUBPIXEL, &CLocatorMfcDlg::OnChoiceChanged)
    ON_NOTIFY(TCN_SELCHANGE, IDC_MODE_TABS, &CLocatorMfcDlg::OnModeTabsChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_RESULT_LIST, &CLocatorMfcDlg::OnResultItemChanged)
    ON_MESSAGE(WM_IMAGE_SELECTION_CHANGED, &CLocatorMfcDlg::OnImageSelectionChanged)
    ON_MESSAGE(WM_IMAGE_CURSOR_POSITION, &CLocatorMfcDlg::OnImageCursorPosition)
    ON_MESSAGE(WM_LOCATOR_SELF_TEST, &CLocatorMfcDlg::OnSelfTest)
    ON_MESSAGE(WM_LOCATOR_SINGLE_COMPLETE, &CLocatorMfcDlg::OnSingleComplete)
    ON_MESSAGE(WM_LOCATOR_BATCH_PROGRESS, &CLocatorMfcDlg::OnBatchProgress)
    ON_MESSAGE(WM_LOCATOR_BATCH_COMPLETE, &CLocatorMfcDlg::OnBatchComplete)
    ON_MESSAGE(WM_LOCATOR_DIRECTORY_SCANNED, &CLocatorMfcDlg::OnDirectoryScanned)
    ON_WM_DESTROY()
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
    const int groupIds[] = {
        IDC_ANGLE_TITLE, IDC_SCALE_TITLE, IDC_RESULT_OPTIONS_TITLE,
        IDC_BATCH_DIRECTORY_TITLE, IDC_BATCH_SETTINGS_TITLE, IDC_BATCH_STATS_TITLE
    };
    for (int id : groupIds) {
        SetWindowSubclass(GetDlgItem(id)->GetSafeHwnd(), groupBackgroundProc, 1, 0);
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
    batchFilter_.SubclassDlgItem(IDC_BATCH_FILTER, this);
    for (const wchar_t* value : { L"全部", L"成功", L"无匹配", L"执行错误" }) {
        batchFilter_.AddString(value);
    }
    batchFilter_.SetCurSel(0);
    const int numericIds[] = {
        IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP, IDC_SCALE_MIN, IDC_SCALE_MAX,
        IDC_SCALE_STEP, IDC_PYRAMID, IDC_MIN_SCORE, IDC_MAX_RESULTS,
        IDC_X_VICINITY, IDC_Y_VICINITY, IDC_BATCH_COUNT, IDC_BATCH_THREADS,
        IDC_BACKEND, IDC_SUBPIXEL, IDC_BATCH_FILTER
    };
    for (int id : numericIds) {
        CWnd* input = GetDlgItem(id);
        input->SetFont(&numericFont_);
        wchar_t className[16] = {};
        GetClassNameW(input->GetSafeHwnd(), className, 16);
        if (std::wcscmp(className, L"Edit") == 0) {
            SetWindowSubclass(input->GetSafeHwnd(), editFrameProc, 2, 0);
        }
    }
    SetWindowSubclass(GetDlgItem(IDC_BATCH_PATH)->GetSafeHwnd(), editFrameProc, 2, 0);
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
    const std::wstring remembered = LocatorWorkflow::readBatchDirectoryMemory(
        LocatorWorkflow::batchDirectorySettingsPath());
    SetDlgItemText(IDC_BATCH_PATH, remembered.empty() ? L"." : remembered.c_str());
    SetDlgItemText(IDC_BATCH_DIRECTORY_LABEL, L"首次定位工程根，之后记住上次选择");
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
        IDC_ANGLE_STEP, IDC_SCALE_TITLE, IDC_SCALE_HINT,
        IDC_SCALE_MIN_LABEL, IDC_SCALE_MAX_LABEL, IDC_SCALE_STEP_LABEL, IDC_SCALE_MIN,
        IDC_SCALE_MAX, IDC_SCALE_STEP, IDC_RESULT_OPTIONS_TITLE,
        IDC_PRECISION_HINT, IDC_PYRAMID_LABEL, IDC_SUBPIXEL_LABEL, IDC_MIN_SCORE_LABEL,
        IDC_PYRAMID, IDC_SUBPIXEL, IDC_MIN_SCORE, IDC_MAX_RESULTS_LABEL,
        IDC_X_VICINITY_LABEL, IDC_Y_VICINITY_LABEL, IDC_MAX_RESULTS, IDC_X_VICINITY,
        IDC_Y_VICINITY, IDC_EXECUTION_HINT, IDC_EXECUTE
    };
    const int batchIds[] = {
        IDC_BATCH_DIRECTORY_TITLE, IDC_BATCH_DIRECTORY_LABEL, IDC_BATCH_PATH,
        IDC_BATCH_BROWSE, IDC_BATCH_SETTINGS_TITLE, IDC_BATCH_SETTINGS_HINT,
        IDC_BATCH_COUNT_LABEL, IDC_BATCH_THREADS_LABEL, IDC_BATCH_COUNT,
        IDC_BATCH_THREADS, IDC_BATCH_CONFIG_HINT, IDC_BATCH_RUN, IDC_BATCH_CANCEL,
        IDC_BATCH_PROGRESS, IDC_BATCH_FILTER, IDC_BATCH_STATS_TITLE, IDC_BATCH_STATS
    };
    for (int id : searchIds) {
        GetDlgItem(id)->ShowWindow(page == 0 ? SW_SHOW : SW_HIDE);
    }
    for (int id : batchIds) {
        GetDlgItem(id)->ShowWindow(page == 1 ? SW_SHOW : SW_HIDE);
    }
    activeInspectorPage_ = page;
    updateExecutionHint();
    if (modeTabs_.GetCurSel() != page) {
        modeTabs_.SetCurSel(page);
    }
    modeTabs_.RedrawWindow();
    redrawInputFrames(this);
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
        L"总任务数", L"成功 / 无匹配 / 错误", L"预处理均值", L"搜索均值",
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
    place(IDC_RESULT_LIST, box(results.left + sx(12), results.top + sy(44),
        results.Width() - sx(24), results.Height() - sy(56)));

    modeTabs_.SetItemSize(CSize((rightWidth - sx(8)) / 2, sy(36)));
    place(IDC_MODE_TABS, box(right, top, rightWidth, sy(40)));
    const int bodyX = right + sx(16);
    const int bodyY = top + sy(52);
    const int bodyWidth = rightWidth - sx(32);
    const int sectionShift = std::max(0, std::min(sy(24),
        (static_cast<int>(inspector.Height()) - sy(640)) / 4));
    const int sectionX = bodyX + sx(12);
    const int sectionWidth = bodyWidth - sx(24);
    place(IDC_PARAMETER_HINT, box(bodyX, bodyY, bodyWidth, sy(18)));
    place(IDC_ANGLE_TITLE, box(bodyX, bodyY + sy(20) + sectionShift, bodyWidth, sy(132)));
    place(IDC_ANGLE_HINT, box(sectionX, bodyY + sy(48) + sectionShift, sectionWidth, sy(16)));
    place(IDC_SCALE_TITLE, box(bodyX, bodyY + sy(154) + sectionShift * 2, bodyWidth, sy(130)));
    place(IDC_SCALE_HINT, box(sectionX, bodyY + sy(180) + sectionShift * 2, sectionWidth, sy(16)));
    place(IDC_RESULT_OPTIONS_TITLE, box(bodyX, bodyY + sy(288) + sectionShift * 3, bodyWidth, sy(182)));
    place(IDC_PRECISION_HINT, box(sectionX, bodyY + sy(312) + sectionShift * 3, sectionWidth, sy(16)));
    const auto fieldRow = [&place, &box, &sx, &sy, sectionX, bodyY, sectionWidth](
        const int (&labels)[3], const int (&edits)[3], int labelY, int editY, int offset) {
        const int fieldWidth = (sectionWidth - sx(16)) / 3;
        for (int column = 0; column < 3; ++column) {
            const int x = sectionX + column * (fieldWidth + sx(8));
            place(labels[column], box(x, bodyY + sy(labelY) + offset, fieldWidth, sy(16)));
            const int height = edits[column] == IDC_SUBPIXEL ? sy(150) : sy(32);
            place(edits[column], box(x, bodyY + sy(editY) + offset, fieldWidth, height));
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
    place(IDC_EXECUTION_HINT, box(bodyX, inspector.bottom - sy(90), bodyWidth, sy(38)));
    place(IDC_EXECUTE, box(bodyX, inspector.bottom - sy(52), bodyWidth, sy(36)));

    place(IDC_BATCH_DIRECTORY_TITLE, box(bodyX, bodyY - sy(6), bodyWidth, sy(104)));
    place(IDC_BATCH_DIRECTORY_LABEL, box(sectionX, bodyY + sy(26), sectionWidth, sy(16)));
    const int browseWidth = sectionWidth / 4;
    place(IDC_BATCH_PATH, box(sectionX, bodyY + sy(52),
        sectionWidth - browseWidth - sx(8), sy(32)));
    place(IDC_BATCH_BROWSE, box(sectionX + sectionWidth - browseWidth,
        bodyY + sy(52), browseWidth, sy(32)));
    place(IDC_BATCH_SETTINGS_TITLE, box(bodyX, bodyY + sy(112), bodyWidth, sy(230)));
    place(IDC_BATCH_SETTINGS_HINT, box(sectionX, bodyY + sy(140), sectionWidth, sy(16)));
    const int batchFieldWidth = (sectionWidth - sx(8)) / 2;
    place(IDC_BATCH_COUNT_LABEL, box(sectionX, bodyY + sy(174), batchFieldWidth, sy(16)));
    place(IDC_BATCH_THREADS_LABEL, box(sectionX + batchFieldWidth + sx(8),
        bodyY + sy(174), batchFieldWidth, sy(16)));
    place(IDC_BATCH_COUNT, box(sectionX, bodyY + sy(192), batchFieldWidth, sy(32)));
    place(IDC_BATCH_THREADS, box(sectionX + batchFieldWidth + sx(8),
        bodyY + sy(192), batchFieldWidth, sy(32)));
    place(IDC_BATCH_CONFIG_HINT, box(sectionX, bodyY + sy(234), sectionWidth, sy(54)));
    place(IDC_BATCH_RUN, box(sectionX, bodyY + sy(296), batchFieldWidth, sy(36)));
    place(IDC_BATCH_CANCEL, box(sectionX + batchFieldWidth + sx(8), bodyY + sy(296),
        batchFieldWidth, sy(36)));
    const int statsTop = bodyY + sy(350);
    const int statsBottom = inspector.bottom - sy(16);
    place(IDC_BATCH_STATS_TITLE, box(bodyX, statsTop, bodyWidth, statsBottom - statsTop));
    place(IDC_BATCH_PROGRESS, box(sectionX, bodyY + sy(374), sectionWidth - sx(142), sy(24)));
    place(IDC_BATCH_FILTER, box(sectionX + sectionWidth - sx(136), bodyY + sy(372),
        sx(136), sy(150)));
    place(IDC_BATCH_STATS, box(sectionX, bodyY + sy(402), sectionWidth,
        statsBottom - bodyY - sy(414)));
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
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
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
    redrawInputFrames(this);
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
        if (id == IDC_STATUS_TEXT || id == IDC_ANGLE_TITLE || id == IDC_SCALE_TITLE ||
            id == IDC_RESULT_OPTIONS_TITLE || id == IDC_BATCH_DIRECTORY_TITLE ||
            id == IDC_BATCH_SETTINGS_TITLE || id == IDC_BATCH_STATS_TITLE) {
            dc->SetTextColor(RGB(22, 114, 135));
        } else if (id == IDC_APP_SUBTITLE || id == IDC_RESULT_HINT ||
                   id == IDC_PRECISION_HINT || id == IDC_SEARCH_PATH ||
                   id == IDC_TEMPLATE_PATH || id == IDC_IMAGE_META_SEARCH ||
                   id == IDC_IMAGE_META_TEMPLATE || id == IDC_PARAMETER_HINT ||
                   id == IDC_ANGLE_HINT || id == IDC_SCALE_HINT ||
                   id == IDC_BATCH_DIRECTORY_LABEL || id == IDC_BATCH_SETTINGS_HINT ||
                   id == IDC_BATCH_CONFIG_HINT || id == IDC_EXECUTION_HINT) {
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
    std::string loadError;
    if (!loadGrayImage(std::string(CT2A(path.GetString(), CP_ACP)), image, &loadError)) {
        const CString message = L"搜索图读取失败：" + CString(CA2W(loadError.c_str(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, message);
        AfxMessageBox(message);
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
    std::string loadError;
    if (!loadGrayImage(std::string(CT2A(path.GetString(), CP_ACP)), image, &loadError)) {
        const CString message = L"模板读取失败：" + CString(CA2W(loadError.c_str(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, message);
        AfxMessageBox(message);
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

LRESULT CLocatorMfcDlg::OnImageCursorPosition(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;
    if (searchImage_.empty()) {
        return 0;
    }
    const CPoint position = searchView_.cursorImagePosition();
    CString text;
    if (position.x >= 0) {
        text.Format(L"%d × %d | x=%d y=%d | %d%%", searchImage_.cols, searchImage_.rows,
                    position.x, position.y, static_cast<int>(std::lround(searchView_.zoom() * 100)));
    } else {
        text.Format(L"%d × %d | 缩放 %d%%", searchImage_.cols, searchImage_.rows,
                    static_cast<int>(std::lround(searchView_.zoom() * 100)));
    }
    SetDlgItemText(IDC_IMAGE_META_SEARCH, text);
    return 0;
}

void CLocatorMfcDlg::OnParameterChanged(UINT id)
{
    if (id == IDC_BATCH_PATH) {
        batchPairCount_ = 0;
    }
    if (id == IDC_BATCH_PATH || id == IDC_BATCH_COUNT) {
        updateBatchDataHint();
    }
    updateExecutionHint();
}

void CLocatorMfcDlg::updateBatchDataHint(int requestedCount)
{
    if (batchPairCount_ <= 0) {
        SetDlgItemText(IDC_BATCH_SETTINGS_HINT, L"目录待扫描；任务数 0 表示全部");
        return;
    }
    int count = requestedCount;
    if (count < 0 && !readInt(this, IDC_BATCH_COUNT, count)) {
        SetDlgItemText(IDC_BATCH_SETTINGS_HINT, L"任务数必须是大于或等于 0 的整数");
        return;
    }
    if (count < 0) {
        SetDlgItemText(IDC_BATCH_SETTINGS_HINT, L"任务数必须是大于或等于 0 的整数");
        return;
    }
    const std::string hint = LocatorWorkflow::formatBatchDataHint(batchPairCount_, count);
    SetDlgItemText(IDC_BATCH_SETTINGS_HINT, CString(CA2W(hint.c_str(), CP_UTF8)));
}

LRESULT CLocatorMfcDlg::OnDirectoryScanned(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    batchPairCount_ = static_cast<int>(lParam);
    updateBatchDataHint();
    return 0;
}

void CLocatorMfcDlg::OnChoiceChanged()
{
    updateExecutionHint();
}

void CLocatorMfcDlg::updateExecutionHint()
{
    LocatorConfig config;
    if (!readConfig(config)) {
        SetDlgItemText(IDC_EXECUTION_HINT, L"参数待修正，请检查数值格式");
        return;
    }
    CString angle;
    if (config.angleMinDeg == config.angleMaxDeg) {
        angle.Format(L"角度 %.6g°", config.angleMinDeg);
    } else {
        angle.Format(L"角度 %.6g～%.6g°（步进 %.6g°）", config.angleMinDeg,
                     config.angleMaxDeg, config.angleStepDeg);
    }
    CString scale;
    if (config.scaleMin == config.scaleMax) {
        scale.Format(L"尺度 %.6g", config.scaleMin);
    } else {
        scale.Format(L"尺度 %.6g～%.6g（步进 %.6g）", config.scaleMin,
                     config.scaleMax, config.scaleStep);
    }
    CString range;
    if (useRoi_) {
        range.Format(L"ROI：x=%d，y=%d，宽=%d，高=%d", roi_.x, roi_.y, roi_.width, roi_.height);
    } else {
        range = L"搜索范围：完整图像";
    }
    SetDlgItemText(IDC_EXECUTION_HINT, angle + L"，" + scale + L"\n" + range);
    updateBatchHint();
}

void CLocatorMfcDlg::updateBatchHint()
{
    LocatorConfig config;
    if (!readConfig(config)) {
        SetDlgItemText(IDC_BATCH_CONFIG_HINT, L"搜索参数待修正，请返回搜索参数页检查");
        return;
    }
    CString angle;
    CString scale;
    if (config.angleMinDeg == config.angleMaxDeg) {
        angle.Format(L"%g°", config.angleMinDeg);
    } else {
        angle.Format(L"%g～%g°，步进%g", config.angleMinDeg, config.angleMaxDeg, config.angleStepDeg);
    }
    if (config.scaleMin == config.scaleMax) {
        scale.Format(L"%g", config.scaleMin);
    } else {
        scale.Format(L"%g～%g，步进%g", config.scaleMin, config.scaleMax, config.scaleStep);
    }
    CString roi;
    if (useRoi_) {
        roi.Format(L"ROI=(%d,%d,%d,%d)", roi_.x, roi_.y, roi_.width, roi_.height);
    } else {
        roi = L"完整图像";
    }
    CString summary;
    summary.Format(L"后端=%s | 金字塔=%d | 亚像素=1/%d | 最低分=%g\n"
                   L"角度=%s | 尺度=%s\nTop-N=%d | 去重=(%d,%d)px | %s",
                   config.backend == LocatorBackend::Gpu ? L"GPU" : L"CPU",
                   config.pyramidLevels, config.subPixelResolution, config.minScore,
                   angle.GetString(), scale.GetString(), config.maxResults,
                   config.xVicinity, config.yVicinity, roi.GetString());
    SetDlgItemText(IDC_BATCH_CONFIG_HINT, summary);
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

bool CLocatorMfcDlg::readConfig(LocatorConfig& config, CString* diagnosis, int* invalidControl)
{
    const auto fail = [diagnosis, invalidControl](int id, const wchar_t* message) {
        if (diagnosis != nullptr) {
            *diagnosis = message;
        }
        if (invalidControl != nullptr) {
            *invalidControl = id;
        }
        return false;
    };
    const int backend = backendCombo_.GetCurSel();
    if (backend < 0) {
        return fail(IDC_BACKEND, L"计算后端必须选择 CPU 或 GPU");
    }
    config.backend = backend == 1 ? LocatorBackend::Gpu : LocatorBackend::Cpu;
    const struct { int id; double* value; const wchar_t* label; } doubleFields[] = {
        { IDC_ANGLE_MIN, &config.angleMinDeg, L"最小角度" },
        { IDC_ANGLE_MAX, &config.angleMaxDeg, L"最大角度" },
        { IDC_ANGLE_STEP, &config.angleStepDeg, L"角度步进" },
        { IDC_SCALE_MIN, &config.scaleMin, L"最小尺度" },
        { IDC_SCALE_MAX, &config.scaleMax, L"最大尺度" },
        { IDC_SCALE_STEP, &config.scaleStep, L"尺度步进" },
        { IDC_MIN_SCORE, &config.minScore, L"最低分数" }
    };
    for (const auto& field : doubleFields) {
        if (!readDouble(this, field.id, *field.value)) {
            return fail(field.id, CString(field.label) + L"必须是有效的有限数值");
        }
    }
    const struct { int id; int* value; const wchar_t* label; } intFields[] = {
        { IDC_PYRAMID, &config.pyramidLevels, L"金字塔层数" },
        { IDC_MAX_RESULTS, &config.maxResults, L"Top-N" },
        { IDC_X_VICINITY, &config.xVicinity, L"x 去重半径" },
        { IDC_Y_VICINITY, &config.yVicinity, L"y 去重半径" }
    };
    for (const auto& field : intFields) {
        if (!readInt(this, field.id, *field.value)) {
            return fail(field.id, CString(field.label) + L"必须是整数");
        }
    }
    const int selected = subpixelCombo_.GetCurSel();
    if (selected < 0) {
        return fail(IDC_SUBPIXEL, L"亚像素必须选择 1、2、4 或 8 档");
    }
    const int resolutions[] = { 1, 2, 4, 8 };
    config.subPixelResolution = resolutions[selected];
    if (config.angleStepDeg <= 0.0) return fail(IDC_ANGLE_STEP, L"角度步进必须大于 0°");
    if (config.angleMinDeg > config.angleMaxDeg) return fail(IDC_ANGLE_MIN, L"最小角度必须不大于最大角度");
    if (config.scaleStep <= 0.0) return fail(IDC_SCALE_STEP, L"尺度步进必须大于 0");
    if (config.scaleMin <= 0.0) return fail(IDC_SCALE_MIN, L"最小尺度必须大于 0");
    if (config.scaleMax < config.scaleMin) return fail(IDC_SCALE_MAX, L"最大尺度必须不小于最小尺度");
    if (config.pyramidLevels < 1) return fail(IDC_PYRAMID, L"金字塔层数必须至少为 1");
    if (config.minScore < 0.0 || config.minScore > 100.0) return fail(IDC_MIN_SCORE, L"最低分数必须位于 0～100");
    if (config.maxResults < 1) return fail(IDC_MAX_RESULTS, L"Top-N 必须至少为 1");
    if (config.xVicinity < 0) return fail(IDC_X_VICINITY, L"x 去重半径必须大于或等于 0");
    if (config.yVicinity < 0) return fail(IDC_Y_VICINITY, L"y 去重半径必须大于或等于 0");
    return true;
}

void CLocatorMfcDlg::showError(LocatorError error)
{
    const CString message = errorText(error);
    SetDlgItemText(IDC_STATUS_TEXT, message);
    if (IsWindowVisible()) {
        AfxMessageBox(message);
    }
}

void CLocatorMfcDlg::setSingleControlsEnabled(bool enabled)
{
    const int ids[] = {
        IDC_LOAD_SEARCH, IDC_LOAD_TEMPLATE, IDC_SELECT_ROI, IDC_CLEAR_ROI,
        IDC_EXECUTE, IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP, IDC_SCALE_MIN,
        IDC_SCALE_MAX, IDC_SCALE_STEP, IDC_PYRAMID, IDC_SUBPIXEL, IDC_MIN_SCORE,
        IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY, IDC_BACKEND
    };
    for (int id : ids) {
        GetDlgItem(id)->EnableWindow(enabled);
    }
    GetDlgItem(IDC_RESET)->EnableWindow(enabled && baseline_.valid);
}

void CLocatorMfcDlg::OnExecute()
{
    if (singleRunning_ || batchRunning_) {
        return;
    }
    clearResultDisplay();
    if (searchImage_.empty() || modelImage_.empty()) {
        SetDlgItemText(IDC_STATUS_TEXT, L"请先加载搜索图和模板");
        return;
    }
    LocatorConfig config;
    CString diagnosis;
    int invalidControl = 0;
    if (!readConfig(config, &diagnosis, &invalidControl)) {
        SetDlgItemText(IDC_STATUS_TEXT, diagnosis);
        showInspectorPage(0);
        CWnd* input = GetDlgItem(invalidControl);
        if (input != nullptr) {
            input->SetFocus();
            input->SendMessage(EM_SETSEL, 0, -1);
        }
        return;
    }
    config.useRoi = useRoi_;
    config.roi = roi_;
    const cv::Mat model = modelImage_.clone();
    const cv::Mat search = searchImage_.clone();
    const unsigned generation = ++singleGeneration_;
    singleRunning_ = true;
    setSingleControlsEnabled(false);
    SetDlgItemText(IDC_STATUS_TEXT, L"正在后台搜索，可调整窗口或切换页签");
    try {
        singleWorker_ = std::thread([this, model, search, config, generation]() {
            SingleCompletion* completion = new SingleCompletion;
            completion->generation = generation;
            completion->run = LocatorWorkflow::runSingle(model, search, config);
            if (!PostMessage(WM_LOCATOR_SINGLE_COMPLETE, 0,
                             reinterpret_cast<LPARAM>(completion))) {
                delete completion;
            }
        });
    } catch (const std::exception& error) {
        singleRunning_ = false;
        setSingleControlsEnabled(true);
        const CString message(CA2W(error.what(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, L"无法启动单次搜索：" + message);
    }
}

void CLocatorMfcDlg::showSingleResult(const LocatorWorkflow::SingleResult& run)
{
    if (run.error != LocatorError::Ok) {
        clearResultDisplay();
        if (run.diagnostic.empty()) {
            showError(run.error);
        } else {
            const CString message = L"单次搜索异常：" + CString(CA2W(run.diagnostic.c_str(), CP_UTF8));
            SetDlgItemText(IDC_STATUS_TEXT, message);
            if (IsWindowVisible()) {
                AfxMessageBox(message);
            }
        }
        return;
    }
    const std::vector<LocatorResult>& results = run.matches;
    prepareResultList(false);
    for (size_t i = 0; i < results.size(); ++i) {
        const LocatorResult& result = results[i];
        CString value;
        value.Format(L"%d", static_cast<int>(i + 1));
        const int row = resultsList_.InsertItem(static_cast<int>(i), value);
        value.Format(L"%.4f", result.x);
        resultsList_.SetItemText(row, 1, value);
        value.Format(L"%.4f", result.y);
        resultsList_.SetItemText(row, 2, value);
        value.Format(L"%.4f", result.angle);
        resultsList_.SetItemText(row, 3, value);
        value.Format(L"%.4f", result.scale);
        resultsList_.SetItemText(row, 4, value);
        value.Format(L"%.2f", result.score);
        resultsList_.SetItemText(row, 5, value);
    }
    searchView_.setResults(results, modelImage_.cols, modelImage_.rows);
    CString status;
    status.Format(L"预处理 %.3f ms，搜索 %.3f ms，整体 %.3f ms，共 %d 个结果",
                  run.preprocessMs, run.searchMs, run.totalMs, static_cast<int>(results.size()));
    SetDlgItemText(IDC_STATUS_TEXT, status);
}

void CLocatorMfcDlg::showBatchRow(int row)
{
    if (row < 0 || row >= static_cast<int>(batchRows_.size()) || !batchReviewData_ ||
        batchReviewData_->pairs.empty()) {
        return;
    }
    const BatchRow& selected = batchRows_[row];
    if (selected.taskId < 0 ||
        selected.taskId >= static_cast<int>(batchReviewData_->data.tasks.size()) ||
        selected.taskId >= static_cast<int>(batchResultData_.size()) ||
        batchResultData_[selected.taskId].id != selected.taskId) {
        return;
    }
    const BatchTask& task = batchReviewData_->data.tasks[selected.taskId];
    const BatchResult& result = batchResultData_[selected.taskId];
    if (selected.matchIndex >= static_cast<int>(result.matches.size())) {
        return;
    }
    searchImage_ = cv::Mat(task.search.height, task.search.width, CV_8UC1,
        const_cast<unsigned char*>(task.search.data), task.search.stride).clone();
    modelImage_ = cv::Mat(task.model.height, task.model.width, CV_8UC1,
        const_cast<unsigned char*>(task.model.data), task.model.stride).clone();
    searchView_.setImage(searchImage_);
    templateView_.setImage(modelImage_);
    useRoi_ = task.config.useRoi;
    roi_ = task.config.roi;
    if (useRoi_) {
        searchView_.setSelection(cv::Rect(roi_.x, roi_.y, roi_.width, roi_.height));
    }
    searchView_.setSelectionEnabled(false);
    searchView_.setResults(result.error == LocatorError::Ok ? result.matches : std::vector<LocatorResult>(),
                           modelImage_.cols, modelImage_.rows);
    searchView_.setSelectedResult(selected.matchIndex);

    const ThickPair& pair = batchReviewData_->pairs[
        static_cast<size_t>(selected.taskId) % batchReviewData_->pairs.size()];
    SetDlgItemText(IDC_SEARCH_PATH, CString(CA2W(pair.searchPath.c_str(), CP_ACP)));
    SetDlgItemText(IDC_TEMPLATE_PATH, CString(CA2W(pair.kernelPath.c_str(), CP_ACP)));
    CString dimensions;
    dimensions.Format(L"%d × %d", searchImage_.cols, searchImage_.rows);
    SetDlgItemText(IDC_IMAGE_META_SEARCH, dimensions);
    dimensions.Format(L"%d × %d", modelImage_.cols, modelImage_.rows);
    SetDlgItemText(IDC_IMAGE_META_TEMPLATE, dimensions);
    CString batchHint;
    GetDlgItemText(IDC_BATCH_CONFIG_HINT, batchHint);
    updateExecutionHint();
    SetDlgItemText(IDC_BATCH_CONFIG_HINT, batchHint);

    CString status;
    if (result.error != LocatorError::Ok) {
        status.Format(L"查看批量任务 %d（Thick_%d）：%s", result.id, pair.id,
                      errorText(result.error));
    } else if (result.matches.empty()) {
        status.Format(L"查看批量任务 %d（Thick_%d）：无匹配，已显示对应图片", result.id, pair.id);
    } else {
        status.Format(L"查看批量任务 %d（Thick_%d）：匹配 %d / %d", result.id, pair.id,
                      selected.matchIndex + 1, static_cast<int>(result.matches.size()));
    }
    SetDlgItemText(IDC_STATUS_TEXT, status);
}

void CLocatorMfcDlg::OnResultItemChanged(NMHDR* header, LRESULT* result)
{
    const NMLISTVIEW* change = reinterpret_cast<const NMLISTVIEW*>(header);
    if ((change->uNewState & LVIS_SELECTED) != 0) {
        if (batchResults_) {
            showBatchRow(change->iItem);
        } else {
            searchView_.setSelectedResult(change->iItem);
        }
    }
    *result = 0;
}

LRESULT CLocatorMfcDlg::OnSingleComplete(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    SingleCompletion* completion = reinterpret_cast<SingleCompletion*>(lParam);
    if (completion == nullptr) {
        return 0;
    }
    const bool current = completion->generation == singleGeneration_;
    singleRunning_ = false;
    if (singleWorker_.joinable()) {
        singleWorker_.join();
    }
    setSingleControlsEnabled(true);
    if (current) {
        showSingleResult(completion->run);
    }
    delete completion;
    return 0;
}

void CLocatorMfcDlg::OnDestroy()
{
    ++singleGeneration_;
    ++batchGeneration_;
    if (singleWorker_.joinable()) {
        singleWorker_.join();
    }
    if (batchWorker_.joinable()) {
        batchWorker_.join();
    }
    singleRunning_ = false;
    batchRunning_ = false;
    MSG message;
    while (PeekMessage(&message, GetSafeHwnd(), WM_LOCATOR_SINGLE_COMPLETE,
                       WM_LOCATOR_BATCH_COMPLETE, PM_REMOVE)) {
        if (message.message == WM_LOCATOR_SINGLE_COMPLETE) {
            delete reinterpret_cast<SingleCompletion*>(message.lParam);
        } else if (message.message == WM_LOCATOR_BATCH_COMPLETE) {
            delete reinterpret_cast<BatchCompletion*>(message.lParam);
        } else if (message.message == WM_LOCATOR_BATCH_PROGRESS) {
            delete reinterpret_cast<BatchProgressData*>(message.lParam);
        }
    }
    CDialogEx::OnDestroy();
}


void CLocatorMfcDlg::prepareResultList(bool batch)
{
    resultsList_.DeleteAllItems();
    batchRows_.clear();
    CHeaderCtrl* header = resultsList_.GetHeaderCtrl();
    while (header != nullptr && header->GetItemCount() > 0) {
        resultsList_.DeleteColumn(0);
    }
    const wchar_t* batchTitles[] = {
        L"任务编号", L"状态", L"匹配序号", L"中心 x", L"中心 y", L"角度",
        L"尺度", L"分数", L"预处理 ms", L"搜索 ms", L"整体 ms"
    };
    const wchar_t* singleTitles[] = {
        L"序号", L"中心 x（px）", L"中心 y（px）", L"角度（°）", L"尺度（倍率）", L"分数（%）"
    };
    const wchar_t** titles = batch ? batchTitles : singleTitles;
    const int count = batch ? 11 : 6;
    for (int column = 0; column < count; ++column) {
        resultsList_.InsertColumn(column, titles[column], LVCFMT_LEFT, 100);
    }
    batchResults_ = batch;
    resizeResultColumns();
}

void CLocatorMfcDlg::resizeResultColumns()
{
    CHeaderCtrl* header = resultsList_.GetHeaderCtrl();
    if (header == nullptr) {
        return;
    }
    const int count = header->GetItemCount();
    CRect client;
    resultsList_.GetClientRect(&client);
    const int width = client.Width();
    if (count == 11) {
        const int batchWidths[] = { 60, 140, 60, 85, 85, 75, 75, 80, 95, 95, 95 };
        const int totalWidth = 945;
        for (int column = 0; column < count; ++column) {
            resultsList_.SetColumnWidth(column, std::max(width, totalWidth) * batchWidths[column] / totalWidth);
        }
        return;
    }
    if (count != 6) {
        return;
    }
    const int singleWidths[] = { 8, 18, 18, 16, 16, 24 };
    for (int column = 0; column < count; ++column) {
        resultsList_.SetColumnWidth(column, width * singleWidths[column] / 100);
    }
}

int CALLBACK browseInitialDirectoryCallback(HWND browser, UINT message, LPARAM, LPARAM data)
{
    if (message == BFFM_INITIALIZED) {
        SendMessageW(browser, BFFM_SETSELECTIONW, TRUE, data);
    }
    return 0;
}

void CLocatorMfcDlg::OnBatchBrowse()
{
    CString input;
    GetDlgItemText(IDC_BATCH_PATH, input);
    input.Trim();
    const std::wstring project = LocatorWorkflow::projectDirectory();
    std::wstring initial = LocatorWorkflow::resolveBatchDirectory(input.GetString(), project);
    if (initial.empty()) {
        initial = LocatorWorkflow::readBatchDirectoryMemory(LocatorWorkflow::batchDirectorySettingsPath());
    }
    if (initial.empty()) {
        initial = project;
    }
    const HRESULT initialized = OleInitialize(nullptr);
    if (FAILED(initialized)) {
        SetDlgItemText(IDC_STATUS_TEXT, L"无法初始化目录选择所需的 COM 环境");
        return;
    }
    BROWSEINFO info = {};
    info.hwndOwner = GetSafeHwnd();
    info.lpszTitle = L"选择包含 Thick 搜索图与模板的目录";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    info.lpfn = browseInitialDirectoryCallback;
    info.lParam = reinterpret_cast<LPARAM>(initial.c_str());
    LPITEMIDLIST selection = SHBrowseForFolderW(&info);
    OleUninitialize();
    if (selection == nullptr) {
        return;
    }
    wchar_t path[MAX_PATH] = {};
    const BOOL selected = SHGetPathFromIDListW(selection, path);
    CoTaskMemFree(selection);
    if (!selected) {
        SetDlgItemText(IDC_STATUS_TEXT, L"无法取得所选目录路径");
        return;
    }
    SetDlgItemText(IDC_BATCH_PATH, path);
    const bool remembered = LocatorWorkflow::writeBatchDirectoryMemory(
        LocatorWorkflow::batchDirectorySettingsPath(), path);
    const CString memoryWarning = remembered ? CString() : CString(L"；目录记忆保存失败，请检查程序目录写入权限");
    try {
        const std::string nativePath(CT2A(path, CP_ACP));
        batchPairCount_ = static_cast<int>(findThickPairs(nativePath).size());
        updateBatchDataHint();
        SetDlgItemText(IDC_STATUS_TEXT, L"目录扫描完成" + memoryWarning);
    } catch (const std::exception& error) {
        const CString message(CA2W(error.what(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, message + memoryWarning);
    }
    if (!remembered) {
        AfxMessageBox(L"已使用本次选择的目录，但无法保存目录记忆。请检查程序目录的写入权限。");
    }
}

void CLocatorMfcDlg::setBatchControlsEnabled(bool enabled)
{
    const int ids[] = { IDC_BATCH_PATH, IDC_BATCH_BROWSE, IDC_BATCH_COUNT,
                        IDC_BATCH_THREADS, IDC_BATCH_RUN, IDC_BATCH_FILTER };
    for (int id : ids) {
        GetDlgItem(id)->EnableWindow(enabled);
    }
    GetDlgItem(IDC_BATCH_CANCEL)->EnableWindow(!enabled && batchRunning_);
}

void CLocatorMfcDlg::clearBatchResultDisplay()
{
    batchResultData_.clear();
    displayedBatchResults_.clear();
    batchReviewData_.reset();
    batchFilter_.SetCurSel(0);
    prepareResultList(true);
    searchView_.setResults({}, modelImage_.cols, modelImage_.rows);
    setBatchStats(std::vector<CString>(7, CString(L"本次未产生")));
    SetDlgItemText(IDC_BATCH_PROGRESS, L"等待本次执行");
}

void CLocatorMfcDlg::populateBatchResults()
{
    prepareResultList(true);
    const int selected = std::max(0, batchFilter_.GetCurSel());
    displayedBatchResults_ = LocatorWorkflow::filterBatchResults(
        batchResultData_, static_cast<LocatorWorkflow::BatchFilter>(selected));
    for (const BatchResult& result : displayedBatchResults_) {
        const int matchCount = std::max(1, static_cast<int>(result.matches.size()));
        for (int matchIndex = 0; matchIndex < matchCount; ++matchIndex) {
            const int row = resultsList_.GetItemCount();
            CString value;
            value.Format(L"%d", result.id);
            resultsList_.InsertItem(row, value);
            batchRows_.push_back({ result.id, result.matches.empty() ? -1 : matchIndex });
            resultsList_.SetItemText(row, 1, result.error != LocatorError::Ok
                ? CString(L"执行错误：") + errorText(result.error)
                : result.matches.empty() ? CString(L"无匹配") : CString(L"成功"));
            if (result.matches.empty()) {
                resultsList_.SetItemText(row, 2, L"-");
            } else {
                const LocatorResult& match = result.matches[matchIndex];
                value.Format(L"%d", matchIndex + 1);
                resultsList_.SetItemText(row, 2, value);
                const double fields[] = { match.x, match.y, match.angle, match.scale, match.score };
                for (int field = 0; field < 5; ++field) {
                    value.Format(L"%.4f", fields[field]);
                    resultsList_.SetItemText(row, field + 3, value);
                }
            }
            const double timings[] = { result.preprocessMs, result.searchMs, result.totalMs };
            for (int field = 0; field < 3; ++field) {
                value.Format(L"%.3f", timings[field]);
                resultsList_.SetItemText(row, field + 8, value);
            }
        }
    }
}

void CLocatorMfcDlg::showBatchResult(const LocatorWorkflow::BatchRun& run)
{
    batchResultData_ = run.results;
    populateBatchResults();
    const LocatorWorkflow::BatchSummary summary = LocatorWorkflow::summarizeBatch(run);
    std::vector<CString> statistics;
    CString value;
    value.Format(L"%d / %d", run.completed, run.total);
    statistics.push_back(value);
    if (run.cancelled) {
        statistics.push_back(L"已取消");
    } else {
        value.Format(L"%d / %d / %d", summary.matched, summary.noMatch, summary.errors);
        statistics.push_back(value);
    }
    const auto statistic = [&statistics](double number) {
        CString value;
        value.Format(L"%.3f ms", number);
        statistics.push_back(value);
    };
    statistic(summary.preprocessMeanMs);
    statistic(summary.searchMeanMs);
    statistic(summary.totalMeanMs);
    statistic(summary.p99Ms);
    statistic(run.totalMs);
    setBatchStats(statistics);
    CString progress;
    progress.Format(L"已完成 %d / %d", run.completed, run.total);
    SetDlgItemText(IDC_BATCH_PROGRESS, progress);
    if (run.cancelled) {
        SetDlgItemText(IDC_STATUS_TEXT, L"批量评估已取消，只保留已完成任务的结果");
    } else {
        SetDlgItemText(IDC_STATUS_TEXT, L"批量评估完成，点击结果可查看图像及匹配");
    }
}

void CLocatorMfcDlg::OnBatchFilterChanged()
{
    populateBatchResults();
}

void CLocatorMfcDlg::OnBatchCancel()
{
    if (batchRunning_) {
        batchCancelRequested_ = true;
        GetDlgItem(IDC_BATCH_CANCEL)->EnableWindow(FALSE);
        SetDlgItemText(IDC_STATUS_TEXT, L"正在取消，当前任务或 CUDA 分组完成后停止");
    }
}

void CLocatorMfcDlg::OnBatchRun()
{
    if (batchRunning_ || singleRunning_) {
        return;
    }
    clearBatchResultDisplay();
    CString directory;
    GetDlgItemText(IDC_BATCH_PATH, directory);
    directory.Trim();
    directory = LocatorWorkflow::resolveBatchDirectory(
        directory.GetString(), LocatorWorkflow::projectDirectory()).c_str();
    int count = 0;
    int threads = 0;
    LocatorConfig config;
    CString diagnosis;
    if (directory.IsEmpty()) {
        SetDlgItemText(IDC_STATUS_TEXT, L"请输入或选择 Thick 数据目录");
        return;
    }
    if (!readInt(this, IDC_BATCH_COUNT, count) || count < 0) {
        SetDlgItemText(IDC_STATUS_TEXT, L"批量任务数必须是大于或等于 0 的整数");
        return;
    }
    if (!readInt(this, IDC_BATCH_THREADS, threads)) {
        SetDlgItemText(IDC_STATUS_TEXT, L"批量线程数必须是整数，0 表示自动选择");
        return;
    }
    if (!readConfig(config, &diagnosis)) {
        SetDlgItemText(IDC_STATUS_TEXT, diagnosis);
        return;
    }
    config.useRoi = useRoi_;
    config.roi = roi_;
    const std::string path(CT2A(directory.GetString(), CP_ACP));
    const unsigned generation = ++batchGeneration_;
    batchCancelRequested_ = false;
    batchRunning_ = true;
    setBatchControlsEnabled(false);
    setSingleControlsEnabled(false);
    SetDlgItemText(IDC_STATUS_TEXT, L"正在后台准备并执行批量评估");
    try {
        batchWorker_ = std::thread([this, path, count, threads, config, generation]() {
            BatchCompletion* completion = new BatchCompletion;
            completion->generation = generation;
            try {
                completion->review = std::make_shared<BatchReviewData>();
                completion->review->pairs = findThickPairs(path);
                PostMessage(WM_LOCATOR_DIRECTORY_SCANNED, 0,
                            static_cast<LPARAM>(completion->review->pairs.size()));
                const int actualCount = count == 0 ?
                    static_cast<int>(completion->review->pairs.size()) : count;
                completion->review->data = makeBenchmarkTasks(completion->review->pairs, actualCount);
                for (BatchTask& task : completion->review->data.tasks) {
                    task.config = config;
                }
                completion->run = LocatorWorkflow::runBatchControlled(completion->review->data.tasks, threads,
                    [this](int completed, int total) {
                        BatchProgressData* progress = new BatchProgressData{ completed, total };
                        if (!PostMessage(WM_LOCATOR_BATCH_PROGRESS, 0,
                                         reinterpret_cast<LPARAM>(progress))) {
                            delete progress;
                        }
                    },
                    [this]() { return batchCancelRequested_.load(); });
            } catch (const std::exception& error) {
                completion->run.diagnostic = error.what();
                completion->run.total = count;
            } catch (...) {
                completion->run.diagnostic = "未知异常";
                completion->run.total = count;
            }
            if (!PostMessage(WM_LOCATOR_BATCH_COMPLETE, 0,
                             reinterpret_cast<LPARAM>(completion))) {
                delete completion;
            }
        });
    } catch (const std::exception& error) {
        batchRunning_ = false;
        setBatchControlsEnabled(true);
        setSingleControlsEnabled(true);
        const CString message(CA2W(error.what(), CP_UTF8));
        SetDlgItemText(IDC_STATUS_TEXT, L"无法启动批量评估：" + message);
    }
}

LRESULT CLocatorMfcDlg::OnBatchProgress(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    const BatchProgressData* progress = reinterpret_cast<const BatchProgressData*>(lParam);
    if (progress != nullptr && batchRunning_) {
        CString text;
        text.Format(L"已完成 %d / %d", progress->completed, progress->total);
        SetDlgItemText(IDC_BATCH_PROGRESS, text);
    }
    delete progress;
    return 0;
}

LRESULT CLocatorMfcDlg::OnBatchComplete(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    BatchCompletion* completion = reinterpret_cast<BatchCompletion*>(lParam);
    if (completion == nullptr) {
        return 0;
    }
    const bool current = completion->generation == batchGeneration_;
    batchRunning_ = false;
    if (batchWorker_.joinable()) {
        batchWorker_.join();
    }
    setBatchControlsEnabled(true);
    setSingleControlsEnabled(true);
    if (current) {
        if (!completion->run.diagnostic.empty()) {
            clearBatchResultDisplay();
            const CString message(CA2W(completion->run.diagnostic.c_str(), CP_UTF8));
            SetDlgItemText(IDC_STATUS_TEXT, L"批量评估失败：" + message);
        } else {
            batchReviewData_ = completion->review;
            showBatchResult(completion->run);
        }
    }
    delete completion;
    return 0;
}

void CLocatorMfcDlg::clearResultDisplay()
{
    resultsList_.DeleteAllItems();
    searchView_.setResults({}, modelImage_.cols, modelImage_.rows);
    updateExecutionHint();
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
    const int removedControlIds[] = {
        1112, 1113, 1114, 1115, 1116, 1117, 1118, 1119, 1120, 1125, 1126, 1127
    };
    for (int id : removedControlIds) {
        require(GetDlgItem(id) == nullptr,
                ("不应保留已删除的 ROI 或批量辅助控件，ID=" + std::to_string(id)).c_str());
    }
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
    searchView_.resetView();
    CPoint wheelPoint = searchView_.imageToClient(58.0, 58.0);
    searchView_.ClientToScreen(&wheelPoint);
    searchView_.SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(0, 120),
                            MAKELPARAM(wheelPoint.x, wheelPoint.y));
    require(searchView_.zoom() > 1.0, "滚轮没有放大搜索图");
    const auto selectKnownRoi = [this, &require] {
        searchView_.clearSelection();
        searchView_.setSelectionEnabled(true);
        const CPoint first = searchView_.imageToClient(16.0, 16.0);
        const CPoint second = searchView_.imageToClient(47.0, 47.0);
        searchView_.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(first.x, first.y));
        searchView_.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(second.x, second.y));
        searchView_.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(second.x, second.y));
        const cv::Rect actual = searchView_.selectedRect();
        const CRect display = searchView_.displayRect();
        const std::string detail = "缩放=" + std::to_string(searchView_.zoom()) +
            "，位图=" + std::to_string(display.left) + "," + std::to_string(display.top) + "," +
            std::to_string(display.Width()) + "," + std::to_string(display.Height()) +
            "，反算=" + std::to_string((second.x - display.left) * 118.0 / display.Width()) +
            "，点=" + std::to_string(first.x) + "," + std::to_string(first.y) + " / " +
            std::to_string(second.x) + "," + std::to_string(second.y) +
            "，选区=" + std::to_string(actual.x) + "," + std::to_string(actual.y) + "," +
            std::to_string(actual.width) + "," + std::to_string(actual.height);
        require(actual == cv::Rect(16, 16, 32, 32),
                (std::string("缩放或平移后的 ROI 原图坐标不一致，") + detail).c_str());
    };
    selectKnownRoi();
    CPoint panStart = searchView_.imageToClient(58.0, 58.0);
    searchView_.SendMessage(WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(panStart.x, panStart.y));
    searchView_.SendMessage(WM_MOUSEMOVE, MK_MBUTTON,
                            MAKELPARAM(panStart.x + 20, panStart.y + 10));
    searchView_.SendMessage(WM_MBUTTONUP, 0, MAKELPARAM(panStart.x + 20, panStart.y + 10));
    selectKnownRoi();
    SendMessage(WM_IMAGE_CURSOR_POSITION, 0, 0);
    CString cursorHint;
    GetDlgItemText(IDC_IMAGE_META_SEARCH, cursorHint);
    require(cursorHint.Find(L"x=") >= 0 && cursorHint.Find(L"%") >= 0,
            "搜索图缺少原图坐标和缩放提示");
    searchView_.resetView();
    searchView_.clearSelection();
    searchView_.setSelectionEnabled(false);
    ShowWindow(SW_HIDE);

    const auto controlRect = [this](int id) {
        CRect rect;
        GetDlgItem(id)->GetWindowRect(&rect);
        ScreenToClient(&rect);
        if (id == IDC_SUBPIXEL) {
            COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
            if (::GetComboBoxInfo(GetDlgItem(id)->GetSafeHwnd(), &info)) {
                rect.bottom = rect.top + info.rcItem.bottom + 4;
            }
        }
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
    SetWindowSubclass(GetDlgItem(IDC_ANGLE_TITLE)->GetSafeHwnd(), countControlPaint, 1, 0);
    SetWindowSubclass(GetDlgItem(IDC_SEARCH_VIEW)->GetSafeHwnd(), countControlPaint, 1, 0);
    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    groupPaintCount = 0;
    imagePaintCount = 0;
    applyLayout();
    require(groupPaintCount > 0 && imagePaintCount > 0,
            (std::string("布局后分组框和图像控件没有统一重绘，次数=") +
             std::to_string(groupPaintCount) + "/" + std::to_string(imagePaintCount)).c_str());
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
            controlRect(IDC_RESULT_OPTIONS_TITLE).bottom <= MulDiv(120, dpiY_, 96),
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
    const std::pair<int, std::vector<int>> sectionGroups[] = {
        { IDC_ANGLE_TITLE, { IDC_ANGLE_HINT, IDC_ANGLE_MIN_LABEL, IDC_ANGLE_MAX_LABEL,
            IDC_ANGLE_STEP_LABEL, IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP } },
        { IDC_SCALE_TITLE, { IDC_SCALE_HINT, IDC_SCALE_MIN_LABEL, IDC_SCALE_MAX_LABEL,
            IDC_SCALE_STEP_LABEL, IDC_SCALE_MIN, IDC_SCALE_MAX, IDC_SCALE_STEP } },
        { IDC_RESULT_OPTIONS_TITLE, { IDC_PRECISION_HINT, IDC_PYRAMID_LABEL,
            IDC_SUBPIXEL_LABEL, IDC_MIN_SCORE_LABEL, IDC_PYRAMID, IDC_SUBPIXEL,
            IDC_MIN_SCORE, IDC_MAX_RESULTS_LABEL, IDC_X_VICINITY_LABEL,
            IDC_Y_VICINITY_LABEL, IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY } },
        { IDC_BATCH_DIRECTORY_TITLE, { IDC_BATCH_DIRECTORY_LABEL, IDC_BATCH_PATH, IDC_BATCH_BROWSE } },
        { IDC_BATCH_SETTINGS_TITLE, { IDC_BATCH_SETTINGS_HINT, IDC_BATCH_COUNT_LABEL,
            IDC_BATCH_THREADS_LABEL, IDC_BATCH_COUNT, IDC_BATCH_THREADS,
            IDC_BATCH_CONFIG_HINT, IDC_BATCH_RUN, IDC_BATCH_CANCEL } },
        { IDC_BATCH_STATS_TITLE, { IDC_BATCH_PROGRESS, IDC_BATCH_FILTER, IDC_BATCH_STATS } }
    };
    const auto validateSectionGroups = [this, &require, &controlRect, &sectionGroups] {
        for (const auto& group : sectionGroups) {
            CWnd* frame = GetDlgItem(group.first);
            require(frame != nullptr && (frame->GetStyle() & BS_TYPEMASK) == BS_GROUPBOX,
                    (std::string("功能分区缺少带标题的完整边界，ID=") +
                     std::to_string(group.first)).c_str());
            const CRect bounds = controlRect(group.first);
            for (int child : group.second) {
                const CRect content = controlRect(child);
                require(bounds.PtInRect(content.TopLeft()) &&
                        bounds.PtInRect(CPoint(content.right - 1, content.bottom - 1)),
                        (std::string("控件超出所属功能分组，分组=") + std::to_string(group.first) +
                         "，控件=" + std::to_string(child)).c_str());
            }
        }
    };
    const auto validateVisibleLayout = [this, &require, &sectionGroups, &validateSectionGroups](bool allowEllipsis) {
        validateSectionGroups();
        CRect bounds;
        GetClientRect(&bounds);
        CClientDC dc(this);
        std::vector<std::pair<int, CRect>> visibleRects;
        for (CWnd* child = GetWindow(GW_CHILD); child != nullptr;
             child = child->GetNextWindow()) {
            if ((child->GetStyle() & WS_VISIBLE) == 0) {
                continue;
            }
            CRect rect;
            child->GetWindowRect(&rect);
            ScreenToClient(&rect);
            if (child->GetDlgCtrlID() == IDC_BACKEND ||
                child->GetDlgCtrlID() == IDC_SUBPIXEL ||
                child->GetDlgCtrlID() == IDC_BATCH_FILTER) {
                COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
                if (::GetComboBoxInfo(child->GetSafeHwnd(), &info)) {
                    rect.bottom = rect.top + info.rcItem.bottom + 4;
                }
            }
            require(rect.left >= bounds.left && rect.top >= bounds.top &&
                    rect.right <= bounds.right && rect.bottom <= bounds.bottom,
                    "可见控件超出对话框边界");
            const int id = child->GetDlgCtrlID();
            for (const auto& previous : visibleRects) {
                bool grouped = false;
                for (const auto& group : sectionGroups) {
                    if (previous.first == group.first &&
                        previous.second.PtInRect(rect.TopLeft()) &&
                        previous.second.PtInRect(CPoint(rect.right - 1, rect.bottom - 1))) {
                        grouped = true;
                        break;
                    }
                }
                CRect overlap;
                require(grouped || !overlap.IntersectRect(rect, previous.second),
                        (std::string("可见控件之间存在重叠，ID=") +
                         std::to_string(id) + "，矩形=" +
                         std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
                         std::to_string(rect.right) + "," + std::to_string(rect.bottom) + "，已有=" +
                         std::to_string(previous.second.left) + "," + std::to_string(previous.second.top) + "," +
                         std::to_string(previous.second.right) + "," + std::to_string(previous.second.bottom)).c_str());
            }
            visibleRects.push_back({ id, rect });
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
                    (std::string("说明或按钮文字被裁切，ID=") + std::to_string(id) +
                     "，文字宽=" + std::to_string(textWidth) + "，可用宽=" +
                     std::to_string(rect.Width() - 6) + "，文字高=" + std::to_string(textHeight) +
                     "，可用高=" + std::to_string(rect.Height())).c_str());
        }
    };
    validateVisibleLayout(false);
    CRect listClient;
    resultsList_.GetClientRect(&listClient);
    int columnWidth = 0;
    for (int column = 0; column < resultsList_.GetHeaderCtrl()->GetItemCount(); ++column) {
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
    showInspectorPage(0);
    CString executionSummary;
    GetDlgItemText(IDC_EXECUTION_HINT, executionSummary);
    require(executionSummary.Find(L"-2") >= 0 && executionSummary.Find(L"1") >= 0 &&
            executionSummary.Find(L"0.9") >= 0 && executionSummary.Find(L"1.05") >= 0 &&
            executionSummary.Find(L"10") >= 0 && executionSummary.Find(L"100") >= 0,
            "执行摘要没有反映实际角度、尺度和 ROI");
    const auto dragRoi = [this](const CPoint& first, const CPoint& second) {
        searchView_.setSelectionEnabled(true);
        searchView_.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(first.x, first.y));
        searchView_.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(second.x, second.y));
        searchView_.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(second.x, second.y));
        SendMessage(WM_IMAGE_SELECTION_CHANGED, 0, 0);
    };
    dragRoi(searchView_.imageToClient(12.5, 14.5), searchView_.imageToClient(51.5, 43.5));
    const std::string mouseRoiDetail = "实际=" + std::to_string(roi_.x) + "," +
        std::to_string(roi_.y) + "," + std::to_string(roi_.width) + "," + std::to_string(roi_.height);
    require(useRoi_ && roi_.x == 12 && roi_.y == 14 && roi_.width == 40 && roi_.height == 30 &&
            searchView_.selectedRect() == cv::Rect(12, 14, 40, 30),
            ("鼠标 ROI、红框和核心配置没有同步，" + mouseRoiDetail).c_str());
    OnClearRoi();
    require(!useRoi_ && searchView_.selectedRect().empty(), "清除 ROI 后红框或配置未清空");
    dragRoi(searchView_.imageToClient(10.5, 10.5), searchView_.imageToClient(109.5, 109.5));
    require(useRoi_ && searchView_.selectedRect() == cv::Rect(10, 10, 100, 100),
            "重新鼠标框选后 ROI 不正确");
    showInspectorPage(1);
    CString batchConfigHint;
    GetDlgItemText(IDC_BATCH_CONFIG_HINT, batchConfigHint);
    require(batchConfigHint.Find(L"金字塔=2") >= 0 && batchConfigHint.Find(L"亚像素=1/4") >= 0 &&
            batchConfigHint.Find(L"最低分=85") >= 0 && batchConfigHint.Find(L"Top-N=2") >= 0 &&
            batchConfigHint.Find(L"去重=(2,3)") >= 0 &&
            batchConfigHint.Find(L"ROI=(10,10,100,100)") >= 0,
            "批量页没有完整显示实际生效配置");
    showInspectorPage(0);

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
    require(!GetDlgItem(IDC_EXECUTE)->IsWindowEnabled() &&
            !GetDlgItem(IDC_LOAD_SEARCH)->IsWindowEnabled() &&
            !GetDlgItem(IDC_RESET)->IsWindowEnabled(),
            "单次搜索期间未限制重复搜索、加载和复位");
    showInspectorPage(1);
    showInspectorPage(0);
    const auto waitSingle = [this, &require] {
        const DWORD waitStart = GetTickCount();
        while (!GetDlgItem(IDC_EXECUTE)->IsWindowEnabled()) {
            require(GetTickCount() - waitStart < 30000, "等待单次后台搜索超时");
            MSG message;
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
            Sleep(1);
        }
    };
    waitSingle();
    require(resultsList_.GetItemCount() == static_cast<int>(expected.matches.size()) &&
            searchView_.hasResults(), "对话框单次结果数量或叠加数据与核心不一致");
    for (int field = 0; field < 5; ++field) {
        const CString actual = resultsList_.GetItemText(0, field + 1);
        if (actual != expectedFields[field]) {
            throw std::runtime_error("对话框单次结果字段 " + std::to_string(field) +
                                     " 实际为：" + std::string(CT2A(actual.GetString(), CP_UTF8)));
        }
    }

    LocatorWorkflow::SingleResult multiple;
    multiple.error = LocatorError::Ok;
    multiple.matches = expected.matches;
    multiple.matches.push_back(expected.matches[0]);
    multiple.matches[1].x += 2.0;
    multiple.matches[1].score -= 0.01;
    showSingleResult(multiple);
    require(resultsList_.GetHeaderCtrl()->GetItemCount() == 6 &&
            resultsList_.GetItemText(0, 0) == L"1" && resultsList_.GetItemText(1, 0) == L"2",
            "多目标结果缺少序号列");
    resultsList_.SetItemState(1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    require(searchView_.selectedResult() == 1,
            "列表第二行没有高亮对应的图像结果");
    ShowWindow(SW_SHOWNOACTIVATE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    const std::vector<CRect> labelRects = searchView_.resultLabelRects();
    require(labelRects.size() == 2, "多目标标签数量不正确");
    CRect imageClient;
    searchView_.GetClientRect(&imageClient);
    CRect labelOverlap;
    require(labelRects[0].Width() > 0 && labelRects[1].Width() > 0 &&
            labelRects[0].left >= imageClient.left && labelRects[0].right <= imageClient.right &&
            labelRects[1].left >= imageClient.left && labelRects[1].right <= imageClient.right &&
            labelRects[0].top >= imageClient.top && labelRects[0].bottom <= imageClient.bottom &&
            labelRects[1].top >= imageClient.top && labelRects[1].bottom <= imageClient.bottom &&
            !labelOverlap.IntersectRect(labelRects[0], labelRects[1]),
            "多目标标签重叠或超出图像控件");
    if (previewPath != nullptr && *previewPath != L'\0') {
        const std::wstring path = std::wstring(previewPath) + L".multiple.png";
        savePreview(path.c_str());
    }
    ShowWindow(SW_HIDE);
    OnExecute();
    waitSingle();

    SetDlgItemText(IDC_ANGLE_MIN, L"非法值");
    OnExecute();
    require(resultsList_.GetItemCount() == 0 && !searchView_.hasResults(),
            "参数格式错误后仍显示上一次成功结果");
    SetDlgItemText(IDC_ANGLE_MIN, L"-2");
    roi_ = { 0, 0, 1, 1 };
    useRoi_ = true;
    OnExecute();
    waitSingle();
    require(resultsList_.GetItemCount() == 0 && !searchView_.hasResults(),
            "核心执行失败后仍显示上一次成功结果");
    const LocatorWorkflow::SingleResult fault = LocatorWorkflow::protectSingle(
        []() -> LocatorWorkflow::SingleResult { throw std::runtime_error("界面异常诊断探针"); });
    showSingleResult(fault);
    CString faultStatus;
    GetDlgItemText(IDC_STATUS_TEXT, faultStatus);
    require(faultStatus.Find(L"界面异常诊断探针") >= 0 && resultsList_.GetItemCount() == 0,
            "界面未显示单次异常诊断或未清除结果");
    roi_ = { 10, 10, 100, 100 };
    OnExecute();
    waitSingle();
    require(resultsList_.GetItemCount() == static_cast<int>(expected.matches.size()),
            "恢复有效配置后不能重新执行搜索");
    const struct { int id; const wchar_t* value; const wchar_t* valid; const wchar_t* hint; } invalidCases[] = {
        { IDC_ANGLE_STEP, L"0", L"1", L"角度步进" },
        { IDC_ANGLE_MIN, L"3", L"-2", L"最小角度" },
        { IDC_SCALE_STEP, L"0", L"0.05", L"尺度步进" },
        { IDC_SCALE_MIN, L"0", L"0.9", L"最小尺度" },
        { IDC_SCALE_MAX, L"0.8", L"1.05", L"最大尺度" },
        { IDC_PYRAMID, L"0", L"2", L"金字塔" },
        { IDC_MIN_SCORE, L"101", L"85", L"最低分数" },
        { IDC_MAX_RESULTS, L"0", L"2", L"Top-N" },
        { IDC_X_VICINITY, L"-1", L"2", L"x 去重" },
        { IDC_Y_VICINITY, L"-1", L"3", L"y 去重" }
    };
    for (const auto& test : invalidCases) {
        SetDlgItemText(test.id, test.value);
        OnExecute();
        CString diagnosis;
        GetDlgItemText(IDC_STATUS_TEXT, diagnosis);
        require(diagnosis.Find(test.hint) >= 0 && diagnosis.Find(L"必须") >= 0 &&
                GetFocus() != nullptr && GetFocus()->GetDlgCtrlID() == test.id,
                (std::string("参数错误未定位字段或说明有效范围，ID=") + std::to_string(test.id)).c_str());
        SetDlgItemText(test.id, test.valid);
    }
    OnExecute();
    waitSingle();

    CRect originalWindow;
    GetWindowRect(&originalWindow);
    {
        SetWindowPos(nullptr, 0, 0, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(SW_SHOWNOACTIVATE);
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        const auto captureClient = [this, &require](CImage& capture) {
            CRect bounds;
            GetClientRect(&bounds);
            if (!capture.IsNull()) {
                capture.Destroy();
            }
            const HRESULT created = capture.Create(bounds.Width(), bounds.Height(), 32);
            require(SUCCEEDED(created),
                    (std::string("无法创建连续缩放像素检查位图，宽高=") +
                     std::to_string(bounds.Width()) + "," + std::to_string(bounds.Height()) +
                     "，结果=" + std::to_string(static_cast<long>(created))).c_str());
            CDC* source = GetDC();
            CDC* target = CDC::FromHandle(capture.GetDC());
            const BOOL copied = target->BitBlt(0, 0, bounds.Width(), bounds.Height(), source, 0, 0, SRCCOPY);
            capture.ReleaseDC();
            ReleaseDC(source);
            require(copied != FALSE, "无法读取连续缩放后的实际窗口像素");
        };
        const auto requireWhite = [&require](const CImage& capture, const CRect& area, const char* name) {
            for (int y = area.top; y < area.bottom; ++y) {
                for (int x = area.left; x < area.right; ++x) {
                    require(capture.GetPixel(x, y) == RGB(255, 255, 255),
                            (std::string("连续缩放后残留旧像素，区域=") + name +
                             "，坐标=" + std::to_string(x) + "," + std::to_string(y) +
                             "，分组绘制/擦除=" + std::to_string(groupPaintCount) + "/" +
                             std::to_string(groupEraseCount)).c_str());
                }
            }
        };
        CImage resized;
        groupPaintCount = 0;
        groupEraseCount = 0;
        for (int step = 0; step < 12; ++step) {
            SetWindowPos(nullptr, 0, 0, originalWindow.Width() + 240 - step * 18,
                         originalWindow.Height() + 120 - step * 8,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        captureClient(resized);
        if (previewPath != nullptr && *previewPath != L'\0') {
            resized.Save((std::wstring(previewPath) + L".resize-pixels.png").c_str());
        }
        const CRect angle = controlRect(IDC_ANGLE_TITLE);
        const CRect angleEdit = controlRect(IDC_ANGLE_MIN);
        requireWhite(resized, CRect(angle.left + 8, angleEdit.bottom + 4,
                                    angle.right - 8, angle.bottom - 8), "角度分组下部");
        const CRect stepEdit = controlRect(IDC_ANGLE_STEP);
        requireWhite(resized, CRect(stepEdit.left + 8, stepEdit.bottom - 5,
                                    stepEdit.right - 8, stepEdit.bottom - 3), "角度步进输入框底部");
        showInspectorPage(1);
        for (int step = 0; step < 12; ++step) {
            SetWindowPos(nullptr, 0, 0, originalWindow.Width() - step * 8,
                         originalWindow.Height() + 72 - step * 6,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        captureClient(resized);
        if (previewPath != nullptr && *previewPath != L'\0') {
            resized.Save((std::wstring(previewPath) + L".batch-resize-pixels.png").c_str());
        }
        const CRect settings = controlRect(IDC_BATCH_SETTINGS_TITLE);
        const CRect settingsHint = controlRect(IDC_BATCH_SETTINGS_HINT);
        requireWhite(resized, CRect(settings.right - 24, settingsHint.top + 2,
                                    settings.right - 8, settingsHint.bottom - 2), "批量设置说明右侧");
        const CRect batchEdit = controlRect(IDC_BATCH_THREADS);
        requireWhite(resized, CRect(batchEdit.left + 8, batchEdit.bottom - 5,
                                    batchEdit.right - 8, batchEdit.bottom - 3), "批量线程输入框底部");
        std::string interactionFailures;
        const auto checkInteraction = [&interactionFailures](bool condition, const std::string& message) {
            if (!condition && interactionFailures.size() < 3000) {
                interactionFailures += message + "\n";
            }
        };
        const auto checkEditFrames = [this, &controlRect, &checkInteraction](
            const CImage& capture, const std::vector<int>& ids) {
            const auto darkPixel = [&capture](int x, int y) {
                const COLORREF pixel = capture.GetPixel(x, y);
                return GetRValue(pixel) < 220 && GetGValue(pixel) < 220 && GetBValue(pixel) < 220;
            };
            for (int id : ids) {
                const CRect rect = controlRect(id);
                bool edges[4] = {};
                for (int x = rect.left + 1; x < rect.right - 1; ++x) {
                    edges[0] = edges[0] || darkPixel(x, rect.top);
                    edges[1] = edges[1] || darkPixel(x, rect.bottom - 1);
                }
                for (int y = rect.top + 1; y < rect.bottom - 1; ++y) {
                    edges[2] = edges[2] || darkPixel(rect.left, y);
                    edges[3] = edges[3] || darkPixel(rect.right - 1, y);
                }
                checkInteraction(edges[0] && edges[1] && edges[2] && edges[3],
                    "输入框四边没有完整显示，ID=" + std::to_string(id) +
                    "，上下左右=" + std::to_string(edges[0]) + std::to_string(edges[1]) +
                    std::to_string(edges[2]) + std::to_string(edges[3]) +
                    "，矩形=" + std::to_string(rect.left) + "," + std::to_string(rect.top) +
                    "," + std::to_string(rect.right) + "," + std::to_string(rect.bottom));
            }
        };
        checkEditFrames(resized, { IDC_BATCH_PATH, IDC_BATCH_COUNT, IDC_BATCH_THREADS });
        showInspectorPage(0);
        RedrawWindow(nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
        captureClient(resized);
        checkEditFrames(resized, { IDC_ANGLE_MIN, IDC_ANGLE_MAX, IDC_ANGLE_STEP,
            IDC_SCALE_MIN, IDC_SCALE_MAX, IDC_SCALE_STEP, IDC_PYRAMID, IDC_MIN_SCORE,
            IDC_MAX_RESULTS, IDC_X_VICINITY, IDC_Y_VICINITY });
        CRect dropped;
        const LRESULT droppedMeasured = subpixelCombo_.SendMessage(
            CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped));
        const int itemHeight = static_cast<int>(subpixelCombo_.SendMessage(CB_GETITEMHEIGHT, 0, 0));
        checkInteraction(droppedMeasured != 0 && dropped.Height() >= itemHeight * 4,
            "亚像素下拉区域不足四项，实际高=" + std::to_string(dropped.Height()) +
            "，单项高=" + std::to_string(itemHeight));
        subpixelCombo_.SendMessage(CB_SHOWDROPDOWN, TRUE, 0);
        checkInteraction(subpixelCombo_.SendMessage(CB_GETDROPPEDSTATE, 0, 0) != FALSE,
                         "亚像素下拉菜单不能进入展开状态");
        COMBOBOXINFO popup = { sizeof(COMBOBOXINFO) };
        require(::GetComboBoxInfo(subpixelCombo_.GetSafeHwnd(), &popup) != FALSE,
                "无法读取亚像素原生弹出列表");
        CRect popupRect;
        ::GetWindowRect(popup.hwndList, &popupRect);
        checkInteraction(popup.hwndList != nullptr && ::IsWindowVisible(popup.hwndList) &&
                         popupRect.Height() >= itemHeight * 4,
                         "亚像素原生弹出列表不可见或高度不足");
        if (previewPath != nullptr && *previewPath != L'\0') {
            CImage popupCapture;
            require(SUCCEEDED(popupCapture.Create(popupRect.Width(), popupRect.Height(), 32)),
                    "无法创建亚像素弹出列表截图");
            HDC source = ::GetDC(popup.hwndList);
            CDC* target = CDC::FromHandle(popupCapture.GetDC());
            target->BitBlt(0, 0, popupRect.Width(), popupRect.Height(),
                           CDC::FromHandle(source), 0, 0, SRCCOPY);
            popupCapture.ReleaseDC();
            ::ReleaseDC(popup.hwndList, source);
            popupCapture.Save((std::wstring(previewPath) + L".dropdown.png").c_str());
        }
        subpixelCombo_.SendMessage(CB_SHOWDROPDOWN, FALSE, 0);
        const auto checkImageClip = [&controlRect, &checkInteraction](
            const CImage& capture, int id, const char* name) {
            const CRect view = controlRect(id);
            for (int x = view.left + 4; x < view.right - 4; ++x) {
                for (int offset = 1; offset <= 3; ++offset) {
                    checkInteraction(capture.GetPixel(x, view.top - offset) == RGB(255, 255, 255) &&
                                     capture.GetPixel(x, view.bottom - 1 + offset) == RGB(255, 255, 255),
                                     std::string(name) + "越出上下边界");
                }
            }
            for (int y = view.top + 4; y < view.bottom - 4; ++y) {
                for (int offset = 1; offset <= 3; ++offset) {
                    checkInteraction(capture.GetPixel(view.left - offset, y) == RGB(255, 255, 255) &&
                                     capture.GetPixel(view.right - 1 + offset, y) == RGB(255, 255, 255),
                                     std::string(name) + "越出左右边界");
                }
            }
        };
        LocatorResult zoomResult = {};
        zoomResult.x = 58.0;
        zoomResult.y = 58.0;
        zoomResult.scale = 1.0;
        zoomResult.score = 100.0;
        searchView_.setResults({ zoomResult }, modelImage_.cols, modelImage_.rows);
        for (CImageViewControl* view : { &searchView_, &templateView_ }) {
            CRect viewClient;
            view->GetClientRect(&viewClient);
            CPoint center(viewClient.Width() / 2, viewClient.Height() / 2);
            view->ClientToScreen(&center);
            view->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(0, 120),
                              MAKELPARAM(center.x, center.y));
        }
        RedrawWindow(nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
        captureClient(resized);
        if (previewPath != nullptr && *previewPath != L'\0') {
            resized.Save((std::wstring(previewPath) + L".zoom-clipping.png").c_str());
        }
        checkImageClip(resized, IDC_SEARCH_VIEW, "搜索图及叠加");
        checkImageClip(resized, IDC_TEMPLATE_VIEW, "模板");
        searchView_.resetView();
        templateView_.resetView();
        require(interactionFailures.empty(), interactionFailures.c_str());
        SetWindowPos(nullptr, 0, 0, originalWindow.Width(), originalWindow.Height(),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(SW_HIDE);
    }
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
    for (int column = 0; column < resultsList_.GetHeaderCtrl()->GetItemCount(); ++column) {
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
    require(!GetDlgItem(IDC_BATCH_RUN)->IsWindowEnabled() &&
            GetDlgItem(IDC_BATCH_CANCEL)->IsWindowEnabled(),
            "批量执行期间未限制重复运行或未提供取消");
    showInspectorPage(0);
    showInspectorPage(1);
    const auto waitBatch = [this, &require] {
        const DWORD waitStart = GetTickCount();
        while (!GetDlgItem(IDC_BATCH_RUN)->IsWindowEnabled()) {
            require(GetTickCount() - waitStart < 120000, "等待批量后台执行超时");
            MSG message;
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
            Sleep(1);
        }
    };
    waitBatch();
    require(batchStats_.GetItemText(0, 1) == L"197 / 197" &&
            batchStats_.GetItemText(1, 1) == L"197 / 0 / 0",
            "对话框批量三态统计不正确");
    require(batchPairCount_ == 197 && batchStats_.GetItemCount() == 7,
            "批量目录扫描没有记录实际配对数");
    CString directoryHint;
    GetDlgItemText(IDC_BATCH_SETTINGS_HINT, directoryHint);
    require(directoryHint.Find(L"197") >= 0, "批量目录说明未显示实际配对数");
    CComboBox* filter = static_cast<CComboBox*>(GetDlgItem(IDC_BATCH_FILTER));
    int successRows = 0;
    for (const BatchResult& result : batchResultData_) {
        if (result.error == LocatorError::Ok && !result.matches.empty()) {
            successRows += static_cast<int>(result.matches.size());
        }
    }
    require(filter != nullptr && resultsList_.GetItemCount() == successRows,
            (std::string("批量全部结果数量错误，实际=") +
             std::to_string(resultsList_.GetItemCount()) + "，缓存=" +
             std::to_string(batchResultData_.size()) + "，预期行=" +
             std::to_string(successRows)).c_str());
    const int filterCounts[] = { successRows, successRows, 0, 0 };
    for (int index = 0; index < 4; ++index) {
        filter->SetCurSel(index);
        SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BATCH_FILTER, CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(filter->GetSafeHwnd()));
        require(resultsList_.GetItemCount() == filterCounts[index],
                "批量结果筛选与实际分类不一致");
    }
    filter->SetCurSel(0);
    SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BATCH_FILTER, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(filter->GetSafeHwnd()));
    require(resultsList_.GetItemCount() == successRows, "恢复全部筛选后结果数量错误");
    wchar_t absoluteDirectory[MAX_PATH] = {};
    require(GetFullPathNameW(CA2W(directory.c_str(), CP_ACP), MAX_PATH,
                             absoluteDirectory, nullptr) > 0,
            "无法生成批量联动测试的绝对预期路径");
    const std::vector<ThickPair> reviewPairs = findThickPairs(
        std::string(CW2A(absoluteDirectory, CP_ACP)));
    const auto findBatchRow = [this](int task, int match) {
        for (int row = 0; row < resultsList_.GetItemCount(); ++row) {
            if (resultsList_.GetItemText(row, 0) == CString(std::to_wstring(task).c_str()) &&
                resultsList_.GetItemText(row, 2) == CString(std::to_wstring(match).c_str())) {
                return row;
            }
        }
        return -1;
    };
    const auto selectBatchRow = [this, &require](int row) {
        require(row >= 0, "批量测试缺少指定任务和匹配序号的结果行");
        resultsList_.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        require((resultsList_.GetItemState(row, LVIS_SELECTED) & LVIS_SELECTED) != 0,
                "实际列表控件没有选中批量结果行");
    };
    selectBatchRow(findBatchRow(0, 1));
    require(searchView_.hasResults() && searchView_.selectedResult() == 0,
            "点击批量成功行后没有显示对应匹配框");
    selectBatchRow(findBatchRow(1, 1));
    cv::Mat expectedReviewSearch;
    cv::Mat expectedReviewModel;
    require(loadGrayImage(reviewPairs[1].searchPath, expectedReviewSearch) &&
            loadGrayImage(reviewPairs[1].kernelPath, expectedReviewModel),
            "无法读取批量联动预期图片");
    CString reviewedSearchPath;
    CString reviewedModelPath;
    GetDlgItemText(IDC_SEARCH_PATH, reviewedSearchPath);
    GetDlgItemText(IDC_TEMPLATE_PATH, reviewedModelPath);
    require(searchImage_.size() == expectedReviewSearch.size() &&
            modelImage_.size() == expectedReviewModel.size() &&
            cv::countNonZero(searchImage_ != expectedReviewSearch) == 0 &&
            cv::countNonZero(modelImage_ != expectedReviewModel) == 0 &&
            reviewedSearchPath == CString(CA2W(reviewPairs[1].searchPath.c_str(), CP_ACP)) &&
            reviewedModelPath == CString(CA2W(reviewPairs[1].kernelPath.c_str(), CP_ACP)) &&
            searchView_.hasResults() && searchView_.selectedResult() == 0 &&
            searchView_.selectedRect() == cv::Rect(10, 10, 100, 100),
            "批量行没有恢复对应图片、路径、ROI 或匹配项");
    int multipleRow = -1;
    for (int row = 0; row < resultsList_.GetItemCount(); ++row) {
        if (resultsList_.GetItemText(row, 2) == L"2") {
            multipleRow = row;
            break;
        }
    }
    selectBatchRow(multipleRow);
    require(searchView_.selectedResult() == 1,
            "批量结果行没有高亮行内第二个匹配项");
    const std::vector<BatchResult> successfulBatch = batchResultData_;
    batchResultData_.resize(2);
    batchResultData_[0] = BatchResult();
    batchResultData_[0].id = 0;
    batchResultData_[0].error = LocatorError::Ok;
    batchResultData_[1] = BatchResult();
    batchResultData_[1].id = 1;
    batchResultData_[1].error = LocatorError::InvalidImage;
    filter->SetCurSel(2);
    SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BATCH_FILTER, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(filter->GetSafeHwnd()));
    selectBatchRow(0);
    GetDlgItemText(IDC_SEARCH_PATH, reviewedSearchPath);
    require(!searchView_.hasResults() && searchView_.selectedResult() == -1 &&
            reviewedSearchPath == CString(CA2W(reviewPairs[0].searchPath.c_str(), CP_ACP)) &&
            cv::countNonZero(searchImage_ != search) == 0,
            "无匹配批量行没有切换图片或仍保留旧匹配框");
    if (previewPath != nullptr && *previewPath != L'\0') {
        const std::wstring path = std::wstring(previewPath) + L".batch-no-match.png";
        savePreview(path.c_str());
    }
    filter->SetCurSel(3);
    SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BATCH_FILTER, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(filter->GetSafeHwnd()));
    selectBatchRow(0);
    require(!searchView_.hasResults() &&
            cv::countNonZero(searchImage_ != expectedReviewSearch) == 0,
            "错误批量行没有显示对应图片或仍保留旧匹配框");
    batchResultData_ = successfulBatch;
    filter->SetCurSel(0);
    SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BATCH_FILTER, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(filter->GetSafeHwnd()));
    selectBatchRow(findBatchRow(0, 1));
    if (previewPath != nullptr && *previewPath != L'\0') {
        showInspectorPage(1);
        const std::wstring batchPreview = std::wstring(previewPath) + L".batch.png";
        savePreview(batchPreview.c_str());
        const std::wstring selectionPreview = std::wstring(previewPath) + L".batch-selection.png";
        savePreview(selectionPreview.c_str());
        showInspectorPage(0);
    }
    SetDlgItemText(IDC_BATCH_COUNT, L"500");
    OnBatchRun();
    OnBatchCancel();
    waitBatch();
    CString cancelStatus;
    GetDlgItemText(IDC_STATUS_TEXT, cancelStatus);
    require(cancelStatus.Find(L"取消") >= 0 && batchStats_.GetItemText(1, 1) == L"已取消",
            "取消批量后未显示部分完成状态");
    require(batchStats_.GetItemText(0, 1) != L"500 / 500" || resultsList_.GetItemCount() == 0,
            "取消后把未执行任务冒充为已完成");

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
