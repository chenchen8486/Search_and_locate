#include "Locator.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
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

    // 步骤 4：设置搜索参数，默认使用 CPU、角度 0、尺度 1 和 1/8 像素档位。
    LocatorConfig config;
    if (argc == 2 && std::string(argv[1]) == "--gpu") {
        config.backend = LocatorBackend::Gpu;
    } else if (argc != 1) {
        std::cerr << "用法：LocatorExample [--gpu]\n";
        return 2;
    }

    // 步骤 5：构造定位器并预处理模板，后续可重复搜索其他图像。
    Locator locator;
    LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        std::cerr << "初始化失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }

    // 步骤 6：执行搜索，结果包含中心 x、y、角度、尺度和分数。
    std::vector<LocatorResult> results;
    error = locator.execute(search, results);
    if (error != LocatorError::Ok || results.empty()) {
        std::cerr << "搜索失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }
    if (results[0].x != 3.0 || results[0].y != 1.5 || results[0].score < 99.9) {
        std::cerr << "搜索结果与预期不符\n";
        return 1;
    }

    // 步骤 7：读取最佳结果。本例模板来自 (2, 1)，几何中心应为 (3, 1.5)。
    std::cout << "定位成功：x=" << results[0].x << "，y=" << results[0].y
              << "，分数=" << results[0].score << '\n';
    return 0;
}
