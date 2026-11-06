#include "PyramidSearch.h"

#include "NccMatcher.h"
#include "ResultRefiner.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace PyramidSearch {

std::vector<cv::Mat> buildPyramids(const cv::Mat& image, int levels)
{
    if (image.empty() || levels < 1) {
        throw std::invalid_argument("金字塔图像或层数无效");
    }

    std::vector<cv::Mat> images(1, image);
    for (int level = 1; level < levels; ++level) {
        const cv::Mat& previous = images.back();
        if (previous.cols <= 1 || previous.rows <= 1) {
            throw std::invalid_argument("图像不足以构建请求的层数");
        }
        cv::Mat reduced;
        cv::pyrDown(previous, reduced);
        images.push_back(reduced);
    }
    return images;
}

std::vector<TransformSearch::Candidate> searchPyramid(
    const std::vector<TransformSearch::TransformModel>& models,
    const std::vector<cv::Mat>& searchLevels,
    const LocatorConfig& config)
{
    if (searchLevels.size() != static_cast<size_t>(config.pyramidLevels)) {
        throw std::invalid_argument("搜索金字塔层数不一致");
    }

    const cv::Mat& coarseSearch = searchLevels.back();
    const double factor = std::ldexp(1.0, config.pyramidLevels - 1);
    const int radius = static_cast<int>(std::ceil(factor * 2.0)) + 2;
    std::vector<TransformSearch::Candidate> candidates;

    for (const TransformSearch::TransformModel& model : models) {
        if (model.pyramid.size() != searchLevels.size()) {
            throw std::invalid_argument("模板金字塔层数不一致");
        }
        const cv::Mat& coarseModel = model.pyramid.back();
        const cv::Mat response = NccMatcher::match(coarseSearch, coarseModel, config.backend);
        const double coarseMinScore = std::max(0.0, config.minScore - 10.0);
        std::vector<TransformSearch::Candidate> coarseCandidates;

        for (int y = 0; y < response.rows; ++y) {
            for (int x = 0; x < response.cols; ++x) {
                const float value = response.at<float>(y, x);
                if (!std::isfinite(value)) {
                    continue;
                }
                const double score = std::max(0.0, static_cast<double>(value)) * 100.0;
                if (score < coarseMinScore) {
                    continue;
                }
                TransformSearch::Candidate candidate;
                candidate.x = x * factor;
                candidate.y = y * factor;
                candidate.angle = model.angle;
                candidate.scale = model.scale;
                candidate.score = score;
                coarseCandidates.push_back(candidate);
            }
        }

        LocatorConfig coarseConfig = config;
        coarseConfig.minScore = coarseMinScore;
        // ponytail: 每个变换按请求的 Top-N 保留粗层候选，回归若暴露漏检再调整。
        coarseConfig.maxResults = config.maxResults;
        coarseConfig.xVicinity = 1;
        coarseConfig.yVicinity = 1;
        coarseCandidates = ResultRefiner::selectResults(coarseCandidates, coarseConfig);

        const std::vector<TransformSearch::TransformModel> singleModel(1, model);
        if (coarseCandidates.empty()) {
            const auto fallback = TransformSearch::searchAngles(singleModel, searchLevels[0], config);
            candidates.insert(candidates.end(), fallback.begin(), fallback.end());
            continue;
        }

        const size_t modelBegin = candidates.size();
        for (const TransformSearch::Candidate& coarse : coarseCandidates) {
            const int maximumLeft = searchLevels[0].cols - model.image.cols;
            const int maximumTop = searchLevels[0].rows - model.image.rows;
            const int left = std::max(0, std::min(maximumLeft,
                static_cast<int>(std::lround(coarse.x)) - radius));
            const int top = std::max(0, std::min(maximumTop,
                static_cast<int>(std::lround(coarse.y)) - radius));
            const int width = std::min(searchLevels[0].cols - left,
                                       model.image.cols + radius * 2 + 1);
            const int height = std::min(searchLevels[0].rows - top,
                                        model.image.rows + radius * 2 + 1);
            const cv::Mat window = searchLevels[0](cv::Rect(left, top, width, height));
            auto refined = TransformSearch::searchAngles(singleModel, window, config);
            for (TransformSearch::Candidate& candidate : refined) {
                candidate.x += left;
                candidate.y += top;
            }
            candidates.insert(candidates.end(), refined.begin(), refined.end());
        }
        if (config.maxResults > 1) {
            const std::vector<TransformSearch::Candidate> refined(
                candidates.begin() + modelBegin, candidates.end());
            if (ResultRefiner::selectResults(refined, config).size() <
                static_cast<size_t>(config.maxResults)) {
                const auto fallback = TransformSearch::searchAngles(singleModel, searchLevels[0], config);
                candidates.insert(candidates.end(), fallback.begin(), fallback.end());
            }
        }
    }
    return candidates;
}

} // namespace PyramidSearch
