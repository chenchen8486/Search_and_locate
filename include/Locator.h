#pragma once

#include <memory>
#include <vector>

enum class LocatorError {
    Ok = 0,
    InvalidImage,
    InvalidConfig,
    InvalidRoi,
    NotInitialized,
    BackendUnavailable,
    InternalError
};

enum class LocatorBackend {
    Cpu = 0,
    Gpu
};

// 8 位单通道灰度图。stride 单位为字节，调用方保证数据在调用期间有效。
struct LocatorImage {
    const unsigned char* data;
    int width;
    int height;
    int stride;
};

struct LocatorRect {
    int x;
    int y;
    int width;
    int height;
};

struct LocatorConfig {
    LocatorBackend backend = LocatorBackend::Cpu;
    bool useRoi = false;
    LocatorRect roi = { 0, 0, 0, 0 };
    double angleMinDeg = 0.0;
    double angleMaxDeg = 0.0;
    double angleStepDeg = 1.0;
    double scaleMin = 1.0;
    double scaleMax = 1.0;
    double scaleStep = 0.01;
    int pyramidLevels = 1;
    int subPixelResolution = 8;
    double minScore = 0.0;
    int maxResults = 1;
    int xVicinity = 0;
    int yVicinity = 0;
};

// x、y 为模板几何中心，x 向右、y 向下，正角度为顺时针，scale = 1 为原始尺寸。
struct LocatorResult {
    double x;
    double y;
    double angle;
    double scale;
    double score;
};

class Locator {
public:
    Locator();
    LocatorError initialize(const LocatorImage& model, const LocatorConfig& config);
    LocatorError execute(const LocatorImage& search,
                         std::vector<LocatorResult>& results) const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
