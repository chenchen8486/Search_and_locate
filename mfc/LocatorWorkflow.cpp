#include "LocatorWorkflow.h"

#include <chrono>

namespace LocatorWorkflow {
namespace {

LocatorImage asLocatorImage(const cv::Mat& image)
{
    LocatorImage view = { image.data, image.cols, image.rows, static_cast<int>(image.step) };
    return view;
}

} // namespace

SingleResult runSingle(const cv::Mat& model, const cv::Mat& search,
                       const LocatorConfig& config)
{
    SingleResult result;
    Locator locator;
    const auto start = std::chrono::steady_clock::now();
    result.error = locator.initialize(asLocatorImage(model), config);
    const auto initialized = std::chrono::steady_clock::now();
    result.preprocessMs = std::chrono::duration<double, std::milli>(initialized - start).count();
    if (result.error == LocatorError::Ok) {
        result.error = locator.execute(asLocatorImage(search), result.matches);
    }
    const auto finished = std::chrono::steady_clock::now();
    result.searchMs = std::chrono::duration<double, std::milli>(finished - initialized).count();
    result.totalMs = std::chrono::duration<double, std::milli>(finished - start).count();
    return result;
}

double runBatch(const std::vector<BatchTask>& tasks, int threads,
                std::vector<BatchResult>& results)
{
    const auto start = std::chrono::steady_clock::now();
    results = executeBatch(tasks, threads);
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

} // namespace LocatorWorkflow
