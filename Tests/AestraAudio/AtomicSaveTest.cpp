// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static bool writeAtomicTextFile(const fs::path& target, const std::string& content) {
    const fs::path tmp = target.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good())
            return false;
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec); // best-effort for platforms that require replace-by-remove
        ec.clear();
        fs::rename(tmp, target, ec);
        if (ec)
            return false;
    }

    return true;
}

int main() {
    std::cout << "Starting Atomic Save Test..." << std::endl;

    const fs::path testFile = "test_atomic_save.json";
    const fs::path tempFile = "test_atomic_save.json.tmp";

    std::error_code ec;
    fs::remove(testFile, ec);
    ec.clear();
    fs::remove(tempFile, ec);

    // Test 1: initial write
    std::cout << "Test 1: Normal Save... ";
    if (!writeAtomicTextFile(testFile, "{\"lane\":\"Test Lane\"}")) {
        std::cout << "FAILURE" << std::endl;
        return 1;
    }

    if (fs::exists(testFile) && !fs::exists(tempFile)) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAILURE (File missing or temp file not cleaned)" << std::endl;
        return 1;
    }

    // Test 2: overwrite existing
    std::cout << "Test 2: Overwrite existing... ";
    if (!writeAtomicTextFile(testFile, "{\"lane\":\"Updated Lane\"}")) {
        std::cout << "FAILURE" << std::endl;
        return 1;
    }

    std::ifstream in(testFile, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("Updated Lane") != std::string::npos) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAILURE: content mismatch" << std::endl;
        return 1;
    }

    fs::remove(testFile, ec);
    fs::remove(tempFile, ec);

    std::cout << "Atomic Save Verification Complete." << std::endl;
    return 0;
}
