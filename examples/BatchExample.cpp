#include "BatchLocator.h"

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

    // 步骤 3：填写图像视图，四个字段依次为像素地址、宽、高和行跨度。
    const LocatorImage model = { modelPixels.data(), 3, 2, 3 };
    const LocatorImage search = { pixels.data(), searchWidth, searchHeight, searchWidth };

    // 步骤 4：设置每个任务共用的搜索参数。
    LocatorConfig config;
    config.minScore = 80.0;
    config.maxResults = 1;

    // 步骤 5：构造 500 个一次性批量任务。本例循环复用同一对只读图像，实际工程可为每项填写不同图像。
    const int taskCount = 500;
    std::vector<BatchTask> tasks;
    tasks.reserve(static_cast<size_t>(taskCount));
    for (int i = 0; i < taskCount; ++i) {
        BatchTask task = {};
        task.id = i;
        task.model = model;
        task.search = search;
        task.config = config;
        tasks.push_back(task);
    }

    // 步骤 6：使用两个工作线程同步执行批量搜索。
    const std::vector<BatchResult> results = executeBatch(tasks, 2);
    if (results.size() != tasks.size()) {
        std::cerr << "批量结果数量与任务数量不一致。\n";
        return 1;
    }

    // 步骤 7：逐个检查任务错误码并输出最佳结果。
    for (const BatchResult& result : results) {
        if (result.error != LocatorError::Ok) {
            std::cerr << "任务 " << result.id << " 执行失败，错误码："
                      << static_cast<int>(result.error) << '\n';
            return 1;
        }
        if (result.matches.empty()) {
            std::cerr << "任务 " << result.id << " 没有匹配结果。\n";
            return 1;
        }

        const LocatorResult& best = result.matches[0];
        if (best.x != 3.0 || best.y != 1.5 || best.angle != 0.0 ||
            best.scale != 1.0 || best.score < 99.9) {
            std::cerr << "任务 " << result.id << " 的搜索结果与预期不符。\n";
            return 1;
        }
        std::cout << "任务 " << result.id << "：x=" << best.x << "，y=" << best.y
                  << "，角度=" << best.angle << "，尺度=" << best.scale
                  << "，分数=" << best.score << '\n';
    }
    return 0;
}
