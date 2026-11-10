#include "LocatorWorkflow.h"
#include "BatchControl.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include <windows.h>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace LocatorWorkflow {
namespace {

std::wstring moduleDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::wstring();
    }
    std::wstring directory(path, length);
    const size_t separator = directory.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : directory.substr(0, separator);
}

bool isDirectory(const std::wstring& path)
{
    if (path.empty()) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace

std::wstring projectDirectory()
{
    std::wstring root = moduleDirectory();
    for (int level = 0; level < 3 && !root.empty(); ++level) {
        const size_t separator = root.find_last_of(L"\\/");
        root = separator == std::wstring::npos ? std::wstring() : root.substr(0, separator);
    }
    if (GetFileAttributesW((root + L"\\Locator.sln").c_str()) != INVALID_FILE_ATTRIBUTES) {
        return root;
    }
    wchar_t current[MAX_PATH] = {};
    const DWORD length = GetCurrentDirectoryW(MAX_PATH, current);
    return length > 0 && length < MAX_PATH ? std::wstring(current, length) : std::wstring();
}

std::wstring batchDirectorySettingsPath()
{
    const std::wstring directory = moduleDirectory();
    return directory.empty() ? std::wstring() : directory + L"\\LocatorMfc.ini";
}

std::wstring resolveBatchDirectory(const std::wstring& input, const std::wstring& projectRoot)
{
    const std::wstring requested = input.empty() ? L"." : input;
    const bool isAbsolute = (requested.size() > 1 && requested[1] == L':') ||
                            requested[0] == L'\\' || requested[0] == L'/';
    const std::wstring combined = isAbsolute ? requested : projectRoot + L"\\" + requested;
    wchar_t absolute[MAX_PATH] = {};
    if (GetFullPathNameW(combined.c_str(), MAX_PATH, absolute, nullptr) == 0 ||
        !isDirectory(absolute)) {
        return std::wstring();
    }
    return absolute;
}

std::wstring readBatchDirectoryMemory(const std::wstring& settingsPath)
{
    wchar_t remembered[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Batch", L"LastDirectory", L"", remembered, MAX_PATH,
                             settingsPath.c_str());
    if ((remembered[0] != L'\\' &&
         (remembered[0] == L'\0' || remembered[1] != L':')) || !isDirectory(remembered)) {
        return std::wstring();
    }
    return remembered;
}

bool writeBatchDirectoryMemory(const std::wstring& settingsPath, const std::wstring& directory)
{
    return !settingsPath.empty() && isDirectory(directory) &&
           WritePrivateProfileStringW(L"Batch", L"LastDirectory", directory.c_str(),
                                      settingsPath.c_str()) != FALSE;
}

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

BatchRun BatchSession::run(const std::vector<BatchTask>& tasks, int threads, bool rebuild,
                           const std::function<void(int, int)>& progress,
                           const std::function<bool()>& cancel)
{
    BatchRun run;
    run.total = static_cast<int>(tasks.size());
    const auto totalStart = std::chrono::steady_clock::now();
    try {
        const bool initialized = rebuild || locators_.size() != tasks.size();
        if (initialized) {
            locators_.clear();
            locators_.resize(tasks.size());
            initializationErrors_.assign(tasks.size(), LocatorError::InternalError);
            preprocessTimes_.assign(tasks.size(), 0.0);
            for (size_t i = 0; i < tasks.size(); ++i) {
                const auto start = std::chrono::steady_clock::now();
                try {
                    initializationErrors_[i] = locators_[i].initialize(tasks[i].model, tasks[i].config);
                } catch (...) {
                    initializationErrors_[i] = LocatorError::InternalError;
                }
                preprocessTimes_[i] = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            }
        }

        std::vector<BatchResult> results(tasks.size());
        std::vector<char> finished(tasks.size(), 0);
        std::atomic<int> completed(0);
#ifdef _OPENMP
        const int workers = threads > 0 ? threads : omp_get_max_threads();
#else
        const int workers = 1;
#endif
        const auto runTask = [&](size_t index) {
            BatchResult& result = results[index];
            result.id = tasks[index].id;
            result.preprocessMs = initialized ? preprocessTimes_[index] : 0.0;
            result.error = initializationErrors_[index];
            result.searchMs = 0.0;
            if (result.error == LocatorError::Ok) {
                const auto searchStart = std::chrono::steady_clock::now();
                try {
                    result.error = locators_[index].execute(tasks[index].search, result.matches);
                } catch (...) {
                    result.error = LocatorError::InternalError;
                    result.matches.clear();
                }
                result.searchMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - searchStart).count();
            }
            result.totalMs = result.preprocessMs + result.searchMs;
        };

#ifdef _OPENMP
#pragma omp parallel for num_threads(workers)
#endif
        for (long long index = 0; index < static_cast<long long>(tasks.size()); ++index) {
            if (cancel && cancel()) {
                continue;
            }
            runTask(static_cast<size_t>(index));
            finished[index] = 1;
            if (progress) {
                progress(completed.fetch_add(1) + 1, static_cast<int>(tasks.size()));
            }
        }
        run.results.reserve(tasks.size());
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (finished[i]) {
                run.results.push_back(std::move(results[i]));
            }
        }
        run.completed = static_cast<int>(run.results.size());
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
        std::chrono::steady_clock::now() - totalStart).count();
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

std::string formatBatchDataHint(int pairCount, int requestedCount)
{
    if (pairCount < 0 || requestedCount < 0) {
        throw std::invalid_argument("配对数和任务数不能为负数");
    }
    const int actual = requestedCount == 0 ? pairCount : requestedCount;
    std::ostringstream text;
    text << "已扫描 " << pairCount << " 对；";
    if (requestedCount > pairCount) {
        text << actual << " 个任务按编号循环复用";
    } else if (requestedCount == 0) {
        text << "本次执行全部 " << actual << " 对";
    } else {
        text << "本次执行 " << actual << " 个任务";
    }
    return text.str();
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
