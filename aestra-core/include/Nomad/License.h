#pragma once

#include <string>

namespace nomad {
	bool verifyLicense(const std::string& licenseBlob);
	std::string licenseStatus();
}
