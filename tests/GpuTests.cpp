#define _CRT_SECURE_NO_WARNINGS
#include "BatchLocator.h"
#include "gpu/CudaMatcher.h"
#include "Locator.h"
#include "NccMatcher.h"
#include "TestSupport.h"
#include "TransformSearch.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

void compareCpuGpu(const cv::Mat& model, const cv::Mat& search, const LocatorConfig& config)
{
    LocatorConfig gpuConfig = config;
    gpuConfig.backend = LocatorBackend::Gpu;
    Locator cpu;
    Locator gpu;
    expect(cpu.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "CPU 对照初始化失败");
    expect(gpu.initialize(asLocatorImage(model), gpuConfig) == LocatorError::Ok,
           "GPU 初始化应成功，不能静默回退 CPU");
    std::vector<LocatorResult> expected;
    std::vector<LocatorResult> actual;
    expect(cpu.execute(asLocatorImage(search), expected) == LocatorError::Ok,
           "CPU 对照搜索失败");
    expect(gpu.execute(asLocatorImage(search), actual) == LocatorError::Ok,
           "GPU 搜索失败");
    expect(actual.size() == expected.size(), "CPU/GPU 结果数量不一致");
    for (size_t i = 0; i < actual.size(); ++i) {
        expectNear(actual[i].x, expected[i].x, 1e-9, "CPU/GPU 中心 x 不一致");
        expectNear(actual[i].y, expected[i].y, 1e-9, "CPU/GPU 中心 y 不一致");
        expectNear(actual[i].angle, expected[i].angle, 1e-9, "CPU/GPU 角度不一致");
        expectNear(actual[i].scale, expected[i].scale, 1e-9, "CPU/GPU 尺度不一致");
        expectNear(actual[i].score, expected[i].score, 1e-4, "CPU/GPU 分数不一致");
    }
}

} // namespace

void testGpuAvailability()
{
    const cv::Mat model = makeTexturedImage(12, 10, 16001);
    LocatorConfig config;
    config.backend = LocatorBackend::Gpu;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "GPU 后端应完成有效模板初始化，当前构建或设备不支持 CUDA");
}

void testGpuCpuEquivalence()
{
    const cv::Mat search = makeTexturedImage(96, 80, 16002);
    cv::Mat model;
    search(cv::Rect(37, 29, 16, 12)).copyTo(model);
    compareCpuGpu(model, search, LocatorConfig());

    LocatorConfig gpuConfig;
    gpuConfig.backend = LocatorBackend::Gpu;
    const auto prepared = TransformSearch::makeTransformModels(model, gpuConfig);
    const cv::Mat expected = NccMatcher::match(search, model, LocatorBackend::Gpu);
    const cv::Mat actual = NccMatcher::match(search, model, LocatorBackend::Gpu,
                                             &prepared[0].nccStats);
    expect(actual.size() == expected.size() && actual.type() == expected.type(),
           "GPU 预备统计响应尺寸或类型错误");
    expect(cv::norm(actual, expected, cv::NORM_INF) <= 1e-6,
           "GPU 预备统计与原模板统计的响应误差超过 1e-6");
}

void testGpuNonContinuousRoi()
{
    const cv::Mat logical = makeTexturedImage(100, 90, 16003);
    cv::Mat padded(90, 117, CV_8UC1, cv::Scalar(0));
    logical.copyTo(padded(cv::Rect(0, 0, logical.cols, logical.rows)));
    cv::Mat model;
    logical(cv::Rect(10, 10, 16, 12)).copyTo(model);
    model.copyTo(logical(cv::Rect(60, 50, model.cols, model.rows)));
    model.copyTo(padded(cv::Rect(60, 50, model.cols, model.rows)));
    LocatorImage searchView = asLocatorImage(padded);
    searchView.width = logical.cols;

    LocatorConfig config;
    config.useRoi = true;
    config.roi = { 50, 40, 40, 35 };
    config.backend = LocatorBackend::Gpu;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "GPU 非连续输入初始化失败");
    std::vector<LocatorResult> results;
    expect(locator.execute(searchView, results) == LocatorError::Ok,
           "GPU 非连续 ROI 搜索失败");
    expect(results.size() == 1, "GPU 非连续 ROI 应返回唯一目标");
    expectNear(results[0].x, 67.5, 1e-9, "GPU 非连续 ROI 中心 x 错误");
    expectNear(results[0].y, 55.5, 1e-9, "GPU 非连续 ROI 中心 y 错误");
}

void testGpuTransformAndPyramid()
{
    const cv::Mat model = makeTexturedImage(24, 18, 16004);
    LocatorConfig transformedConfig;
    transformedConfig.angleMinDeg = 10.0;
    transformedConfig.angleMaxDeg = 10.0;
    transformedConfig.scaleMin = 1.05;
    transformedConfig.scaleMax = 1.05;
    const auto transformed = TransformSearch::makeTransformModels(model, transformedConfig);
    cv::Mat search = makeTexturedImage(160, 140, 16005);
    transformed[0].image.copyTo(search(cv::Rect(60, 50,
        transformed[0].image.cols, transformed[0].image.rows)));

    LocatorConfig config;
    config.angleMinDeg = -10.0;
    config.angleMaxDeg = 10.0;
    config.angleStepDeg = 10.0;
    config.scaleMin = 0.95;
    config.scaleMax = 1.05;
    config.scaleStep = 0.1;
    config.pyramidLevels = 3;
    config.subPixelResolution = 4;
    config.minScore = 90.0;
    compareCpuGpu(model, search, config);
}

void testGpuInvalidInput()
{
    LocatorConfig config;
    config.backend = LocatorBackend::Gpu;
    const cv::Mat constant(12, 12, CV_8UC1, cv::Scalar(128));
    Locator locator;
    expect(locator.initialize(asLocatorImage(constant), config) == LocatorError::InvalidImage,
           "GPU 常量模板应返回 InvalidImage");
    const cv::Mat model = makeTexturedImage(12, 12, 16006);
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "GPU 有效模板初始化失败");
    const cv::Mat zero(40, 40, CV_8UC1, cv::Scalar(0));
    std::vector<LocatorResult> results;
    expect(locator.execute(asLocatorImage(zero), results) == LocatorError::Ok && results.empty(),
           "GPU 全零搜索图不得返回虚假匹配");
    config.useRoi = true;
    config.roi = { 0, 0, 8, 8 };
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::Ok,
           "GPU ROI 初始化失败");
    expect(locator.execute(asLocatorImage(zero), results) == LocatorError::InvalidRoi,
           "GPU 小于模板的 ROI 应返回 InvalidRoi");
}

void testGpuParallelEquivalence()
{
    std::vector<BatchTask> tasks(8);
    std::vector<cv::Mat> models;
    std::vector<cv::Mat> searches;
    for (size_t i = 0; i < tasks.size(); ++i) {
        models.push_back(makeTexturedImage(12, 10, 16100 + static_cast<unsigned int>(i)));
        searches.push_back(makeTexturedImage(70, 60, 16200 + static_cast<unsigned int>(i)));
        models[i].copyTo(searches[i](cv::Rect(10 + static_cast<int>(i), 12, 12, 10)));
        tasks[i].id = static_cast<int>(i);
        tasks[i].model = asLocatorImage(models[i]);
        tasks[i].search = asLocatorImage(searches[i]);
        tasks[i].config.backend = LocatorBackend::Gpu;
    }
    const auto serial = executeBatch(tasks, 1);
    const auto parallel = executeBatch(tasks, 4);
    expect(serial.size() == parallel.size(), "GPU 串并行任务数不一致");
    for (size_t i = 0; i < serial.size(); ++i) {
        expect(serial[i].error == LocatorError::Ok && parallel[i].error == LocatorError::Ok &&
               serial[i].id == parallel[i].id && serial[i].matches.size() == 1 &&
               parallel[i].matches.size() == 1, "GPU 并行任务顺序或执行结果错误");
        expectNear(serial[i].matches[0].x, parallel[i].matches[0].x, 1e-9,
                   "GPU 串并行 x 不一致");
        expectNear(serial[i].matches[0].y, parallel[i].matches[0].y, 1e-9,
                   "GPU 串并行 y 不一致");
        expectNear(serial[i].matches[0].score, parallel[i].matches[0].score, 1e-4,
                   "GPU 串并行分数不一致");
    }
}

void testGpuSyntheticFixedSeed100()
{
    runSyntheticFixedSeed100(LocatorBackend::Gpu, "temp/t11_gpu_synthetic_results.csv");
}

void testGpuRealDataAll197()
{
    runRealDataAll197(LocatorBackend::Gpu, 1e-4);
}

void testGpuConstantSearchWindow()
{
    const cv::Mat model = makeTexturedImage(12, 10, 17003);
    cv::Mat search = makeTexturedImage(40, 40, 17004);
    search(cv::Rect(20, 0, 20, 40)).setTo(cv::Scalar(127));
    const cv::Mat response = NccMatcher::match(search, model, LocatorBackend::Gpu);
    expect(!response.empty() && std::isfinite(response.at<float>(0, 28)),
           "GPU 混合搜索图的 NCC 响应应有效");
    expect(response.at<float>(0, 28) == 0.0f,
           "GPU 常量搜索窗口的零方差 NCC 应为 0");
}

void testGpuUnavailable()
{
    if (std::getenv("LOCATOR_TEST_UNAVAILABLE_CHILD") != nullptr) {
        const cv::Mat model = makeTexturedImage(12, 10, 19200);
        LocatorConfig config;
        config.backend = LocatorBackend::Gpu;
        Locator locator;
        expect(locator.initialize(asLocatorImage(model), config) == LocatorError::BackendUnavailable,
               "隐藏 CUDA 设备后必须返回 BackendUnavailable");
        return;
    }
    expect(runGpuUnavailableIsolated() == 0,
           "独立进程的 GPU 不可用验收失败");
}

void testGpuPerformanceBenchmark500()
{
    runPerformanceBenchmark500(LocatorBackend::Gpu);
}

void testGpuPersistentPerformance500()
{
    runPersistentPerformanceBenchmark(500, LocatorBackend::Gpu);
}

#ifdef LOCATOR_WITH_CUDA
void testGpuBatchResponseEquivalence()
{
    std::vector<cv::Mat> owned;
    std::vector<CudaMatcher::BatchInput> inputs;
    for (int index = 0; index < 3; ++index) {
        cv::Mat model = makeTexturedImage(12 + index, 10 + index,
                                          16500 + static_cast<unsigned int>(index));
        cv::Mat search = makeTexturedImage(70 + index * 3, 60 + index * 2,
                                           16600 + static_cast<unsigned int>(index));
        model.copyTo(search(cv::Rect(10 + index, 12, model.cols, model.rows)));
        cv::Mat padded(search.rows, search.cols + 7, CV_8UC1, cv::Scalar(0));
        search.copyTo(padded(cv::Rect(0, 0, search.cols, search.rows)));
        owned.push_back(model);
        owned.push_back(padded);
        inputs.push_back({ owned[owned.size() - 1](cv::Rect(0, 0, search.cols, search.rows)),
                           owned[owned.size() - 2] });
    }
    const cv::Mat constant(70, 60, CV_8UC1, cv::Scalar(127));
    inputs.push_back({ constant, inputs[0].model });
    const std::vector<cv::Mat> actual = CudaMatcher::matchBatch(inputs);
    expect(actual.size() == inputs.size(), "GPU 批量响应数量错误");
    for (size_t index = 0; index < inputs.size(); ++index) {
        const cv::Mat expected = NccMatcher::match(inputs[index].search, inputs[index].model,
                                                    LocatorBackend::Gpu);
        expect(actual[index].size() == expected.size() && actual[index].type() == expected.type(),
               "GPU 批量响应尺寸或类型错误");
        if (expected.empty()) {
            expect(actual[index].empty(), "GPU 批量常量搜索图必须返回空响应");
            continue;
        }
        expect(cv::norm(actual[index], expected, cv::NORM_INF) == 0.0,
               "GPU 批量与单任务响应不完全一致，索引 " + std::to_string(index));
    }
}
#endif
