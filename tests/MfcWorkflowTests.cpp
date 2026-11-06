#include "BatchData.h"
#include "LocatorWorkflow.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

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
