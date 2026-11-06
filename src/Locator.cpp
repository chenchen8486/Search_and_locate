#include "Locator.h"
#include "LocatorInternal.h"
#include "PyramidSearch.h"
#include "ResultRefiner.h"
#include "TransformSearch.h"

#ifdef LOCATOR_WITH_CUDA
#include "gpu/CudaMatcher.h"
#endif

#include <cmath>
#include <opencv2/core.hpp>
#include <stdexcept>

Locator::Locator() = default;

LocatorError Locator::initialize(const LocatorImage& model, const LocatorConfig& config)
{
    if (!locator_internal::isValidImage(model)) {
        return LocatorError::InvalidImage;
    }
    if (config.backend == LocatorBackend::Gpu) {
#ifdef LOCATOR_WITH_CUDA
        if (!CudaMatcher::isAvailable()) {
            return LocatorError::BackendUnavailable;
        }
#else
        return LocatorError::BackendUnavailable;
#endif
    } else if (config.backend != LocatorBackend::Cpu) {
        return LocatorError::BackendUnavailable;
    }
    if (config.pyramidLevels < 1 || config.maxResults < 1 ||
        config.xVicinity < 0 || config.yVicinity < 0 ||
        (config.subPixelResolution != 1 && config.subPixelResolution != 2 &&
         config.subPixelResolution != 4 && config.subPixelResolution != 8) ||
        !std::isfinite(config.minScore) || config.minScore < 0.0 || config.minScore > 100.0) {
        return LocatorError::InvalidConfig;
    }

    cv::Mat modelView = locator_internal::makeView(model);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(modelView, &minimum, &maximum);
    if (minimum == maximum) {
        return LocatorError::InvalidImage;
    }

    std::vector<TransformSearch::TransformModel> models;
    try {
        models = TransformSearch::makeTransformModels(modelView, config);
        if (config.pyramidLevels > 1) {
            for (TransformSearch::TransformModel& transformed : models) {
                transformed.pyramid = PyramidSearch::buildPyramids(transformed.image,
                                                                     config.pyramidLevels);
            }
        }
    } catch (const std::invalid_argument&) {
        return LocatorError::InvalidConfig;
    }

    state_ = std::make_shared<State>();
    state_->model = modelView.clone();
    state_->config = config;
    state_->models = std::move(models);
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
        searchView = searchView(cv::Rect(roi.x, roi.y, roi.width, roi.height));
        offset = cv::Point(roi.x, roi.y);
    }

    for (const TransformSearch::TransformModel& model : state_->models) {
        if (model.image.cols > searchView.cols || model.image.rows > searchView.rows) {
            return state_->config.useRoi ? LocatorError::InvalidRoi : LocatorError::InvalidImage;
        }
    }

    std::vector<LocatorResult> candidates;
    try {
        if (state_->config.pyramidLevels == 1) {
            candidates = TransformSearch::searchAngles(state_->models, searchView, state_->config);
        } else {
            const auto searchLevels = PyramidSearch::buildPyramids(searchView,
                                                                    state_->config.pyramidLevels);
            candidates = PyramidSearch::searchPyramid(state_->models, searchLevels, state_->config);
        }
    } catch (const std::invalid_argument&) {
        return LocatorError::InvalidConfig;
    }
#ifdef LOCATOR_WITH_CUDA
    catch (const CudaMatcher::Unavailable&) {
        return LocatorError::BackendUnavailable;
    } catch (const CudaMatcher::Failure&) {
        return LocatorError::InternalError;
    }
#endif
    for (LocatorResult& candidate : candidates) {
        candidate.x += offset.x;
        candidate.y += offset.y;
    }
    results = ResultRefiner::selectResults(candidates, state_->config);
    return LocatorError::Ok;
}
