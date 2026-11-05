#include "TransformSearch.h"

#include "NccMatcher.h"

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
