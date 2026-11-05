#include "NccMatcher.h"

#include <opencv2/imgproc.hpp>

namespace NccMatcher {

cv::Mat match(const cv::Mat& search, const cv::Mat& model)
{
    if (search.empty() || model.empty() || search.type() != CV_8UC1 ||
        model.type() != CV_8UC1 || model.cols > search.cols || model.rows > search.rows) {
        return cv::Mat();
    }

    double searchMin = 0.0;
    double searchMax = 0.0;
    cv::minMaxLoc(search, &searchMin, &searchMax);
    if (searchMin == searchMax) {
        return cv::Mat();
    }

    cv::Mat response;
    cv::matchTemplate(search, model, response, cv::TM_CCOEFF_NORMED);
    return response;
}

} // namespace NccMatcher
