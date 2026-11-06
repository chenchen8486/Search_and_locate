#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>

#include <string>

void expect(bool condition, const std::string& message);
void expectNear(double actual, double expected, double tolerance, const std::string& message);
cv::Mat makeTexturedImage(int width, int height, unsigned int seed);
LocatorImage asLocatorImage(const cv::Mat& image);
std::string realDataDirectory();
std::string batchDataTempDirectory();
void runSyntheticFixedSeed100(LocatorBackend backend, const std::string& reportFile);
void runRealDataAll197(LocatorBackend backend, double scoreTolerance);
void runPerformanceBenchmark500(LocatorBackend backend);
int runGpuUnavailableIsolated();
