# Search_and_locate 搜索匹配定位库

　　Search_and_locate 是面向 8 位单通道灰度图的 C++ 模板匹配定位库。核心算法采用归一化互相关（NCC），支持 CPU 和可选 CUDA 后端，并提供单次搜索、批量执行和 MFC 可视化工具。

　　核心库以源码集成，不依赖 Sapera 或其他商业视觉 SDK。调用方准备模板图和搜索图，设置 `LocatorConfig` 后即可执行匹配。公开接口和带步骤注释的用例见 [API 调用手册](docs/API调用手册.md)。

## 1. 主要功能

- 图像输入：接收像素指针、宽、高和行跨度，支持行尾有填充字节的灰度图。
- 搜索配置：支持矩形 ROI、多角度、多尺度、金字塔和亚像素定位。
- 匹配结果：返回模板中心坐标、顺时针角度、缩放倍率及 0～100 分匹配分数。
- 多目标定位：支持最低分过滤、Top-N 和邻近结果去重。
- 批量执行：支持 OpenMP 任务级并行、逐任务错误隔离及分阶段计时。
- 双后端：默认使用 CPU，也可请求 CUDA。GPU 不可用时返回错误，不自动切换到 CPU。
- MFC 工具：支持单次搜索、鼠标框选 ROI、结果叠加、批量评估及结果图像联动。

　　本期不支持彩色图、模板遮罩、边缘匹配、Blob、OCR、标定和 Sapera ABI 兼容。公共接口不依赖 OpenCV 图像对象，核心实现使用 OpenCV。

## 2. 环境依赖

- 操作系统：Windows 10 64 位。
- 编译环境：Visual Studio 2019、v142 工具集，保持默认 C++14 语言模式。
- OpenCV：使用支持 VS2019 的 OpenCV 4 或更高版本。工程当前配置为 OpenCV 4.10.0 vc16，更换版本时需同步调整库名。
- CPU 构建：不需要 CUDA。
- GPU 构建：CUDA Toolkit 11.4 或更高版本，以及兼容的 NVIDIA 显卡和驱动。工程默认使用 CUDA 11.4 的 Visual Studio 集成文件。使用更高版本时，需要修改 `tools/Cuda.props` 中版本化的 `.props`、`.targets` 文件名及 `CudaTargetsPath`，并确认工具包与 VS2019、显卡驱动兼容。
- MFC 工具：需要在 Visual Studio 中安装 MFC 和 ATL 组件。只调用核心算法时不需要这些组件。
- 批量并行：在 Visual Studio 中启用 OpenMP 后使用任务级并行。未启用 OpenMP 时可串行编译和运行。

## 3. 工程目录

```text
Search_and_locate/
├─ Locator.sln                         Visual Studio 2019 解决方案
├─ include/                            核心公共头文件
│  ├─ Locator.h                        单图搜索接口
│  └─ BatchLocator.h                   批量搜索接口
├─ src/                                核心算法与内部实现
│  └─ gpu/                             可选 CUDA 实现
├─ examples/                           三种全链路调用示例及 VS 工程
│  ├─ MinimalExample.cpp               单图搜索完整示例
│  ├─ BatchExample.cpp                 批量搜索完整示例
│  └─ OpenMPLoopExample.cpp            多模板常驻 OpenMP 完整示例
├─ mfc/                                MFC 演示与批量评估工具
├─ tools/                              OpenCV、CUDA 属性表与数据辅助代码
├─ docs/
│  └─ API调用手册.md                   公共接口、参数和调用用例
└─ build/x64/Release/                  正式 Release 可执行程序
```

　　核心库不生成独立的静态库或动态库。接入方可参考 `examples/LocatorExample.vcxproj`，将公共头文件和所需源码加入自己的工程。

## 4. 在 Visual Studio 中构建

### 4.1 配置与生成

1. 使用 Visual Studio 2019 打开 `Locator.sln`，将解决方案配置设为 `Release`，平台设为 `x64`。
2. 在属性管理器中配置用户宏 `OpenCVRoot`，值为 OpenCV 的 `build` 目录，例如 `C:\opencv\build`。也可通过 `OPENCV_DIR` 环境变量指定该目录。
3. 只使用 CPU 时保持 `EnableCuda=false`。需要 GPU 时将该属性设为 `true`，并配置 `CUDA_PATH` 或 `CudaToolkitRoot`。详细属性见 `tools/Cuda.props`。
4. 选择“生成解决方案”。可执行程序统一输出到工程根目录下的 `build/x64/Release/`。

### 4.2 可执行程序位置

- 最小示例：`build/x64/Release/LocatorExample.exe`。
- MFC 工具：`build/x64/Release/LocatorMfc.exe`。

　　使用 GPU 构建后，可将 `LocatorExample` 项目属性的“调试 → 命令参数”设为 `--gpu`，再运行示例。MFC 工具则在界面顶部选择 CPU 或 GPU。

## 5. 最小调用

　　以下代码演示核心调用顺序。`model` 是模板图，`search` 是搜索图，两者均为已经填写好的 `LocatorImage`。图像视图的构造方法见 API 手册第 3.1 节。

```cpp
#include "Locator.h"
#include <vector>

LocatorError locateOne(const LocatorImage& model, const LocatorImage& search,
                       std::vector<LocatorResult>& results)
{
    // 步骤 1：设置基础搜索参数，这里只找一个至少 80 分的目标。
    LocatorConfig config;
    config.minScore = 80.0;
    config.maxResults = 1;

    // 步骤 2：构造定位器并预处理模板。
    Locator locator;
    const LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        return error;
    }

    // 步骤 3：执行搜索，通过 results 返回中心、角度、尺度和分数。
    return locator.execute(search, results);
}
```

　　无匹配时返回 `LocatorError::Ok` 和空数组。批量任务、外部 OpenMP 循环及结果读取见 [API 调用手册第 2 章](docs/API调用手册.md#2-快速上手)。

## 6. MFC 工具操作

1. 分别加载 8 位灰度搜索图和模板图，在顶部选择 CPU 或 GPU。
2. 在“搜索参数”页设置角度、尺度、金字塔、亚像素、最低分、Top-N 和去重半径。
3. 需要限制搜索范围时，点击“设置 ROI”，在搜索图上拖动框选。“清除 ROI”恢复完整图像。
4. 执行单次搜索。结果列表与图像中的匹配框联动，滚轮缩放图像，搜索图可用中键平移。
5. 在“批量评估”页选择图像目录，设置任务数和线程数后运行。任务数为 0 表示执行全部真实配对。批量使用“搜索参数”页中的当前配置。
6. 点击批量结果行，可查看对应搜索图、模板和匹配框。无匹配与执行错误可分别筛选。
7. “复位（初始状态）”恢复当前图片对加载齐全时的状态，不自动搜索，也不修改批量设置。

　　目录选择器首次定位工程根，之后记住上次成功选择的目录，记忆保存在程序旁的 `LocatorMfc.ini`。

## 7. 调用索引

　　三个示例都是独立完整的 `.cpp` 文件，包含 `main`、内存图生成、图像视图、参数配置、实际调用、错误检查和结果输出。按业务场景选择对应文件。

| 示例 | 完整源码 | 功能与特色 | 适用场景 |
| --- | --- | --- | --- |
| Demo 1：多模板常驻并行 | [OpenMPLoopExample.cpp](examples/OpenMPLoopExample.cpp) | 500 个 `Locator` 常驻。建模时分别 `initialize` 一次，实时循环用 OpenMP 并行调用 `execute`，逐核保存错误码和结果 | 500～600 个定位核连续检测、印刷机实时定位、复用 Sapera 式建模结果的生产流程 |
| Demo 2：单图搜索 | [MinimalExample.cpp](examples/MinimalExample.cpp) | 一个 `Locator` 完成模板初始化、单图搜索和最佳结果读取。支持 `--gpu` 参数，现有 `LocatorExample` 工程可直接构建 | 单模板单图搜索、集成验证、CPU 或 GPU 功能检查 |
| Demo 3：一次性批量 | [BatchExample.cpp](examples/BatchExample.cpp) | 构造 500 个 `BatchTask`，一次 `executeBatch` 并行完成各任务的初始化和搜索。返回任务编号、错误码、匹配结果及分阶段耗时 | 一次性批处理、离线评估、每批模板或配置不同且无需跨帧复用的任务 |

　　连续实时帧优先使用常驻 OpenMP 示例。第 5 章的 `locateOne` 每次调用都会构造 `Locator` 并执行 `initialize`，适合单次调用。N 个定位核反复处理实时帧时，不要逐帧重复执行该函数。

　　三个文件应分别构建，不要加入同一个可执行程序。`BatchExample.cpp` 需要额外编译 `src/BatchLocator.cpp`。`OpenMPLoopExample.cpp` 在未启用 OpenMP 时按串行执行。接口参数和构建细节见 [API 调用手册第 2 章](docs/API调用手册.md#2-快速上手)。

## 8. 作者

　　作者：CHEN.C。

## 9. 许可证

　　本项目为闭源专有项目，版权所有并保留所有权利。未经授权不得使用、复制、修改或分发。完整条款见 [LICENSE](LICENSE)。
