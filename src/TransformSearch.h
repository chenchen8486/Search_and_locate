#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>
#include <vector>

namespace TransformSearch {

struct TransformModel {
    cv::Mat image;
    double angle;
    double scale;
};

using Candidate = LocatorResult;

std::vector<double> generateAngles(const LocatorConfig& config);
std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config);
std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config);

} // namespace TransformSearch
