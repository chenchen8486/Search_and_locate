#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>

namespace NccMatcher {

struct ModelStats {
    float mean = 0.0f;
    float variance = 0.0f;
    bool valid = false;
};

ModelStats prepareModelStats(const cv::Mat& model);
cv::Mat match(const cv::Mat& search, const cv::Mat& model,
              LocatorBackend backend = LocatorBackend::Cpu,
              const ModelStats* preparedStats = nullptr);
cv::Mat matchSameSize(const cv::Mat& search, const cv::Mat& model);
double scoreSameSize(const cv::Mat& search, const cv::Mat& model);

} // namespace NccMatcher
