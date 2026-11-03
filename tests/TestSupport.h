#pragma once

#include <string>

void expect(bool condition, const std::string& message);
std::string realDataDirectory();
std::string batchDataTempDirectory();
