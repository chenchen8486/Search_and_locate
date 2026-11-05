#include "TransformSearch.h"

#include "NccMatcher.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace TransformSearch {

std::vector<double> generateAngles(const LocatorConfig& config)
{
    if (!std::isfinite(config.angleMinDeg) || !std::isfinite(config.angleMaxDeg) ||
        !std::isfinite(config.angleStepDeg) || config.angleStepDeg <= 0.0 ||
        config.angleMinDeg > config.angleMaxDeg) {
        throw std::invalid_argument("角度范围或步长无效");
    }

    const double tolerance = 1e-9 * std::max(1.0, std::abs(config.angleMaxDeg));
    std::vector<double> values;
    for (size_t index = 0;; ++index) {
        double value = config.angleMinDeg + index * config.angleStepDeg;
        if (value > config.angleMaxDeg) {
            if (value - config.angleMaxDeg > tolerance) {
                break;
            }
            value = config.angleMaxDeg;
        }
        if (!values.empty() && value <= values.back()) {
            break;
        }
        values.push_back(value);
        if (value == config.angleMaxDeg) {
            break;
        }
    }
    return values;
}

std::vector<TransformModel> makeAngleModels(const cv::Mat& model, const LocatorConfig& config)
{
    std::vector<TransformModel> models;
    const std::vector<double> angles = generateAngles(config);
    const double centerX = (model.cols - 1) / 2.0;
    const double centerY = (model.rows - 1) / 2.0;

    for (double angle : angles) {
        if (angle == 0.0) {
            models.push_back({ model.clone(), 0.0, 1.0 });
            continue;
        }

        const double radians = angle * 3.14159265358979323846 / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        const int width = std::max(1, static_cast<int>(std::ceil(
            std::abs(cosine) * model.cols + std::abs(sine) * model.rows)));
        const int height = std::max(1, static_cast<int>(std::ceil(
            std::abs(sine) * model.cols + std::abs(cosine) * model.rows)));
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
        cv::warpAffine(model, rotated, transform, cv::Size(width, height),
                       cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        models.push_back({ rotated, angle, 1.0 });
    }
    return models;
}

std::vector<Candidate> searchAngles(const std::vector<TransformModel>& models,
                                    const cv::Mat& search,
                                    const LocatorConfig& config)
{
    std::vector<Candidate> candidates;
    for (const TransformModel& model : models) {
        const cv::Mat response = NccMatcher::match(search, model.image);
        for (int y = 0; y < response.rows; ++y) {
            for (int x = 0; x < response.cols; ++x) {
                const float value = response.at<float>(y, x);
                if (!std::isfinite(value)) {
                    continue;
                }
                const double score = std::max(0.0, static_cast<double>(value)) * 100.0;
                if (score < config.minScore) {
                    continue;
                }
                Candidate candidate;
                candidate.x = x + (model.image.cols - 1) / 2.0;
                candidate.y = y + (model.image.rows - 1) / 2.0;
                candidate.angle = model.angle;
                candidate.scale = model.scale;
                candidate.score = score;
                candidates.push_back(candidate);
            }
        }
    }
    return candidates;
}

} // namespace TransformSearch
