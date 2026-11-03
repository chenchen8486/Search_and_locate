#include "Locator.h"

#include <iostream>

int main()
{
    const unsigned char pixels[] = { 0, 64, 128, 255 };
    LocatorImage model = { pixels, 2, 2, 2 };
    LocatorConfig config;
    Locator locator;

    const LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        std::cerr << "Locator 初始化失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }

    std::cout << "Locator 初始化成功，搜索功能将在后续任务提供。\n";
    return 0;
}
