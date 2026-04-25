#pragma once

#include <string>

namespace Aestra {

struct UserProfile {
	std::string username;
	std::string tier;     // Display-only cache; trusted entitlement must come from LicenseGate.
	std::string serial;
	std::string signature; // Display/debug metadata only in default builds.
	bool verified = false;
};

// Loads non-authoritative profile metadata from %USERPROFILE%/.Aestra/user_info.json.
// Returns default Core/unverified profile when missing or invalid.
UserProfile loadProfile();

// Saves profile to %USERPROFILE%/.Aestra/user_info.json.
bool saveProfile(const UserProfile& profile);

// Verifies profile signature only when explicit test-license mode is enabled.
// Default builds always return false and force Core/unverified.
bool verifyLicense(UserProfile& profile);

// Returns absolute path to the license file used by load/save.
std::string getLicenseFilePath();

} // namespace Aestra
