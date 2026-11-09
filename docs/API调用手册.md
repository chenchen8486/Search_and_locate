# Locator API 调用手册

　　本手册以 `include/Locator.h` 和 `include/BatchLocator.h` 为准，覆盖全部公共类型、三个核心调用函数及 `tools/BatchData.h` 的三个数据辅助函数。示例采用 C++14，默认从工程根目录组织文件。示例中的 `D:/data/Search` 是自备图片目录的占位路径，使用前需替换为实际目录，仓库不提供测试图片。

- `Locator::initialize`：校验配置并预处理模板。
- `Locator::execute`：在一幅搜索图中执行定位。
- `executeBatch`：并行执行一组相互独立的定位任务。
- `loadGrayImage`、`findThickPairs`、`makeBenchmarkTasks`：可选的文件与测试数据辅助接口。

　　`src/` 下的 NCC、金字塔和结果精化函数属于内部实现。`mfc/LocatorWorkflow.h` 服务于界面及界面测试，不属于核心库的稳定公共 API。集成方无需调用这些内部函数。

## 1. 接口速查

| 调用 | 头文件 | 作用 |
| --- | --- | --- |
| `Locator locator;` | `Locator.h` | 创建未初始化的定位器 |
| `locator.initialize(model, config)` | `Locator.h` | 预处理模板并保存配置 |
| `locator.execute(search, results)` | `Locator.h` | 同步执行单次定位 |
| `executeBatch(tasks, threadCount)` | `BatchLocator.h` | 同步执行批量定位 |
| `loadGrayImage(path, image, error)` | `tools/BatchData.h` | 读取并严格检查灰度图片 |
| `findThickPairs(directory)` | `tools/BatchData.h` | 配对 Thick 搜索图与模板 |
| `makeBenchmarkTasks(pairs, count)` | `tools/BatchData.h` | 构造循环复用的批量任务 |

　　公共类型和函数位于全局命名空间。调用单次搜索只需包含 `Locator.h`，调用批量接口只需包含 `BatchLocator.h`，后者已包含前者。

## 2. 公共数据类型

### 2.1 LocatorImage

```cpp
struct LocatorImage {
    const unsigned char* data;
    int width;
    int height;
    int stride;
};
```

| 字段 | 含义与要求 |
| --- | --- |
| `data` | 第一行第一个有效像素的地址，不能为空 |
| `width` | 每行有效像素数，必须大于 0 |
| `height` | 图像行数，必须大于 0 |
| `stride` | 相邻行起始地址之间的字节数，必须大于或等于 `width` |

　　每个像素占 1 字节。行尾允许有填充字节，不支持负行跨度。`LocatorImage` 只是内存视图，不拥有像素，也不会释放像素。

```cpp
const int width = 640;
const int height = 480;
const int stride = 672;
std::vector<unsigned char> pixels(stride * height, 0);

LocatorImage image = {};
image.data = pixels.data();
image.width = width;
image.height = height;
image.stride = stride;
```

　　模板内存在 `initialize` 返回后可以释放或修改，定位器会保存预处理结果。搜索图必须保持有效且内容不变，直到 `execute` 返回。批量任务的全部模板和搜索图必须保持有效且内容不变，直到 `executeBatch` 返回。

### 2.2 LocatorRect

```cpp
struct LocatorRect {
    int x;
    int y;
    int width;
    int height;
};
```

　　ROI 使用搜索图的原图像素坐标，左上角为 `(x, y)`，宽和高分别为 `width`、`height`。启用后，矩形必须完整位于搜索图内，宽高均大于 0，并能容纳每个实际变换后的模板。

### 2.3 LocatorConfig

　　使用 `LocatorConfig config;` 可获得下表中的默认值。修改配置后必须重新调用 `initialize`。

| 字段 | 默认值 | 合法值与作用 |
| --- | --- | --- |
| `backend` | `LocatorBackend::Cpu` | 选择 CPU 或 GPU 后端 |
| `useRoi` | `false` | `false` 使用完整搜索图，`true` 使用 `roi` |
| `roi` | `{ 0, 0, 0, 0 }` | 搜索图上的矩形 ROI，仅在 `useRoi=true` 时生效 |
| `angleMinDeg` | `0.0` | 最小角度，单位为度，必须有限且不大于最大值 |
| `angleMaxDeg` | `0.0` | 最大角度，单位为度，必须有限且不小于最小值 |
| `angleStepDeg` | `1.0` | 角度步长，必须为有限正数 |
| `scaleMin` | `1.0` | 最小缩放倍率，必须为有限正数且不大于最大值 |
| `scaleMax` | `1.0` | 最大缩放倍率，必须为有限正数且不小于最小值 |
| `scaleStep` | `0.01` | 缩放步长，必须为有限正数 |
| `pyramidLevels` | `1` | 金字塔层数，至少为 1，且图像必须能构建对应层数 |
| `subPixelResolution` | `8` | 只支持 1、2、4、8，量化单位分别为 1、1/2、1/4、1/8 像素 |
| `minScore` | `0.0` | 最低匹配分数，范围为 0～100，等于阈值的候选保留 |
| `maxResults` | `1` | 最大返回结果数，至少为 1 |
| `xVicinity` | `0` | 中心点 x 方向的邻近抑制半径，单位为像素，不能小于 0 |
| `yVicinity` | `0` | 中心点 y 方向的邻近抑制半径，单位为像素，不能小于 0 |

　　角度从最小值开始按步长递增，尺度同理。范围不能整除步长时，最后生成的候选不超过最大值，最大值不一定会被单独追加。例如 `0～0.55、步长 0.1` 的最后一个角度是 `0.5`。

　　`scale=1` 表示原始模板尺寸，其他倍率基于原始模板缩放。结果中的 `scale` 是搜索候选的请求倍率，不是缩放后像素尺寸的反算比值。缩放后宽高会取整，空模板或模板无法放入搜索区域会返回错误。

　　`maxResults>1` 时，若两个中心的 x 差和 y 差分别不超过对应半径，后一个候选会被抑制。半径为 0 不会抑制不同中心，相同中心仍会去重。

### 2.4 LocatorResult

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `x` | `double` | 匹配模板几何中心在搜索图中的 x 坐标，向右为正 |
| `y` | `double` | 匹配模板几何中心在搜索图中的 y 坐标，向下为正 |
| `angle` | `double` | 实际候选角度，单位为度，正值表示顺时针 |
| `scale` | `double` | 实际候选缩放倍率，1 为原始尺寸 |
| `score` | `double` | 匹配分数，范围为 0～100 |

　　模板中心按 `(width-1)/2`、`(height-1)/2` 计算，偶数尺寸模板可以产生半像素中心。使用 ROI 时，结果仍转换回完整搜索图的坐标。

　　分数由 `max(0.0, ncc)*100.0` 得到。结果按分数降序、x 升序、y 升序、角度升序、尺度升序排列，返回数量不超过 `maxResults`。

## 3. 后端与错误码

### 3.1 LocatorBackend

| 枚举值 | 含义 |
| --- | --- |
| `LocatorBackend::Cpu` | 默认后端，使用 CPU 计算 |
| `LocatorBackend::Gpu` | 请求 CUDA 后端，未编译 CUDA 或设备不可用时返回 `BackendUnavailable` |

　　GPU 后端不会自动切换为 CPU。输入 `LocatorImage` 仍指向主机内存，上传、下载和同步由实现处理。当前接口没有设备索引参数，多设备环境需在调用前设置 CUDA 当前设备。

### 3.2 LocatorError

| 枚举值 | 数值 | 常见原因 |
| --- | ---: | --- |
| `Ok` | 0 | 调用成功。无匹配时也可能返回该值 |
| `InvalidImage` | 1 | 空图像、非法尺寸或行跨度、常量模板、变换模板大于完整搜索图 |
| `InvalidConfig` | 2 | 非法范围、步长、阈值、Top-N、亚像素、金字塔或邻近半径 |
| `InvalidRoi` | 3 | ROI 为空、越界或无法容纳变换后的模板 |
| `NotInitialized` | 4 | 尚未成功初始化便调用 `execute` |
| `BackendUnavailable` | 5 | 后端值无效、未启用 CUDA 或 CUDA 设备不可用 |
| `InternalError` | 6 | CUDA 执行失败或批量任务中的未分类异常 |

　　核心接口使用错误码，不提供错误码转字符串函数。调用方可按下例映射日志：

```cpp
const char* errorText(LocatorError error)
{
    switch (error) {
    case LocatorError::Ok: return "成功";
    case LocatorError::InvalidImage: return "图像无效";
    case LocatorError::InvalidConfig: return "配置无效";
    case LocatorError::InvalidRoi: return "ROI 无效";
    case LocatorError::NotInitialized: return "定位器尚未初始化";
    case LocatorError::BackendUnavailable: return "请求的后端不可用";
    case LocatorError::InternalError: return "内部执行错误";
    }
    return "未知错误";
}
```

## 4. 单次搜索

### 4.1 Locator 构造

```cpp
Locator locator;
```

　　构造后对象处于未初始化状态。析构由 C++ 自动管理，不需要额外的创建、释放或设备关闭函数。

### 4.2 initialize

```cpp
LocatorError initialize(const LocatorImage& model, const LocatorConfig& config);
```

| 参数 | 说明 |
| --- | --- |
| `model` | 原始 8 位灰度模板，必须包含灰度变化，不能接受常量模板 |
| `config` | 后端、ROI、角度、尺度、精度和结果筛选配置 |

　　返回 `Ok` 后，同一对象可对多幅搜索图重复执行 `execute`，不必重复生成变换模板和模板金字塔。ROI 不检查模板尺寸是否合法，因为实际搜索图要等 `execute` 才确定。

　　重新初始化会替换已有配置和模板。若重复初始化在参数、后端或图像校验处失败，已经存在的旧状态可能仍然保留。为避免把旧结果误认为新模板结果，推荐使用新 `Locator` 完成初始化，成功后再替换旧对象。

### 4.3 execute

```cpp
LocatorError execute(const LocatorImage& search,
                     std::vector<LocatorResult>& results) const;
```

| 参数 | 说明 |
| --- | --- |
| `search` | 待搜索的 8 位灰度图，函数不会修改图像 |
| `results` | 输出数组。进入函数时清空旧内容，结束时按既定规则排序 |

　　该函数同步返回。`Ok` 且 `results.empty()` 表示执行成功但没有满足阈值的结果，全零搜索图也属于这种情况。任何错误返回后都不要使用 `results` 中的匹配数据。

### 4.4 完整内存图示例

　　下例不依赖文件路径。模板取自搜索图左上角 `(2, 1)` 的 3×2 区域，因此最佳中心为 `(3, 1.5)`。

```cpp
#include "Locator.h"

#include <iostream>
#include <vector>

int main()
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

    const LocatorImage model = { modelPixels.data(), 3, 2, 3 };
    const LocatorImage search = { pixels.data(), searchWidth, searchHeight, searchWidth };
    const LocatorConfig config;
    Locator locator;

    LocatorError error = locator.initialize(model, config);
    if (error != LocatorError::Ok) {
        std::cerr << "初始化失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }

    std::vector<LocatorResult> results;
    error = locator.execute(search, results);
    if (error != LocatorError::Ok) {
        std::cerr << "搜索失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }
    if (results.empty()) {
        std::cout << "搜索成功，但没有匹配结果。\n";
        return 0;
    }

    const LocatorResult& best = results[0];
    std::cout << "x=" << best.x << "，y=" << best.y
              << "，角度=" << best.angle << "，尺度=" << best.scale
              << "，分数=" << best.score << '\n';
    return 0;
}
```

　　项目中的 [`examples/MinimalExample.cpp`](../examples/MinimalExample.cpp) 还演示了 `--gpu` 参数和预期结果断言。

## 5. 常用配置用例

### 5.1 从文件读取灰度图

　　核心接口不负责文件解码。项目提供 `loadGrayImage`，只接受原始解码结果为 `CV_8UC1` 的文件，不会把彩色图静默转换成灰度图。

```cpp
#include "Locator.h"
#include "BatchData.h"

#include <opencv2/core.hpp>
#include <iostream>
#include <string>

LocatorImage asImage(const cv::Mat& image)
{
    return { image.data, image.cols, image.rows, static_cast<int>(image.step) };
}

int main()
{
    cv::Mat modelMat;
    cv::Mat searchMat;
    std::string diagnostic;
    if (!loadGrayImage("D:/data/Search/Thick_0_Kernel.bmp", modelMat, &diagnostic) ||
        !loadGrayImage("D:/data/Search/Thick_0.bmp", searchMat, &diagnostic)) {
        std::cerr << diagnostic << '\n';
        return 1;
    }

    const LocatorConfig config;
    Locator locator;
    const LocatorError initError = locator.initialize(asImage(modelMat), config);
    if (initError != LocatorError::Ok) {
        std::cerr << "初始化失败，错误码：" << static_cast<int>(initError) << '\n';
        return 1;
    }

    std::vector<LocatorResult> results;
    const LocatorError error = locator.execute(asImage(searchMat), results);
    if (error != LocatorError::Ok) {
        std::cerr << "搜索失败，错误码：" << static_cast<int>(error) << '\n';
        return 1;
    }
    std::cout << "匹配数量：" << results.size() << '\n';
    return 0;
}
```

　　`asImage` 是本手册的适配函数，不是库函数。自行适配 `cv::Mat` 时，必须先确认 `!image.empty()`、`image.type()==CV_8UC1`，并确保 `image.step` 能由 `int` 表示。`cv::Mat` 可以是非连续 ROI 视图，`stride` 必须使用 `image.step`，不能写成 `image.cols`。

　　使用 `loadGrayImage` 时，除了核心源文件，还需加入 `tools/BatchData.cpp` 并链接 OpenCV 图像编解码库。

### 5.2 ROI、多角度、多尺度与多目标

```cpp
LocatorConfig config;
config.useRoi = true;
config.roi = { 10, 20, 300, 240 };
config.angleMinDeg = -10.0;
config.angleMaxDeg = 10.0;
config.angleStepDeg = 0.5;
config.scaleMin = 0.9;
config.scaleMax = 1.1;
config.scaleStep = 0.02;
config.pyramidLevels = 1;
config.subPixelResolution = 8;
config.minScore = 80.0;
config.maxResults = 5;
config.xVicinity = 8;
config.yVicinity = 8;

Locator locator;
LocatorError error = locator.initialize(model, config);
std::vector<LocatorResult> results;
if (error == LocatorError::Ok) {
    error = locator.execute(search, results);
}
```

　　ROI 在搜索图上生效，不在模板上生效。若 `pyramidLevels>1`，请求的层数必须能由变换模板和搜索区域实际构建，否则返回 `InvalidConfig`，不会自动减少层数。大图可尝试多层搜索，小图的预处理开销可能抵消加速收益。

### 5.3 复用同一模板处理多幅图像

```cpp
Locator locator;
const LocatorError initError = locator.initialize(model, config);
if (initError != LocatorError::Ok) {
    return initError;
}

for (const LocatorImage& search : searches) {
    std::vector<LocatorResult> results;
    const LocatorError error = locator.execute(search, results);
    if (error != LocatorError::Ok) {
        return error;
    }
    // 在这里消费当前 search 对应的 results。
}
return LocatorError::Ok;
```

　　`searches` 是调用方准备的 `std::vector<LocatorImage>`。所有图像必须保持有效到各自调用结束。需要更换模板、ROI 或搜索参数时，重新初始化定位器。

### 5.4 请求 GPU

```cpp
LocatorConfig config;
config.backend = LocatorBackend::Gpu;
config.minScore = 80.0;

Locator locator;
LocatorError error = locator.initialize(model, config);
if (error == LocatorError::BackendUnavailable) {
    // 当前构建没有 CUDA，或没有可用 CUDA 设备。
    return error;
}
if (error != LocatorError::Ok) {
    return error;
}

std::vector<LocatorResult> results;
error = locator.execute(search, results);
return error;
```

　　GPU 后端执行核心 NCC，模板变换、候选筛选和亚像素局部精化仍在 CPU。双后端允许存在浮点舍入差异，应按项目回归阈值比较结果。

## 6. 批量搜索

### 6.1 BatchTask

```cpp
struct BatchTask {
    int id;
    LocatorImage model;
    LocatorImage search;
    LocatorConfig config;
};
```

| 字段 | 含义 |
| --- | --- |
| `id` | 调用方任务编号，原样复制到结果，不要求连续或唯一 |
| `model` | 本任务的模板图像 |
| `search` | 本任务的搜索图像 |
| `config` | 本任务的完整搜索配置，各任务可以不同 |

　　手工创建任务时可用 `BatchTask task = {};`，再填写四个字段。每个任务拥有独立的模板和配置，批量入口会为每个任务独立初始化定位器。

### 6.2 BatchResult

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | `int` | 对应输入 `BatchTask::id` |
| `error` | `LocatorError` | 本任务的错误码，不会用其他任务的结果替代 |
| `matches` | `std::vector<LocatorResult>` | 本任务的匹配结果 |
| `preprocessMs` | `double` | 模板预处理耗时，单位为毫秒 |
| `searchMs` | `double` | 搜索执行耗时，单位为毫秒 |
| `totalMs` | `double` | 本任务预处理与搜索的整体耗时，单位为毫秒 |

　　结果按输入任务顺序返回，不按 `id` 或分数排序。`error==Ok` 且 `matches.empty()` 表示无匹配。错误任务的匹配数组为空，错误不会中断其他任务。

　　这些耗时来自各工作线程的单调时钟，不能直接相加得到批量墙钟耗时。符合 GPU 打包条件的任务会把组内 GPU 时间均摊到各任务，因此还需在 `executeBatch` 外部测量整体耗时。计时用于评估，不是性能保证。

### 6.3 executeBatch

```cpp
std::vector<BatchResult> executeBatch(const std::vector<BatchTask>& tasks,
                                      int threadCount);
```

| 参数 | 说明 |
| --- | --- |
| `tasks` | 任务数组。空数组返回空结果 |
| `threadCount` | 大于 0 时指定工作线程数。小于或等于 0 时，OpenMP 构建使用默认线程数，无 OpenMP 的构建使用 1 |

　　该函数同步返回，公共接口没有进度回调或取消参数。`threadCount=1` 可请求串行执行。批量调用期间不要修改任务、图像或底层像素。

### 6.4 完整批量用例

　　下例使用项目的 Thick 数据辅助函数。`data` 保有全部图像，必须活过 `executeBatch`。

```cpp
#include "BatchData.h"
#include "BatchLocator.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <vector>

int main()
{
    try {
        const std::vector<ThickPair> pairs = findThickPairs("D:/data/Search");
        BatchDataSet data = makeBenchmarkTasks(pairs, 8);
        for (BatchTask& task : data.tasks) {
            task.config.minScore = 80.0;
            task.config.maxResults = 2;
            task.config.xVicinity = 8;
            task.config.yVicinity = 8;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<BatchResult> results = executeBatch(data.tasks, 4);
        const double wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        for (size_t i = 0; i < results.size(); ++i) {
            const BatchResult& result = results[i];
            std::cout << "任务 " << result.id
                      << "，错误码=" << static_cast<int>(result.error)
                      << "，匹配数=" << result.matches.size()
                      << "，预处理=" << result.preprocessMs << " ms"
                      << "，搜索=" << result.searchMs << " ms" << '\n';
            if (result.id != data.tasks[i].id) {
                std::cerr << "结果顺序与输入不一致。\n";
                return 1;
            }
        }
        std::cout << "批量墙钟耗时：" << wallMs << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "批量数据准备失败：" << error.what() << '\n';
        return 1;
    }
}
```

　　CPU 批量按任务并行。CUDA 构建中，全部任务同时满足单层、无 ROI、单角度、单尺度和 GPU 后端时，实现按 4 个任务一组打包计算。其他配置仍走 GPU 路径，不回退 CPU。

## 7. Thick 数据辅助接口

　　本节接口位于 `tools/BatchData.h`，需要 `tools/BatchData.cpp` 和 OpenCV。这些函数用于测试数据和文件适配，不影响只使用内存图的核心接口。

### 7.1 loadGrayImage

```cpp
bool loadGrayImage(const std::string& path, cv::Mat& image,
                   std::string* error = nullptr);
```

- `path`：图片路径。
- `image`：输出图像。加载失败时不修改原图像。
- `error`：可选诊断字符串，调用开始会清空，失败时写入原因。

　　返回 `true` 表示文件存在、解码成功且类型为 `CV_8UC1`，否则返回 `false`。完整用例见第 5.1 节。

### 7.2 ThickPair 与 findThickPairs

```cpp
struct ThickPair {
    int id;
    std::string searchPath;
    std::string kernelPath;
};

std::vector<ThickPair> findThickPairs(const std::string& directory);
```

| 字段 | 含义 |
| --- | --- |
| `id` | 从文件名解析出的 Thick 编号 |
| `searchPath` | 搜索图路径 |
| `kernelPath` | 同编号的模板图路径 |

　　函数扫描指定目录的第一级文件，不递归子目录。文件名按不区分大小写的 `Thick_<编号>.bmp` 与 `Thick_<编号>_Kernel.bmp` 配对，不符合命名的文件会被忽略。返回结果按编号升序排列。

```cpp
try {
    const std::vector<ThickPair> pairs = findThickPairs("D:/data/Search");
    for (const ThickPair& pair : pairs) {
        std::cout << pair.id << "：" << pair.searchPath
                  << "，" << pair.kernelPath << '\n';
    }
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
}
```

　　目录不可读、没有找到配对、缺少一方、重复配对或编号超出 `int` 范围时抛出异常。辅助函数使用异常报告数据准备错误，与核心搜索接口的错误码不同。

### 7.3 BatchDataSet 与 makeBenchmarkTasks

```cpp
struct BatchDataSet {
    std::vector<cv::Mat> models;
    std::vector<cv::Mat> searches;
    std::vector<BatchTask> tasks;
};

BatchDataSet makeBenchmarkTasks(const std::vector<ThickPair>& pairs, int count);
```

| 参数或字段 | 含义 |
| --- | --- |
| `pairs` | `findThickPairs` 返回的配对 |
| `count` | 目标任务数，不能小于 0。为 0 时返回空数据集 |
| `models` | 按任务保存模板图像，负责内存保活 |
| `searches` | 按任务保存搜索图像，负责内存保活 |
| `tasks` | 指向上述图像的任务数组，使用默认 `LocatorConfig` |

　　`count>0` 且 `pairs` 为空时抛出 `std::invalid_argument`。图像读取失败时抛出 `std::runtime_error`。任务按 `pairs[i % pairs.size()]` 循环复用，`task.id` 是从 0 开始的任务索引，不是原始 Thick 编号。

```cpp
BatchDataSet data = makeBenchmarkTasks(pairs, 500);
for (BatchTask& task : data.tasks) {
    task.config.subPixelResolution = 4;
}
const std::vector<BatchResult> results = executeBatch(data.tasks, 20);
```

　　500 个任务使用 197 对独立图像时，会把这 197 对按序号循环展开，不代表有 500 对独立图像。创建任务后不要增删 `models`、`searches` 或替换其中的 `cv::Mat`，以免任务指针失效。保留 `data` 到全部结果消费结束后再释放最稳妥。

## 8. 生命周期、线程与异常边界

- `Locator` 没有显式销毁接口，离开作用域自动释放资源。
- 单次输入内存只需覆盖当前调用。`initialize` 返回后模板可被定位器独立保存。
- 批量输入内存覆盖整个 `executeBatch`，不要只让指针覆盖任务数组的构造过程。
- 推荐每个并行任务使用独立 `Locator`。不要在其他线程执行同一对象或其副本时调用 `initialize`，副本共享内部状态。
- 公共批量接口同步执行，没有取消、进度、异步句柄或回调注册函数。
- 常规图像和配置错误通过 `LocatorError` 返回。批量把任务执行中的未分类异常映射为 `InternalError`。
- 数据辅助函数用异常报告配对、参数和文件读取问题，调用方应捕获 `std::exception`。
- 资源耗尽等进程级问题不属于常规参数错误，不能依赖错误码替代调用方的资源管理。

## 9. 常见误判与排查

| 现象 | 检查方法 |
| --- | --- |
| 返回 `Ok` 但没有结果 | 检查 `minScore`、模板与搜索图内容。全零搜索图会正常返回空结果 |
| 初始化返回 `InvalidImage` | 检查空指针、宽高、`stride>=width`，以及模板是否为常量图 |
| 执行返回 `InvalidRoi` | 检查 ROI 是否完整在搜索图内，以及所有旋转、缩放后的模板能否放入 ROI |
| 初始化返回 `InvalidConfig` | 检查范围是否反向、步长是否大于 0、尺度是否大于 0、亚像素是否为 1/2/4/8 |
| GPU 返回 `BackendUnavailable` | 检查是否使用 `EnableCuda=true` 构建、CUDA 运行时是否可加载、当前设备是否可用 |
| 批量结果看似错位 | 按返回数组的输入位置核对，不要假定 `id` 连续、唯一或已排序 |
| 批量耗时之和大于实际等待时间 | 区分并行任务耗时之和与批量墙钟耗时，后者需在函数外部测量 |
| 非连续图结果异常 | 检查是否把 `width` 误传为 `stride`，以及行尾内存是否有效 |
| 修改配置没有生效 | 修改 `LocatorConfig` 后重新调用 `initialize`，再执行搜索 |
| 重新初始化失败后又出现旧结果 | 旧对象可能保留上次成功状态。使用新对象初始化，成功后再替换 |

## 10. Visual Studio 集成与验证入口

1. 将 `include/` 和 `src/` 加入项目的包含目录。使用数据辅助接口时再加入 `tools/`。
2. 单次搜索加入 `Locator.cpp`、`NccMatcher.cpp`、`ResultRefiner.cpp`、`TransformSearch.cpp`、`PyramidSearch.cpp`，并保留 `src/` 下的内部头文件。
3. 批量搜索额外加入 `BatchLocator.cpp`。使用 `BatchData.h` 时额外加入 `tools/BatchData.cpp`。
4. 按现有示例工程配置 OpenCV 的包含目录、库目录、链接库和运行时 DLL。当前工程使用 `opencv_world4100.lib`，Debug 使用 `opencv_world4100d.lib`。
5. CPU 构建保持 `EnableCuda=false`，不加入 `.cu` 文件。GPU 构建按 `tools/Cuda.props` 配置 CUDA，加入 `src/gpu/CudaMatcher.cu`，定义 `LOCATOR_WITH_CUDA` 并链接 `cudart.lib`。
6. 可直接打开 [`examples/LocatorExample.vcxproj`](../examples/LocatorExample.vcxproj) 对照配置，或将解决方案中的 `LocatorExample` 设为启动项目运行。

　　测试参数通过 Visual Studio 项目属性的“调试 → 命令参数”设置，无需使用命令行构建。工作目录设为工程根目录，测试图片放在调用方自己的目录中。

- 最小内存图调用见 [`examples/MinimalExample.cpp`](../examples/MinimalExample.cpp)。
- 批量串并行、错误隔离和顺序检查见 [`tests/PerformanceTests.cpp`](../tests/PerformanceTests.cpp)。
- 坐标、ROI、角度、尺度和边界用例见 [`tests/CoreTests.cpp`](../tests/CoreTests.cpp)。
- 测试套件名称与注册用例见 [`tests/TestMain.cpp`](../tests/TestMain.cpp)。

　　接口或行为调整时，应同步修改公共头文件、本手册和调用示例，并通过对应功能回归。
