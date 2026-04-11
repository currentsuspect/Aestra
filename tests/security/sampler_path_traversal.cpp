// © 2026 Aestra Studios — All Rights Reserved.
// SEC-004: SamplerPlugin must reject path traversal in loadState()

#include "Plugin/SamplerPlugin.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Aestra::Audio::Plugins::SamplerPlugin;

namespace {
void writeTinyWav(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    const uint32_t fileSize = 36u + 4u;
    const uint32_t fmtChunkSize = 16u;
    const uint16_t audioFormat = 1u;
    const uint16_t numChannels = 1u;
    const uint32_t sampleRate = 44100u;
    const uint32_t byteRate = 88200u;
    const uint16_t blockAlign = 2u;
    const uint16_t bitsPerSample = 16u;
    const uint32_t dataSize = 4u;
    const int16_t samples[2] = {0, 0};

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&fileSize), sizeof(fileSize));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmtChunkSize), sizeof(fmtChunkSize));
    out.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
    out.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
    out.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
    out.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
    out.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
    out.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    out.write(reinterpret_cast<const char*>(samples), sizeof(samples));
}

std::vector<uint8_t> makeState(const std::string& samplePath) {
    const std::string json = "{"
                             "\"params\":[0.01,0.1,1.0,0.1,0.5],"
                             "\"samplePath\":\"" + samplePath + "\"}";
    return std::vector<uint8_t>(json.begin(), json.end());
}
} // namespace

int main() {
    std::cout << "=== SEC-004: SamplerPlugin path traversal rejection ===" << std::endl;

    const fs::path tempDir = fs::temp_directory_path() / "aestra_sec004";
    const fs::path sampleDir = tempDir / "samples";
    const fs::path legitPath = sampleDir / "legit.wav";
    fs::create_directories(sampleDir);
    writeTinyWav(legitPath);

    const fs::path oldCwd = fs::current_path();
    fs::current_path(tempDir);

    SamplerPlugin plugin;
    plugin.initialize(44100.0, 512);

    const bool legitLoaded = plugin.loadState(makeState("samples/legit.wav"));
    if (!legitLoaded) {
        std::cout << "  [FAIL] legitimate in-project sample did not load" << std::endl;
        fs::current_path(oldCwd);
        fs::remove_all(tempDir);
        return 1;
    }
    const auto savedBytes = plugin.saveState();
    const std::string savedState(savedBytes.begin(), savedBytes.end());
    if (savedState.find("samples/legit.wav") == std::string::npos) {
        std::cout << "  [FAIL] legitimate sample path was not retained after loadState()" << std::endl;
        fs::current_path(oldCwd);
        fs::remove_all(tempDir);
        return 1;
    }

    const std::vector<std::string> attackPaths = {
        "../../../etc/passwd",
        "..\\\\..\\\\windows\\\\system32\\\\config\\\\sam",
        "/etc/shadow",
        "\\\\server\\share\\file.wav",
        "\\absolute\\windows\\path.wav",
    };

    bool blockedAll = true;
    for (const auto& attack : attackPaths) {
        const bool allowed = plugin.loadState(makeState(attack));
        std::cout << "  " << attack << " -> " << (allowed ? "ALLOWED" : "BLOCKED") << std::endl;
        if (allowed) {
            blockedAll = false;
        }
    }

    fs::current_path(oldCwd);
    fs::remove_all(tempDir);

    if (!blockedAll) {
        std::cout << "\n[FAIL] One or more traversal paths were accepted by SamplerPlugin::loadState()." << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] Real SamplerPlugin::loadState() accepts local sample paths and blocks traversal."
              << std::endl;
    return 0;
}
