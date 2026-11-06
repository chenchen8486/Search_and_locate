#include "ResultRefiner.h"

#include <algorithm>
#include <cmath>

namespace ResultRefiner {

namespace {

cv::Point2d fitOffset(const cv::Mat& response, cv::Point point)
{
    const double center = response.at<float>(point.y, point.x);
    const double left = response.at<float>(point.y, point.x - 1);
    const double right = response.at<float>(point.y, point.x + 1);
    const double top = response.at<float>(point.y - 1, point.x);
    const double bottom = response.at<float>(point.y + 1, point.x);
    const double gradientX = (right - left) / 2.0;
    const double gradientY = (bottom - top) / 2.0;
    const double curvatureX = left - 2.0 * center + right;
    const double curvatureY = top - 2.0 * center + bottom;
    const double cross = (response.at<float>(point.y + 1, point.x + 1) -
                          response.at<float>(point.y - 1, point.x + 1) -
                          response.at<float>(point.y + 1, point.x - 1) +
                          response.at<float>(point.y - 1, point.x - 1)) / 4.0;
    const double determinant = curvatureX * curvatureY - cross * cross;
    if (curvatureX >= -1e-12 || curvatureY >= -1e-12 || determinant <= 1e-12) {
        return cv::Point2d(0.0, 0.0);
    }
    const double offsetX = (-gradientX * curvatureY + gradientY * cross) / determinant;
    const double offsetY = (-gradientY * curvatureX + gradientX * cross) / determinant;
    if (!std::isfinite(offsetX) || !std::isfinite(offsetY)) {
        return cv::Point2d(0.0, 0.0);
    }
    return cv::Point2d(std::max(-0.5, std::min(0.5, offsetX)),
                       std::max(-0.5, std::min(0.5, offsetY)));
}

bool isBetter(const LocatorResult& left, const LocatorResult& right)
{
    if (left.score != right.score) {
        return left.score > right.score;
    }
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    if (left.angle != right.angle) {
        return left.angle < right.angle;
    }
    return left.scale < right.scale;
}

} // namespace

cv::Point2d refineSubPixel(const cv::Mat& response, cv::Point bestPoint, int resolution)
{
    if (resolution != 1 && resolution != 2 && resolution != 4 && resolution != 8) {
        throw std::invalid_argument("亚像素仅支持 1、2、4、8 档");
    }
    const cv::Point2d integerPoint(bestPoint.x, bestPoint.y);
    if (response.empty() || response.type() != CV_32FC1 || bestPoint.x <= 0 || bestPoint.y <= 0 ||
        bestPoint.x >= response.cols - 1 || bestPoint.y >= response.rows - 1) {
        return integerPoint;
    }

    const cv::Point2d offset = fitOffset(response, bestPoint);
    return cv::Point2d(
        bestPoint.x + std::floor(offset.x * resolution + 0.5) / resolution,
        bestPoint.y + std::floor(offset.y * resolution + 0.5) / resolution);
}

std::vector<LocatorResult> selectResults(const std::vector<LocatorResult>& candidates,
                                         const LocatorConfig& config)
{
    if (config.maxResults == 1) {
        const LocatorResult* best = nullptr;
        for (const LocatorResult& candidate : candidates) {
            if (candidate.score >= config.minScore &&
                (best == nullptr || isBetter(candidate, *best))) {
                best = &candidate;
            }
        }
        return best == nullptr ? std::vector<LocatorResult>() :
                                 std::vector<LocatorResult>{ *best };
    }

    std::vector<LocatorResult> sorted = candidates;
    std::stable_sort(sorted.begin(), sorted.end(), isBetter);

    std::vector<LocatorResult> selected;
    for (const LocatorResult& candidate : sorted) {
        if (candidate.score < config.minScore) {
            continue;
        }

        bool duplicate = false;
        for (const LocatorResult& existing : selected) {
            if (std::abs(candidate.x - existing.x) <= config.xVicinity &&
                std::abs(candidate.y - existing.y) <= config.yVicinity) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            selected.push_back(candidate);
            if (selected.size() == static_cast<size_t>(config.maxResults)) {
                break;
            }
        }
    }
    return selected;
}

} // namespace ResultRefiner
