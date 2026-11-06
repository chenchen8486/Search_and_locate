#include "Locator.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    const int searchWidth = 6;
    const int searchHeight = 5;
    std::vector<unsigned char> pixels(searchWidth * searchHeight);
    for (int y = 0; y < searchHeight; ++y) {
        for (int x = 0; x < searchWidth; ++x) {
            pixels[y * searchWidth + x] = static_cast<unsigned char>(
                (x * 17 + y * 23 + x * y * 19 + x * x * 3 + y * y * 5) % 256);
        }
    }

    std::vector<unsigned char> modelPixels(3 * 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            modelPixels[y * 3 + x] = pixels[(y + 1) * searchWidth + x + 2];
        }
    }

    LocatorImage model = { modelPixels.data(), 3, 2, 3 };
    LocatorImage search = { pixels.data(), searchWidth, searchHeight, searchWidth };
    LocatorConfig config;
    if (argc == 2 && std::string(argv[1]) == "--gpu") {
        config.backend = LocatorBackend::Gpu;
    } else if (argc != 1) {
        std::cerr << "用法：LocatorExample [--gpu]\n";
        return 2;
    }
    Locator locator;

    LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        std::cerr << "初始化失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }

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

    std::cout << "定位成功：x=" << results[0].x << "，y=" << results[0].y
              << "，分数=" << results[0].score << '\n';
    return 0;
}
