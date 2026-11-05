#include "Locator.h"
#include "NccMatcher.h"
#include "ResultRefiner.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

#include <vector>

void testNccExactLocation()
{
    cv::Mat search = makeTexturedImage(96, 80, 1234);
    cv::Mat model;
    search(cv::Rect(37, 29, 16, 12)).copyTo(model);

    cv::Mat response = NccMatcher::match(search, model);
    expect(response.type() == CV_32FC1, "NCC 响应图应为 CV_32FC1");
    expect(response.cols == 81 && response.rows == 69, "NCC 响应图尺寸错误");

    double maxValue = 0.0;
    cv::Point maxLocation;
    cv::minMaxLoc(response, nullptr, &maxValue, nullptr, &maxLocation);
    expect(maxLocation.x == 37 && maxLocation.y == 29, "NCC 最大值位置错误");
    expectNear(maxValue, 1.0, 1e-6, "完全相同区域的 NCC 应为 1");

    Locator locator;
    expect(locator.initialize(asLocatorImage(model), LocatorConfig()) == LocatorError::Ok,
           "有效模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "有效图像搜索失败");
    expect(results.size() == 1, "单目标搜索应返回一个结果");
    expectNear(results[0].x, 44.5, 1e-9, "结果 x 应为模板几何中心");
    expectNear(results[0].y, 34.5, 1e-9, "结果 y 应为模板几何中心");
    expect(results[0].angle == 0.0 && results[0].scale == 1.0,
           "固定搜索应返回角度 0、尺度 1");
    expect(results[0].score > 99.9, "精确匹配分数应大于 99.9");
}

void testNccPaddedStride()
{
    cv::Mat logical = makeTexturedImage(72, 60, 5678);
    cv::Mat padded(60, 89, CV_8UC1, cv::Scalar(0));
    logical.copyTo(padded(cv::Rect(0, 0, logical.cols, logical.rows)));

    cv::Mat model;
    logical(cv::Rect(20, 16, 12, 10)).copyTo(model);
    cv::Mat paddedModel(10, 20, CV_8UC1, cv::Scalar(0));
    model.copyTo(paddedModel(cv::Rect(0, 0, model.cols, model.rows)));

    LocatorImage searchView = asLocatorImage(padded);
    searchView.width = logical.cols;
    LocatorImage modelView = asLocatorImage(paddedModel);
    modelView.width = model.cols;
    expect(searchView.stride > searchView.width, "搜索图测试必须包含填充行");
    expect(modelView.stride > modelView.width, "模板测试必须包含填充行");

    Locator locator;
    expect(locator.initialize(modelView, LocatorConfig()) == LocatorError::Ok,
           "非连续模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(searchView, results) == LocatorError::Ok,
           "非连续搜索图执行失败");
    expect(results.size() == 1, "带行跨度搜索应返回一个结果");
    expectNear(results[0].x, 25.5, 1e-9, "带行跨度结果的 x 错误");
    expectNear(results[0].y, 20.5, 1e-9, "带行跨度结果的 y 错误");
    expect(results[0].score > 99.9, "带行跨度精确匹配分数应大于 99.9");
}

void testNccConstantInput()
{
    LocatorConfig config;
    Locator locator;
    cv::Mat constantModel(12, 12, CV_8UC1, cv::Scalar(128));
    expect(locator.initialize(asLocatorImage(constantModel), config) == LocatorError::InvalidImage,
           "常量模板应被拒绝，避免产生无效 NCC");

    cv::Mat validModel = makeTexturedImage(12, 12, 9012);
    cv::Mat zeroSearch(40, 40, CV_8UC1, cv::Scalar(0));
    expect(locator.initialize(asLocatorImage(validModel), config) == LocatorError::Ok,
           "有效模板重新初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(zeroSearch), results) == LocatorError::Ok,
           "全零搜索图不应导致执行异常");
    expect(results.empty(), "全零搜索图不应返回 NaN 或虚假匹配");
}

void testRoiRestrictsSearch()
{
    cv::Mat search = makeTexturedImage(100, 80, 3456);
    cv::Mat model;
    search(cv::Rect(5, 5, 12, 10)).copyTo(model);
    model.copyTo(search(cv::Rect(60, 40, 12, 10)));

    LocatorConfig config;
    config.useRoi = true;
    config.roi = { 50, 30, 30, 25 };
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "ROI 测试模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "有效 ROI 搜索失败");
    expect(results.size() == 1, "ROI 内精确目标应返回一个结果");
    expectNear(results[0].x, 65.5, 1e-9, "ROI 结果 x 不应来自区域外目标");
    expectNear(results[0].y, 44.5, 1e-9, "ROI 结果 y 不应来自区域外目标");
}

void testRoiInvalidRejected()
{
    cv::Mat search = makeTexturedImage(60, 50, 7890);
    cv::Mat model = makeTexturedImage(12, 10, 7891);
    const LocatorRect invalidRois[] = {
        { 0, 0, 0, 10 }, { 0, 0, 10, 0 }, { -1, 0, 10, 10 },
        { 0, -1, 10, 10 }, { 50, 0, 11, 10 }, { 0, 40, 10, 11 },
        { 0, 0, 8, 10 }, { 0, 0, 12, 9 }
    };

    for (const LocatorRect& roi : invalidRois) {
        LocatorConfig config;
        config.useRoi = true;
        config.roi = roi;
        Locator locator;
        expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
               "ROI 测试模板初始化失败");
        std::vector<LocatorResult> results;
        expect(locator.execute(asLocatorImage(search), results) == LocatorError::InvalidRoi,
               "空、越界或小于模板的 ROI 应返回 InvalidRoi");
    }

    cv::Mat oversizedModel = makeTexturedImage(70, 60, 7892);
    Locator oversizedLocator;
    expect(oversizedLocator.initialize(asLocatorImage(oversizedModel), LocatorConfig()) ==
               LocatorError::Ok, "大尺寸模板初始化失败");
    std::vector<LocatorResult> results;
    expect(oversizedLocator.execute(asLocatorImage(search), results) == LocatorError::InvalidImage,
           "模板大于搜索图应返回 InvalidImage");

    const double invalidScores[] = { -0.1, 100.1 };
    for (double score : invalidScores) {
        LocatorConfig config;
        config.minScore = score;
        Locator locator;
        expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
               "分数阈值超出 0～100 应返回 InvalidConfig");
    }

    LocatorConfig invalidCount;
    invalidCount.maxResults = 0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), invalidCount) == LocatorError::InvalidConfig,
           "maxResults 小于 1 应返回 InvalidConfig");
}

void testTopNScoreAndStableOrder()
{
    const std::vector<LocatorResult> candidates = {
        { 30, 20, 0, 1, 80 },
        { 20, 0, 0, 1, 95 },
        { 10, 30, 0, 1, 95 },
        { 10, 20, 0, 1, 95 }
    };
    LocatorConfig config;
    config.minScore = 90.0;
    config.maxResults = 3;

    const std::vector<LocatorResult> results = ResultRefiner::selectResults(candidates, config);
    expect(results.size() == 3, "阈值过滤后 Top-3 应只返回三个结果");
    expect(results[0].x == 10 && results[0].y == 20,
           "同分时应先按 x 升序、再按 y 升序排列");
    expect(results[1].x == 10 && results[1].y == 30, "第二个结果应为同 x 的较大 y");
    expect(results[2].x == 20 && results[2].y == 0, "第三个结果应为次优 x 坐标");
}

void testVicinitySuppressesDuplicates()
{
    const std::vector<LocatorResult> candidates = {
        { 50, 50, 0, 1, 99 },
        { 52, 53, 0, 1, 98 },
        { 54, 50, 0, 1, 97 },
        { 50, 55, 0, 1, 96 },
        { 53, 54, 0, 1, 95 }
    };
    LocatorConfig config;
    config.maxResults = 10;
    config.xVicinity = 3;
    config.yVicinity = 4;

    const std::vector<LocatorResult> results = ResultRefiner::selectResults(candidates, config);
    expect(results.size() == 3, "邻近去重后应保留三个独立目标");
    expect(results[0].score == 99 && results[1].score == 97 && results[2].score == 96,
           "去重应保留高分结果并维持降序");
    expect(results[1].x == 54 && results[2].x == 50, "超出任一邻近半径的目标不得误删");
}
