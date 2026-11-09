#include "BatchData.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string fileName(const std::string& path)
{
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool parseThickFile(const std::string& path, int& id, bool& kernel)
{
    std::string name = toLower(fileName(path));
    if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".bmp") != 0) {
        return false;
    }
    name.erase(name.size() - 4);

    kernel = false;
    const std::string suffix = "_kernel";
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        kernel = true;
        name.erase(name.size() - suffix.size());
    }

    const std::string prefix = "thick_";
    if (name.compare(0, prefix.size(), prefix) != 0 || name.size() == prefix.size()) {
        return false;
    }

    int value = 0;
    for (size_t i = prefix.size(); i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (!std::isdigit(ch)) {
            return false;
        }
        const int digit = name[i] - '0';
        if (value > (std::numeric_limits<int>::max() - digit) / 10) {
            throw std::runtime_error("数据编号超出范围：" + fileName(path));
        }
        value = value * 10 + digit;
    }
    id = value;
    return true;
}

} // namespace

std::vector<ThickPair> findThickPairs(const std::string& directory)
{
    std::vector<std::string> paths;
    try {
        cv::glob(directory + "/*", paths, false);
    } catch (const cv::Exception& error) {
        throw std::runtime_error("无法读取数据目录 " + directory + "：" + error.what());
    }

    std::map<int, ThickPair> indexed;
    for (size_t i = 0; i < paths.size(); ++i) {
        int id = 0;
        bool kernel = false;
        if (!parseThickFile(paths[i], id, kernel)) {
            continue;
        }

        ThickPair& pair = indexed[id];
        pair.id = id;
        std::string& target = kernel ? pair.kernelPath : pair.searchPath;
        if (!target.empty()) {
            throw std::runtime_error("数据配对重复：Thick_" + std::to_string(id) +
                                     (kernel ? "_Kernel" : ""));
        }
        target = paths[i];
    }

    std::vector<ThickPair> pairs;
    for (std::map<int, ThickPair>::const_iterator it = indexed.begin(); it != indexed.end(); ++it) {
        const ThickPair& pair = it->second;
        if (pair.searchPath.empty() || pair.kernelPath.empty()) {
            throw std::runtime_error("数据配对不完整：Thick_" + std::to_string(pair.id) +
                                     (pair.searchPath.empty() ? " 缺少搜索图" : " 缺少定位核"));
        }
        pairs.push_back(pair);
    }
    if (pairs.empty()) {
        throw std::runtime_error("目录中未找到 Thick BMP 数据：" + directory);
    }
    return pairs;
}

bool loadGrayImage(const std::string& path, cv::Mat& image, std::string* error)
{
    if (error != nullptr) {
        error->clear();
    }
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input.good()) {
        if (error != nullptr) {
            *error = "文件不存在或无法读取：" + path;
        }
        return false;
    }
    input.close();

    cv::Mat decoded;
    try {
        decoded = cv::imread(path, cv::IMREAD_UNCHANGED);
    } catch (const cv::Exception& exception) {
        if (error != nullptr) {
            *error = std::string("图片解码异常：") + exception.what();
        }
        return false;
    }
    if (decoded.empty()) {
        if (error != nullptr) {
            *error = "图片损坏或格式无法解码：" + path;
        }
        return false;
    }
    if (decoded.type() != CV_8UC1) {
        if (error != nullptr) {
            *error = "原始图片不是 8 位单通道，通道数为 " + std::to_string(decoded.channels()) +
                     "，深度类型为 " + std::to_string(decoded.depth());
        }
        return false;
    }
    image = decoded;
    return true;
}

BatchDataSet makeBenchmarkTasks(const std::vector<ThickPair>& pairs, int count)
{
    if (count < 0 || (count > 0 && pairs.empty())) {
        throw std::invalid_argument("批量任务数量或输入配对无效");
    }
    BatchDataSet data;
    data.models.reserve(count);
    data.searches.reserve(count);
    data.tasks.reserve(count);
    for (int index = 0; index < count; ++index) {
        const ThickPair& pair = pairs[index % pairs.size()];
        cv::Mat model;
        cv::Mat search;
        if (!loadGrayImage(pair.kernelPath, model) || !loadGrayImage(pair.searchPath, search)) {
            throw std::runtime_error("批量图像读取失败：Thick_" + std::to_string(pair.id));
        }
        data.models.push_back(model);
        data.searches.push_back(search);

        BatchTask task = {};
        task.id = index;
        task.model = { data.models[index].data, data.models[index].cols,
                       data.models[index].rows, static_cast<int>(data.models[index].step) };
        task.search = { data.searches[index].data, data.searches[index].cols,
                        data.searches[index].rows, static_cast<int>(data.searches[index].step) };
        data.tasks.push_back(task);
    }
    return data;
}
