#include "CudaMatcher.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>

namespace CudaMatcher {
namespace {

__global__ void nccKernel(const unsigned char* search, int searchWidth,
                          const unsigned char* model, int modelWidth, int modelHeight,
                          float* response, int responseWidth, int responseHeight)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= responseWidth || y >= responseHeight) {
        return;
    }

    const int count = modelWidth * modelHeight;
    double modelSum = 0.0;
    double searchSum = 0.0;
    for (int row = 0; row < modelHeight; ++row) {
        for (int col = 0; col < modelWidth; ++col) {
            modelSum += model[row * modelWidth + col];
            searchSum += search[(y + row) * searchWidth + x + col];
        }
    }
    const double modelMean = modelSum / count;
    const double searchMean = searchSum / count;
    double modelVariance = 0.0;
    double searchVariance = 0.0;
    double covariance = 0.0;
    for (int row = 0; row < modelHeight; ++row) {
        for (int col = 0; col < modelWidth; ++col) {
            const double modelValue = model[row * modelWidth + col] - modelMean;
            const double searchValue = search[(y + row) * searchWidth + x + col] - searchMean;
            modelVariance += modelValue * modelValue;
            searchVariance += searchValue * searchValue;
            covariance += modelValue * searchValue;
        }
    }
    double value = 0.0;
    if (modelVariance > 1e-12 && searchVariance > 1e-12) {
        value = covariance / sqrt(modelVariance * searchVariance);
        value = max(-1.0, min(1.0, value));
    }
    response[y * responseWidth + x] = static_cast<float>(value);
}

class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t bytes)
    {
        const cudaError_t error = cudaMalloc(&data_, bytes);
        if (error != cudaSuccess) {
            throw Failure(cudaGetErrorString(error));
        }
    }

    ~DeviceBuffer()
    {
        cudaFree(data_);
    }

    void* get() const
    {
        return data_;
    }

private:
    void* data_ = nullptr;
};

void check(cudaError_t error)
{
    if (error != cudaSuccess) {
        throw Failure(cudaGetErrorString(error));
    }
}

} // namespace

bool isAvailable()
{
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        cudaGetLastError();
        return false;
    }
    return count > 0;
}

cv::Mat match(const cv::Mat& search, const cv::Mat& model)
{
    if (!isAvailable()) {
        throw Unavailable();
    }
    cv::Mat continuousSearch = search.isContinuous() ? search : search.clone();
    cv::Mat continuousModel = model.isContinuous() ? model : model.clone();
    const int responseWidth = search.cols - model.cols + 1;
    const int responseHeight = search.rows - model.rows + 1;
    cv::Mat response(responseHeight, responseWidth, CV_32FC1);
    DeviceBuffer deviceSearch(continuousSearch.total());
    DeviceBuffer deviceModel(continuousModel.total());
    DeviceBuffer deviceResponse(response.total() * response.elemSize());
    check(cudaMemcpy(deviceSearch.get(), continuousSearch.data, continuousSearch.total(),
                     cudaMemcpyHostToDevice));
    check(cudaMemcpy(deviceModel.get(), continuousModel.data, continuousModel.total(),
                     cudaMemcpyHostToDevice));
    const dim3 block(16, 16);
    const dim3 grid((responseWidth + block.x - 1) / block.x,
                    (responseHeight + block.y - 1) / block.y);
    nccKernel<<<grid, block>>>(
        static_cast<const unsigned char*>(deviceSearch.get()), search.cols,
        static_cast<const unsigned char*>(deviceModel.get()), model.cols, model.rows,
        static_cast<float*>(deviceResponse.get()), responseWidth, responseHeight);
    check(cudaGetLastError());
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(response.data, deviceResponse.get(),
                     response.total() * response.elemSize(), cudaMemcpyDeviceToHost));
    return response;
}

} // namespace CudaMatcher
