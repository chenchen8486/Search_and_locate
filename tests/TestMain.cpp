#include "Locator.h"
#include "TestSupport.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

void testThickPairs();
void testPairingErrors();
void testNccExactLocation();
void testNccPaddedStride();
void testNccConstantInput();
void testRoiRestrictsSearch();
void testRoiInvalidRejected();
void testTopNScoreAndStableOrder();
void testVicinitySuppressesDuplicates();
void testAngleSequence();
void testAngleClockwise();
void testAngleRecoverKnownAngle();
void testScaleSequence();
void testScaleRecoverKnownScale();
void testTransformCombinedSearch();
void testTransformInvalidBounds();
void testPyramidInvalidLevel();
void testPyramidEquivalentResult();
void testPyramidKeepsRequestedRange();
void testSubPixelKnownShift();
void testSubPixelBorderFallback();
void testSyntheticFixedSeed100();

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

const TestCase tests[] = {
    { "Config.Defaults", testConfigDefaults },
    { "Image.EmptyRejected", testEmptyImageRejected },
    { "Execute.NotInitialized", testExecuteNotInitialized },
    { "Data.ThickPairs", testThickPairs },
    { "Data.PairingErrors", testPairingErrors },
    { "Ncc.ExactLocation", testNccExactLocation },
    { "Ncc.PaddedStride", testNccPaddedStride },
    { "Ncc.ConstantInput", testNccConstantInput },
    { "Roi.RestrictsSearch", testRoiRestrictsSearch },
    { "Roi.InvalidRejected", testRoiInvalidRejected },
    { "TopN.ScoreAndStableOrder", testTopNScoreAndStableOrder },
    { "Vicinity.SuppressesDuplicates", testVicinitySuppressesDuplicates },
    { "Angle.Sequence", testAngleSequence },
    { "Angle.Clockwise", testAngleClockwise },
    { "Angle.RecoverKnownAngle", testAngleRecoverKnownAngle },
    { "Scale.Sequence", testScaleSequence },
    { "Scale.RecoverKnownScale", testScaleRecoverKnownScale },
    { "Transform.CombinedSearch", testTransformCombinedSearch },
    { "Transform.InvalidBounds", testTransformInvalidBounds },
    { "Pyramid.InvalidLevel", testPyramidInvalidLevel },
    { "Pyramid.EquivalentResult", testPyramidEquivalentResult },
    { "Pyramid.KeepsRequestedRange", testPyramidKeepsRequestedRange },
    { "SubPixel.KnownShift", testSubPixelKnownShift },
    { "SubPixel.BorderFallback", testSubPixelBorderFallback },
    { "Synthetic.FixedSeed100", testSyntheticFixedSeed100 }
};

bool isCoreTest(const std::string& name)
{
    return name == "Config.Defaults" || name == "Image.EmptyRejected" ||
           name == "Execute.NotInitialized" || name == "Ncc.ExactLocation" ||
           name == "Ncc.PaddedStride" || name == "Ncc.ConstantInput" ||
           name == "Roi.RestrictsSearch" || name == "Roi.InvalidRejected" ||
           name == "TopN.ScoreAndStableOrder" || name == "Vicinity.SuppressesDuplicates" ||
           name == "Angle.Sequence" || name == "Angle.Clockwise" ||
           name == "Angle.RecoverKnownAngle" || name == "Scale.Sequence" ||
           name == "Scale.RecoverKnownScale" || name == "Transform.CombinedSearch" ||
           name == "Transform.InvalidBounds" || name == "Pyramid.InvalidLevel" ||
           name == "Pyramid.EquivalentResult" || name == "Pyramid.KeepsRequestedRange" ||
           name == "SubPixel.KnownShift" || name == "SubPixel.BorderFallback";
}

bool shouldRun(const TestCase& test, const std::string& mode, const std::string& value)
{
    if (mode == "--test") {
        return value == test.name;
    }
    if (value == "all") {
        return true;
    }
    if (value == "core") {
        return isCoreTest(test.name);
    }
    if (value == "synthetic") {
        return test.name == std::string("Synthetic.FixedSeed100");
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    std::string mode = "--suite";
    std::string value = "all";
    if (argc == 3 && (std::string(argv[1]) == "--test" || std::string(argv[1]) == "--suite")) {
        mode = argv[1];
        value = argv[2];
    } else if (argc != 1) {
        std::cerr << "用法：LocatorTests [--test <测试名> | --suite <core|synthetic|all|performance|gpu>]\n";
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
    return passed == selected ? 0 : 1;
}
