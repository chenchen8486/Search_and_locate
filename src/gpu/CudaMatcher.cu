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
    float modelSum = 0.0f;
    float searchSum = 0.0f;
    for (int row = 0; row < modelHeight; ++row) {
        for (int col = 0; col < modelWidth; ++col) {
            modelSum += model[row * modelWidth + col];
            searchSum += search[(y + row) * searchWidth + x + col];
        }
    }
    const float modelMean = modelSum / count;
    const float searchMean = searchSum / count;
    float modelVariance = 0.0f;
    float searchVariance = 0.0f;
    float covariance = 0.0f;
    for (int row = 0; row < modelHeight; ++row) {
        for (int col = 0; col < modelWidth; ++col) {
            const float modelValue = model[row * modelWidth + col] - modelMean;
            const float searchValue = search[(y + row) * searchWidth + x + col] - searchMean;
            modelVariance += modelValue * modelValue;
            searchVariance += searchValue * searchValue;
            covariance += modelValue * searchValue;
        }
    }
    float value = 0.0f;
    if (modelVariance > 1e-12f && searchVariance > 1e-12f) {
        value = covariance / sqrtf(modelVariance * searchVariance);
        value = fmaxf(-1.0f, fminf(1.0f, value));
    }
    response[y * responseWidth + x] = value;
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

class Stream {
public:
    Stream()
    {
        check(cudaStreamCreate(&stream_));
    }

    ~Stream()
    {
        cudaStreamDestroy(stream_);
    }

    cudaStream_t get() const
    {
        return stream_;
    }

private:
    cudaStream_t stream_ = nullptr;
};

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
    Stream stream;
    DeviceBuffer deviceSearch(continuousSearch.total());
    DeviceBuffer deviceModel(continuousModel.total());
    DeviceBuffer deviceResponse(response.total() * response.elemSize());
    check(cudaMemcpyAsync(deviceSearch.get(), continuousSearch.data, continuousSearch.total(),
                          cudaMemcpyHostToDevice, stream.get()));
    check(cudaMemcpyAsync(deviceModel.get(), continuousModel.data, continuousModel.total(),
                          cudaMemcpyHostToDevice, stream.get()));
    const dim3 block(16, 16);
    const dim3 grid((responseWidth + block.x - 1) / block.x,
                    (responseHeight + block.y - 1) / block.y);
    nccKernel<<<grid, block, 0, stream.get()>>>(
        static_cast<const unsigned char*>(deviceSearch.get()), search.cols,
        static_cast<const unsigned char*>(deviceModel.get()), model.cols, model.rows,
        static_cast<float*>(deviceResponse.get()), responseWidth, responseHeight);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(response.data, deviceResponse.get(),
                          response.total() * response.elemSize(), cudaMemcpyDeviceToHost,
                          stream.get()));
    check(cudaStreamSynchronize(stream.get()));
    return response;
}

} // namespace CudaMatcher
