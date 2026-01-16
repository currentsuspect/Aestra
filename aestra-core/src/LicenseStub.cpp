#include "Aestra/License.h"

namespace nomad {
	bool verifyLicense(const std::string&) { return false; }
	std::string licenseStatus() { return "Aestra Core (unlicensed for premium)"; }
}
