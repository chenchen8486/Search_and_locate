#include "CudaMatcher.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

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

struct BatchDescriptor {
    size_t searchOffset;
    size_t modelOffset;
    size_t responseOffset;
    int searchWidth;
    int modelWidth;
    int modelHeight;
    int responseWidth;
    int responseHeight;
};

__global__ void batchModelStatsKernel(const unsigned char* models,
                                      const BatchDescriptor* descriptors, int descriptorCount,
                                      float* stats)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= descriptorCount) {
        return;
    }
    const BatchDescriptor descriptor = descriptors[index];
    const unsigned char* model = models + descriptor.modelOffset;
    const int count = descriptor.modelWidth * descriptor.modelHeight;
    float modelSum = 0.0f;
    for (int row = 0; row < descriptor.modelHeight; ++row) {
        for (int col = 0; col < descriptor.modelWidth; ++col) {
            modelSum += model[row * descriptor.modelWidth + col];
        }
    }
    const float modelMean = modelSum / count;
    float modelVariance = 0.0f;
    for (int row = 0; row < descriptor.modelHeight; ++row) {
        for (int col = 0; col < descriptor.modelWidth; ++col) {
            const float modelValue = model[row * descriptor.modelWidth + col] - modelMean;
            modelVariance += modelValue * modelValue;
        }
    }
    stats[index * 2] = modelMean;
    stats[index * 2 + 1] = modelVariance;
}

__global__ void batchNccKernel(const unsigned char* searches, const unsigned char* models,
                               const BatchDescriptor* descriptors, int descriptorCount,
                               const float* modelStats, float* responses,
                               size_t responseCount)
{
    const size_t responseIndex = blockIdx.x * blockDim.x + threadIdx.x;
    if (responseIndex >= responseCount) {
        return;
    }
    int low = 0;
    int high = descriptorCount - 1;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (descriptors[middle].responseOffset <= responseIndex) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    const BatchDescriptor descriptor = descriptors[low];
    const size_t localIndex = responseIndex - descriptor.responseOffset;
    const int x = static_cast<int>(localIndex % descriptor.responseWidth);
    const int y = static_cast<int>(localIndex / descriptor.responseWidth);
    const unsigned char* search = searches + descriptor.searchOffset;
    const unsigned char* model = models + descriptor.modelOffset;
    const int count = descriptor.modelWidth * descriptor.modelHeight;
    const float modelMean = modelStats[low * 2];
    const float modelVariance = modelStats[low * 2 + 1];
    float searchSum = 0.0f;
    for (int row = 0; row < descriptor.modelHeight; ++row) {
        for (int col = 0; col < descriptor.modelWidth; ++col) {
            searchSum += search[(y + row) * descriptor.searchWidth + x + col];
        }
    }
    const float searchMean = searchSum / count;
    float searchVariance = 0.0f;
    float covariance = 0.0f;
    for (int row = 0; row < descriptor.modelHeight; ++row) {
        for (int col = 0; col < descriptor.modelWidth; ++col) {
            const float modelValue = model[row * descriptor.modelWidth + col] - modelMean;
            const float searchValue = search[(y + row) * descriptor.searchWidth + x + col] -
                                      searchMean;
            searchVariance += searchValue * searchValue;
            covariance += modelValue * searchValue;
        }
    }
    float value = 0.0f;
    if (modelVariance > 1e-12f && searchVariance > 1e-12f) {
        value = covariance / sqrtf(modelVariance * searchVariance);
        value = fmaxf(-1.0f, fminf(1.0f, value));
    }
    responses[responseIndex] = value;
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
        cudaFree(descriptors_);
        cudaFree(stats_);
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

    void reserveDescriptors(size_t bytes)
    {
        reserve(descriptors_, descriptorBytes_, bytes);
    }

    void reserveStats(size_t bytes)
    {
        reserve(stats_, statsBytes_, bytes);
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

    BatchDescriptor* descriptors() const
    {
        return static_cast<BatchDescriptor*>(descriptors_);
    }

    float* stats() const
    {
        return static_cast<float*>(stats_);
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
    void* descriptors_ = nullptr;
    void* stats_ = nullptr;
    void* response_ = nullptr;
    size_t searchBytes_ = 0;
    size_t modelBytes_ = 0;
    size_t descriptorBytes_ = 0;
    size_t statsBytes_ = 0;
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

std::vector<cv::Mat> matchBatch(const std::vector<BatchInput>& inputs)
{
    if (inputs.empty()) {
        return {};
    }
    if (!isAvailable()) {
        throw Unavailable();
    }

    size_t searchBytes = 0;
    size_t modelBytes = 0;
    size_t responseCount = 0;
    std::vector<BatchDescriptor> hostDescriptors(inputs.size());
    std::vector<char> emptyResponses(inputs.size(), 0);
    for (size_t index = 0; index < inputs.size(); ++index) {
        const cv::Mat& search = inputs[index].search;
        const cv::Mat& model = inputs[index].model;
        if (search.empty() || model.empty() || search.type() != CV_8UC1 ||
            model.type() != CV_8UC1 || model.cols > search.cols || model.rows > search.rows) {
            throw Failure("Invalid CUDA batch input");
        }
        double minimum = 0.0;
        double maximum = 0.0;
        cv::minMaxLoc(search, &minimum, &maximum);
        emptyResponses[index] = minimum == maximum ? 1 : 0;
        const int responseWidth = search.cols - model.cols + 1;
        const int responseHeight = search.rows - model.rows + 1;
        hostDescriptors[index] = { searchBytes, modelBytes, responseCount, search.cols,
                                   model.cols, model.rows, responseWidth, responseHeight };
        searchBytes += search.total();
        modelBytes += model.total();
        responseCount += static_cast<size_t>(responseWidth) * responseHeight;
    }
    if (responseCount > 0x7fffffff) {
        throw Failure("CUDA batch response is too large");
    }

    thread_local std::vector<unsigned char> hostSearches;
    thread_local std::vector<unsigned char> hostModels;
    hostSearches.resize(searchBytes);
    hostModels.resize(modelBytes);
    for (size_t index = 0; index < inputs.size(); ++index) {
        const BatchDescriptor& descriptor = hostDescriptors[index];
        for (int y = 0; y < inputs[index].search.rows; ++y) {
            std::memcpy(hostSearches.data() + descriptor.searchOffset +
                        static_cast<size_t>(y) * descriptor.searchWidth,
                        inputs[index].search.ptr<unsigned char>(y), descriptor.searchWidth);
        }
        for (int y = 0; y < inputs[index].model.rows; ++y) {
            std::memcpy(hostModels.data() + descriptor.modelOffset +
                        static_cast<size_t>(y) * descriptor.modelWidth,
                        inputs[index].model.ptr<unsigned char>(y), descriptor.modelWidth);
        }
    }

    thread_local Workspace workspace;
    workspace.reserveSearch(searchBytes);
    workspace.reserveModel(modelBytes);
    workspace.reserveDescriptors(inputs.size() * sizeof(BatchDescriptor));
    workspace.reserveStats(inputs.size() * sizeof(float) * 2);
    workspace.reserveResponse(responseCount * sizeof(float));
    check(cudaMemcpyAsync(workspace.search(), hostSearches.data(), searchBytes,
                          cudaMemcpyHostToDevice, workspace.stream()));
    check(cudaMemcpyAsync(workspace.model(), hostModels.data(), modelBytes,
                          cudaMemcpyHostToDevice, workspace.stream()));
    check(cudaMemcpyAsync(workspace.descriptors(), hostDescriptors.data(),
                          inputs.size() * sizeof(BatchDescriptor), cudaMemcpyHostToDevice,
                          workspace.stream()));
    const int inputCount = static_cast<int>(inputs.size());
    batchModelStatsKernel<<<(inputCount + 255) / 256, 256, 0, workspace.stream()>>>(
        static_cast<const unsigned char*>(workspace.model()), workspace.descriptors(), inputCount,
        workspace.stats());
    check(cudaGetLastError());
    batchNccKernel<<<static_cast<unsigned int>((responseCount + 255) / 256), 256, 0,
                     workspace.stream()>>>(
        static_cast<const unsigned char*>(workspace.search()),
        static_cast<const unsigned char*>(workspace.model()), workspace.descriptors(), inputCount,
        workspace.stats(), static_cast<float*>(workspace.response()), responseCount);
    check(cudaGetLastError());

    cv::Mat responseStorage(1, static_cast<int>(responseCount), CV_32FC1);
    check(cudaMemcpyAsync(responseStorage.data, workspace.response(), responseCount * sizeof(float),
                          cudaMemcpyDeviceToHost, workspace.stream()));
    check(cudaStreamSynchronize(workspace.stream()));
    std::vector<cv::Mat> responses;
    responses.reserve(inputs.size());
    for (size_t index = 0; index < hostDescriptors.size(); ++index) {
        if (emptyResponses[index]) {
            responses.push_back(cv::Mat());
            continue;
        }
        const BatchDescriptor& descriptor = hostDescriptors[index];
        const int begin = static_cast<int>(descriptor.responseOffset);
        const int end = begin + descriptor.responseWidth * descriptor.responseHeight;
        responses.push_back(responseStorage.colRange(begin, end).reshape(1, descriptor.responseHeight));
    }
    return responses;
}

} // namespace CudaMatcher
