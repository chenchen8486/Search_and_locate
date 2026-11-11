#pragma once

#include "Locator.h"
#include "NccMatcher.h"

#include <opencv2/core.hpp>
#include <vector>

namespace TransformSearch {

struct SubpixelSample {
    cv::Mat image;
    double sum;
    double squares;
    double offsetX;
    double offsetY;
};

struct TransformModel {
    cv::Mat image;
    double angle;
    double scale;
    std::vector<cv::Mat> pyramid;
    std::vector<SubpixelSample> subpixelSamples;
    NccMatcher::ModelStats nccStats;
};

using Candidate = LocatorResult;

std::vector<double> generateAngles(const LocatorConfig& config);
std::vector<double> generateScales(const LocatorConfig& config);
std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config);
std::vector<TransformModel> makeTransformModels(const cv::Mat& model, const LocatorConfig& config);
cv::Mat translateLinear(const cv::Mat& image, double offsetX, double offsetY);
std::vector<SubpixelSample> makeSubpixelSamples(const cv::Mat& model, int resolution);
std::vector<double> scoreSubpixelSamples(const cv::Mat& patch,
                                         const std::vector<SubpixelSample>& samples);
void appendIntegerCandidates(const TransformModel& model, const LocatorConfig& config,
                             const cv::Mat& response, std::vector<Candidate>& candidates);
void appendModelCandidates(const TransformModel& model, const cv::Mat& search,
                           const LocatorConfig& config, const cv::Mat& response,
                           std::vector<Candidate>& candidates);
std::vector<Candidate> searchIntegerAngles(const std::vector<TransformModel>& models,
                                           const cv::Mat& search,
                                           const LocatorConfig& config);
std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config);

} // namespace TransformSearch
