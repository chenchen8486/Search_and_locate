#include "BatchData.h"
#include "LocatorWorkflow.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

#include <atomic>
#include <stdexcept>
#include <vector>

void testMfcWorkflowSingleSearch()
{
    const cv::Mat search = makeTexturedImage(96, 80, 18001);
    cv::Mat model;
    search(cv::Rect(37, 29, 16, 12)).copyTo(model);
    const LocatorWorkflow::SingleResult result = LocatorWorkflow::runSingle(model, search, LocatorConfig());
    expect(result.error == LocatorError::Ok && result.matches.size() == 1,
           "MFC 工作流单次搜索应返回唯一结果");
    expectNear(result.matches[0].x, 44.5, 1e-9, "MFC 工作流中心 x 错误");
    expectNear(result.matches[0].y, 34.5, 1e-9, "MFC 工作流中心 y 错误");
    expect(result.preprocessMs >= 0.0 && result.searchMs >= 0.0 && result.totalMs >= 0.0,
           "MFC 工作流单次计时应为非负值");
}

void testMfcWorkflowBatchTiming()
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet data = makeBenchmarkTasks(pairs, 197);
    std::vector<BatchResult> results;
    const double wallMs = LocatorWorkflow::runBatch(data.tasks, 20, results);
    expect(results.size() == 197 && wallMs >= 0.0, "MFC 工作流应返回 197 个任务及有效计时");
    for (const BatchResult& result : results) {
        expect(result.error == LocatorError::Ok && result.matches.size() == 1,
               "MFC 工作流批量任务 " + std::to_string(result.id) + " 失败");
    }
}

void testMfcWorkflowBatchCancellation()
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet data = makeBenchmarkTasks(pairs, 197);
    std::atomic<bool> cancelled(false);
    std::atomic<int> progressCalls(0);
    const LocatorWorkflow::BatchRun run = LocatorWorkflow::runBatchControlled(
        data.tasks, 20,
        [&cancelled, &progressCalls](int completed, int total) {
            expect(total == 197 && completed > 0, "批量进度数量无效");
            ++progressCalls;
            cancelled = true;
        },
        [&cancelled]() { return cancelled.load(); });
    expect(run.cancelled && run.results.size() < 197,
           "取消后仍启动了全部批量任务");
    expect(run.completed == static_cast<int>(run.results.size()) &&
           progressCalls == run.completed,
           "取消后的进度与已完成结果数量不一致");
    for (size_t index = 0; index < run.results.size(); ++index) {
        expect(index == 0 || run.results[index - 1].id < run.results[index].id,
               "取消后的批量结果没有保持输入相对顺序");
    }
    expect(run.totalMs >= 0.0, "取消批量计时不应为负数");
}

void testMfcWorkflowSingleExceptions()
{
    for (int kind = 0; kind < 3; ++kind) {
        const LocatorWorkflow::SingleResult result = LocatorWorkflow::protectSingle([kind]() -> LocatorWorkflow::SingleResult {
            if (kind == 0) {
                throw cv::Exception(1, "模拟 OpenCV 异常", "test", __FILE__, __LINE__);
            }
            if (kind == 1) {
                throw std::runtime_error("模拟标准异常");
            }
            throw 7;
        });
        expect(result.error == LocatorError::InternalError && result.matches.empty(),
               "单次异常必须转换为 InternalError 且不得保留结果");
        expect(!result.diagnostic.empty(), "单次异常缺少诊断文本");
        if (kind == 0) {
            expect(result.diagnostic.find("OpenCV") != std::string::npos,
                   "OpenCV 异常诊断未标出来源");
        } else if (kind == 1) {
            expect(result.diagnostic.find("模拟标准异常") != std::string::npos,
                   "标准异常诊断未保留原始信息");
        } else {
            expect(result.diagnostic.find("未知") != std::string::npos,
                   "未知异常诊断不明确");
        }
    }
}
