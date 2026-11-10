#include "Locator.h"
#include "NccMatcher.h"
#include "PyramidSearch.h"
#include "ResultRefiner.h"
#include "TransformSearch.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
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

void testNccSameSizeScore()
{
    cv::Mat model = makeTexturedImage(16, 12, 13001);
    cv::Mat padded(12, 24, CV_8UC1, cv::Scalar(0));
    model.copyTo(padded(cv::Rect(0, 0, model.cols, model.rows)));
    const cv::Mat nonContinuous = padded(cv::Rect(0, 0, model.cols, model.rows));
    const float reference = NccMatcher::match(model, model).at<float>(0, 0);
    expectNear(NccMatcher::scoreSameSize(nonContinuous, model), reference, 1e-6,
               "单点 NCC 应与 OpenCV 同尺寸结果一致并支持非连续行");
    cv::Mat constant(model.size(), CV_8UC1, cv::Scalar(127));
    expectNear(NccMatcher::scoreSameSize(constant, model), 0.0, 1e-12,
               "零方差单点 NCC 必须返回 0");
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

void testAngleSequence()
{
    LocatorConfig config;
    config.angleMinDeg = -10.0;
    config.angleMaxDeg = 10.0;
    config.angleStepDeg = 1.0;
    const std::vector<double> angles = TransformSearch::generateAngles(config);
    expect(angles.size() == 21, "±10 度、步长 1 应生成 21 个角度");
    expect(angles.front() == -10.0 && angles.back() == 10.0, "角度端点应包含最小值和最大值");
    for (size_t i = 1; i < angles.size(); ++i) {
        expect(angles[i] > angles[i - 1], "角度序列不得重复或倒退");
    }

    config.angleMinDeg = 0.0;
    config.angleMaxDeg = 0.5;
    config.angleStepDeg = 0.1;
    const std::vector<double> fractional = TransformSearch::generateAngles(config);
    expect(fractional.size() == 6, "0～0.5、步长 0.1 应生成 6 个角度");
    for (size_t i = 0; i < fractional.size(); ++i) {
        expectNear(fractional[i], i * 0.1, 1e-9, "浮点角度序列错误");
    }

    config.angleMaxDeg = 0.55;
    const std::vector<double> nonDivisible = TransformSearch::generateAngles(config);
    expect(nonDivisible.size() == 6 && nonDivisible.back() <= 0.55,
           "不能整除步长时不得越界、重复或死循环");

    const auto expectInvalid = [](const LocatorConfig& invalid) {
        bool rejected = false;
        try {
            TransformSearch::generateAngles(invalid);
        } catch (...) {
            rejected = true;
        }
        expect(rejected, "非法角度范围或步长应被拒绝");
    };
    LocatorConfig invalid;
    invalid.angleStepDeg = 0.0;
    expectInvalid(invalid);
    invalid.angleStepDeg = -1.0;
    expectInvalid(invalid);
    invalid.angleStepDeg = 1.0;
    invalid.angleMinDeg = 2.0;
    invalid.angleMaxDeg = -2.0;
    expectInvalid(invalid);
    invalid.angleMinDeg = std::numeric_limits<double>::quiet_NaN();
    invalid.angleMaxDeg = 2.0;
    expectInvalid(invalid);
    invalid.angleMinDeg = 0.0;
    invalid.angleStepDeg = std::numeric_limits<double>::infinity();
    expectInvalid(invalid);
}

void testAngleClockwise()
{
    cv::Mat model(21, 21, CV_8UC1, cv::Scalar(0));
    model.at<unsigned char>(10, 16) = 255;
    LocatorConfig config;
    config.angleMinDeg = 90.0;
    config.angleMaxDeg = 90.0;

    const auto rotated = TransformSearch::makeAngleModels(model, config);
    expect(rotated.size() == 1, "单角度应只生成一个旋转模板");
    double maximum = 0.0;
    cv::Point maxLocation;
    cv::minMaxLoc(rotated[0].image, nullptr, &maximum, nullptr, &maxLocation);
    expect(maxLocation.x == 10 && maxLocation.y == 16,
           "中心右侧像素顺时针旋转 90 度后应位于中心下方");
    expect(rotated[0].image.at<unsigned char>(3, 10) == 0,
           "顺时针方向不得误转为逆时针");
}

void testAngleRecoverKnownAngle()
{
    cv::Mat model = makeTexturedImage(24, 16, 2468);
    LocatorConfig rotatedConfig;
    rotatedConfig.angleMinDeg = 20.0;
    rotatedConfig.angleMaxDeg = 20.0;
    const auto rotatedModels = TransformSearch::makeAngleModels(model, rotatedConfig);
    const cv::Mat& rotated = rotatedModels[0].image;

    cv::Mat search = makeTexturedImage(100, 90, 2469);
    const cv::Point topLeft(31, 35);
    rotated.copyTo(search(cv::Rect(topLeft.x, topLeft.y, rotated.cols, rotated.rows)));

    LocatorConfig config;
    config.angleMinDeg = -30.0;
    config.angleMaxDeg = 30.0;
    config.angleStepDeg = 10.0;
    config.minScore = 99.0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "多角度模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "多角度搜索执行失败");
    expect(results.size() == 1, "已知旋转目标应返回一个最佳结果");
    expectNear(results[0].angle, 20.0, 5.0, "恢复角度误差不得超过角度步长的一半");
    expectNear(results[0].x, topLeft.x + (rotated.cols - 1) / 2.0, 1e-9,
               "旋转目标中心 x 错误");
    expectNear(results[0].y, topLeft.y + (rotated.rows - 1) / 2.0, 1e-9,
               "旋转目标中心 y 错误");
    expect(results[0].score > 99.9, "已知旋转目标分数应大于 99.9");
}

void testScaleSequence()
{
    LocatorConfig config;
    config.scaleMin = 0.9;
    config.scaleMax = 1.1;
    config.scaleStep = 0.02;
    const std::vector<double> scales = TransformSearch::generateScales(config);
    expect(scales.size() == 11, "0.9～1.1、步长 0.02 应生成 11 个尺度");
    expectNear(scales.front(), 0.9, 1e-9, "尺度最小值错误");
    expectNear(scales.back(), 1.1, 1e-9, "尺度最大值错误");
    for (size_t i = 1; i < scales.size(); ++i) {
        expect(scales[i] > scales[i - 1], "尺度序列不得重复或倒退");
    }

    config.scaleMax = 1.115;
    const std::vector<double> nonDivisible = TransformSearch::generateScales(config);
    expect(nonDivisible.size() == 11 && nonDivisible.back() <= 1.115,
           "不能整除尺度步长时不得越界或死循环");

    const auto expectInvalid = [](const LocatorConfig& invalid) {
        bool rejected = false;
        try {
            TransformSearch::generateScales(invalid);
        } catch (...) {
            rejected = true;
        }
        expect(rejected, "非法尺度范围、步长或非正值应被拒绝");
    };
    LocatorConfig invalid;
    invalid.scaleStep = 0.0;
    expectInvalid(invalid);
    invalid.scaleStep = -1.0;
    expectInvalid(invalid);
    invalid.scaleStep = 0.01;
    invalid.scaleMin = 1.2;
    invalid.scaleMax = 0.8;
    expectInvalid(invalid);
    invalid.scaleMin = 0.0;
    invalid.scaleMax = 1.0;
    expectInvalid(invalid);
    invalid.scaleMin = std::numeric_limits<double>::quiet_NaN();
    expectInvalid(invalid);
    invalid.scaleMin = 1.0;
    invalid.scaleStep = std::numeric_limits<double>::infinity();
    expectInvalid(invalid);
}

void testScaleRecoverKnownScale()
{
    cv::Mat model = makeTexturedImage(24, 16, 1357);
    LocatorConfig scaledConfig;
    scaledConfig.scaleMin = 1.2;
    scaledConfig.scaleMax = 1.2;
    const auto scaledModels = TransformSearch::makeTransformModels(model, scaledConfig);
    const cv::Mat& scaled = scaledModels[0].image;

    cv::Mat search = makeTexturedImage(100, 90, 1358);
    const cv::Point topLeft(25, 30);
    scaled.copyTo(search(cv::Rect(topLeft.x, topLeft.y, scaled.cols, scaled.rows)));

    LocatorConfig config;
    config.scaleMin = 0.9;
    config.scaleMax = 1.3;
    config.scaleStep = 0.1;
    config.minScore = 99.0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "多尺度模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "多尺度搜索执行失败");
    expect(results.size() == 1, "已知缩放目标应返回一个结果");
    expectNear(results[0].scale, 1.2, 1e-9, "恢复尺度错误");
    expectNear(results[0].x, topLeft.x + (scaled.cols - 1) / 2.0, 1e-9,
               "缩放目标中心 x 错误");
    expectNear(results[0].y, topLeft.y + (scaled.rows - 1) / 2.0, 1e-9,
               "缩放目标中心 y 错误");
    expect(results[0].score > 99.9, "已知缩放目标分数应大于 99.9");
}

void testTransformCombinedSearch()
{
    cv::Mat model = makeTexturedImage(20, 14, 9753);
    LocatorConfig transformedConfig;
    transformedConfig.angleMinDeg = 15.0;
    transformedConfig.angleMaxDeg = 15.0;
    transformedConfig.scaleMin = 0.85;
    transformedConfig.scaleMax = 0.85;
    const auto transformedModels = TransformSearch::makeTransformModels(model, transformedConfig);
    const cv::Mat& transformed = transformedModels[0].image;

    cv::Mat search = makeTexturedImage(100, 90, 9754);
    const cv::Point topLeft(40, 35);
    transformed.copyTo(search(cv::Rect(topLeft.x, topLeft.y, transformed.cols, transformed.rows)));

    LocatorConfig config;
    config.angleMinDeg = -15.0;
    config.angleMaxDeg = 15.0;
    config.angleStepDeg = 15.0;
    config.scaleMin = 0.85;
    config.scaleMax = 0.95;
    config.scaleStep = 0.05;
    config.minScore = 99.0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "角度尺度组合初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "角度尺度组合搜索失败");
    expect(results.size() == 1, "组合目标应返回一个最佳结果");
    expectNear(results[0].angle, 15.0, 1e-9, "组合候选角度错误");
    expectNear(results[0].scale, 0.85, 1e-9, "组合候选尺度错误");
    expectNear(results[0].x, topLeft.x + (transformed.cols - 1) / 2.0, 1e-9,
               "组合候选中心 x 错误");
    expectNear(results[0].y, topLeft.y + (transformed.rows - 1) / 2.0, 1e-9,
               "组合候选中心 y 错误");
}

void testTransformInvalidBounds()
{
    cv::Mat model = makeTexturedImage(24, 16, 8642);
    LocatorConfig tinyConfig;
    tinyConfig.scaleMin = 0.001;
    tinyConfig.scaleMax = 0.001;
    Locator tinyLocator;
    expect(tinyLocator.initialize(asLocatorImage(model), tinyConfig) == LocatorError::InvalidConfig,
           "缩放后空模板应返回 InvalidConfig");

    cv::Mat smallSearch = makeTexturedImage(50, 50, 8643);
    LocatorConfig largeConfig;
    largeConfig.scaleMin = 4.0;
    largeConfig.scaleMax = 4.0;
    Locator largeLocator;
    expect(largeLocator.initialize(asLocatorImage(model), largeConfig) == LocatorError::Ok,
           "放大模板初始化失败");
    std::vector<LocatorResult> results;
    expect(largeLocator.execute(asLocatorImage(smallSearch), results) == LocatorError::InvalidImage,
           "变换后模板大于搜索图应返回 InvalidImage");

    cv::Mat largeSearch = makeTexturedImage(100, 90, 8644);
    LocatorConfig roiConfig = largeConfig;
    roiConfig.useRoi = true;
    roiConfig.roi = { 0, 0, 60, 60 };
    Locator roiLocator;
    expect(roiLocator.initialize(asLocatorImage(model), roiConfig) == LocatorError::Ok,
           "ROI 放大模板初始化失败");
    expect(roiLocator.execute(asLocatorImage(largeSearch), results) == LocatorError::InvalidRoi,
           "变换后模板大于 ROI 应返回 InvalidRoi");
}

void testPyramidInvalidLevel()
{
    cv::Mat model = makeTexturedImage(24, 18, 1122);
    LocatorConfig config;
    config.pyramidLevels = 0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
           "金字塔层数 0 应返回 InvalidConfig");

    config.pyramidLevels = -1;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
           "负金字塔层数应返回 InvalidConfig");

    config.pyramidLevels = 10;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
           "超过模板实际可计算层数应返回 InvalidConfig");
}

void testPyramidEquivalentResult()
{
    cv::Mat model = makeTexturedImage(48, 40, 1123);
    cv::Mat search = makeTexturedImage(512, 512, 1124);
    const cv::Point topLeft(200, 180);
    model.copyTo(search(cv::Rect(topLeft.x, topLeft.y, model.cols, model.rows)));

    const auto pyramid = PyramidSearch::buildPyramids(search, 3);
    expect(pyramid.size() == 3, "请求三层时应建立三层图像");
    expect(pyramid[0].cols == 512 && pyramid[0].rows == 512, "金字塔第 0 层应为原图");
    expect(pyramid[1].cols == 256 && pyramid[1].rows == 256, "金字塔第 1 层尺寸应减半");
    expect(pyramid[2].cols == 128 && pyramid[2].rows == 128, "金字塔第 2 层应按 2 倍继续缩小");

    LocatorConfig oneConfig;
    oneConfig.minScore = 99.0;
    oneConfig.pyramidLevels = 1;
    Locator oneLayer;
    expect(oneLayer.initialize(asLocatorImage(model), oneConfig) == LocatorError::Ok,
           "单层模板初始化失败");
    std::vector<LocatorResult> oneResults;
    expect(oneLayer.execute(asLocatorImage(search), oneResults) == LocatorError::Ok,
           "单层搜索失败");

    LocatorConfig threeConfig = oneConfig;
    threeConfig.pyramidLevels = 3;
    Locator threeLayers;
    expect(threeLayers.initialize(asLocatorImage(model), threeConfig) == LocatorError::Ok,
           "三层模板初始化失败");
    std::vector<LocatorResult> threeResults;
    expect(threeLayers.execute(asLocatorImage(search), threeResults) == LocatorError::Ok,
           "三层搜索失败");

    expect(oneResults.size() == 1 && threeResults.size() == 1, "单层和三层都应找回真目标");
    expectNear(threeResults[0].x, oneResults[0].x, 1e-9, "三层结果 x 与单层不一致");
    expectNear(threeResults[0].y, oneResults[0].y, 1e-9, "三层结果 y 与单层不一致");
    expectNear(threeResults[0].score, oneResults[0].score, 0.1, "三层分数与单层差异异常");

    const auto measure = [&](Locator& locator, std::vector<LocatorResult>& results) {
        std::vector<double> samples;
        for (int i = 0; i < 30; ++i) {
            const auto start = std::chrono::steady_clock::now();
            expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
                   "金字塔重复计时执行失败");
            samples.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    const double oneMs = measure(oneLayer, oneResults);
    const double threeMs = measure(threeLayers, threeResults);
    std::cout << "金字塔 30 次中位数：1 层 " << oneMs << " ms，3 层 " << threeMs << " ms\n";
}

void testPyramidKeepsRequestedRange()
{
    cv::Mat model = makeTexturedImage(20, 14, 1125);
    LocatorConfig transformedConfig;
    transformedConfig.angleMinDeg = 15.0;
    transformedConfig.angleMaxDeg = 15.0;
    transformedConfig.scaleMin = 0.85;
    transformedConfig.scaleMax = 0.85;
    const auto transformedModels = TransformSearch::makeTransformModels(model, transformedConfig);
    const cv::Mat& transformed = transformedModels[0].image;

    cv::Mat search = makeTexturedImage(100, 90, 1126);
    const cv::Point topLeft(40, 35);
    transformed.copyTo(search(cv::Rect(topLeft.x, topLeft.y, transformed.cols, transformed.rows)));

    LocatorConfig config;
    config.angleMinDeg = -15.0;
    config.angleMaxDeg = 15.0;
    config.angleStepDeg = 15.0;
    config.scaleMin = 0.85;
    config.scaleMax = 0.95;
    config.scaleStep = 0.05;
    config.minScore = 99.0;
    config.pyramidLevels = 3;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "金字塔组合模板初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "金字塔组合搜索失败");
    expect(results.size() == 1, "金字塔组合目标应返回一个结果");
    expectNear(results[0].angle, 15.0, 1e-9, "金字塔裁减了请求的角度范围");
    expectNear(results[0].scale, 0.85, 1e-9, "金字塔裁减了请求的尺度范围");
    expectNear(results[0].x, topLeft.x + (transformed.cols - 1) / 2.0, 1e-9,
               "金字塔组合结果中心 x 错误");
    expectNear(results[0].y, topLeft.y + (transformed.rows - 1) / 2.0, 1e-9,
               "金字塔组合结果中心 y 错误");
    expect(results[0].score > 99.9, "金字塔精化后分数应大于 99.9");
}


void testTransformLinearShift()
{
    cv::Mat model = makeTexturedImage(9, 7, 14001);
    const int resolutions[] = { 1, 2, 4, 8 };
    for (int resolution : resolutions) {
        const int half = resolution > 1 ? resolution / 2 : 0;
        for (int y = -half; y <= half; ++y) {
            for (int x = -half; x <= half; ++x) {
                const double offsetX = static_cast<double>(x) / resolution;
                const double offsetY = static_cast<double>(y) / resolution;
                const cv::Mat shift = (cv::Mat_<double>(2, 3) <<
                    1.0, 0.0, offsetX, 0.0, 1.0, offsetY);
                cv::Mat expected;
                cv::warpAffine(model, expected, shift, model.size(),
                               cv::INTER_LINEAR, cv::BORDER_REPLICATE);
                const cv::Mat actual = TransformSearch::translateLinear(model, offsetX, offsetY);
                expect(!actual.empty() && cv::norm(actual, expected, cv::NORM_INF) == 0.0,
                       "直接线性平移与 OpenCV 不一致，偏移 " + std::to_string(offsetX) +
                       "，" + std::to_string(offsetY));
            }
        }
    }
}

void testNccConstantSearchWindow()
{
    const cv::Mat model = makeTexturedImage(12, 10, 17001);
    cv::Mat search(40, 40, CV_8UC1, cv::Scalar(127));
    cv::Mat textured = makeTexturedImage(40, 40, 17002);
    textured(cv::Rect(0, 0, 20, 40)).copyTo(search(cv::Rect(0, 0, 20, 40)));
    const cv::Mat response = NccMatcher::match(search, model);
    expect(!response.empty() && std::isfinite(response.at<float>(0, 25)),
           "混合搜索图的 NCC 响应应有效");
    expect(response.at<float>(0, 25) == 0.0f,
           "常量搜索窗口的零方差 NCC 应为 0，不能产生虚假高分");
}

void testPyramidCoarseOrderReversal()
{
    const cv::Mat model = makeTexturedImage(24, 24, 19001);
    const cv::Mat noise = makeTexturedImage(24, 24, 19002);
    cv::Mat target = model.clone();
    for (int y = 0; y < target.rows; ++y) {
        for (int x = 0; x < target.cols; ++x) {
            const int value = target.at<unsigned char>(y, x) +
                              (static_cast<int>(noise.at<unsigned char>(y, x)) - 128) / 32;
            target.at<unsigned char>(y, x) = static_cast<unsigned char>(
                std::max(0, std::min(255, value)));
        }
    }
    cv::Mat distractor;
    cv::GaussianBlur(model, distractor, cv::Size(5, 5), 1.2);
    cv::Mat search = makeTexturedImage(72, 64, 19003);
    target.copyTo(search(cv::Rect(8, 10, 24, 24)));
    distractor.copyTo(search(cv::Rect(40, 32, 24, 24)));
    const cv::Mat coarseModel = PyramidSearch::buildPyramids(model, 3).back();
    const cv::Mat coarseSearch = PyramidSearch::buildPyramids(search, 3).back();
    const double targetFine = NccMatcher::scoreSameSize(target, model);
    const double distractorFine = NccMatcher::scoreSameSize(distractor, model);
    const double targetCoarse = NccMatcher::scoreSameSize(
        coarseSearch(cv::Rect(2, 2, 6, 6)), coarseModel);
    const double distractorCoarse = NccMatcher::scoreSameSize(
        coarseSearch(cv::Rect(10, 8, 6, 6)), coarseModel);
    std::cout << "原层目标/干扰分数：" << targetFine << "，" << distractorFine
              << "，粗层目标/干扰分数：" << targetCoarse << "，" << distractorCoarse << '\n';
    expect(targetFine > distractorFine && distractorCoarse > targetCoarse,
           "测试数据必须形成粗层排序反转");

    LocatorConfig config;
    config.pyramidLevels = 3;
    config.minScore = 80.0;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "粗层排序反转用例初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "粗层排序反转用例搜索失败");
    expect(results.size() == 1, "粗层排序反转用例应返回一个结果");
    expectNear(results[0].x, 19.5, 1e-9,
               "粗层错误候选不得覆盖原图更优目标，实际 x=" + std::to_string(results[0].x));
    expectNear(results[0].y, 21.5, 1e-9,
               "粗层错误候选不得覆盖原图更优目标，实际 y=" + std::to_string(results[0].y));
}

void testScaleShrunkModelFits()
{
    const cv::Mat model = makeTexturedImage(100, 100, 19004);
    const cv::Mat search = makeTexturedImage(60, 60, 19005);
    LocatorConfig config;
    config.scaleMin = 0.5;
    config.scaleMax = 0.5;
    Locator fullImage;
    expect(fullImage.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "缩小模板初始化失败");
    std::vector<LocatorResult> results;
    expect(fullImage.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "原模板大于图像但缩小后可容纳时应允许搜索");

    config.useRoi = true;
    config.roi = { 5, 5, 55, 55 };
    Locator roi;
    expect(roi.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "缩小模板 ROI 初始化失败");
    expect(roi.execute(asLocatorImage(search), results) == LocatorError::Ok,
           "原模板大于 ROI 但缩小后可容纳时应允许搜索");
}

void testVicinityNegativeRejected()
{
    const cv::Mat model = makeTexturedImage(12, 10, 19100);
    LocatorConfig config;
    config.xVicinity = -1;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
           "负 x 邻近半径应返回 InvalidConfig");
    config.xVicinity = 0;
    config.yVicinity = -1;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::InvalidConfig,
           "负 y 邻近半径应返回 InvalidConfig");
}

void testSubPixelPreparedSamples()
{
    cv::Mat modelStorage(13, 17, CV_8UC1, cv::Scalar(0));
    const cv::Mat texturedModel = makeTexturedImage(11, 9, 14301);
    texturedModel.copyTo(modelStorage(cv::Rect(0, 0, texturedModel.cols, texturedModel.rows)));
    const cv::Mat model = modelStorage(cv::Rect(0, 0, texturedModel.cols, texturedModel.rows));
    cv::Mat patchStorage(15, 16, CV_8UC1, cv::Scalar(0));
    const cv::Mat texturedPatch = makeTexturedImage(11, 9, 14302);
    texturedPatch.copyTo(patchStorage(cv::Rect(0, 0, texturedPatch.cols, texturedPatch.rows)));
    const cv::Mat patch = patchStorage(cv::Rect(0, 0, texturedPatch.cols, texturedPatch.rows));

    const int resolutions[] = { 1, 2, 4, 8 };
    for (int resolution : resolutions) {
        const std::vector<TransformSearch::SubpixelSample> samples =
            TransformSearch::makeSubpixelSamples(model, resolution);
        const std::vector<double> scores = TransformSearch::scoreSubpixelSamples(patch, samples);
        const int half = resolution > 1 ? resolution / 2 : 0;
        const int side = half * 2 + 1;
        expect(samples.size() == static_cast<size_t>(side * side) && scores.size() == samples.size(),
               "亚像素预备样本数量错误");
        for (size_t index = 0; index < samples.size(); ++index) {
            const TransformSearch::SubpixelSample& sample = samples[index];
            const int stepX = static_cast<int>(index % side) - half;
            const int stepY = static_cast<int>(index / side) - half;
            const double offsetX = static_cast<double>(stepX) / resolution;
            const double offsetY = static_cast<double>(stepY) / resolution;
            expect(sample.offsetX == offsetX && sample.offsetY == offsetY,
                   "亚像素预备样本顺序或偏移错误");
            const cv::Mat expected = TransformSearch::translateLinear(model, offsetX, offsetY);
            expect(cv::norm(sample.image, expected, cv::NORM_INF) == 0.0,
                   "亚像素预备样本与原平移结果不一致");
            const double expectedScore = NccMatcher::scoreSameSize(patch, expected);
            expect(scores[index] == expectedScore,
                   "亚像素预备评分与原双精度计算不完全一致，索引 " + std::to_string(index));
        }
    }

    LocatorConfig config;
    config.subPixelResolution = 4;
    const std::vector<TransformSearch::TransformModel> transformed =
        TransformSearch::makeTransformModels(model, config);
    expect(transformed.size() == 1, "固定角度尺度应生成一个变换模板");
    const std::vector<TransformSearch::SubpixelSample>& cached = transformed[0].subpixelSamples;
    expect(cached.size() == 25, "建模阶段应预生成 25 个四分之一像素样本");
    const std::vector<TransformSearch::SubpixelSample> expected =
        TransformSearch::makeSubpixelSamples(model, 4);
    for (size_t index = 0; index < cached.size(); ++index) {
        expect(cv::norm(cached[index].image, expected[index].image, cv::NORM_INF) == 0.0 &&
               cached[index].sum == expected[index].sum &&
               cached[index].squares == expected[index].squares &&
               cached[index].offsetX == expected[index].offsetX &&
               cached[index].offsetY == expected[index].offsetY,
               "缓存的亚像素样本、统计或偏移错误，索引 " + std::to_string(index));
    }
}

void testNccSameSizeMatch()
{
    cv::Mat padded(31, 37, CV_8UC1, cv::Scalar(0));
    const cv::Mat textured = makeTexturedImage(29, 31, 17101);
    textured.copyTo(padded(cv::Rect(0, 0, textured.cols, textured.rows)));
    const cv::Mat search = padded(cv::Rect(0, 0, textured.cols, textured.rows));
    const cv::Mat model = makeTexturedImage(29, 31, 17102);
    const cv::Mat expected = NccMatcher::match(search, model);
    const cv::Mat actual = NccMatcher::matchSameSize(search, model);
    expect(actual.size() == cv::Size(1, 1) && actual.type() == CV_32FC1,
           "同尺寸复核响应应为 1×1 浮点图");
    expect(actual.at<float>(0, 0) == expected.at<float>(0, 0),
           "同尺寸直接复核与原 OpenCV 路径不完全一致");
}
