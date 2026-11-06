#include "Locator.h"
#include "ResultRefiner.h"
#include "TestSupport.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void testSubPixelKnownShift()
{
    cv::Mat response = cv::Mat::zeros(5, 5, CV_32FC1);
    response.at<float>(2, 2) = 0.9f;
    response.at<float>(2, 1) = 0.6f;
    response.at<float>(2, 3) = 0.8f;
    response.at<float>(1, 2) = 0.8f;
    response.at<float>(3, 2) = 0.7f;
    response.at<float>(1, 1) = 0.5f;
    response.at<float>(1, 3) = 0.7f;
    response.at<float>(3, 1) = 0.4f;
    response.at<float>(3, 3) = 0.6f;

    const cv::Point2d eighth = ResultRefiner::refineSubPixel(response, cv::Point(2, 2), 8);
    expectNear(eighth.x, 2.25, 1e-9, "3×3 拟合的 x 偏移错误");
    expectNear(eighth.y, 1.875, 1e-9, "3×3 拟合的 y 偏移错误");
    const cv::Point2d half = ResultRefiner::refineSubPixel(response, cv::Point(2, 2), 2);
    expectNear(half.x, 2.5, 1e-9, "1/2 像素量化错误");
    expectNear(half.y, 2.0, 1e-9, "1/2 像素量化错误");

    cv::Mat clamped = cv::Mat::zeros(5, 5, CV_32FC1);
    clamped.at<float>(2, 2) = 0.8f;
    clamped.at<float>(2, 1) = 0.2f;
    clamped.at<float>(2, 3) = 0.9f;
    clamped.at<float>(1, 2) = 0.6f;
    clamped.at<float>(3, 2) = 0.6f;
    clamped.at<float>(1, 1) = 0.0f;
    clamped.at<float>(1, 3) = 0.7f;
    clamped.at<float>(3, 1) = 0.0f;
    clamped.at<float>(3, 3) = 0.7f;
    const cv::Point2d limited = ResultRefiner::refineSubPixel(clamped, cv::Point(2, 2), 8);
    expectNear(limited.x, 2.5, 1e-9, "异常拟合偏移应限制在相邻整数响应范围内");
    expectNear(limited.y, 2.0, 1e-9, "没有有效纵向邻域时应保持整数 y");

    cv::Mat coupled(5, 5, CV_32FC1);
    for (int y = 0; y < coupled.rows; ++y) {
        for (int x = 0; x < coupled.cols; ++x) {
            const double dx = x - 2.375;
            const double dy = y - 2.375;
            coupled.at<float>(y, x) = static_cast<float>(1.0 - 2.0 * dx * dx -
                3.0 * dy * dy + 3.0 * dx * dy);
        }
    }
    const cv::Point2d twoDimensional = ResultRefiner::refineSubPixel(
        coupled, cv::Point(2, 2), 8);
    expectNear(twoDimensional.x, 2.375, 1e-9, "二维曲面拟合应消除 xy 交叉项对 x 的偏差");
    expectNear(twoDimensional.y, 2.375, 1e-9, "二维曲面拟合应消除 xy 交叉项对 y 的偏差");
}

void testSubPixelBorderFallback()
{
    cv::Mat response = cv::Mat::zeros(3, 3, CV_32FC1);
    response.at<float>(0, 0) = 1.0f;
    const cv::Point2d border = ResultRefiner::refineSubPixel(response, cv::Point(0, 0), 8);
    expect(border == cv::Point2d(0.0, 0.0), "边界点缺少完整邻域时应退回整数位置");

    response.at<float>(1, 1) = 0.5f;
    const cv::Point2d flat = ResultRefiner::refineSubPixel(response, cv::Point(1, 1), 8);
    expect(flat == cv::Point2d(1.0, 1.0), "零曲率拟合应退回整数位置");

    bool rejected = false;
    try {
        ResultRefiner::refineSubPixel(response, cv::Point(1, 1), 3);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "亚像素仅支持 1、2、4、8 档");
}

namespace {

struct ExpectedMatch {
    double x;
    double y;
    double angle;
    double scale;
};

cv::Mat makeTransformedTemplate(const cv::Mat& model, double angle, double scale,
                                double fractionX, double fractionY)
{
    cv::Mat scaled;
    cv::resize(model, scaled, cv::Size(static_cast<int>(std::lround(model.cols * scale)),
                                      static_cast<int>(std::lround(model.rows * scale))),
               0.0, 0.0, cv::INTER_LINEAR);
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const int width = static_cast<int>(std::ceil(
        std::abs(cosine) * scaled.cols + std::abs(sine) * scaled.rows));
    const int height = static_cast<int>(std::ceil(
        std::abs(sine) * scaled.cols + std::abs(cosine) * scaled.rows));
    const double centerX = (scaled.cols - 1) / 2.0;
    const double centerY = (scaled.rows - 1) / 2.0;
    const double targetX = (width - 1) / 2.0;
    const double targetY = (height - 1) / 2.0;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) <<
        cosine, -sine, targetX - cosine * centerX + sine * centerY,
        sine, cosine, targetY - sine * centerX - cosine * centerY);
    cv::Mat transformed;
    cv::warpAffine(scaled, transformed, transform, cv::Size(width, height),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1.0, 0.0, fractionX,
                           0.0, 1.0, fractionY);
    cv::Mat shifted;
    cv::warpAffine(transformed, shifted, shift, transformed.size(),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return shifted;
}

cv::Point2d pasteAtCenter(const cv::Mat& transformed, cv::Mat& search,
                          double centerX, double centerY, double fractionX, double fractionY)
{
    const int left = static_cast<int>(std::lround(centerX - (transformed.cols - 1) / 2.0));
    const int top = static_cast<int>(std::lround(centerY - (transformed.rows - 1) / 2.0));
    expect(left >= 0 && top >= 0 && left + transformed.cols <= search.cols &&
           top + transformed.rows <= search.rows, "合成目标必须位于搜索图内");
    transformed.copyTo(search(cv::Rect(left, top, transformed.cols, transformed.rows)));
    return cv::Point2d(left + (transformed.cols - 1) / 2.0 + fractionX,
                       top + (transformed.rows - 1) / 2.0 + fractionY);
}

} // namespace

void runSyntheticFixedSeed100(LocatorBackend backend, const std::string& reportFile)
{
    const double fractions[] = { 0.0, -0.375, 0.25, 0.5, -0.125, 0.375, -0.25, 0.125 };
    std::ofstream report(reportFile);
    expect(report.is_open(), "无法保存合成回归真值与结果");
    report << "case,expected_x,expected_y,expected_angle,expected_scale,actual_x,actual_y,actual_angle,actual_scale,score,center_error\n";
    std::vector<std::string> failures;
    double maximumError = 0.0;

    for (int index = 0; index < 100; ++index) {
        const unsigned int seed = 91000 + index;
        cv::Mat model = makeTexturedImage(30 + index % 4 * 2, 26 + index % 3 * 2, seed);
        cv::GaussianBlur(model, model, cv::Size(5, 5), 1.2);
        cv::Mat search = makeTexturedImage(180, 150, seed + 1000);
        const double angle = -12.0 + index % 13 * 2.0;
        const double scale = 0.9 + index % 5 * 0.04;
        const double fractionX = fractions[index % 8];
        const double fractionY = fractions[(index * 3 + 1) % 8];
        const cv::Mat transformed = makeTransformedTemplate(model, angle, scale,
                                                             fractionX, fractionY);
        const cv::Point2d firstCenter = pasteAtCenter(
            transformed, search, 70.0 + index % 5 * 4.0,
            48.0 + index % 4 * 5.0, fractionX, fractionY);
        std::vector<ExpectedMatch> expected = {
            { firstCenter.x, firstCenter.y, angle, scale }
        };

        LocatorConfig config;
        config.backend = backend;
        config.angleMinDeg = angle - 4.0;
        config.angleMaxDeg = angle + 4.0;
        config.angleStepDeg = 2.0;
        config.scaleMin = scale - 0.04;
        config.scaleMax = scale + 0.04;
        config.scaleStep = 0.02;
        config.minScore = 85.0;
        config.pyramidLevels = index % 3 == 0 ? 3 : 1;
        if (index % 5 == 0) {
            const cv::Point2d secondCenter = pasteAtCenter(
                transformed, search, 125.0, 110.0, fractionX, fractionY);
            expected.push_back({ secondCenter.x, secondCenter.y, angle, scale });
            config.maxResults = 2;
            config.xVicinity = 3;
            config.yVicinity = 3;
        }
        if (index % 4 == 0) {
            pasteAtCenter(transformed, search, 22.0, 75.0, fractionX, fractionY);
            config.useRoi = true;
            config.roi = { 40, 8, 135, 137 };
        }

        Locator locator;
        expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
               "合成用例 " + std::to_string(index) + " 初始化失败");
        std::vector<LocatorResult> results;
        expect(locator.execute(asLocatorImage(search), results) == LocatorError::Ok,
               "合成用例 " + std::to_string(index) + " 搜索失败");
        std::vector<bool> claimed(results.size(), false);
        for (const ExpectedMatch& truth : expected) {
            int found = -1;
            double nearest = 0.0;
            for (size_t resultIndex = 0; resultIndex < results.size(); ++resultIndex) {
                if (claimed[resultIndex]) {
                    continue;
                }
                const double distance = std::hypot(results[resultIndex].x - truth.x,
                                                   results[resultIndex].y - truth.y);
                if (found < 0 || distance < nearest) {
                    found = static_cast<int>(resultIndex);
                    nearest = distance;
                }
            }
            if (found < 0) {
                failures.push_back(std::to_string(index) + "：返回结果数不足，实际 " +
                                   std::to_string(results.size()) + " 个");
                report << index << ',' << truth.x << ',' << truth.y << ',' << truth.angle << ','
                       << truth.scale << ",,,,,\n";
                continue;
            }
            claimed[found] = true;
            const LocatorResult& match = results[found];
            maximumError = std::max(maximumError, nearest);
            report << index << ',' << truth.x << ',' << truth.y << ',' << truth.angle << ','
                   << truth.scale << ',' << match.x << ',' << match.y << ',' << match.angle
                   << ',' << match.scale << ',' << match.score << ',' << nearest << '\n';
            if (!std::isfinite(match.x) || !std::isfinite(match.y) || nearest > 0.125 ||
                std::abs(match.angle - truth.angle) > config.angleStepDeg / 2.0) {
                failures.push_back(std::to_string(index) + "：中心误差 " + std::to_string(nearest) +
                                   "，角度误差 " + std::to_string(match.angle - truth.angle));
            }
        }
    }
    std::cout << "100 组合成回归最大中心误差：" << maximumError << " 像素\n";
    expect(failures.empty(), failures.empty() ? "" : "合成回归失败：" + failures.front());
}

void testSyntheticFixedSeed100()
{
    runSyntheticFixedSeed100(LocatorBackend::Cpu, "temp/t09_synthetic_results.csv");
}
