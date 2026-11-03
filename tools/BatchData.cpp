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

bool loadGrayImage(const std::string& path, cv::Mat& image)
{
    image.release();
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input.good()) {
        return false;
    }
    input.close();

    try {
        image = cv::imread(path, cv::IMREAD_GRAYSCALE);
    } catch (const cv::Exception&) {
        image.release();
    }
    return !image.empty() && image.type() == CV_8UC1;
}
