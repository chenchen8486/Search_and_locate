#include "BatchLocator.h"

#include <algorithm>
#include <chrono>

#ifdef _OPENMP
#include <omp.h>
#endif

std::vector<BatchResult> executeBatch(const std::vector<BatchTask>& tasks,
                                      int threadCount)
{
    std::vector<BatchResult> results(tasks.size());
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
    const int actualThreads = threadCount > 0 ? threadCount : omp_get_max_threads();
#pragma omp parallel for num_threads(actualThreads)
    for (long long index = 0; index < static_cast<long long>(tasks.size()); ++index) {
        runTask(static_cast<size_t>(index));
    }
#else
    for (size_t index = 0; index < tasks.size(); ++index) {
        runTask(index);
    }
#endif
    return results;
}
