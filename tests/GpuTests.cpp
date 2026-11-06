#include "Locator.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>

#include <vector>

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

    LocatorConfig cpuConfig;
    LocatorConfig gpuConfig = cpuConfig;
    gpuConfig.backend = LocatorBackend::Gpu;
    Locator cpu;
    Locator gpu;
    expect(cpu.initialize(asLocatorImage(model), cpuConfig) == LocatorError::Ok,
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
        expectNear(actual[i].score, expected[i].score, 1e-4, "CPU/GPU 分数不一致");
    }
}
