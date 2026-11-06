#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>

namespace NccMatcher {

cv::Mat match(const cv::Mat& search, const cv::Mat& model,
              LocatorBackend backend = LocatorBackend::Cpu);
double scoreSameSize(const cv::Mat& search, const cv::Mat& model);

} // namespace NccMatcher
