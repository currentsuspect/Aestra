// © 2026 Aestra Studios — All Rights Reserved.
// SEC-003: ProjectSerializer must guard numChannels == 0 from malformed WAV input

#include "ProjectSerializer.h"
#include "TrackManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using Aestra::Audio::ClipSourceID;
using Aestra::Audio::TrackManager;

namespace {
void writeZeroChannelWav(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    const uint32_t fileSize = 36u + 8u;
    const uint32_t fmtChunkSize = 16u;
    const uint16_t audioFormat = 1u;
    const uint16_t numChannels = 0u;
    const uint32_t sampleRate = 44100u;
    const uint32_t byteRate = 0u;
    const uint16_t blockAlign = 0u;
    const uint16_t bitsPerSample = 16u;
    const uint32_t dataSize = 8u;
    const uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};

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
    out.write(reinterpret_cast<const char*>(data), sizeof(data));
}

std::string buildProjectJson(const std::string& wavPath) {
    return "{\n"
           "  \"version\": 1,\n"
           "  \"tempo\": 120.0,\n"
           "  \"playhead\": 0.0,\n"
           "  \"sources\": [\n"
           "    {\n"
           "      \"id\": 1,\n"
           "      \"path\": \"" + wavPath + "\"\n"
           "    }\n"
           "  ],\n"
           "  \"lanes\": []\n"
           "}\n";
}
} // namespace

int main() {
    std::cout << "=== SEC-003: ProjectSerializer zero-channel WAV handling ===" << std::endl;

    const fs::path tempDir = fs::temp_directory_path() / "aestra_sec003";
    fs::create_directories(tempDir);

    const fs::path wavPath = tempDir / "zero_channels.wav";
    const fs::path projectPath = tempDir / "zero_channels.aes";
    writeZeroChannelWav(wavPath);

    std::ofstream out(projectPath);
    out << buildProjectJson(wavPath.string());
    out.close();

    auto trackManager = std::make_shared<TrackManager>();
    ProjectSerializer::LoadResult result;
    try {
        result = ProjectSerializer::load(projectPath.string(), trackManager);
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] load threw: " << e.what() << std::endl;
        fs::remove_all(tempDir);
        return 1;
    } catch (...) {
        std::cout << "  [FAIL] load threw unknown exception" << std::endl;
        fs::remove_all(tempDir);
        return 1;
    }

    if (!result.ok) {
        std::cout << "  [FAIL] load failed: " << result.errorMessage << std::endl;
        fs::remove_all(tempDir);
        return 1;
    }

    const auto sourceIds = trackManager->getSourceManager().getAllSourceIDs();
    if (sourceIds.size() != 1) {
        std::cout << "  [FAIL] expected one decoded source, got " << sourceIds.size() << std::endl;
        fs::remove_all(tempDir);
        return 1;
    }

    const auto* source = trackManager->getSourceManager().getSource(sourceIds.front());
    if (!source || !source->getBuffer()) {
        std::cout << "  [FAIL] decoded source buffer missing" << std::endl;
        fs::remove_all(tempDir);
        return 1;
    }

    if (source->getNumChannels() != 1) {
        std::cout << "  [FAIL] expected fallback to mono, got " << source->getNumChannels() << " channels" << std::endl;
        fs::remove_all(tempDir);
        return 1;
    }

    std::cout << "  [PASS] Zero-channel WAV loaded safely with mono fallback and no crash." << std::endl;
    fs::remove_all(tempDir);
    return 0;
}
