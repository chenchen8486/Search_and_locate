#include "Locator.h"

#include <iostream>
#include <vector>

int main()
{
    // 步骤 1：准备 6×5 的 8 位灰度搜索图，像素保存在连续内存中。
    const int searchWidth = 6;
    const int searchHeight = 5;
    std::vector<unsigned char> pixels(searchWidth * searchHeight);
    for (int y = 0; y < searchHeight; ++y) {
        for (int x = 0; x < searchWidth; ++x) {
            pixels[y * searchWidth + x] = static_cast<unsigned char>(
                (x * 17 + y * 23 + x * y * 19 + x * x * 3 + y * y * 5) % 256);
        }
    }

    // 步骤 2：从搜索图左上角 (2, 1) 截取 3×2 区域作为模板。
    std::vector<unsigned char> modelPixels(3 * 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            modelPixels[y * 3 + x] = pixels[(y + 1) * searchWidth + x + 2];
        }
    }

    // 步骤 3：填写图像视图。本例循环复用同一对图像，实际工程可逐核填写不同指针。
    const LocatorImage model = { modelPixels.data(), 3, 2, 3 };
    const LocatorImage search = { pixels.data(), searchWidth, searchHeight, searchWidth };
    const int taskCount = 500;
    const std::vector<LocatorImage> models(taskCount, model);
    const std::vector<LocatorImage> searches(taskCount, search);

    // 步骤 4：设置公共参数，为每个定位核准备独立对象、错误码和结果数组。
    LocatorConfig config;
    config.minScore = 80.0;
    config.maxResults = 1;
    std::vector<Locator> locators(taskCount);
    std::vector<LocatorError> errors(taskCount);
    std::vector<std::vector<LocatorResult> > allResults(taskCount);

    // 步骤 5：建模阶段只执行一次。实际工程中每个对象可使用不同模板、ROI 和配置。
    for (int i = 0; i < taskCount; ++i) {
        errors[i] = locators[i].initialize(models[i], config);
        if (errors[i] != LocatorError::Ok) {
            std::cerr << "定位核 " << i << " 初始化失败，错误码："
                      << static_cast<int>(errors[i]) << '\n';
            return 1;
        }
    }

    // 步骤 6：实时阶段只更换搜索图并执行，不再构造对象或初始化模板。
#ifdef _OPENMP
    std::cout << "OpenMP 已启用，使用两个工作线程。\n";
#else
    std::cout << "当前构建未启用 OpenMP，循环按串行执行。\n";
#endif
    #pragma omp parallel for num_threads(2) schedule(static)
    for (int i = 0; i < taskCount; ++i) {
        const size_t index = static_cast<size_t>(i);
        errors[index] = locators[index].execute(searches[index], allResults[index]);
    }

    // 步骤 7：并行循环结束后统一检查结果，避免多线程交错输出。
    for (size_t i = 0; i < searches.size(); ++i) {
        if (errors[i] != LocatorError::Ok) {
            std::cerr << "定位核 " << i << " 执行失败，错误码："
                      << static_cast<int>(errors[i]) << '\n';
            return 1;
        }
        if (allResults[i].empty()) {
            std::cerr << "定位核 " << i << " 没有匹配结果。\n";
            return 1;
        }

        const LocatorResult& best = allResults[i][0];
        if (best.x != 3.0 || best.y != 1.5 || best.angle != 0.0 ||
            best.scale != 1.0 || best.score < 99.9) {
            std::cerr << "定位核 " << i << " 的搜索结果与预期不符。\n";
            return 1;
        }
        std::cout << "定位核 " << i << "：x=" << best.x << "，y=" << best.y
                  << "，角度=" << best.angle << "，尺度=" << best.scale
                  << "，分数=" << best.score << '\n';
    }
    return 0;
}
