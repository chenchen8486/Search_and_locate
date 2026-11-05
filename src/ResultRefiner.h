#pragma once

#include "Locator.h"

#include <vector>

namespace ResultRefiner {

std::vector<LocatorResult> selectResults(const std::vector<LocatorResult>& candidates,
                                         const LocatorConfig& config);

} // namespace ResultRefiner
