# Locator 搜索匹配定位库

　　Locator 是用于 8 位单通道灰度图的 C++ 模板匹配定位库，采用归一化互相关（NCC），提供 CPU 和可选 CUDA 后端。核心算法、批量接口、控制台测试和 MFC 演示工具共用同一套源码，不依赖 Sapera 或商业视觉 SDK。

　　核心库以源码集成，不提供核心静态库或动态库。公开接口、全部配置字段、错误码和调用用例见 [API 调用手册](docs/API调用手册.md)。

## 功能

| 功能 | 说明 |
| --- | --- |
| 图像输入 | 接收指针、宽、高和行跨度，支持非连续行。只接受 8 位单通道灰度图 |
| 模板匹配 | NCC 分数映射到 0～100，支持最低分数过滤和模板预处理复用 |
| 搜索范围 | 支持完整图像或矩形 ROI、多角度、多尺度和金字塔搜索 |
| 定位结果 | 返回模板中心、顺时针角度、缩放倍率和分数，支持 1、1/2、1/4、1/8 像素档位 |
| 多目标 | 支持 Top-N、邻近去重和稳定排序 |
| 批量执行 | 支持 OpenMP 任务级并行、逐任务错误隔离、结果顺序保持和分阶段计时 |
| 双后端 | 默认 CPU，可选 CUDA。GPU 不可用时返回错误，不自动回退 CPU |
| MFC 工具 | 提供单次搜索、鼠标 ROI、结果叠加、批量评估、结果筛选和图像联动 |

　　本期不支持彩色图、模板遮罩、边缘匹配、Blob、OCR、标定和 Sapera ABI 兼容。公共接口不要求 OpenCV 图像对象，核心实现使用 OpenCV。

## 环境

- Windows 10、Visual Studio 2019、v142 工具集和默认 C++14 模式是开发基线，当前桌面回归在 Windows 11 上完成。
- 已验证 OpenCV 4.10.0 vc16。其他 OpenCV 4.x 版本需要同步调整工程库名并重新验证。
- CPU 构建不需要 CUDA，GPU 构建已验证 CUDA Toolkit 11.4。
- MFC 工具需要 Visual Studio 的 MFC 和 ATL 组件，核心算法不依赖 MFC。
- 500 任务 20 ms 的性能目标尚未达到，需求指定的 CPU、GPU 和 Windows 10 环境仍需实测。

## 目录结构

```text
Locator.sln                         Visual Studio 2019 解决方案
├─ include/                         核心公共头文件
│  ├─ Locator.h                     单张搜索接口
│  └─ BatchLocator.h                批量搜索接口
├─ src/                             核心算法与内部实现
│  └─ gpu/                          可选 CUDA 实现
├─ examples/                        最小调用示例及 VS 工程
├─ mfc/                             MFC 演示与批量评估工具
├─ tests/                           核心、GPU、精度及性能测试
├─ tools/                           OpenCV、CUDA 属性表与数据辅助代码
├─ docs/
│  └─ API调用手册.md                公共接口及调用用例
├─ build/x64/Release/               正式 Release 可执行程序
└─ temp/                            构建中间文件与测试输出
```

　　`temp/` 不作为程序交付目录。测试图片由开发者自备，不属于源码目录。

## 在 Visual Studio 中构建

1. 使用 Visual Studio 2019 打开 `Locator.sln`，将解决方案配置设为 `Release`，平台设为 `x64`。
2. 在属性管理器中配置用户宏 `OpenCVRoot`，值为 OpenCV 的 `build` 目录，例如 `C:\opencv\build`。也可通过 `OPENCV_DIR` 环境变量指定该目录。相关属性位于 `tools/OpenCV.props`。
3. 只使用 CPU 时保持 `EnableCuda=false`。需要 GPU 时将该属性设为 `true`，并按 `tools/Cuda.props` 配置 CUDA 11.4 路径。
4. 使用“生成解决方案”构建全部工程。可执行程序统一输出到 `build/x64/Release/`，包括 `LocatorExample.exe`、`LocatorTests.exe` 和 `LocatorMfc.exe`。

　　接入现有工程时，可参考 `examples/LocatorExample.vcxproj` 的包含目录、源文件清单和 OpenCV 配置。使用批量接口还需加入 `src/BatchLocator.cpp`。完整说明见 [API 调用手册](docs/API调用手册.md)。

## 运行示例与 MFC 工具

- 最小示例：在解决方案资源管理器中右击 `LocatorExample`，选择“设为启动项目”，再选择“开始执行(不调试)”。示例使用内存图，预期输出 `x=3、y=1.5、分数=100`。
- GPU 示例：在项目属性的“调试 → 命令参数”中填写 `--gpu`，要求使用 CUDA 构建并有可用 NVIDIA 设备。
- MFC 工具：将 `LocatorMfc` 设为启动项目并运行，也可直接打开 `build/x64/Release/LocatorMfc.exe`。

## 最小调用

```cpp
#include "Locator.h"
#include <vector>

LocatorError locate(const LocatorImage& model, const LocatorImage& search,
                    std::vector<LocatorResult>& results)
{
    LocatorConfig config;
    config.minScore = 80.0;
    config.maxResults = 1;

    Locator locator;
    const LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        return error;
    }
    return locator.execute(search, results);
}
```

　　`initialize` 预处理模板，`execute` 执行一次搜索。无匹配时返回 `Ok` 和空数组。图像内存在调用期间由调用方保活，修改配置后需要重新初始化。

## MFC 工具使用

1. 分别加载 8 位灰度搜索图和模板图，选择 CPU 或 GPU。
2. 在“搜索参数”页设置角度、尺度、金字塔、亚像素、最低分、Top-N 和去重半径。
3. 需要限制搜索范围时，点击“设置 ROI”并在搜索图上拖动框选。“清除 ROI”恢复完整图像。
4. 执行单次搜索，结果列表与图像中的匹配框联动。滚轮缩放图像，搜索图可用中键平移。
5. 在“批量评估”页选择自备的数据目录，设置任务数和线程数后运行。任务数为 0 表示执行全部真实配对。
6. 点击任一批量结果行可查看对应搜索图、模板和匹配框。无匹配与执行错误可分别筛选。
7. “复位（初始状态）”恢复当前图片对加载齐全时的状态，不自动搜索，也不修改批量设置。

　　批量目录选择器首次定位工程根，之后记住上次成功选择的目录，记忆保存在程序旁的 `LocatorMfc.ini`。批量沿用“搜索参数”页的实际配置，执行前可在批量页核对摘要。

## 在 Visual Studio 中测试

　　将 `LocatorTests` 设为启动项目，在项目属性的“调试 → 命令参数”中填写下表参数，然后运行。工作目录设为工程根目录。

| 命令参数 | 用途 |
| --- | --- |
| `--suite core` | 核心功能与边界回归 |
| `--suite synthetic` | 固定种子的合成图精度回归 |
| `--suite all` | CPU 全量回归，不含性能和 GPU 用例 |
| `--suite performance` | 500 任务性能测试，预热 5 轮、记录 50 轮 |
| `--suite gpu` | GPU 精度、一致性及性能回归，需要 CUDA 构建和设备 |
| `--test <测试名>` | 运行单项测试，测试名见 `tests/TestMain.cpp` |

　　真实数据测试需要自备 Thick 配对图片。`--suite performance` 将实际配对循环组成 500 个任务，不代表有 500 对独立图片。测试图片的命名和批量构造方法见 API 手册。

　　MFC 自测需将 `LocatorMfc` 设为启动项目，命令参数填写 `--self-test <实际Thick数据目录>`。自测包含 CPU 和 GPU 检查，仅 CPU 构建无需运行。界面功能也可以通过正常启动逐项操作检查。

## 调用索引

- [API 调用手册](docs/API调用手册.md)：全部公共接口、参数、错误处理，以及单张、GPU、批量和文件读取用例。
- [最小调用示例](examples/MinimalExample.cpp)：可直接编译运行的内存图定位代码。
- [测试入口](tests/TestMain.cpp)：测试套件名称、命令参数和已注册用例。
