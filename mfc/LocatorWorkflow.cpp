#include "LocatorWorkflow.h"
#include "BatchControl.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace LocatorWorkflow {
namespace {

LocatorImage asLocatorImage(const cv::Mat& image)
{
    LocatorImage view = { image.data, image.cols, image.rows, static_cast<int>(image.step) };
    return view;
}

} // namespace

SingleResult protectSingle(const std::function<SingleResult()>& action)
{
    try {
        return action();
    } catch (const cv::Exception& error) {
        SingleResult result;
        result.error = LocatorError::InternalError;
        result.diagnostic = std::string("OpenCV 异常：") + error.what();
        return result;
    } catch (const std::exception& error) {
        SingleResult result;
        result.error = LocatorError::InternalError;
        result.diagnostic = std::string("标准异常：") + error.what();
        return result;
    } catch (...) {
        SingleResult result;
        result.error = LocatorError::InternalError;
        result.diagnostic = "未知异常";
        return result;
    }
}

SingleResult runSingle(const cv::Mat& model, const cv::Mat& search,
                       const LocatorConfig& config)
{
    return protectSingle([&model, &search, &config]() {
        SingleResult result;
        Locator locator;
        const auto start = std::chrono::steady_clock::now();
        result.error = locator.initialize(asLocatorImage(model), config);
        const auto initialized = std::chrono::steady_clock::now();
        result.preprocessMs = std::chrono::duration<double, std::milli>(initialized - start).count();
        if (result.error == LocatorError::Ok) {
            result.error = locator.execute(asLocatorImage(search), result.matches);
        }
        const auto finished = std::chrono::steady_clock::now();
        result.searchMs = std::chrono::duration<double, std::milli>(finished - initialized).count();
        result.totalMs = std::chrono::duration<double, std::milli>(finished - start).count();
        return result;
    });
}

double runBatch(const std::vector<BatchTask>& tasks, int threads,
                std::vector<BatchResult>& results)
{
    const auto start = std::chrono::steady_clock::now();
    results = executeBatch(tasks, threads);
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

BatchRun runBatchControlled(const std::vector<BatchTask>& tasks, int threads,
                            const std::function<void(int, int)>& progress,
                            const std::function<bool()>& cancel)
{
    BatchRun run;
    run.total = static_cast<int>(tasks.size());
    const auto start = std::chrono::steady_clock::now();
    try {
        locator_internal::BatchObserver observer;
        observer.progress = progress;
        observer.cancel = cancel;
        run.completed = locator_internal::executeBatchObserved(tasks, threads, observer, run.results);
        run.cancelled = cancel && cancel();
    } catch (const cv::Exception& error) {
        run.diagnostic = std::string("OpenCV 异常：") + error.what();
        run.results.clear();
        run.completed = 0;
    } catch (const std::exception& error) {
        run.diagnostic = error.what();
        run.results.clear();
        run.completed = 0;
    } catch (...) {
        run.diagnostic = "未知异常";
        run.results.clear();
        run.completed = 0;
    }
    run.totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return run;
}

BenchmarkRun runFixedBenchmark(const std::vector<BatchTask>& tasks, int threads,
                               int warmupRounds, int measuredRounds, int sourcePairCount)
{
    if (tasks.empty() || warmupRounds < 1 || measuredRounds < 1 || sourcePairCount < 0) {
        throw std::invalid_argument("固定基准要求非空任务、有效配对数、至少一轮预热和一轮记录");
    }
    BenchmarkRun run;
    run.taskCount = static_cast<int>(tasks.size());
    run.pairCount = sourcePairCount == 0 ? run.taskCount : sourcePairCount;
    for (int round = 0; round < warmupRounds + measuredRounds; ++round) {
        std::vector<BatchResult> results;
        const double elapsed = runBatch(tasks, threads, results);
        if (round < warmupRounds) {
            run.warmupMs.push_back(elapsed);
        } else {
            run.roundMs.push_back(elapsed);
            run.finalRun.results = std::move(results);
            run.finalRun.completed = run.finalRun.total = run.taskCount;
            run.finalRun.totalMs = elapsed;
        }
    }
    run.firstRoundMs = run.warmupMs[0];
    std::vector<double> sorted = run.roundMs;
    std::sort(sorted.begin(), sorted.end());
    const size_t middle = sorted.size() / 2;
    run.medianMs = sorted.size() % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0
                                          : sorted[middle];
    return run;
}

void exportBenchmarkCsv(const std::string& path, const BenchmarkRun& run)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        throw std::runtime_error("无法创建固定基准 CSV：" + path);
    }
    output << "\xEF\xBB\xBF";
    output << "任务数," << run.taskCount << "\r\n独立图像对," << run.pairCount
           << "\r\n预热轮数," << run.warmupMs.size() << "\r\n记录轮数," << run.roundMs.size()
           << "\r\n计时边界,模板预处理、搜索、GPU 传输与同步\r\n首轮预热耗时ms,";
    output << std::fixed << std::setprecision(6) << run.firstRoundMs
           << "\r\n整体中位数ms," << run.medianMs << "\r\n轮次,类型,整体耗时ms\r\n";
    for (size_t index = 0; index < run.warmupMs.size(); ++index) {
        output << index << ",预热," << run.warmupMs[index] << "\r\n";
    }
    for (size_t index = 0; index < run.roundMs.size(); ++index) {
        output << index << ",记录," << run.roundMs[index] << "\r\n";
    }
    if (!output.good()) {
        throw std::runtime_error("固定基准 CSV 写入失败：" + path);
    }
}

BatchSummary summarizeBatch(const BatchRun& run)
{
    BatchSummary summary;
    double total = 0.0;
    std::vector<double> totals;
    for (const BatchResult& result : run.results) {
        if (result.error != LocatorError::Ok) {
            ++summary.errors;
        } else if (result.matches.empty()) {
            ++summary.noMatch;
        } else {
            ++summary.matched;
        }
        summary.preprocessMeanMs += result.preprocessMs;
        summary.searchMeanMs += result.searchMs;
        total += result.totalMs;
        totals.push_back(result.totalMs);
    }
    if (!run.results.empty()) {
        const double count = static_cast<double>(run.results.size());
        summary.preprocessMeanMs /= count;
        summary.searchMeanMs /= count;
        summary.totalMeanMs = total / count;
        std::sort(totals.begin(), totals.end());
        summary.p99Ms = totals[static_cast<size_t>(std::ceil(count * 0.99)) - 1];
    }
    return summary;
}

std::vector<BatchResult> filterBatchResults(const std::vector<BatchResult>& results,
                                            BatchFilter filter)
{
    std::vector<BatchResult> filtered;
    for (const BatchResult& result : results) {
        const bool matches = result.error == LocatorError::Ok && !result.matches.empty();
        const bool noMatch = result.error == LocatorError::Ok && result.matches.empty();
        if (filter == BatchFilter::All ||
            (filter == BatchFilter::Matched && matches) ||
            (filter == BatchFilter::NoMatch && noMatch) ||
            (filter == BatchFilter::Error && result.error != LocatorError::Ok)) {
            filtered.push_back(result);
        }
    }
    return filtered;
}

void exportBatchCsv(const std::string& path, const std::vector<BatchResult>& results)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        throw std::runtime_error("无法创建 CSV 文件：" + path);
    }
    output << "\xEF\xBB\xBF";
    output << "任务编号,状态,匹配序号,x,y,angle,scale,score,预处理ms,搜索ms,整体ms\r\n";
    output << std::fixed << std::setprecision(4);
    for (const BatchResult& result : results) {
        const char* statusUtf8 = result.error != LocatorError::Ok ? "执行错误" :
                                 result.matches.empty() ? "无匹配" : "成功";
        if (result.matches.empty()) {
            output << result.id << ',' << statusUtf8 << ",,,,,,,"
                   << result.preprocessMs << ',' << result.searchMs << ',' << result.totalMs << "\r\n";
            continue;
        }
        for (size_t index = 0; index < result.matches.size(); ++index) {
            const LocatorResult& match = result.matches[index];
            output << result.id << ',' << statusUtf8 << ',' << index + 1 << ','
                   << match.x << ',' << match.y << ',' << match.angle << ',' << match.scale << ','
                   << match.score << ',' << result.preprocessMs << ',' << result.searchMs << ','
                   << result.totalMs << "\r\n";
        }
    }
    if (!output.good()) {
        throw std::runtime_error("CSV 写入失败：" + path);
    }
}

} // namespace LocatorWorkflow
