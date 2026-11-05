#include "Locator.h"
#include "LocatorInternal.h"
#include "NccMatcher.h"
#include "ResultRefiner.h"

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>

struct Locator::State {
    cv::Mat model;
    LocatorConfig config;
};

Locator::Locator() = default;

LocatorError Locator::initialize(const LocatorImage& model, const LocatorConfig& config)
{
    if (!locator_internal::isValidImage(model)) {
        return LocatorError::InvalidImage;
    }
    if (config.backend != LocatorBackend::Cpu) {
        return LocatorError::BackendUnavailable;
    }
    if (config.maxResults < 1 || !std::isfinite(config.minScore) ||
        config.minScore < 0.0 || config.minScore > 100.0) {
        return LocatorError::InvalidConfig;
    }

    cv::Mat modelView = locator_internal::makeView(model);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(modelView, &minimum, &maximum);
    if (minimum == maximum) {
        return LocatorError::InvalidImage;
    }

    state_ = std::make_shared<State>();
    state_->model = modelView.clone();
    state_->config = config;
    return LocatorError::Ok;
}

LocatorError Locator::execute(const LocatorImage& search,
                              std::vector<LocatorResult>& results) const
{
    results.clear();
    if (!state_) {
        return LocatorError::NotInitialized;
    }
    if (!locator_internal::isValidImage(search)) {
        return LocatorError::InvalidImage;
    }

    cv::Mat searchView = locator_internal::makeView(search);
    cv::Point offset;
    if (state_->config.useRoi) {
        const LocatorRect& roi = state_->config.roi;
        const long long right = static_cast<long long>(roi.x) + roi.width;
        const long long bottom = static_cast<long long>(roi.y) + roi.height;
        if (roi.width <= 0 || roi.height <= 0 || roi.x < 0 || roi.y < 0 ||
            right > search.width || bottom > search.height) {
            return LocatorError::InvalidRoi;
        }
        if (state_->model.cols > roi.width || state_->model.rows > roi.height) {
            return LocatorError::InvalidRoi;
        }
        searchView = searchView(cv::Rect(roi.x, roi.y, roi.width, roi.height));
        offset = cv::Point(roi.x, roi.y);
    }

    if (state_->model.cols > searchView.cols || state_->model.rows > searchView.rows) {
        return LocatorError::InvalidImage;
    }

    const cv::Mat response = NccMatcher::match(searchView, state_->model);
    std::vector<LocatorResult> candidates;
    for (int y = 0; y < response.rows; ++y) {
        for (int x = 0; x < response.cols; ++x) {
            const float value = response.at<float>(y, x);
            if (!std::isfinite(value)) {
                continue;
            }
            const double score = std::max(0.0, static_cast<double>(value)) * 100.0;
            if (score < state_->config.minScore) {
                continue;
            }
            LocatorResult result;
            result.x = offset.x + x + (state_->model.cols - 1) / 2.0;
            result.y = offset.y + y + (state_->model.rows - 1) / 2.0;
            result.angle = 0.0;
            result.scale = 1.0;
            result.score = score;
            candidates.push_back(result);
        }
    }

    results = ResultRefiner::selectResults(candidates, state_->config);
    return LocatorError::Ok;
}
