#pragma once

#include "BatchLocator.h"

#include <opencv2/core.hpp>

#include <functional>
#include <string>
#include <vector>

namespace LocatorWorkflow {

struct SingleResult {
    LocatorError error = LocatorError::InternalError;
    std::vector<LocatorResult> matches;
    double preprocessMs = 0.0;
    double searchMs = 0.0;
    double totalMs = 0.0;
    std::string diagnostic;
};

struct BatchRun {
    std::vector<BatchResult> results;
    int completed = 0;
    int total = 0;
    double totalMs = 0.0;
    bool cancelled = false;
    std::string diagnostic;
};

SingleResult protectSingle(const std::function<SingleResult()>& action);
SingleResult runSingle(const cv::Mat& model, const cv::Mat& search,
                       const LocatorConfig& config);
double runBatch(const std::vector<BatchTask>& tasks, int threads,
                std::vector<BatchResult>& results);
BatchRun runBatchControlled(const std::vector<BatchTask>& tasks, int threads,
                            const std::function<void(int, int)>& progress,
                            const std::function<bool()>& cancel);

} // namespace LocatorWorkflow
