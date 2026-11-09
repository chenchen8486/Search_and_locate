#pragma once

#include "BatchLocator.h"

#include <functional>
#include <vector>

namespace locator_internal {

struct BatchObserver {
    std::function<bool()> cancel;
    std::function<void(int, int)> progress;
};

int executeBatchObserved(const std::vector<BatchTask>& tasks, int threadCount,
                         const BatchObserver& observer, std::vector<BatchResult>& results);

} // namespace locator_internal
