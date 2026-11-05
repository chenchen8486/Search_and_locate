#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>
#include <vector>

namespace TransformSearch {

struct TransformModel {
    cv::Mat image;
    double angle;
    double scale;
    std::vector<cv::Mat> pyramid;
};

using Candidate = LocatorResult;

std::vector<double> generateAngles(const LocatorConfig& config);
std::vector<double> generateScales(const LocatorConfig& config);
std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config);
std::vector<TransformModel> makeTransformModels(const cv::Mat& model, const LocatorConfig& config);
cv::Mat translateLinear(const cv::Mat& image, double offsetX, double offsetY);
std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config);

} // namespace TransformSearch
