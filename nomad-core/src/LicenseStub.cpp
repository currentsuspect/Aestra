#include "Nomad/License.h"

namespace nomad {
	bool verifyLicense(const std::string&) { return false; }
	std::string licenseStatus() { return "Nomad Core (unlicensed for premium)"; }
}
