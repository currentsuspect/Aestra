// © 2026 Aestra Studios — All Rights Reserved.
// SEC-LICENSE-001: local profile edits must not unlock verified/premium state by default

#include "LicenseVerifier.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "  [FAIL] " << message << std::endl;
        return false;
    }
    return true;
}

bool writeTextFile(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    out << text;
    return out.good();
}

void setTestHomeDir(const fs::path& homeDir) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", homeDir.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
    setenv("HOME", homeDir.string().c_str(), 1);
#endif
}
} // namespace

int main() {
    std::cout << "=== SEC-LICENSE-001: profile spoof hardening ===" << std::endl;
    bool ok = true;

    const fs::path tempRoot = fs::temp_directory_path() / "aestra_sec_license_profile";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);
    setTestHomeDir(tempRoot);
    const fs::path profilePath = tempRoot / ".Aestra" / "user_info.json";

    // 1) Missing profile file -> default Core/unverified.
    fs::remove(profilePath);
    {
        Aestra::UserProfile profile = Aestra::loadProfile();
        bool verified = Aestra::verifyLicense(profile);
        ok &= expect(profile.tier == "Aestra Core", "Missing profile should default to Aestra Core tier");
        ok &= expect(!profile.verified, "Missing profile should remain unverified");
        ok &= expect(!verified, "Missing profile verifyLicense() should return false");
    }

    // 2) Spoofed profile with MOCK-VALID and founder tier must not verify in default builds.
    ok &= expect(writeTextFile(profilePath,
                               "{\n"
                               "  \"username\":\"Mallory\",\n"
                               "  \"tier\":\"Aestra Founder\",\n"
                               "  \"serial\":\"FAKE-1234\",\n"
                               "  \"signature\":\"MOCK-VALID\"\n"
                               "}\n"),
                 "Failed writing spoofed profile JSON");
    {
        Aestra::UserProfile profile = Aestra::loadProfile();
        bool verified = Aestra::verifyLicense(profile);
        ok &= expect(profile.username == "Mallory", "Username metadata should still load from profile");
        ok &= expect(profile.serial == "FAKE-1234", "Serial metadata should still load from profile");
#if defined(AESTRA_ENABLE_TEST_LICENSES) && AESTRA_ENABLE_TEST_LICENSES
        ok &= expect(false, "AESTRA_ENABLE_TEST_LICENSES must not be enabled in default builds");
#else
        ok &= expect(profile.tier == "Aestra Core", "Spoofed founder tier must not survive verification");
        ok &= expect(!profile.verified, "Spoofed MOCK-VALID signature must not mark profile verified");
        ok &= expect(!verified, "Spoofed MOCK-VALID signature must not verify in default builds");
#endif
    }

    // 3) Invalid/corrupted profile should fail safely.
    ok &= expect(writeTextFile(profilePath, "{ invalid json"), "Failed writing malformed profile JSON");
    {
        Aestra::UserProfile profile = Aestra::loadProfile();
        bool verified = Aestra::verifyLicense(profile);
        ok &= expect(profile.tier == "Aestra Core", "Invalid profile should fall back to Core");
        ok &= expect(!profile.verified, "Invalid profile should remain unverified");
        ok &= expect(!verified, "Invalid profile verifyLicense() should return false");
    }

    fs::remove_all(tempRoot);

    if (!ok) {
        std::cout << "\n[FAIL] Profile-based license spoof protection regression detected." << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] Local profile edits cannot unlock verified/premium state by default." << std::endl;
    return 0;
}
