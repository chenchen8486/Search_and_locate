#pragma once

#include "TransformSearch.h"

#include <opencv2/core.hpp>
#include <vector>

namespace PyramidSearch {

struct SearchStats {
    std::size_t refinedWindows = 0;
    std::size_t fullImageRefinements = 0;
    std::size_t gpuBatchCalls = 0;
};

std::vector<cv::Mat> buildPyramids(const cv::Mat& image, int levels);
std::vector<TransformSearch::Candidate> searchPyramid(
    const std::vector<TransformSearch::TransformModel>& models,
    const std::vector<cv::Mat>& searchLevels,
    const LocatorConfig& config,
    SearchStats* stats = nullptr);

} // namespace PyramidSearch
