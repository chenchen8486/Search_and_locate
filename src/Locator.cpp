#include "Locator.h"

struct Locator::State {
};

namespace {

bool isValidImage(const LocatorImage& image)
{
    return image.data != nullptr && image.width > 0 && image.height > 0 &&
           image.stride >= image.width;
}

} // namespace

Locator::Locator() = default;

LocatorError Locator::initialize(const LocatorImage& model, const LocatorConfig& config)
{
    if (!isValidImage(model)) {
        return LocatorError::InvalidImage;
    }
    if (config.backend != LocatorBackend::Cpu) {
        return LocatorError::BackendUnavailable;
    }

    state_ = std::make_shared<State>();
    return LocatorError::Ok;
}

LocatorError Locator::execute(const LocatorImage& search,
                              std::vector<LocatorResult>& results) const
{
    results.clear();
    if (!state_) {
        return LocatorError::NotInitialized;
    }
    if (!isValidImage(search)) {
        return LocatorError::InvalidImage;
    }
    return LocatorError::Ok;
}
