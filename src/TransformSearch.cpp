#include "TransformSearch.h"

#include "NccMatcher.h"
#include "ResultRefiner.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace TransformSearch {

namespace {

std::vector<double> generateSequence(double minimum, double maximum, double step)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) ||
        step <= 0.0 || minimum > maximum) {
        throw std::invalid_argument("范围或步长无效");
    }

    const double tolerance = 1e-9 * std::max(1.0, std::abs(maximum));
    std::vector<double> values;
    for (size_t index = 0;; ++index) {
        double value = minimum + index * step;
        if (value > maximum) {
            if (value - maximum > tolerance) {
                break;
            }
            value = maximum;
        }
        if (!values.empty() && value <= values.back()) {
            break;
        }
        values.push_back(value);
        if (value == maximum) {
            break;
        }
    }
    return values;
}

TransformModel rotateModel(const cv::Mat& image, double angle, double scale)
{
    if (angle == 0.0) {
        return { image.clone(), 0.0, scale };
    }

    const double centerX = (image.cols - 1) / 2.0;
    const double centerY = (image.rows - 1) / 2.0;
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const int width = std::max(1, static_cast<int>(std::ceil(
        std::abs(cosine) * image.cols + std::abs(sine) * image.rows)));
    const int height = std::max(1, static_cast<int>(std::ceil(
        std::abs(sine) * image.cols + std::abs(cosine) * image.rows)));
    const double targetCenterX = (width - 1) / 2.0;
    const double targetCenterY = (height - 1) / 2.0;

    cv::Mat transform(2, 3, CV_64FC1);
    transform.at<double>(0, 0) = cosine;
    transform.at<double>(0, 1) = -sine;
    transform.at<double>(0, 2) = targetCenterX - cosine * centerX + sine * centerY;
    transform.at<double>(1, 0) = sine;
    transform.at<double>(1, 1) = cosine;
    transform.at<double>(1, 2) = targetCenterY - sine * centerX - cosine * centerY;

    cv::Mat rotated;
    cv::warpAffine(image, rotated, transform, cv::Size(width, height),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return { rotated, angle, scale };
}

} // namespace

std::vector<double> generateAngles(const LocatorConfig& config)
{
    return generateSequence(config.angleMinDeg, config.angleMaxDeg, config.angleStepDeg);
}

std::vector<double> generateScales(const LocatorConfig& config)
{
    if (config.scaleMin <= 0.0) {
        throw std::invalid_argument("尺度必须大于 0");
    }
    return generateSequence(config.scaleMin, config.scaleMax, config.scaleStep);
}

std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config)
{
    std::vector<TransformModel> models;
    for (double angle : generateAngles(config)) {
        models.push_back(rotateModel(model, angle, 1.0));
    }
    return models;
}

std::vector<TransformModel> makeTransformModels(const cv::Mat& model, const LocatorConfig& config)
{
    const std::vector<double> angles = generateAngles(config);
    const std::vector<double> scales = generateScales(config);
    std::vector<TransformModel> models;

    for (double scale : scales) {
        const int width = static_cast<int>(std::lround(model.cols * scale));
        const int height = static_cast<int>(std::lround(model.rows * scale));
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("缩放后模板为空");
        }

        cv::Mat scaled = model.clone();
        if (scale != 1.0) {
            cv::resize(model, scaled, cv::Size(width, height), 0.0, 0.0, cv::INTER_LINEAR);
        }
        for (double angle : angles) {
            models.push_back(rotateModel(scaled, angle, scale));
        }
    }
    return models;
}

namespace {

struct LinearMap {
    std::vector<int> left;
    std::vector<int> right;
    std::vector<int> leftWeight;
    std::vector<int> rightWeight;
};

LinearMap makeLinearMap(int width, double offsetX)
{
    LinearMap map;
    map.left.resize(width);
    map.right.resize(width);
    map.leftWeight.resize(width);
    map.rightWeight.resize(width);
    for (int x = 0; x < width; ++x) {
        const double sourceX = x - offsetX;
        const int floorX = static_cast<int>(std::floor(sourceX));
        map.left[x] = std::max(0, std::min(width - 1, floorX));
        map.right[x] = std::max(0, std::min(width - 1, floorX + 1));
        map.rightWeight[x] = static_cast<int>(std::lround((sourceX - floorX) * 32.0));
        map.leftWeight[x] = 32 - map.rightWeight[x];
    }
    return map;
}

void translateLinearWithMap(const cv::Mat& image, double offsetY, const LinearMap& map,
                            cv::Mat& shifted)
{
    shifted.create(image.size(), CV_8UC1);
    for (int y = 0; y < image.rows; ++y) {
        const double sourceY = y - offsetY;
        const int floorY = static_cast<int>(std::floor(sourceY));
        const int y0 = std::max(0, std::min(image.rows - 1, floorY));
        const int y1 = std::max(0, std::min(image.rows - 1, floorY + 1));
        const int weightY = static_cast<int>(std::lround((sourceY - floorY) * 32.0));
        const int topWeight = 32 - weightY;
        const unsigned char* row0 = image.ptr<unsigned char>(y0);
        const unsigned char* row1 = image.ptr<unsigned char>(y1);
        unsigned char* output = shifted.ptr<unsigned char>(y);
        for (int x = 0; x < image.cols; ++x) {
            const int sum = (row0[map.left[x]] * map.leftWeight[x] +
                             row0[map.right[x]] * map.rightWeight[x]) * topWeight +
                            (row1[map.left[x]] * map.leftWeight[x] +
                             row1[map.right[x]] * map.rightWeight[x]) * weightY;
            output[x] = static_cast<unsigned char>((sum + 512) >> 10);
        }
    }
}

} // namespace

cv::Mat translateLinear(const cv::Mat& image, double offsetX, double offsetY)
{
    if (image.empty() || image.type() != CV_8UC1) {
        return cv::Mat();
    }
    if (offsetX == 0.0 && offsetY == 0.0) {
        return image.clone();
    }
    cv::Mat shifted;
    translateLinearWithMap(image, offsetY, makeLinearMap(image.cols, offsetX), shifted);
    return shifted;
}

std::vector<SubpixelSample> makeSubpixelSamples(const cv::Mat& model, int resolution)
{
    if (model.empty() || model.type() != CV_8UC1 ||
        (resolution != 1 && resolution != 2 && resolution != 4 && resolution != 8)) {
        throw std::invalid_argument("亚像素模板或档位无效");
    }
    const int half = resolution > 1 ? resolution / 2 : 0;
    const int side = half * 2 + 1;
    cv::Mat storage(model.rows * side * side, model.cols, CV_8UC1);
    std::vector<LinearMap> maps;
    maps.reserve(side);
    for (int step = -half; step <= half; ++step) {
        maps.push_back(makeLinearMap(model.cols, static_cast<double>(step) / resolution));
    }

    cv::Mat horizontal(model.rows * side, model.cols, CV_16UC1);
    for (int stepX = -half; stepX <= half; ++stepX) {
        const LinearMap& map = maps[stepX + half];
        for (int y = 0; y < model.rows; ++y) {
            const unsigned char* source = model.ptr<unsigned char>(y);
            unsigned short* output = horizontal.ptr<unsigned short>((stepX + half) * model.rows + y);
            for (int x = 0; x < model.cols; ++x) {
                output[x] = static_cast<unsigned short>(source[map.left[x]] * map.leftWeight[x] +
                                                        source[map.right[x]] * map.rightWeight[x]);
            }
        }
    }

    std::vector<SubpixelSample> samples;
    samples.reserve(side * side);
    for (int stepY = -half; stepY <= half; ++stepY) {
        const double offsetY = static_cast<double>(stepY) / resolution;
        for (int stepX = -half; stepX <= half; ++stepX) {
            const int index = (stepY + half) * side + stepX + half;
            SubpixelSample sample;
            sample.offsetX = static_cast<double>(stepX) / resolution;
            sample.offsetY = offsetY;
            sample.image = storage.rowRange(index * model.rows, (index + 1) * model.rows);
            for (int y = 0; y < model.rows; ++y) {
                const double sourceY = y - offsetY;
                const int floorY = static_cast<int>(std::floor(sourceY));
                const int y0 = std::max(0, std::min(model.rows - 1, floorY));
                const int y1 = std::max(0, std::min(model.rows - 1, floorY + 1));
                const int weightY = static_cast<int>(std::lround((sourceY - floorY) * 32.0));
                const int topWeight = 32 - weightY;
                const unsigned short* row0 = horizontal.ptr<unsigned short>(
                    (stepX + half) * model.rows + y0);
                const unsigned short* row1 = horizontal.ptr<unsigned short>(
                    (stepX + half) * model.rows + y1);
                unsigned char* output = sample.image.ptr<unsigned char>(y);
                for (int x = 0; x < model.cols; ++x) {
                    const int sum = row0[x] * topWeight + row1[x] * weightY;
                    output[x] = static_cast<unsigned char>((sum + 512) >> 10);
                }
            }
            sample.sum = 0.0;
            sample.squares = 0.0;
            for (int y = 0; y < model.rows; ++y) {
                const unsigned char* row = sample.image.ptr<unsigned char>(y);
                for (int x = 0; x < model.cols; ++x) {
                    const double value = row[x];
                    sample.sum += value;
                    sample.squares += value * value;
                }
            }
            samples.push_back(sample);
        }
    }
    return samples;
}

std::vector<double> scoreSubpixelSamples(const cv::Mat& patch,
                                         const std::vector<SubpixelSample>& samples)
{
    std::vector<double> scores;
    scores.reserve(samples.size());
    if (patch.empty() || patch.type() != CV_8UC1) {
        return scores;
    }
    double searchSum = 0.0;
    double searchSquares = 0.0;
    for (int y = 0; y < patch.rows; ++y) {
        const unsigned char* row = patch.ptr<unsigned char>(y);
        for (int x = 0; x < patch.cols; ++x) {
            const double value = row[x];
            searchSum += value;
            searchSquares += value * value;
        }
    }
    const double count = static_cast<double>(patch.rows * patch.cols);
    const double searchVariance = searchSquares - searchSum * searchSum / count;
    for (const SubpixelSample& sample : samples) {
        if (sample.image.size() != patch.size() || sample.image.type() != CV_8UC1) {
            scores.push_back(0.0);
            continue;
        }
        double productSum = 0.0;
        for (int y = 0; y < patch.rows; ++y) {
            const unsigned char* searchRow = patch.ptr<unsigned char>(y);
            const unsigned char* modelRow = sample.image.ptr<unsigned char>(y);
            for (int x = 0; x < patch.cols; ++x) {
                productSum += static_cast<double>(searchRow[x]) * modelRow[x];
            }
        }
        const double modelVariance = sample.squares - sample.sum * sample.sum / count;
        if (searchVariance <= 1e-12 || modelVariance <= 1e-12) {
            scores.push_back(0.0);
            continue;
        }
        const double covariance = productSum - searchSum * sample.sum / count;
        scores.push_back(std::max(-1.0, std::min(1.0,
            covariance / std::sqrt(searchVariance * modelVariance))));
    }
    return scores;
}

void appendModelCandidates(const TransformModel& model, const cv::Mat& search,
                           const LocatorConfig& config, const cv::Mat& response,
                           std::vector<Candidate>& candidates)
{
    std::vector<double> offsets;
    const int resolution = config.subPixelResolution;
    const int half = resolution > 1 ? resolution / 2 : 0;
    for (int step = -half; step <= half; ++step) {
        offsets.push_back(static_cast<double>(step) / resolution);
    }

    const std::vector<SubpixelSample> sampledModels = makeSubpixelSamples(model.image, resolution);
    std::vector<Candidate> coarseCandidates;
    for (int y = 0; y < response.rows; ++y) {
        for (int x = 0; x < response.cols; ++x) {
            const float value = response.at<float>(y, x);
            if (!std::isfinite(value)) {
                continue;
            }
            bool localMaximum = true;
            for (int neighborY = std::max(0, y - 1);
                 neighborY <= std::min(response.rows - 1, y + 1); ++neighborY) {
                for (int neighborX = std::max(0, x - 1);
                     neighborX <= std::min(response.cols - 1, x + 1); ++neighborX) {
                    if (response.at<float>(neighborY, neighborX) > value) {
                        localMaximum = false;
                        break;
                    }
                }
                if (!localMaximum) {
                    break;
                }
            }
            const double score = std::max(0.0, static_cast<double>(value)) * 100.0;
            if (!localMaximum || score < config.minScore) {
                continue;
            }
            coarseCandidates.push_back({ static_cast<double>(x), static_cast<double>(y),
                                         model.angle, model.scale, score });
        }
    }

    coarseCandidates = ResultRefiner::selectResults(coarseCandidates, config);
    for (const Candidate& coarse : coarseCandidates) {
        const cv::Point point(static_cast<int>(coarse.x), static_cast<int>(coarse.y));
        const cv::Point2d fitted = ResultRefiner::refineSubPixel(response, point, resolution);
        Candidate candidate;
        candidate.x = fitted.x + (model.image.cols - 1) / 2.0;
        candidate.y = fitted.y + (model.image.rows - 1) / 2.0;
        candidate.angle = model.angle;
        candidate.scale = model.scale;
        candidate.score = coarse.score;
        const cv::Mat patch = search(cv::Rect(point.x, point.y, model.image.cols, model.image.rows));
        const std::vector<double> sampledScores = scoreSubpixelSamples(patch, sampledModels);
        const double bestScore = *std::max_element(sampledScores.begin(), sampledScores.end());
        double patchMinimum = 0.0;
        double patchMaximum = 0.0;
        cv::minMaxLoc(patch, &patchMinimum, &patchMaximum);
        const bool constantPatch = patchMinimum == patchMaximum;
        double verifiedScore = -2.0;
        for (size_t index = 0; index < sampledScores.size(); ++index) {
            if (constantPatch || bestScore - sampledScores[index] > 1e-3) {
                continue;
            }
            const cv::Mat sampledResponse = NccMatcher::matchSameSize(patch, sampledModels[index].image);
            if (sampledResponse.empty()) {
                continue;
            }
            const float sampledValue = sampledResponse.at<float>(0, 0);
            if (std::isfinite(sampledValue) && sampledValue > verifiedScore) {
                verifiedScore = sampledValue;
                candidate.x = point.x + offsets[index % offsets.size()] +
                              (model.image.cols - 1) / 2.0;
                candidate.y = point.y + offsets[index / offsets.size()] +
                              (model.image.rows - 1) / 2.0;
                candidate.score = std::max(0.0, static_cast<double>(sampledValue)) * 100.0;
            }
        }
        candidates.push_back(candidate);
    }
}

std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config)
{
    std::vector<Candidate> candidates;
    for (const TransformModel& model : models) {
        const cv::Mat response = NccMatcher::match(search, model.image, config.backend);
        appendModelCandidates(model, search, config, response, candidates);
    }
    return candidates;
}

} // namespace TransformSearch
