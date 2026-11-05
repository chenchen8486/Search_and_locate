#include "Locator.h"
#include "LocatorInternal.h"
#include "NccMatcher.h"

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>

struct Locator::State {
    cv::Mat model;
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

    cv::Mat modelView = locator_internal::makeView(model);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(modelView, &minimum, &maximum);
    if (minimum == maximum) {
        return LocatorError::InvalidImage;
    }

    state_ = std::make_shared<State>();
    state_->model = modelView.clone();
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

    const cv::Mat response = NccMatcher::match(locator_internal::makeView(search), state_->model);
    if (response.empty()) {
        return LocatorError::Ok;
    }

    bool found = false;
    double bestValue = 0.0;
    cv::Point bestPoint;
    for (int y = 0; y < response.rows; ++y) {
        for (int x = 0; x < response.cols; ++x) {
            const float value = response.at<float>(y, x);
            if (!std::isfinite(value)) {
                continue;
            }
            if (!found || value > bestValue) {
                found = true;
                bestValue = value;
                bestPoint = cv::Point(x, y);
            }
        }
    }

    if (found) {
        LocatorResult result;
        result.x = bestPoint.x + (state_->model.cols - 1) / 2.0;
        result.y = bestPoint.y + (state_->model.rows - 1) / 2.0;
        result.angle = 0.0;
        result.scale = 1.0;
        result.score = std::max(0.0, bestValue) * 100.0;
        results.push_back(result);
    }
    return LocatorError::Ok;
}
