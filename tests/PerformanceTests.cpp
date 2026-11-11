#include "BatchData.h"
#include "BatchLocator.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace {

SIZE_T processWorkingSet()
{
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        throw std::runtime_error("无法读取进程工作集");
    }
    return counters.WorkingSetSize;
}

} // namespace

void testBatchOrderAndErrors()
{
    const cv::Mat model = makeTexturedImage(12, 10, 12001);
    cv::Mat first = makeTexturedImage(64, 52, 12002);
    cv::Mat second = makeTexturedImage(64, 52, 12003);
    model.copyTo(first(cv::Rect(20, 16, model.cols, model.rows)));
    model.copyTo(second(cv::Rect(31, 24, model.cols, model.rows)));

    std::vector<BatchTask> tasks(3);
    tasks[0].id = 20;
    tasks[0].model = asLocatorImage(model);
    tasks[0].search = asLocatorImage(first);
    tasks[1].id = 21;
    tasks[1].model = {};
    tasks[1].search = asLocatorImage(second);
    tasks[2].id = 22;
    tasks[2].model = asLocatorImage(model);
    tasks[2].search = asLocatorImage(second);

    const std::vector<BatchResult> results = executeBatch(tasks, 2);
    expect(results.size() == tasks.size(), "批量结果数量应与任务一致");
    expect(results[0].id == 20 && results[1].id == 21 && results[2].id == 22,
           "并行批量结果必须保持输入顺序");
    expect(results[0].error == LocatorError::Ok && results[2].error == LocatorError::Ok,
           "有效批量任务执行失败");
    expect(results[1].error == LocatorError::InvalidImage && results[1].matches.empty(),
           "无效任务必须隔离错误并返回空结果");
    expect(results[0].matches.size() == 1 && results[2].matches.size() == 1,
           "有效批量任务应返回一个目标");
    expectNear(results[0].matches[0].x, 25.5, 1e-9, "批量任务 0 的 x 错误");
    expectNear(results[0].matches[0].y, 20.5, 1e-9, "批量任务 0 的 y 错误");
    expectNear(results[2].matches[0].x, 36.5, 1e-9, "批量任务 2 的 x 错误");
    expectNear(results[2].matches[0].y, 28.5, 1e-9, "批量任务 2 的 y 错误");
    for (const BatchResult& result : results) {
        expect(std::isfinite(result.preprocessMs) && std::isfinite(result.searchMs) &&
               std::isfinite(result.totalMs) && result.preprocessMs >= 0.0 &&
               result.searchMs >= 0.0 && result.totalMs >= 0.0,
               "批量计时必须是单调时钟得到的非负有限值");
    }
    expect(executeBatch({}, 2).empty(), "空批量应返回空结果");
}

void testBatchSerialParallelEquivalent()
{
    std::vector<BatchTask> tasks(12);
    std::vector<cv::Mat> models;
    std::vector<cv::Mat> searches;
    models.reserve(tasks.size());
    searches.reserve(tasks.size());
    for (size_t i = 0; i < tasks.size(); ++i) {
        const int index = static_cast<int>(i);
        models.push_back(makeTexturedImage(12 + index % 3, 10 + index % 2,
                                           12100 + static_cast<unsigned int>(i)));
        searches.push_back(makeTexturedImage(70, 60, 12200 + static_cast<unsigned int>(i)));
        models[i].copyTo(searches[i](cv::Rect(10 + index, 12 + index,
                                              models[i].cols, models[i].rows)));
        tasks[i].id = static_cast<int>(i);
        tasks[i].model = asLocatorImage(models[i]);
        tasks[i].search = asLocatorImage(searches[i]);
    }
    const std::vector<BatchResult> serial = executeBatch(tasks, 1);
    const std::vector<BatchResult> parallel = executeBatch(tasks, 4);
    expect(serial.size() == parallel.size(), "串并行结果数量不一致");
    for (size_t i = 0; i < serial.size(); ++i) {
        expect(serial[i].id == parallel[i].id && serial[i].error == parallel[i].error &&
               serial[i].matches.size() == parallel[i].matches.size(),
               "串并行任务顺序、错误或结果数量不一致");
        for (size_t j = 0; j < serial[i].matches.size(); ++j) {
            const LocatorResult& expected = serial[i].matches[j];
            const LocatorResult& actual = parallel[i].matches[j];
            expectNear(actual.x, expected.x, 1e-9, "串并行 x 不一致");
            expectNear(actual.y, expected.y, 1e-9, "串并行 y 不一致");
            expectNear(actual.angle, expected.angle, 1e-9, "串并行角度不一致");
            expectNear(actual.scale, expected.scale, 1e-9, "串并行尺度不一致");
            expectNear(actual.score, expected.score, 1e-6, "串并行分数不一致");
        }
    }
}

namespace {

std::vector<BatchResult> runRealData(BatchDataSet& data, LocatorBackend backend)
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    expect(pairs.size() == 197, "真实回归必须有 197 对数据");
    data = makeBenchmarkTasks(pairs, 197);
    for (BatchTask& task : data.tasks) {
        task.config.backend = backend;
    }
    return executeBatch(data.tasks, 1);
}

} // namespace

void exportRealBaseline()
{
    BatchDataSet data;
    const std::vector<BatchResult> results = runRealData(data, LocatorBackend::Cpu);
    std::ofstream output("tests/data/expected_real_data.csv");
    expect(output.is_open(), "无法写入真实数据基准文件");
    output << "id,x,y,angle,scale,score\n" << std::setprecision(17);
    for (const BatchResult& result : results) {
        expect(result.error == LocatorError::Ok && result.matches.size() == 1,
               "导出基准前真实数据必须全部返回一个结果");
        const LocatorResult& match = result.matches[0];
        output << result.id << ',' << match.x << ',' << match.y << ',' << match.angle << ','
               << match.scale << ',' << match.score << '\n';
    }
    std::cout << "已导出 197 对真实数据结果，尚需回归和 MFC 叠加验收。\n";
}

void runRealDataAll197(LocatorBackend backend, double scoreTolerance)
{
    BatchDataSet data;
    const std::vector<BatchResult> results = runRealData(data, backend);
    std::ifstream input("tests/data/expected_real_data.csv");
    expect(input.is_open(), "缺少 tests/data/expected_real_data.csv 基准文件");
    std::string line;
    expect(static_cast<bool>(std::getline(input, line)) && line == "id,x,y,angle,scale,score",
           "真实数据基准表头错误");
    for (size_t i = 0; i < results.size(); ++i) {
        expect(static_cast<bool>(std::getline(input, line)), "真实数据基准行数不足");
        std::stringstream fields(line);
        std::string field;
        std::vector<std::string> values;
        while (std::getline(fields, field, ',')) {
            values.push_back(field);
        }
        expect(values.size() == 6 && std::stoi(values[0]) == results[i].id,
               "真实数据基准编号或列数错误");
        expect(results[i].error == LocatorError::Ok && results[i].matches.size() == 1,
               "真实数据任务 " + std::to_string(results[i].id) + " 未返回唯一结果");
        const LocatorResult& match = results[i].matches[0];
        expectNear(match.x, std::stod(values[1]), 1e-9,
                   "真实数据任务 " + std::to_string(results[i].id) + " x 回归不一致");
        expectNear(match.y, std::stod(values[2]), 1e-9,
                   "真实数据任务 " + std::to_string(results[i].id) + " y 回归不一致");
        expectNear(match.angle, std::stod(values[3]), 1e-9, "真实数据角度回归不一致");
        expectNear(match.scale, std::stod(values[4]), 1e-9, "真实数据尺度回归不一致");
        expectNear(match.score, std::stod(values[5]), scoreTolerance, "真实数据分数回归不一致");
    }
    expect(!std::getline(input, line), "真实数据基准不应包含额外行");
    std::cout << "197 对真实数据逐对回归通过。\n";
}

void testRealDataAll197()
{
    runRealDataAll197(LocatorBackend::Cpu, 1e-6);
}

void testPerformanceTaskComposition()
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet real = makeBenchmarkTasks(pairs, 197);
    BatchDataSet benchmark = makeBenchmarkTasks(pairs, 500);
    for (BatchTask& task : benchmark.tasks) {
        task.config.subPixelResolution = 4;
    }
    expect(benchmark.tasks.size() == 500, "性能压测应展开为 500 个任务");
    for (size_t i = 0; i < benchmark.tasks.size(); ++i) {
        const BatchTask& task = benchmark.tasks[i];
        expect(task.id == static_cast<int>(i), "500 个任务的编号和顺序必须稳定");
        expect(task.model.width == 48 && task.model.height == 48 &&
               task.search.width == 118 && task.search.height == 118,
               "压测图像尺寸与真实数据不一致");
        expect(cv::norm(benchmark.models[i], real.models[i % 197]) == 0.0 &&
               cv::norm(benchmark.searches[i], real.searches[i % 197]) == 0.0,
               "压测任务必须按 i % 197 重复使用真实数据");
        expect(task.config.pyramidLevels == 1 && task.config.subPixelResolution == 4 &&
               task.config.maxResults == 1 && task.config.angleMinDeg == 0.0 &&
               task.config.angleMaxDeg == 0.0 && task.config.scaleMin == 1.0 &&
               task.config.scaleMax == 1.0 && !task.config.useRoi,
               "压测参数不符合固定口径");
    }
    std::cout << "500 个任务，197 对独立图像。\n";
}

void runPerformanceBenchmark500(LocatorBackend backend)
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet data = makeBenchmarkTasks(pairs, 500);
    for (BatchTask& task : data.tasks) {
        task.config.backend = backend;
        task.config.subPixelResolution = 4;
    }
    for (int i = 0; i < 5; ++i) {
        executeBatch(data.tasks, 20);
    }

    std::vector<double> wallTimes;
    std::vector<double> taskTotals;
    double preprocessSum = 0.0;
    double searchSum = 0.0;
    double totalSum = 0.0;
    const char* rawFile = backend == LocatorBackend::Gpu ?
        "temp/t11_gpu_benchmark500.csv" : "temp/t10_benchmark500.csv";
    std::ofstream raw(rawFile);
    expect(raw.is_open(), "无法保存 500 任务原始计时");
    raw << "round,wall_ms,mean_preprocess_ms,mean_search_ms,mean_total_ms,p99_ms\n";
    raw << std::setprecision(17);
    for (int round = 0; round < 50; ++round) {
        const auto start = std::chrono::steady_clock::now();
        const std::vector<BatchResult> results = executeBatch(data.tasks, 20);
        const double wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        wallTimes.push_back(wallMs);
        double roundPreprocess = 0.0;
        double roundSearch = 0.0;
        double roundTotal = 0.0;
        for (const BatchResult& result : results) {
            expect(result.error == LocatorError::Ok && result.matches.size() == 1,
                   "500 任务压测存在失败或空匹配，任务 " + std::to_string(result.id));
            roundPreprocess += result.preprocessMs;
            roundSearch += result.searchMs;
            roundTotal += result.totalMs;
            taskTotals.push_back(result.totalMs);
        }
        preprocessSum += roundPreprocess;
        searchSum += roundSearch;
        totalSum += roundTotal;
        const double count = static_cast<double>(results.size());
        std::vector<double> roundTotals;
        for (const BatchResult& result : results) {
            roundTotals.push_back(result.totalMs);
        }
        std::sort(roundTotals.begin(), roundTotals.end());
        raw << round << ',' << wallMs << ',' << roundPreprocess / count << ','
            << roundSearch / count << ',' << roundTotal / count << ','
            << roundTotals[static_cast<size_t>(std::ceil(count * 0.99)) - 1] << '\n';
    }
    std::sort(wallTimes.begin(), wallTimes.end());
    std::sort(taskTotals.begin(), taskTotals.end());
    const double sampleCount = static_cast<double>(taskTotals.size());
    const double p99 = taskTotals[static_cast<size_t>(std::ceil(sampleCount * 0.99)) - 1];
    const double median = wallTimes[wallTimes.size() / 2];
#ifdef _DEBUG
    const char* configuration = "Debug";
#else
    const char* configuration = "Release";
#endif
    std::cout << "后端：" << (backend == LocatorBackend::Gpu ? "GPU" : "CPU")
              << "，OpenCV " << CV_VERSION << "，" << configuration << " x64，20 线程。\n";
    std::cout << "500 个任务，197 对独立图像，预热 5 轮，记录 50 轮。\n";
    std::cout << "整体耗时中位数 " << median << " ms，单任务预处理均值 "
              << preprocessSum / sampleCount << " ms，搜索均值 " << searchSum / sampleCount
              << " ms，整体均值 " << totalSum / sampleCount << " ms，单任务 P99 "
              << p99 << " ms。20 ms 目标" << (median <= 20.0 ? "达到" : "未达到") << '\n';
}

void testPerformanceBenchmark500()
{
    runPerformanceBenchmark500(LocatorBackend::Cpu);
}

void runPersistentPerformanceBenchmark(int taskCount, LocatorBackend backend,
                                       int pyramidLevels, int subPixelResolution,
                                       int warmupRounds, int measuredRounds)
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    BatchDataSet data = makeBenchmarkTasks(pairs, taskCount);
    std::vector<Locator> locators(data.tasks.size());
    for (BatchTask& task : data.tasks) {
        task.config.backend = backend;
        task.config.pyramidLevels = pyramidLevels;
        task.config.subPixelResolution = subPixelResolution;
    }

    const SIZE_T workingSetBefore = processWorkingSet();
    const auto initializeStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < data.tasks.size(); ++i) {
        expect(locators[i].initialize(data.tasks[i].model, data.tasks[i].config) == LocatorError::Ok,
               "常驻定位核初始化失败，索引 " + std::to_string(i));
    }
    const double initializeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - initializeStart).count();
    const long long workingSetDelta = static_cast<long long>(processWorkingSet()) -
                                      static_cast<long long>(workingSetBefore);
    const double workingSetDeltaMiB = static_cast<double>(workingSetDelta) / (1024.0 * 1024.0);

    std::vector<LocatorError> errors(data.tasks.size());
    std::vector<std::vector<LocatorResult> > matches(data.tasks.size());
    std::vector<double> taskTimes(data.tasks.size());
    const auto executeFrame = [&]() {
#pragma omp parallel for num_threads(20) schedule(static)
        for (long long i = 0; i < static_cast<long long>(locators.size()); ++i) {
            const size_t index = static_cast<size_t>(i);
            const auto start = std::chrono::steady_clock::now();
            errors[index] = locators[index].execute(data.tasks[index].search, matches[index]);
            taskTimes[index] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    };
    for (int i = 0; i < warmupRounds; ++i) {
        executeFrame();
    }

    std::vector<double> wallTimes;
    std::vector<double> allTaskTimes;
    const std::string pyramidSuffix = pyramidLevels == 1 ? std::string() :
        "_pyramid" + std::to_string(pyramidLevels);
    const std::string subpixelSuffix = subPixelResolution == 4 ? std::string() :
        "_subpixel" + std::to_string(subPixelResolution);
    const std::string roundSuffix = measuredRounds == 50 ? std::string() : "_dev";
    const std::string rawFile = std::string("temp/t11_persistent_") +
        (backend == LocatorBackend::Gpu ? "gpu" : "cpu") + std::to_string(taskCount) +
        pyramidSuffix + subpixelSuffix + roundSuffix + ".csv";
    std::ofstream raw(rawFile);
    expect(raw.is_open(), "无法保存常驻定位原始计时");
    raw << "round,wall_ms,mean_execute_ms,p99_ms\n" << std::setprecision(17);
    for (int round = 0; round < measuredRounds; ++round) {
        const auto start = std::chrono::steady_clock::now();
        executeFrame();
        const double wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        wallTimes.push_back(wallMs);
        double roundTotal = 0.0;
        std::vector<double> roundTasks = taskTimes;
        for (size_t i = 0; i < data.tasks.size(); ++i) {
            expect(errors[i] == LocatorError::Ok && matches[i].size() == 1,
                   "常驻定位执行失败或空匹配，任务 " + std::to_string(i));
            roundTotal += taskTimes[i];
            allTaskTimes.push_back(taskTimes[i]);
        }
        std::sort(roundTasks.begin(), roundTasks.end());
        raw << round << ',' << wallMs << ',' << roundTotal / data.tasks.size() << ','
            << roundTasks[static_cast<size_t>(std::ceil(roundTasks.size() * 0.99)) - 1] << '\n';
    }
    std::sort(wallTimes.begin(), wallTimes.end());
    std::sort(allTaskTimes.begin(), allTaskTimes.end());
    const double meanTask = std::accumulate(allTaskTimes.begin(), allTaskTimes.end(), 0.0) /
                            static_cast<double>(allTaskTimes.size());
    const double p99 = allTaskTimes[static_cast<size_t>(std::ceil(allTaskTimes.size() * 0.99)) - 1];
    const double median = wallTimes[wallTimes.size() / 2];
    std::cout << "常驻后端：" << (backend == LocatorBackend::Gpu ? "GPU" : "CPU")
              << "，任务数 " << taskCount << "，金字塔 " << pyramidLevels
              << "，亚像素 " << subPixelResolution << "，197 对独立图像，20 线程，预热 "
              << warmupRounds << " 轮，记录 " << measuredRounds << " 轮。\n"
              << "建模一次耗时 " << initializeMs << " ms，实时整体中位数 " << median
              << " ms，单任务执行均值 " << meanTask << " ms，P99 " << p99
              << " ms，建模工作集增量 " << workingSetDeltaMiB << " MiB。\n";
}

void testPerformancePersistent500()
{
    runPersistentPerformanceBenchmark(500, LocatorBackend::Cpu, 1, 4);
}

void testPerformancePersistent500SubPixel1()
{
    runPersistentPerformanceBenchmark(500, LocatorBackend::Cpu, 1, 1);
}

void testPerformancePersistent500Pyramid2()
{
    runPersistentPerformanceBenchmark(500, LocatorBackend::Cpu, 2, 4, 5, 50);
}

void testPerformancePersistent600()
{
    runPersistentPerformanceBenchmark(600, LocatorBackend::Cpu, 1, 4);
}
