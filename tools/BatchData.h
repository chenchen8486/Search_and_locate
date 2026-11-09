#pragma once

#include "BatchLocator.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct ThickPair {
    int id;
    std::string searchPath;
    std::string kernelPath;
};

struct BatchDataSet {
    std::vector<cv::Mat> models;
    std::vector<cv::Mat> searches;
    std::vector<BatchTask> tasks;
};

std::vector<ThickPair> findThickPairs(const std::string& directory);
bool loadGrayImage(const std::string& path, cv::Mat& image, std::string* error = nullptr);
BatchDataSet makeBenchmarkTasks(const std::vector<ThickPair>& pairs, int count);
