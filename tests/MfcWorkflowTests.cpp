#include "BatchData.h"
#include "LocatorWorkflow.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
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

void testMfcWorkflowBatchReport()
{
    LocatorWorkflow::BatchRun run;
    run.total = 3;
    run.completed = 3;
    run.totalMs = 8.0;
    run.results.resize(3);
    for (int index = 0; index < 3; ++index) {
        run.results[index].id = index;
        run.results[index].error = LocatorError::Ok;
        run.results[index].preprocessMs = 0.25;
        run.results[index].searchMs = 0.75;
        run.results[index].totalMs = index + 1.0;
    }
    run.results[0].matches.push_back(LocatorResult{ 1.25, 2.5, 3.0, 1.0, 98.75 });
    run.results[2].error = LocatorError::InvalidImage;
    const LocatorWorkflow::BatchSummary summary = LocatorWorkflow::summarizeBatch(run);
    expect(summary.matched == 1 && summary.noMatch == 1 && summary.errors == 1,
           "批量成功、无匹配和执行错误分类错误");
    expectNear(summary.totalMeanMs, 2.0, 1e-12, "批量整体均值错误");
    expectNear(summary.p99Ms, 3.0, 1e-12, "批量 P99 错误");
    expect(LocatorWorkflow::filterBatchResults(run.results, LocatorWorkflow::BatchFilter::Matched).size() == 1 &&
           LocatorWorkflow::filterBatchResults(run.results, LocatorWorkflow::BatchFilter::NoMatch).size() == 1 &&
           LocatorWorkflow::filterBatchResults(run.results, LocatorWorkflow::BatchFilter::Error).size() == 1,
           "批量结果筛选错误");

    const std::string path = "temp/t33_batch_results.csv";
    LocatorWorkflow::exportBatchCsv(path, run.results);
    std::ifstream input(path, std::ios::binary);
    const std::string csv((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    expect(csv.find("\xEF\xBB\xBF") == 0 && csv.find("0,成功,1,1.2500,2.5000,3.0000") != std::string::npos &&
           csv.find("1,无匹配") != std::string::npos &&
           csv.find("2,执行错误") != std::string::npos,
           "CSV 导出未保留任务状态、匹配字段或 UTF-8 BOM");
    std::remove(path.c_str());
}

void testMfcWorkflowFixedBenchmark()
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet data = makeBenchmarkTasks(pairs, 197);
    const LocatorWorkflow::BenchmarkRun run =
        LocatorWorkflow::runFixedBenchmark(data.tasks, 4, 2, 5);
    expect(run.warmupMs.size() == 2 && run.roundMs.size() == 5,
           "固定基准预热或记录轮数错误");
    expect(run.firstRoundMs == run.warmupMs[0] && run.taskCount == 197 && run.pairCount == 197,
           "固定基准首轮或任务组成错误");
    expect(run.finalRun.results.size() == 197, "固定基准最终结果数量错误");
    const auto range = std::minmax_element(run.roundMs.begin(), run.roundMs.end());
    expect(run.medianMs >= *range.first && run.medianMs <= *range.second,
           "固定基准中位数超出逐轮范围");
    const std::string path = "temp/t34_fixed_benchmark.csv";
    LocatorWorkflow::exportBenchmarkCsv(path, run);
    std::ifstream input(path, std::ios::binary);
    const std::string csv((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    expect(csv.find("\xEF\xBB\xBF") == 0 && csv.find("0,预热") != std::string::npos &&
           csv.find("0,记录") != std::string::npos && csv.find("4,记录") != std::string::npos,
           "固定基准未导出预热和逐轮原始耗时");
    std::remove(path.c_str());
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
