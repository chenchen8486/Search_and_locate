#include "LocatorMfcApp.h"
#include "LocatorMfcDlg.h"

#include "BatchData.h"
#include "LocatorWorkflow.h"

#include <afxcmn.h>
#include <atlconv.h>

#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool sameMatch(const LocatorResult& actual, const LocatorResult& expected)
{
    return std::abs(actual.x - expected.x) <= 1e-9 &&
           std::abs(actual.y - expected.y) <= 1e-9 &&
           std::abs(actual.angle - expected.angle) <= 1e-9 &&
           std::abs(actual.scale - expected.scale) <= 1e-9 &&
           std::abs(actual.score - expected.score) <= 1e-4;
}

int runSelfTest(const std::string& directory)
{
    try {
        const std::vector<ThickPair> pairs = findThickPairs(directory);
        if (pairs.size() != 197) {
            std::cerr << "MFC 无界面自测要求 197 对真实数据\n";
            return 1;
        }
        BatchDataSet data = makeBenchmarkTasks(pairs, 197);
        std::vector<BatchResult> reference;
        for (int backendIndex = 0; backendIndex < 2; ++backendIndex) {
            LocatorConfig config;
            config.backend = backendIndex == 0 ? LocatorBackend::Cpu : LocatorBackend::Gpu;
            const LocatorWorkflow::SingleResult single =
                LocatorWorkflow::runSingle(data.models[0], data.searches[0], config);
            if (single.error != LocatorError::Ok || single.matches.size() != 1 ||
                std::abs(single.matches[0].x - 58.625) > 1e-9 ||
                std::abs(single.matches[0].y - 58.625) > 1e-9) {
                std::cerr << "MFC 单次工作流自测失败，后端索引 " << backendIndex << '\n';
                return 1;
            }
            for (BatchTask& task : data.tasks) {
                task.config = config;
            }
            std::vector<BatchResult> batch;
            const double wallMs = LocatorWorkflow::runBatch(data.tasks, 20, batch);
            if (batch.size() != 197) {
                std::cerr << "MFC 批量工作流数量错误\n";
                return 1;
            }
            for (size_t i = 0; i < batch.size(); ++i) {
                if (batch[i].error != LocatorError::Ok || batch[i].matches.size() != 1 ||
                    (backendIndex == 1 && !sameMatch(batch[i].matches[0], reference[i].matches[0]))) {
                    std::cerr << "MFC 批量工作流自测失败，任务 " << batch[i].id << '\n';
                    return 1;
                }
            }
            if (backendIndex == 0) {
                reference = std::move(batch);
            }
            std::cout << (backendIndex == 0 ? "CPU" : "GPU")
                      << "：单次整体 " << single.totalMs << " ms，197 对批量 "
                      << wallMs << " ms\n";
        }
        std::cout << "MFC 无界面双后端自测通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MFC 无界面自测失败：" << error.what() << '\n';
        return 1;
    }
}

} // namespace

CLocatorMfcApp theApp;

BOOL CLocatorMfcApp::InitInstance()
{
    INITCOMMONCONTROLSEX controls = { sizeof(INITCOMMONCONTROLSEX),
                                      ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    CWinApp::InitInstance();

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments != nullptr && argumentCount >= 2 &&
        std::wcscmp(arguments[1], L"--self-test") == 0) {
        int status = 2;
        if (argumentCount == 3) {
            status = runSelfTest(std::string(CW2A(arguments[2], CP_ACP)));
        } else {
            std::cerr << "用法：LocatorMfc.exe --self-test <Thick 数据目录>\n";
        }
        LocalFree(arguments);
        std::exit(status);
    }
    if (arguments != nullptr) {
        LocalFree(arguments);
    }

    CLocatorMfcDlg dialog;
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
