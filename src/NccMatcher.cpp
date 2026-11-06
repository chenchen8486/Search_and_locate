#include "NccMatcher.h"

#include <opencv2/imgproc.hpp>

#ifdef LOCATOR_WITH_CUDA
#include "gpu/CudaMatcher.h"
#endif

#include <algorithm>
#include <cmath>

namespace NccMatcher {

cv::Mat match(const cv::Mat& search, const cv::Mat& model, LocatorBackend backend)
{
    if (search.empty() || model.empty() || search.type() != CV_8UC1 ||
        model.type() != CV_8UC1 || model.cols > search.cols || model.rows > search.rows) {
        return cv::Mat();
    }

    double searchMin = 0.0;
    double searchMax = 0.0;
    cv::minMaxLoc(search, &searchMin, &searchMax);
    if (searchMin == searchMax) {
        return cv::Mat();
    }
#ifdef LOCATOR_WITH_CUDA
    if (backend == LocatorBackend::Gpu) {
        return CudaMatcher::match(search, model);
    }
#endif

    cv::Mat response;
    cv::matchTemplate(search, model, response, cv::TM_CCOEFF_NORMED);
    return response;
}

cv::Mat matchSameSize(const cv::Mat& search, const cv::Mat& model)
{
    if (search.empty() || model.empty() || search.type() != CV_8UC1 ||
        model.type() != CV_8UC1 || search.size() != model.size()) {
        return cv::Mat();
    }
    cv::Mat response;
    cv::matchTemplate(search, model, response, cv::TM_CCOEFF_NORMED);
    return response;
}

double scoreSameSize(const cv::Mat& search, const cv::Mat& model)
{
    if (search.empty() || model.empty() || search.type() != CV_8UC1 ||
        model.type() != CV_8UC1 || search.size() != model.size()) {
        return 0.0;
    }

    double searchSum = 0.0;
    double modelSum = 0.0;
    double searchSquares = 0.0;
    double modelSquares = 0.0;
    double productSum = 0.0;
    for (int y = 0; y < search.rows; ++y) {
        const unsigned char* searchRow = search.ptr<unsigned char>(y);
        const unsigned char* modelRow = model.ptr<unsigned char>(y);
        for (int x = 0; x < search.cols; ++x) {
            const double searchValue = searchRow[x];
            const double modelValue = modelRow[x];
            searchSum += searchValue;
            modelSum += modelValue;
            searchSquares += searchValue * searchValue;
            modelSquares += modelValue * modelValue;
            productSum += searchValue * modelValue;
        }
    }
    const double count = static_cast<double>(search.rows * search.cols);
    const double searchVariance = searchSquares - searchSum * searchSum / count;
    const double modelVariance = modelSquares - modelSum * modelSum / count;
    if (searchVariance <= 1e-12 || modelVariance <= 1e-12) {
        return 0.0;
    }
    const double covariance = productSum - searchSum * modelSum / count;
    return std::max(-1.0, std::min(1.0,
        covariance / std::sqrt(searchVariance * modelVariance)));
}

} // namespace NccMatcher
