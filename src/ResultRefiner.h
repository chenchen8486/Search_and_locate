#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>
#include <vector>

namespace ResultRefiner {

cv::Point2d refineSubPixel(const cv::Mat& response, cv::Point bestPoint, int resolution);

std::vector<LocatorResult> selectResults(const std::vector<LocatorResult>& candidates,
                                         const LocatorConfig& config);

} // namespace ResultRefiner
