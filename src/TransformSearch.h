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

struct SubpixelSample {
    cv::Mat image;
    double sum;
    double squares;
    double offsetX;
    double offsetY;
};

std::vector<double> generateAngles(const LocatorConfig& config);
std::vector<double> generateScales(const LocatorConfig& config);
std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config);
std::vector<TransformModel> makeTransformModels(const cv::Mat& model, const LocatorConfig& config);
cv::Mat translateLinear(const cv::Mat& image, double offsetX, double offsetY);
std::vector<SubpixelSample> makeSubpixelSamples(const cv::Mat& model, int resolution);
std::vector<double> scoreSubpixelSamples(const cv::Mat& patch,
                                         const std::vector<SubpixelSample>& samples);
std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config);

} // namespace TransformSearch
