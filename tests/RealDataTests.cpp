#include "BatchData.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <direct.h>
#include <stdexcept>
#include <string>
#include <vector>

void testThickPairs()
{
    const std::vector<ThickPair> pairs = findThickPairs(realDataDirectory());
    expect(pairs.size() == 197, "真实数据应包含 197 对，实际为 " + std::to_string(pairs.size()));

    for (size_t i = 0; i < pairs.size(); ++i) {
        const ThickPair& pair = pairs[i];
        const std::string prefix = "编号 " + std::to_string(pair.id) + "：";
        expect(pair.id == static_cast<int>(i), "数据编号应连续为 0～196 且按升序排列");

        cv::Mat search;
        cv::Mat kernel;
        expect(loadGrayImage(pair.searchPath, search), prefix + "搜索图读取失败");
        expect(loadGrayImage(pair.kernelPath, kernel), prefix + "定位核读取失败");
        expect(search.cols == 118 && search.rows == 118, prefix + "搜索图尺寸应为 118×118");
        expect(kernel.cols == 48 && kernel.rows == 48, prefix + "定位核尺寸应为 48×48");
        expect(search.type() == CV_8UC1, prefix + "搜索图应为 CV_8UC1");
        expect(kernel.type() == CV_8UC1, prefix + "定位核应为 CV_8UC1");
    }
}

void testPairingErrors()
{
    const std::string directory = batchDataTempDirectory();
    _mkdir(directory.c_str());

    cv::Mat pixel(1, 1, CV_8UC1, cv::Scalar(0));
    const std::string searchPath = directory + "/Thick_7.bmp";
    const std::string kernelPath = directory + "/Thick_7_KERNEL.BMP";
    expect(cv::imwrite(searchPath, pixel), "临时搜索图写入失败");

    bool missingPairRejected = false;
    try {
        findThickPairs(directory);
    } catch (const std::runtime_error& error) {
        missingPairRejected = std::string(error.what()).find("Thick_7") != std::string::npos;
    }
    expect(missingPairRejected, "缺失定位核必须报错并指出对应文件");

    expect(cv::imwrite(kernelPath, pixel), "临时定位核写入失败");
    const std::vector<ThickPair> pairs = findThickPairs(directory);
    expect(pairs.size() == 1 && pairs[0].id == 7, "_Kernel 后缀和 BMP 扩展名应不区分大小写");

    cv::Mat image;
    expect(!loadGrayImage(directory + "/NotExist.bmp", image), "不存在的图像路径应返回 false");

    std::remove(searchPath.c_str());
    std::remove(kernelPath.c_str());
    _rmdir(directory.c_str());
}
