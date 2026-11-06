#define _CRT_SECURE_NO_WARNINGS
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <process.h>
#include <random>
#include <stdexcept>
#include <windows.h>

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

int runGpuUnavailableIsolated()
{
    const char* visibleDevices = std::getenv("CUDA_VISIBLE_DEVICES");
    const char* childMarker = std::getenv("LOCATOR_TEST_UNAVAILABLE_CHILD");
    const std::string savedDevices = visibleDevices == nullptr ? "" : visibleDevices;
    const std::string savedMarker = childMarker == nullptr ? "" : childMarker;
    _putenv("CUDA_VISIBLE_DEVICES=-1");
    _putenv("LOCATOR_TEST_UNAVAILABLE_CHILD=1");
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char* arguments[] = { path, "--test", "Gpu.Unavailable", nullptr };
    const intptr_t status = length == 0 || length >= MAX_PATH ? -1 :
        _spawnv(_P_WAIT, path, arguments);
    if (status < 0 || status > 255) {
        return -1;
    }
    _putenv(savedDevices.empty() ? "CUDA_VISIBLE_DEVICES=" :
            ("CUDA_VISIBLE_DEVICES=" + savedDevices).c_str());
    _putenv(savedMarker.empty() ? "LOCATOR_TEST_UNAVAILABLE_CHILD=" :
            ("LOCATOR_TEST_UNAVAILABLE_CHILD=" + savedMarker).c_str());
    return static_cast<int>(status);
}
