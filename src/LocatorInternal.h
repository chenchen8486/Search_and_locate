#pragma once

#include "Locator.h"

#include <opencv2/core.hpp>

namespace locator_internal {

inline bool isValidImage(const LocatorImage& image)
{
    return image.data != nullptr && image.width > 0 && image.height > 0 &&
           image.stride >= image.width;
}

inline cv::Mat makeView(const LocatorImage& image)
{
    return cv::Mat(image.height, image.width, CV_8UC1,
                   const_cast<unsigned char*>(image.data),
                   static_cast<size_t>(image.stride));
}

} // namespace locator_internal
