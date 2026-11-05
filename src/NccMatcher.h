#pragma once

#include <opencv2/core.hpp>

namespace NccMatcher {

cv::Mat match(const cv::Mat& search, const cv::Mat& model);

} // namespace NccMatcher
