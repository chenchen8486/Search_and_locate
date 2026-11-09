#include "BatchData.h"
#include "TestSupport.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <direct.h>
#include <fstream>
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

void testStrictGrayImageLoading()
{
    const std::string directory = batchDataTempDirectory();
    _mkdir(directory.c_str());
    const std::string grayPath = directory + "/valid-gray.bmp";
    const std::string colorPath = directory + "/invalid-color.bmp";
    const std::string brokenPath = directory + "/broken.bmp";
    expect(cv::imwrite(grayPath, cv::Mat(3, 2, CV_8UC1, cv::Scalar(91))),
           "合法灰度测试图写入失败");
    expect(cv::imwrite(colorPath, cv::Mat(3, 2, CV_8UC3, cv::Scalar(1, 2, 3))),
           "彩色测试图写入失败");
    {
        std::ofstream broken(brokenPath, std::ios::binary | std::ios::trunc);
        broken << "BM\\0broken";
    }

    cv::Mat image;
    expect(loadGrayImage(grayPath, image) && image.type() == CV_8UC1 &&
           image.rows == 3 && image.cols == 2, "合法 8 位灰度图读取失败");
    std::string error;
    expect(!loadGrayImage(colorPath, image, &error) && error.find("单通道") != std::string::npos,
           "24 位彩色图必须拒绝并说明原始格式");
    expect(image.rows == 3 && image.cols == 2 && image.type() == CV_8UC1 &&
           cv::countNonZero(image != 91) == 0, "彩色图加载失败后替换了当前有效图像");
    error.clear();
    expect(!loadGrayImage(brokenPath, image, &error) && !error.empty(),
           "损坏图片必须返回明确错误");
    expect(image.rows == 3 && image.cols == 2 && cv::countNonZero(image != 91) == 0,
           "损坏图片加载失败后替换了当前有效图像");

    std::remove(grayPath.c_str());
    std::remove(colorPath.c_str());
    std::remove(brokenPath.c_str());
    _rmdir(directory.c_str());
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
