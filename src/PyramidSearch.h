#pragma once

#include "TransformSearch.h"

#include <opencv2/core.hpp>
#include <vector>

namespace PyramidSearch {

std::vector<cv::Mat> buildPyramids(const cv::Mat& image, int levels);
std::vector<TransformSearch::Candidate> searchPyramid(
    const std::vector<TransformSearch::TransformModel>& models,
    const std::vector<cv::Mat>& searchLevels,
    const LocatorConfig& config);

} // namespace PyramidSearch
