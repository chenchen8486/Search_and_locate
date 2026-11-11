#include "PyramidSearch.h"

#include "NccMatcher.h"
#include "ResultRefiner.h"

#ifdef LOCATOR_WITH_CUDA
#include "gpu/CudaMatcher.h"
#endif

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

#ifdef LOCATOR_WITH_CUDA
std::vector<TransformSearch::Candidate> searchIntegerGpuWindows(
    const TransformSearch::TransformModel& model, const cv::Mat& search,
    const LocatorConfig& config, const std::vector<TransformSearch::Candidate>& coarseCandidates,
    int radius, SearchStats* stats)
{
    std::vector<TransformSearch::Candidate> candidates;
    std::vector<CudaMatcher::BatchInput> inputs;
    std::vector<cv::Point> offsets;
    const size_t batchSize = 256;
    inputs.reserve(batchSize);
    offsets.reserve(batchSize);
    const auto flush = [&]() {
        if (inputs.empty()) {
            return;
        }
        if (stats != nullptr) {
            ++stats->gpuBatchCalls;
        }
        const std::vector<cv::Mat> responses = CudaMatcher::matchBatch(inputs);
        if (responses.size() != inputs.size()) {
            throw CudaMatcher::Failure("CUDA pyramid batch response count mismatch");
        }
        for (size_t index = 0; index < inputs.size(); ++index) {
            std::vector<TransformSearch::Candidate> refined;
            TransformSearch::appendIntegerCandidates(model, config, responses[index], refined);
            for (TransformSearch::Candidate& candidate : refined) {
                candidate.x += offsets[index].x;
                candidate.y += offsets[index].y;
            }
            candidates.insert(candidates.end(), refined.begin(), refined.end());
        }
        inputs.clear();
        offsets.clear();
    };

    for (const TransformSearch::Candidate& coarse : coarseCandidates) {
        if (stats != nullptr) {
            ++stats->refinedWindows;
        }
        const int maximumLeft = search.cols - model.image.cols;
        const int maximumTop = search.rows - model.image.rows;
        const int left = std::max(0, std::min(maximumLeft,
            static_cast<int>(std::lround(coarse.x)) - radius));
        const int top = std::max(0, std::min(maximumTop,
            static_cast<int>(std::lround(coarse.y)) - radius));
        const int width = std::min(search.cols - left, model.image.cols + radius * 2 + 1);
        const int height = std::min(search.rows - top, model.image.rows + radius * 2 + 1);
        inputs.push_back({ search(cv::Rect(left, top, width, height)), model.image });
        offsets.push_back(cv::Point(left, top));
        if (inputs.size() == batchSize) {
            flush();
        }
    }
    flush();
    return candidates;
}
#endif

std::vector<TransformSearch::Candidate> searchPyramid(
    const std::vector<TransformSearch::TransformModel>& models,
    const std::vector<cv::Mat>& searchLevels,
    const LocatorConfig& config,
    SearchStats* stats)
{
    if (searchLevels.size() != static_cast<size_t>(config.pyramidLevels)) {
        throw std::invalid_argument("搜索金字塔层数不一致");
    }

    const cv::Mat& coarseSearch = searchLevels.back();
    const double factor = std::ldexp(1.0, config.pyramidLevels - 1);
    const int radius = static_cast<int>(std::ceil(factor * 2.0)) + 2;
    const int coarseVicinity = std::max(1, static_cast<int>(std::ceil(factor)));
    std::vector<TransformSearch::Candidate> candidates;
    const bool deferSubpixel = config.maxResults == 1;
    if (stats != nullptr) {
        stats->refinedWindows = 0;
        stats->fullImageRefinements = 0;
        stats->gpuBatchCalls = 0;
    }
    const int maximumDirectSize = 256;
    if (models.size() == 1 && config.maxResults == 1 &&
        searchLevels[0].cols <= maximumDirectSize && searchLevels[0].rows <= maximumDirectSize) {
        if (stats != nullptr) {
            ++stats->fullImageRefinements;
        }
        return TransformSearch::searchAngles(models, searchLevels[0], config);
    }

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
        const size_t coarseLimit = config.maxResults == 1 ? 16 : coarseCandidates.size();
        coarseConfig.maxResults = static_cast<int>(std::min(coarseCandidates.size(), coarseLimit));
        coarseConfig.xVicinity = coarseVicinity;
        coarseConfig.yVicinity = coarseVicinity;
        coarseCandidates = ResultRefiner::selectResults(coarseCandidates, coarseConfig);

        const std::vector<TransformSearch::TransformModel> singleModel(1, model);
        if (coarseCandidates.empty()) {
            const auto fallback = deferSubpixel ?
                TransformSearch::searchIntegerAngles(singleModel, searchLevels[0], config) :
                TransformSearch::searchAngles(singleModel, searchLevels[0], config);
            candidates.insert(candidates.end(), fallback.begin(), fallback.end());
            continue;
        }

        const size_t modelBegin = candidates.size();
#ifdef LOCATOR_WITH_CUDA
        if (deferSubpixel && config.backend == LocatorBackend::Gpu) {
            const auto refined = searchIntegerGpuWindows(
                model, searchLevels[0], config, coarseCandidates, radius, stats);
            candidates.insert(candidates.end(), refined.begin(), refined.end());
        } else
#endif
        {
            for (const TransformSearch::Candidate& coarse : coarseCandidates) {
                if (stats != nullptr) {
                    ++stats->refinedWindows;
                }
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
                auto refined = deferSubpixel ?
                    TransformSearch::searchIntegerAngles(singleModel, window, config) :
                    TransformSearch::searchAngles(singleModel, window, config);
                for (TransformSearch::Candidate& candidate : refined) {
                    candidate.x += left;
                    candidate.y += top;
                }
                candidates.insert(candidates.end(), refined.begin(), refined.end());
            }
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
    if (config.maxResults == 1) {
        const std::vector<TransformSearch::Candidate> selected =
            ResultRefiner::selectResults(candidates, config);
        if (selected.empty()) {
            return {};
        }
        const TransformSearch::Candidate& best = selected[0];
        const auto model = std::find_if(models.begin(), models.end(),
            [&best](const TransformSearch::TransformModel& candidate) {
                return candidate.angle == best.angle && candidate.scale == best.scale;
            });
        if (model == models.end()) {
            throw std::invalid_argument("候选对应的模型不存在");
        }
        if (stats != nullptr) {
            ++stats->fullImageRefinements;
        }
        const std::vector<TransformSearch::TransformModel> finalModel(1, *model);
        return TransformSearch::searchAngles(finalModel, searchLevels[0], config);
    }
    return candidates;
}

} // namespace PyramidSearch
