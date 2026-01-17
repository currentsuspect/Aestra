#pragma once

#include <string>

namespace Aestra {

struct UserProfile {
	std::string username;
	std::string tier;     // "Aestra Core", "Aestra Plus", "Aestra Founder", "Aestra Campus"
	std::string serial;
	std::string signature;
	bool verified = false;
};

// Loads profile from %USERPROFILE%/.Aestra/user_info.json; returns default Core profile if missing.
UserProfile loadProfile();

// Saves profile to %USERPROFILE%/.Aestra/user_info.json.
bool saveProfile(const UserProfile& profile);

// Offline verification using baked-in public key (stub in public repo).
// Returns true if signature is valid for {username + serial + tier}.
bool verifyLicense(UserProfile& profile);

// Returns absolute path to the license file used by load/save.
std::string getLicenseFilePath();

} // namespace Aestra
