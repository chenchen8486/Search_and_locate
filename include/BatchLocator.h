#pragma once

#include "Locator.h"

#include <vector>

struct BatchTask {
    int id;
    LocatorImage model;
    LocatorImage search;
    LocatorConfig config;
};

struct BatchResult {
    int id;
    LocatorError error;
    std::vector<LocatorResult> matches;
    double preprocessMs;
    double searchMs;
    double totalMs;
};

std::vector<BatchResult> executeBatch(const std::vector<BatchTask>& tasks,
                                      int threadCount);
