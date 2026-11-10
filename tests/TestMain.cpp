#include "Locator.h"
#include "TestSupport.h"

#include <windows.h>

#include <exception>
#include <iostream>
#include <string>
#include <vector>

void testThickPairs();
void testPairingErrors();
void testStrictGrayImageLoading();
void testNccExactLocation();
void testNccSameSizeScore();
void testNccSameSizeMatch();
void testNccPaddedStride();
void testNccConstantInput();
void testNccConstantSearchWindow();
void testGpuConstantSearchWindow();
void testMfcWorkflowSingleSearch();
void testMfcWorkflowSingleExceptions();
void testMfcWorkflowBatchTiming();
void testMfcWorkflowBatchCancellation();
void testMfcWorkflowBatchReport();
void testMfcWorkflowFixedBenchmark();
void testMfcWorkflowDirectoryHints();
void testMfcWorkflowDirectoryRoots();
void testMfcWorkflowDirectoryMemory();
void testMfcWorkflowPersistentBatch();
void testRoiRestrictsSearch();
void testRoiInvalidRejected();
void testTopNScoreAndStableOrder();
void testVicinitySuppressesDuplicates();
void testVicinityNegativeRejected();
void testGpuUnavailable();
void testAngleSequence();
void testAngleClockwise();
void testAngleRecoverKnownAngle();
void testScaleSequence();
void testScaleRecoverKnownScale();
void testScaleShrunkModelFits();
void testPyramidCoarseOrderReversal();
void testTransformCombinedSearch();
void testTransformInvalidBounds();
void testTransformLinearShift();
void testPyramidInvalidLevel();
void testPyramidEquivalentResult();
void testPyramidKeepsRequestedRange();
void testSubPixelKnownShift();
void testSubPixelBorderFallback();
void testSubPixelPreparedSamples();
void testSyntheticFixedSeed100();
void testBatchOrderAndErrors();
void testBatchSerialParallelEquivalent();
void testRealDataAll197();
void testPerformanceTaskComposition();
void testPerformanceBenchmark500();
void testPerformancePersistent500();
void testPerformancePersistent600();
void testGpuAvailability();
void testGpuCpuEquivalence();
void testGpuNonContinuousRoi();
void testGpuTransformAndPyramid();
void testGpuInvalidInput();
void testGpuParallelEquivalence();
void testGpuSyntheticFixedSeed100();
void testGpuRealDataAll197();
void testGpuPerformanceBenchmark500();
void testGpuPersistentPerformance500();
void testGpuBatchResponseEquivalence();
void exportRealBaseline();

namespace {

struct TestCase {
    const char* name;
    void (*run)();
};

void testConfigDefaults()
{
    LocatorConfig config;

    expect(config.backend == LocatorBackend::Cpu, "默认后端应为 CPU");
    expect(!config.useRoi, "默认不应启用 ROI");
    expect(config.roi.x == 0 && config.roi.y == 0, "默认 ROI 原点应为 (0, 0)");
    expect(config.roi.width == 0 && config.roi.height == 0, "默认 ROI 尺寸应为 0");
    expect(config.angleMinDeg == 0.0 && config.angleMaxDeg == 0.0, "默认角度范围应为 0");
    expect(config.angleStepDeg == 1.0, "默认角度步长应为 1 度");
    expect(config.scaleMin == 1.0 && config.scaleMax == 1.0, "默认尺度范围应为 1");
    expect(config.scaleStep == 0.01, "默认尺度步长应为 0.01");
    expect(config.pyramidLevels == 1, "默认金字塔层数应为 1");
    expect(config.subPixelResolution == 8, "默认亚像素档位应为 8");
    expect(config.minScore == 0.0, "默认分数阈值应为 0");
    expect(config.maxResults == 1, "默认结果数应为 1");
    expect(config.xVicinity == 0 && config.yVicinity == 0, "默认邻近半径应为 0");
}

void testEmptyImageRejected()
{
    Locator locator;
    LocatorImage image = {};
    LocatorConfig config;

    expect(locator.initialize(image, config) == LocatorError::InvalidImage,
           "空图像指针应返回 InvalidImage");
}

void testExecuteNotInitialized()
{
    Locator locator;
    LocatorImage search = {};
    std::vector<LocatorResult> results(1);

    expect(locator.execute(search, results) == LocatorError::NotInitialized,
           "未初始化执行应返回 NotInitialized");
    expect(results.empty(), "执行入口应清空旧结果");
}

#ifndef LOCATOR_WITH_CUDA
void testBackendGpuUnavailable()
{
    const cv::Mat model = makeTexturedImage(12, 10, 19201);
    LocatorConfig config;
    config.backend = LocatorBackend::Gpu;
    Locator locator;
    expect(locator.initialize(asLocatorImage(model), config) == LocatorError::BackendUnavailable,
           "CPU-only 构建选择 GPU 应返回 BackendUnavailable");
    std::vector<LocatorResult> results(1);
    expect(locator.execute(asLocatorImage(model), results) == LocatorError::NotInitialized &&
           results.empty(), "GPU 初始化失败后不得执行 CPU 搜索或保留旧结果");
}
#endif

bool shouldWaitForConsole(int argumentCount, DWORD consoleProcessCount)
{
    return argumentCount == 1 && consoleProcessCount == 1;
}

void testConsoleDoubleClickHold()
{
    expect(shouldWaitForConsole(1, 1), "双击独立控制台应在测试后等待");
    expect(!shouldWaitForConsole(3, 1), "带测试参数的自动化命令不应等待");
    expect(!shouldWaitForConsole(1, 2), "已有终端中的运行不应等待");
    expect(!shouldWaitForConsole(1, 0), "无控制台环境不应等待");
}

const TestCase tests[] = {
    { "Console.DoubleClickHold", testConsoleDoubleClickHold },
#ifndef LOCATOR_WITH_CUDA
    { "Backend.GpuUnavailable", testBackendGpuUnavailable },
#endif
    { "Config.Defaults", testConfigDefaults },
    { "Image.EmptyRejected", testEmptyImageRejected },
    { "Execute.NotInitialized", testExecuteNotInitialized },
    { "Data.ThickPairs", testThickPairs },
    { "Data.PairingErrors", testPairingErrors },
    { "Data.StrictGrayLoading", testStrictGrayImageLoading },
    { "Ncc.ExactLocation", testNccExactLocation },
    { "Ncc.SameSizeScore", testNccSameSizeScore },
    { "Ncc.SameSizeMatch", testNccSameSizeMatch },
    { "Ncc.PaddedStride", testNccPaddedStride },
    { "Ncc.ConstantInput", testNccConstantInput },
    { "Ncc.ConstantSearchWindow", testNccConstantSearchWindow },
    { "Roi.RestrictsSearch", testRoiRestrictsSearch },
    { "Roi.InvalidRejected", testRoiInvalidRejected },
    { "TopN.ScoreAndStableOrder", testTopNScoreAndStableOrder },
    { "Vicinity.SuppressesDuplicates", testVicinitySuppressesDuplicates },
    { "Vicinity.NegativeRejected", testVicinityNegativeRejected },
    { "Angle.Sequence", testAngleSequence },
    { "Angle.Clockwise", testAngleClockwise },
    { "Angle.RecoverKnownAngle", testAngleRecoverKnownAngle },
    { "Scale.Sequence", testScaleSequence },
    { "Scale.RecoverKnownScale", testScaleRecoverKnownScale },
    { "Scale.ShrunkModelFits", testScaleShrunkModelFits },
    { "Transform.CombinedSearch", testTransformCombinedSearch },
    { "Transform.InvalidBounds", testTransformInvalidBounds },
    { "Transform.LinearShift", testTransformLinearShift },
    { "Pyramid.InvalidLevel", testPyramidInvalidLevel },
    { "Pyramid.EquivalentResult", testPyramidEquivalentResult },
    { "Pyramid.CoarseOrderReversal", testPyramidCoarseOrderReversal },
    { "Pyramid.KeepsRequestedRange", testPyramidKeepsRequestedRange },
    { "SubPixel.KnownShift", testSubPixelKnownShift },
    { "SubPixel.BorderFallback", testSubPixelBorderFallback },
    { "SubPixel.PreparedSamples", testSubPixelPreparedSamples },
    { "Synthetic.FixedSeed100", testSyntheticFixedSeed100 },
    { "Batch.OrderAndErrors", testBatchOrderAndErrors },
    { "Batch.SerialParallelEquivalent", testBatchSerialParallelEquivalent },
    { "RealData.All197", testRealDataAll197 },
    { "Performance.TaskComposition", testPerformanceTaskComposition },
    { "Performance.Benchmark500", testPerformanceBenchmark500 },
    { "Performance.Persistent500", testPerformancePersistent500 },
    { "Performance.Persistent600", testPerformancePersistent600 },
    { "Gpu.Availability", testGpuAvailability },
    { "Gpu.CpuEquivalence", testGpuCpuEquivalence },
    { "Gpu.NonContinuousRoi", testGpuNonContinuousRoi },
    { "Gpu.TransformAndPyramid", testGpuTransformAndPyramid },
    { "Gpu.InvalidInput", testGpuInvalidInput },
    { "Gpu.ParallelEquivalence", testGpuParallelEquivalence },
    { "Gpu.Synthetic100", testGpuSyntheticFixedSeed100 },
    { "Gpu.RealData197", testGpuRealDataAll197 },
    { "Gpu.ConstantSearchWindow", testGpuConstantSearchWindow },
    { "Gpu.Unavailable", testGpuUnavailable },
    { "Gpu.Performance500", testGpuPerformanceBenchmark500 },
    { "Gpu.Persistent500", testGpuPersistentPerformance500 },
#ifdef LOCATOR_WITH_CUDA
    { "Gpu.BatchResponseEquivalence", testGpuBatchResponseEquivalence },
#endif
    { "MfcWorkflow.SingleSearch", testMfcWorkflowSingleSearch },
    { "MfcWorkflow.SingleExceptions", testMfcWorkflowSingleExceptions },
    { "MfcWorkflow.BatchTiming", testMfcWorkflowBatchTiming },
    { "MfcWorkflow.BatchCancellation", testMfcWorkflowBatchCancellation },
    { "MfcWorkflow.BatchReport", testMfcWorkflowBatchReport },
    { "MfcWorkflow.FixedBenchmark", testMfcWorkflowFixedBenchmark },
    { "MfcWorkflow.DirectoryHints", testMfcWorkflowDirectoryHints },
    { "MfcWorkflow.DirectoryRoots", testMfcWorkflowDirectoryRoots },
    { "MfcWorkflow.DirectoryMemory", testMfcWorkflowDirectoryMemory },
    { "MfcWorkflow.PersistentBatch", testMfcWorkflowPersistentBatch }
};

bool isCoreTest(const std::string& name)
{
    return name == "Config.Defaults" || name == "Image.EmptyRejected" ||
           name == "Execute.NotInitialized" || name == "Ncc.ExactLocation" ||
           name == "Ncc.SameSizeScore" || name == "Ncc.SameSizeMatch" ||
           name == "Ncc.PaddedStride" || name == "Ncc.ConstantInput" ||
           name == "Ncc.ConstantSearchWindow" ||
           name == "Roi.RestrictsSearch" || name == "Roi.InvalidRejected" ||
           name == "TopN.ScoreAndStableOrder" || name == "Vicinity.SuppressesDuplicates" ||
           name == "Vicinity.NegativeRejected" ||
           name == "Angle.Sequence" || name == "Angle.Clockwise" ||
           name == "Angle.RecoverKnownAngle" || name == "Scale.Sequence" ||
           name == "Scale.RecoverKnownScale" || name == "Scale.ShrunkModelFits" ||
           name == "Transform.CombinedSearch" ||
           name == "Transform.InvalidBounds" || name == "Transform.LinearShift" ||
           name == "Pyramid.InvalidLevel" || name == "Pyramid.CoarseOrderReversal" ||
           name == "Pyramid.EquivalentResult" || name == "Pyramid.KeepsRequestedRange" ||
           name == "SubPixel.KnownShift" || name == "SubPixel.BorderFallback" ||
           name == "SubPixel.PreparedSamples" ||
           name == "Batch.OrderAndErrors" || name == "Batch.SerialParallelEquivalent";
}

bool shouldRun(const TestCase& test, const std::string& mode, const std::string& value)
{
    if (mode == "--test") {
        return value == test.name;
    }
    if (value == "all") {
        return std::string(test.name).compare(0, 12, "Performance.") != 0 &&
               std::string(test.name).compare(0, 4, "Gpu.") != 0;
    }
    if (value == "gpu") {
        return std::string(test.name).compare(0, 4, "Gpu.") == 0;
    }
    if (value == "core") {
        return isCoreTest(test.name);
    }
    if (value == "synthetic") {
        return test.name == std::string("Synthetic.FixedSeed100");
    }
    if (value == "performance") {
        return std::string(test.name).compare(0, 12, "Performance.") == 0;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string(argv[1]) == "--export-real-baseline") {
        try {
            exportRealBaseline();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "导出真实数据基准失败：" << error.what() << '\n';
            return 1;
        }
    }
    std::string mode = "--suite";
    std::string value = "all";
    if (argc == 3 && (std::string(argv[1]) == "--test" || std::string(argv[1]) == "--suite")) {
        mode = argv[1];
        value = argv[2];
    } else if (argc != 1) {
        std::cerr << "用法：LocatorTests [--test <测试名> | --suite <core|synthetic|all|performance|gpu> | --export-real-baseline]\n";
        return 2;
    }

    int selected = 0;
    int passed = 0;
    for (const TestCase& test : tests) {
        if (!shouldRun(test, mode, value)) {
            continue;
        }
        ++selected;
        try {
            test.run();
            ++passed;
            std::cout << "通过 " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "失败 " << test.name << "：" << error.what() << '\n';
        }
    }

    if (selected == 0) {
        std::cerr << "没有匹配的测试，或该测试套件尚未实现。\n";
        return 2;
    }

    std::cout << "测试结果：" << passed << "/" << selected << " 通过\n";
    const int status = passed == selected ? 0 : 1;
    DWORD consoleProcesses[2] = {};
    const DWORD consoleProcessCount = GetConsoleProcessList(consoleProcesses, 2);
    if (shouldWaitForConsole(argc, consoleProcessCount)) {
        std::cout << "\n测试已结束，按回车键关闭窗口。\n";
        std::cin.get();
    }
    return status;
}
