#include "BatchLocator.h"
#include "BatchControl.h"

#include "LocatorInternal.h"
#include "ResultRefiner.h"

#ifdef LOCATOR_WITH_CUDA
#include "gpu/CudaMatcher.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

int workerCount(int requested)
{
#ifdef _OPENMP
    return requested > 0 ? requested : omp_get_max_threads();
#else
    return 1;
#endif
}

} // namespace

#ifdef LOCATOR_WITH_CUDA
namespace locator_internal {

struct BatchExecutor {
    static bool applies(const std::vector<BatchTask>& tasks)
    {
        if (tasks.empty()) {
            return false;
        }
        for (const BatchTask& task : tasks) {
            const LocatorConfig& config = task.config;
            if (config.backend != LocatorBackend::Gpu || config.pyramidLevels != 1 ||
                config.useRoi || config.angleMinDeg != config.angleMaxDeg ||
                config.scaleMin != config.scaleMax) {
                return false;
            }
        }
        return true;
    }

    static std::vector<BatchResult> runChunk(const std::vector<BatchTask>& tasks)
    {
        using Clock = std::chrono::steady_clock;
        std::vector<BatchResult> results(tasks.size());
        std::vector<Locator> locators(tasks.size());
        std::vector<Clock::time_point> totalStarts(tasks.size());

        for (long long index = 0; index < static_cast<long long>(tasks.size()); ++index) {
            BatchResult& result = results[index];
            const BatchTask& task = tasks[index];
            result.id = task.id;
            result.preprocessMs = 0.0;
            result.searchMs = 0.0;
            result.totalMs = 0.0;
            totalStarts[index] = Clock::now();
            try {
                result.error = locators[index].initialize(task.model, task.config);
            } catch (...) {
                result.error = LocatorError::InternalError;
            }
            result.preprocessMs = std::chrono::duration<double, std::milli>(
                Clock::now() - totalStarts[index]).count();
            if (result.error != LocatorError::Ok) {
                result.totalMs = result.preprocessMs;
            }
        }

        std::vector<int> validIndices;
        std::vector<CudaMatcher::BatchInput> inputs;
        for (size_t index = 0; index < tasks.size(); ++index) {
            BatchResult& result = results[index];
            if (result.error != LocatorError::Ok) {
                continue;
            }
            if (!isValidImage(tasks[index].search)) {
                result.error = LocatorError::InvalidImage;
            } else {
                const auto& models = locators[index].state_->models;
                const cv::Mat search = makeView(tasks[index].search);
                if (models.size() != 1) {
                    result.error = LocatorError::InternalError;
                } else if (models[0].image.cols > search.cols || models[0].image.rows > search.rows) {
                    result.error = LocatorError::InvalidImage;
                } else {
                    validIndices.push_back(static_cast<int>(index));
                    inputs.push_back({ search, models[0].image });
                    continue;
                }
            }
            result.searchMs = 0.0;
            result.totalMs = std::chrono::duration<double, std::milli>(
                Clock::now() - totalStarts[index]).count();
        }
        if (validIndices.empty()) {
            return results;
        }

        std::vector<cv::Mat> responses;
        LocatorError batchError = LocatorError::Ok;
        const auto batchStart = Clock::now();
        try {
            responses = CudaMatcher::matchBatch(inputs);
            if (responses.size() != inputs.size()) {
                throw CudaMatcher::Failure("CUDA batch response count mismatch");
            }
        } catch (const CudaMatcher::Unavailable&) {
            batchError = LocatorError::BackendUnavailable;
        } catch (const CudaMatcher::Failure&) {
            batchError = LocatorError::InternalError;
        } catch (...) {
            batchError = LocatorError::InternalError;
        }
        const double batchMs = std::chrono::duration<double, std::milli>(
            Clock::now() - batchStart).count();
        if (batchError != LocatorError::Ok) {
            for (int index : validIndices) {
                BatchResult& result = results[index];
                result.error = batchError;
                result.searchMs = batchMs;
                result.totalMs = result.preprocessMs + result.searchMs;
            }
            return results;
        }

        const double sharedMs = batchMs / static_cast<double>(validIndices.size());
        for (long long position = 0; position < static_cast<long long>(validIndices.size()); ++position) {
            const int index = validIndices[position];
            const BatchTask& task = tasks[index];
            const Locator::State& state = *locators[index].state_;
            BatchResult& result = results[index];
            const auto refineStart = Clock::now();
            try {
                std::vector<TransformSearch::Candidate> candidates;
                TransformSearch::appendModelCandidates(state.models[0], makeView(task.search),
                                                       state.config, responses[position], candidates);
                result.matches = ResultRefiner::selectResults(candidates, state.config);
                result.error = LocatorError::Ok;
            } catch (const std::invalid_argument&) {
                result.error = LocatorError::InvalidConfig;
                result.matches.clear();
            } catch (...) {
                result.error = LocatorError::InternalError;
                result.matches.clear();
            }
            result.searchMs = sharedMs + std::chrono::duration<double, std::milli>(
                Clock::now() - refineStart).count();
            result.totalMs = result.preprocessMs + result.searchMs;
        }
        return results;
    }

    static std::vector<BatchResult> run(const std::vector<BatchTask>& tasks, int threadCount,
                                        const BatchObserver& observer,
                                        std::vector<char>& finished,
                                        std::atomic<int>& completed)
    {
        const size_t chunkSize = 4;
        const size_t chunkCount = (tasks.size() + chunkSize - 1) / chunkSize;
        std::vector<BatchResult> results(tasks.size());
#ifdef _OPENMP
#pragma omp parallel for num_threads(workerCount(threadCount))
#endif
        for (long long chunk = 0; chunk < static_cast<long long>(chunkCount); ++chunk) {
            if (observer.cancel && observer.cancel()) {
                continue;
            }
            const size_t begin = static_cast<size_t>(chunk) * chunkSize;
            const size_t end = std::min(tasks.size(), begin + chunkSize);
            const std::vector<BatchTask> chunkTasks(tasks.begin() + begin, tasks.begin() + end);
            const std::vector<BatchResult> chunkResults = runChunk(chunkTasks);
            for (size_t offset = 0; offset < chunkTasks.size(); ++offset) {
                results[begin + offset] = chunkResults[offset];
                finished[begin + offset] = 1;
            }
            if (observer.progress) {
                const int done = completed.fetch_add(static_cast<int>(chunkTasks.size())) +
                                 static_cast<int>(chunkTasks.size());
                observer.progress(done, static_cast<int>(tasks.size()));
            }
        }
        return results;
    }
};

} // namespace locator_internal
#endif

int locator_internal::executeBatchObserved(const std::vector<BatchTask>& tasks, int threadCount,
                                            const BatchObserver& observer,
                                            std::vector<BatchResult>& results)
{
    std::vector<char> finished(tasks.size(), 0);
    std::atomic<int> completed(0);
#ifdef LOCATOR_WITH_CUDA
    if (BatchExecutor::applies(tasks)) {
        results = BatchExecutor::run(tasks, threadCount, observer, finished, completed);
    } else
#endif
    {
        results.assign(tasks.size(), BatchResult());
        const auto runTask = [&](size_t index) {
            const BatchTask& task = tasks[index];
            BatchResult& result = results[index];
            result.id = task.id;
            const auto totalStart = std::chrono::steady_clock::now();
            auto phaseStart = totalStart;
            bool preprocessing = true;
            try {
                Locator locator;
                result.error = locator.initialize(task.model, task.config);
                phaseStart = std::chrono::steady_clock::now();
                result.preprocessMs = std::chrono::duration<double, std::milli>(phaseStart - totalStart).count();
                preprocessing = false;
                if (result.error == LocatorError::Ok) {
                    result.error = locator.execute(task.search, result.matches);
                }
                result.searchMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - phaseStart).count();
            } catch (...) {
                result.error = LocatorError::InternalError;
                result.matches.clear();
                const auto end = std::chrono::steady_clock::now();
                if (preprocessing) {
                    result.preprocessMs = std::chrono::duration<double, std::milli>(end - totalStart).count();
                    result.searchMs = 0.0;
                } else {
                    result.searchMs = std::chrono::duration<double, std::milli>(end - phaseStart).count();
                }
            }
            result.totalMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - totalStart).count();
        };

#ifdef _OPENMP
#pragma omp parallel for num_threads(workerCount(threadCount))
        for (long long index = 0; index < static_cast<long long>(tasks.size()); ++index) {
            if (observer.cancel && observer.cancel()) {
                continue;
            }
            runTask(static_cast<size_t>(index));
            finished[index] = 1;
            if (observer.progress) {
                observer.progress(completed.fetch_add(1) + 1, static_cast<int>(tasks.size()));
            }
        }
#else
        for (size_t index = 0; index < tasks.size(); ++index) {
            if (observer.cancel && observer.cancel()) {
                continue;
            }
            runTask(index);
            finished[index] = 1;
            if (observer.progress) {
                observer.progress(completed.fetch_add(1) + 1, static_cast<int>(tasks.size()));
            }
        }
#endif
    }
    std::vector<BatchResult> completedResults;
    completedResults.reserve(tasks.size());
    for (size_t index = 0; index < tasks.size(); ++index) {
        if (finished[index]) {
            completedResults.push_back(std::move(results[index]));
        }
    }
    results = std::move(completedResults);
    return static_cast<int>(results.size());
}

std::vector<BatchResult> executeBatch(const std::vector<BatchTask>& tasks, int threadCount)
{
    std::vector<BatchResult> results;
    locator_internal::executeBatchObserved(tasks, threadCount, locator_internal::BatchObserver(), results);
    return results;
}
