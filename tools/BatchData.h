#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct ThickPair {
    int id;
    std::string searchPath;
    std::string kernelPath;
};

std::vector<ThickPair> findThickPairs(const std::string& directory);
bool loadGrayImage(const std::string& path, cv::Mat& image);
