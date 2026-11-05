#include "ResultRefiner.h"

#include <algorithm>
#include <cmath>

namespace ResultRefiner {

std::vector<LocatorResult> selectResults(const std::vector<LocatorResult>& candidates,
                                         const LocatorConfig& config)
{
    std::vector<LocatorResult> sorted = candidates;
    std::stable_sort(sorted.begin(), sorted.end(), [](const LocatorResult& left,
                                                       const LocatorResult& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.x != right.x) {
            return left.x < right.x;
        }
        if (left.y != right.y) {
            return left.y < right.y;
        }
        if (left.angle != right.angle) {
            return left.angle < right.angle;
        }
        return left.scale < right.scale;
    });

    std::vector<LocatorResult> selected;
    for (const LocatorResult& candidate : sorted) {
        if (candidate.score < config.minScore) {
            continue;
        }

        bool duplicate = false;
        for (const LocatorResult& existing : selected) {
            if (std::abs(candidate.x - existing.x) <= config.xVicinity &&
                std::abs(candidate.y - existing.y) <= config.yVicinity) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            selected.push_back(candidate);
            if (selected.size() == static_cast<size_t>(config.maxResults)) {
                break;
            }
        }
    }
    return selected;
}

} // namespace ResultRefiner
