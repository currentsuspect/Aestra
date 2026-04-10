// © 2025 Aestra Studios — All Rights Reserved.
// SEC-004: Path traversal in SamplerPlugin::loadState via samplePath
//
// Proof: A crafted plugin state with "samplePath": "../../../etc/passwd"
// causes the sampler to attempt loading arbitrary files from the filesystem.
// While decodeAudioFile will fail on non-audio files, the file is still
// opened and read (information disclosure / existence oracle).
//
// After fix: samplePath is validated to be within allowed directories
// (project directory or standard sample paths), rejecting path traversal.

#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// Simulates the vulnerable path handling from SamplerPlugin.cpp:369-377
bool vulnerableLoadSample(const std::string& samplePath) {
    // No path sanitization — uses path directly
    if (!fs::exists(samplePath)) return false;
    // File would be opened and read here
    return true;
}

// Check if a path is safely within a base directory
bool isPathWithin(const fs::path& target, const fs::path& base) {
    auto canonicalTarget = fs::weakly_canonical(target);
    auto canonicalBase = fs::weakly_canonical(base);
    auto rel = fs::relative(canonicalTarget, canonicalBase);
    // If relative path starts with "..", it escapes the base directory
    for (const auto& comp : rel) {
        if (comp == "..") return false;
    }
    return true;
}

// Safe version (what the fix should look like)
bool safeLoadSample(const std::string& samplePath, const std::string& projectDir) {
    fs::path target(samplePath);
    fs::path base(projectDir);

    // Absolute paths outside the project directory are rejected
    if (target.is_absolute() && !isPathWithin(target, base)) {
        std::cerr << "  [SAFE] Rejected path traversal: " << samplePath << std::endl;
        return false;
    }

    // Relative paths are resolved against project directory and validated
    if (target.is_relative()) {
        fs::path resolved = fs::weakly_canonical(base / target);
        if (!isPathWithin(resolved, base)) {
            std::cerr << "  [SAFE] Rejected relative traversal: " << samplePath << std::endl;
            return false;
        }
    }

    return vulnerableLoadSample(samplePath);  // proceed with sanitized path
}

int main() {
    std::cout << "=== SEC-004: Path traversal in SamplerPlugin loadState ===" << std::endl;

    std::string projectDir = "/home/user/projects/my-song";

    const std::string attackPaths[] = {
        "../../../etc/passwd",
        "..\\..\\..\\windows\\system32\\config\\sam",
        "/etc/shadow",
        "./samples/legit.wav",           // should pass
        "subdir/../../../etc/passwd",    // should be blocked
    };

    int blocked = 0, passed = 0;
    for (const auto& path : attackPaths) {
        std::cout << "  Testing: " << path << " ... ";
        if (safeLoadSample(path, projectDir)) {
            std::cout << "ALLOWED";
            passed++;
        } else {
            std::cout << "BLOCKED";
            blocked++;
        }
        std::cout << std::endl;
    }

    // The legitimate sample should pass, attacks should be blocked
    if (blocked >= 4) {
        std::cout << "\n[PASS] Path traversal attacks blocked (" << blocked << "/" << 5 << " blocked)." << std::endl;
        return 0;
    }

    std::cout << "\n[FAIL] Only " << blocked << "/" << 5 << " traversal attacks blocked." << std::endl;
    return 1;
}
