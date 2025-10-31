#pragma once

#include <string>

namespace Nomad {

struct UserProfile {
	std::string username;
	std::string tier;     // "Nomad Core", "Nomad Studio+", "Nomad Founder", "Nomad Campus"
	std::string serial;
	std::string signature;
	bool verified = false;
};

// Loads profile from %USERPROFILE%/.nomad/user_info.json; returns default Core profile if missing.
UserProfile loadProfile();

// Saves profile to %USERPROFILE%/.nomad/user_info.json.
bool saveProfile(const UserProfile& profile);

// Offline verification using baked-in public key (stub in public repo).
// Returns true if signature is valid for {username + serial + tier}.
bool verifyLicense(UserProfile& profile);

// Returns absolute path to the license file used by load/save.
std::string getLicenseFilePath();

} // namespace Nomad
