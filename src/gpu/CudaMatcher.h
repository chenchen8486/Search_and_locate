#pragma once

#include <opencv2/core.hpp>

#include <stdexcept>
#include <vector>

namespace CudaMatcher {

class Unavailable : public std::runtime_error {
public:
    Unavailable() : std::runtime_error("CUDA") {}
};

class Failure : public std::runtime_error {
public:
    explicit Failure(const char* message) : std::runtime_error(message) {}
};

struct BatchInput {
    cv::Mat search;
    cv::Mat model;
};

bool isAvailable();
cv::Mat match(const cv::Mat& search, const cv::Mat& model);
std::vector<cv::Mat> matchBatch(const std::vector<BatchInput>& inputs);

} // namespace CudaMatcher
