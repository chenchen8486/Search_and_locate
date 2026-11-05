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

cv::Mat translateLinear(const cv::Mat& image, double offsetX, double offsetY)
{
    if (image.empty() || image.type() != CV_8UC1) {
        return cv::Mat();
    }
    if (offsetX == 0.0 && offsetY == 0.0) {
        return image.clone();
    }
    std::vector<int> mapX(image.cols * 4);
    int* left = mapX.data();
    int* right = left + image.cols;
    int* leftWeight = right + image.cols;
    int* rightWeight = leftWeight + image.cols;
    for (int x = 0; x < image.cols; ++x) {
        const double sourceX = x - offsetX;
        const int floorX = static_cast<int>(std::floor(sourceX));
        left[x] = std::max(0, std::min(image.cols - 1, floorX));
        right[x] = std::max(0, std::min(image.cols - 1, floorX + 1));
        rightWeight[x] = static_cast<int>(std::lround((sourceX - floorX) * 32.0));
        leftWeight[x] = 32 - rightWeight[x];
    }

    cv::Mat shifted(image.size(), CV_8UC1);
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
            const int sum = (row0[left[x]] * leftWeight[x] + row0[right[x]] * rightWeight[x]) *
                            topWeight +
                            (row1[left[x]] * leftWeight[x] + row1[right[x]] * rightWeight[x]) *
                            weightY;
            output[x] = static_cast<unsigned char>((sum + 512) >> 10);
        }
    }
    return shifted;
}

std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config)
{
    std::vector<Candidate> candidates;
    std::vector<double> offsets;
    const int resolution = config.subPixelResolution;
    const int half = resolution > 1 ? resolution / 2 : 0;
    for (int step = -half; step <= half; ++step) {
        offsets.push_back(static_cast<double>(step) / resolution);
    }

    for (const TransformModel& model : models) {
        std::vector<cv::Mat> sampledModels;
        for (double offsetY : offsets) {
            for (double offsetX : offsets) {
                if (offsetX == 0.0 && offsetY == 0.0) {
                    sampledModels.push_back(model.image);
                    continue;
                }
                sampledModels.push_back(translateLinear(model.image, offsetX, offsetY));
            }
        }
        const cv::Mat response = NccMatcher::match(search, model.image);
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
                        }
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
            std::vector<double> sampledScores;
            sampledScores.reserve(offsets.size() * offsets.size());
            for (const cv::Mat& sampled : sampledModels) {
                sampledScores.push_back(NccMatcher::scoreSameSize(patch, sampled));
            }
            const double bestScore = *std::max_element(sampledScores.begin(), sampledScores.end());
            double verifiedScore = -2.0;
            for (size_t index = 0; index < sampledScores.size(); ++index) {
                if (bestScore - sampledScores[index] > 1e-3) {
                    continue;
                }
                const cv::Mat sampledResponse = NccMatcher::match(patch, sampledModels[index]);
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
    return candidates;
}

} // namespace TransformSearch
