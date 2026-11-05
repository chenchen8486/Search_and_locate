#include "TestSupport.h"

#include <cmath>
#include <random>
#include <stdexcept>

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + "，实际值 " + std::to_string(actual) +
                                 "，预期值 " + std::to_string(expected));
    }
}

cv::Mat makeTexturedImage(int width, int height, unsigned int seed)
{
    cv::Mat image(height, width, CV_8UC1);
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> distribution(0, 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.at<unsigned char>(y, x) = static_cast<unsigned char>(distribution(generator));
        }
    }
    return image;
}

LocatorImage asLocatorImage(const cv::Mat& image)
{
    LocatorImage view = { image.data, image.cols, image.rows, static_cast<int>(image.step) };
    return view;
}

std::string realDataDirectory()
{
    return "test_data/Search";
}

std::string batchDataTempDirectory()
{
    return "temp/t02_batch_data";
}
