#include "LocatorMfcDlg.h"

#include "BatchData.h"
#include "BatchLocator.h"
#include "LocatorWorkflow.h"
#include "resource.h"

#include <afxdlgs.h>
#include <atlconv.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
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
END_MESSAGE_MAP()

BOOL CLocatorMfcDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(L"搜索匹配定位演示与评估");

    searchView_.SubclassDlgItem(IDC_SEARCH_VIEW, this);
    templateView_.SubclassDlgItem(IDC_TEMPLATE_VIEW, this);
    resultsList_.SubclassDlgItem(IDC_RESULT_LIST, this);
    resultsList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
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
    SetDlgItemText(IDC_BATCH_STATS, L"批量结果列表仅显示失败任务");
    SetDlgItemText(IDC_SEARCH_PATH, L"未加载搜索图");
    SetDlgItemText(IDC_TEMPLATE_PATH, L"未加载模板");
    SetDlgItemText(IDC_STATUS_TEXT, L"请先加载搜索图和模板");
    return TRUE;
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
        SetDlgItemText(IDC_STATUS_TEXT, CString(L"ROI 已设置"));
    } else {
        searchImage_(selection).copyTo(modelImage_);
        templateView_.setImage(modelImage_);
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
    if (batch) {
        resultsList_.InsertColumn(0, L"任务", LVCFMT_LEFT, 80);
        resultsList_.InsertColumn(1, L"失败原因", LVCFMT_LEFT, 250);
        resultsList_.InsertColumn(2, L"预处理 ms", LVCFMT_LEFT, 120);
        resultsList_.InsertColumn(3, L"搜索 ms", LVCFMT_LEFT, 120);
        resultsList_.InsertColumn(4, L"整体 ms", LVCFMT_LEFT, 120);
    } else {
        resultsList_.InsertColumn(0, L"x", LVCFMT_LEFT, 95);
        resultsList_.InsertColumn(1, L"y", LVCFMT_LEFT, 95);
        resultsList_.InsertColumn(2, L"角度", LVCFMT_LEFT, 80);
        resultsList_.InsertColumn(3, L"尺度", LVCFMT_LEFT, 80);
        resultsList_.InsertColumn(4, L"分数", LVCFMT_LEFT, 100);
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
            L"总数 %d，成功 %d，失败 %d\r\n预处理均值 %.3f ms，搜索均值 %.3f ms\r\n单任务整体均值 %.3f ms，P99 %.3f ms\r\n批量总耗时 %.3f ms",
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
