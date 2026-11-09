#pragma once

#include "BatchLocator.h"

#include <opencv2/core.hpp>

#include <functional>
#include <string>
#include <vector>

namespace LocatorWorkflow {

struct SingleResult {
    LocatorError error = LocatorError::InternalError;
    std::vector<LocatorResult> matches;
    double preprocessMs = 0.0;
    double searchMs = 0.0;
    double totalMs = 0.0;
    std::string diagnostic;
};

struct BatchRun {
    std::vector<BatchResult> results;
    int completed = 0;
    int total = 0;
    double totalMs = 0.0;
    bool cancelled = false;
    std::string diagnostic;
};

struct BenchmarkRun {
    std::vector<double> warmupMs;
    std::vector<double> roundMs;
    double firstRoundMs = 0.0;
    double medianMs = 0.0;
    int taskCount = 0;
    int pairCount = 0;
    BatchRun finalRun;
};

BenchmarkRun runFixedBenchmark(const std::vector<BatchTask>& tasks, int threads,
                               int warmupRounds, int measuredRounds, int sourcePairCount = 0);
void exportBenchmarkCsv(const std::string& path, const BenchmarkRun& run);

enum class BatchFilter {
    All = 0,
    Matched,
    NoMatch,
    Error
};

struct BatchSummary {
    int matched = 0;
    int noMatch = 0;
    int errors = 0;
    double preprocessMeanMs = 0.0;
    double searchMeanMs = 0.0;
    double totalMeanMs = 0.0;
    double p99Ms = 0.0;
};

BatchSummary summarizeBatch(const BatchRun& run);
std::vector<BatchResult> filterBatchResults(const std::vector<BatchResult>& results,
                                            BatchFilter filter);
void exportBatchCsv(const std::string& path, const std::vector<BatchResult>& results);

std::string formatBatchDataHint(int pairCount, int requestedCount);
std::wstring projectDirectory();
std::wstring batchDirectorySettingsPath();
std::wstring resolveBatchDirectory(const std::wstring& input, const std::wstring& projectRoot);
std::wstring readBatchDirectoryMemory(const std::wstring& settingsPath);
bool writeBatchDirectoryMemory(const std::wstring& settingsPath, const std::wstring& directory);

SingleResult protectSingle(const std::function<SingleResult()>& action);
SingleResult runSingle(const cv::Mat& model, const cv::Mat& search,
                       const LocatorConfig& config);
double runBatch(const std::vector<BatchTask>& tasks, int threads,
                std::vector<BatchResult>& results);
BatchRun runBatchControlled(const std::vector<BatchTask>& tasks, int threads,
                            const std::function<void(int, int)>& progress,
                            const std::function<bool()>& cancel);

} // namespace LocatorWorkflow
