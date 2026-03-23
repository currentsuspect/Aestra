#pragma once

#include <string>

namespace Aestra {
bool verifyLicense(const std::string& licenseBlob);
std::string licenseStatus();
} // namespace Aestra
