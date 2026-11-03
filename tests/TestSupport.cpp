#include "TestSupport.h"

#include <stdexcept>

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string realDataDirectory()
{
    return "test_data/Search";
}

std::string batchDataTempDirectory()
{
    return "temp/t02_batch_data";
}
