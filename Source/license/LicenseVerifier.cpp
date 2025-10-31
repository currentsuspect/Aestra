#include "LicenseVerifier.h"

#include <fstream>
#include <sstream>
#include <filesystem>

#include "NomadJSON.h"

namespace Nomad {

namespace {
	// Public key placeholder (Ed25519/ECDSA). Real verification implemented in private repo.
	static constexpr const char* kPublicKeyPem =
		"-----BEGIN PUBLIC KEY-----\n"
		"MOCK_PUBLIC_KEY_PLACEHOLDER\n"
		"-----END PUBLIC KEY-----\n";

	std::string getHomeDir() {
		#ifdef _WIN32
			char* userProfile = std::getenv("USERPROFILE");
			if (userProfile) return std::string(userProfile);
			char* homeDrive = std::getenv("HOMEDRIVE");
			char* homePath = std::getenv("HOMEPATH");
			if (homeDrive && homePath) return std::string(homeDrive) + std::string(homePath);
			return std::string("C:\\Users\\Public");
		#else
			char* home = std::getenv("HOME");
			return home ? std::string(home) : std::string("/");
		#endif
	}
}

std::string getLicenseFilePath() {
	std::filesystem::path p = std::filesystem::path(getHomeDir()) / ".nomad" / "user_info.json";
	return p.string();
}

static UserProfile parseProfile(const Nomad::JSON& j) {
	UserProfile p;
	if (j.isObject()) {
		if (j.has("username")) p.username = j["username"].asString();
		if (j.has("tier"))     p.tier     = j["tier"].asString();
		if (j.has("serial"))   p.serial   = j["serial"].asString();
		if (j.has("signature"))p.signature= j["signature"].asString();
	}
	return p;
}

static Nomad::JSON serializeProfile(const UserProfile& p) {
	Nomad::JSON j = Nomad::JSON::object();
	j.set("username",  p.username);
	j.set("tier",      p.tier);
	j.set("serial",    p.serial);
	j.set("signature", p.signature);
	return j;
}

UserProfile loadProfile() {
	UserProfile profile;
	// Defaults for open-source builds
	profile.username = "Guest";
	profile.tier = "Nomad Core";
	profile.serial = "CORE-XXXXXXX";
	profile.signature = "";
	profile.verified = false;

	const std::string path = getLicenseFilePath();
	std::ifstream f(path, std::ios::in | std::ios::binary);
	if (!f.good()) {
		return profile; // fallback defaults
	}
	std::stringstream buffer; buffer << f.rdbuf();
	try {
		Nomad::JSON j = Nomad::JSON::parse(buffer.str());
		profile = parseProfile(j);
	} catch (...) {
		// ignore parse errors, keep defaults
	}
	return profile;
}

bool saveProfile(const UserProfile& profile) {
	try {
		std::filesystem::path p = std::filesystem::path(getLicenseFilePath());
		std::filesystem::create_directories(p.parent_path());
		std::ofstream f(p.string(), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!f.good()) return false;
		Nomad::JSON j = serializeProfile(profile);
		f << j.toString(2);
		return true;
	} catch (...) {
		return false;
	}
}

bool verifyLicense(UserProfile& profile) {
	// Stubbed verifier for public repo:
	// - If signature equals "MOCK-VALID", mark verified true
	// - Otherwise unverified; force tier to Nomad Core
	const std::string payload = profile.username + profile.serial + profile.tier;
	(void)payload; (void)kPublicKeyPem; // placeholders for real verification
	bool ok = (profile.signature == "MOCK-VALID");
	profile.verified = ok;
	if (!ok) {
		profile.tier = "Nomad Core";
	}
	return ok;
}

} // namespace Nomad
