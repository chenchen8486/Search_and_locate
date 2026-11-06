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

void check(cudaError_t error)
{
    if (error != cudaSuccess) {
        throw Failure(cudaGetErrorString(error));
    }
}

class Workspace {
public:
    Workspace()
    {
        check(cudaStreamCreate(&stream_));
    }

    ~Workspace()
    {
        cudaFree(search_);
        cudaFree(model_);
        cudaFree(response_);
        cudaStreamDestroy(stream_);
    }

    void reserveSearch(size_t bytes)
    {
        reserve(search_, searchBytes_, bytes);
    }

    void reserveModel(size_t bytes)
    {
        reserve(model_, modelBytes_, bytes);
    }

    void reserveResponse(size_t bytes)
    {
        reserve(response_, responseBytes_, bytes);
    }

    cudaStream_t stream() const
    {
        return stream_;
    }

    void* search() const
    {
        return search_;
    }

    void* model() const
    {
        return model_;
    }

    void* response() const
    {
        return response_;
    }

private:
    static void reserve(void*& buffer, size_t& capacity, size_t bytes)
    {
        if (bytes <= capacity) {
            return;
        }
        cudaFree(buffer);
        buffer = nullptr;
        capacity = 0;
        check(cudaMalloc(&buffer, bytes));
        capacity = bytes;
    }

    cudaStream_t stream_ = nullptr;
    void* search_ = nullptr;
    void* model_ = nullptr;
    void* response_ = nullptr;
    size_t searchBytes_ = 0;
    size_t modelBytes_ = 0;
    size_t responseBytes_ = 0;
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
    thread_local Workspace workspace;
    workspace.reserveSearch(continuousSearch.total());
    workspace.reserveModel(continuousModel.total());
    workspace.reserveResponse(response.total() * response.elemSize());
    check(cudaMemcpyAsync(workspace.search(), continuousSearch.data, continuousSearch.total(),
                          cudaMemcpyHostToDevice, workspace.stream()));
    check(cudaMemcpyAsync(workspace.model(), continuousModel.data, continuousModel.total(),
                          cudaMemcpyHostToDevice, workspace.stream()));
    const dim3 block(16, 16);
    const dim3 grid((responseWidth + block.x - 1) / block.x,
                    (responseHeight + block.y - 1) / block.y);
    nccKernel<<<grid, block, 0, workspace.stream()>>>(
        static_cast<const unsigned char*>(workspace.search()), search.cols,
        static_cast<const unsigned char*>(workspace.model()), model.cols, model.rows,
        static_cast<float*>(workspace.response()), responseWidth, responseHeight);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(response.data, workspace.response(),
                          response.total() * response.elemSize(), cudaMemcpyDeviceToHost,
                          workspace.stream()));
    check(cudaStreamSynchronize(workspace.stream()));
    return response;
}

} // namespace CudaMatcher
