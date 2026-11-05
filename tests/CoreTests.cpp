#include "Locator.h"
#include "NccMatcher.h"
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
