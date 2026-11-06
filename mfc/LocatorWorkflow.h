#pragma once

#include "BatchLocator.h"

#include <opencv2/core.hpp>

#include <vector>

namespace LocatorWorkflow {

struct SingleResult {
    LocatorError error = LocatorError::InternalError;
    std::vector<LocatorResult> matches;
    double preprocessMs = 0.0;
    double searchMs = 0.0;
    double totalMs = 0.0;
};

SingleResult runSingle(const cv::Mat& model, const cv::Mat& search,
                       const LocatorConfig& config);
double runBatch(const std::vector<BatchTask>& tasks, int threads,
                std::vector<BatchResult>& results);

} // namespace LocatorWorkflow
