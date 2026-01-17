#include "Aestra/License.h"

namespace Aestra {
	bool verifyLicense(const std::string&) { return false; }
	std::string licenseStatus() { return "Aestra Core (unlicensed for premium)"; }
}
