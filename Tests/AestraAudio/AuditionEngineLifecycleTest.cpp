#include "Playback/AuditionEngine.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void writeUint32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
    out.put(static_cast<char>((value >> 16) & 0xFF));
    out.put(static_cast<char>((value >> 24) & 0xFF));
}

void writeUint16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
}

bool writeTestWav(const std::string& path) {
    std::ofstream wav(path, std::ios::binary);
    if (!wav) {
        return false;
    }

    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t sampleRate = 48000;
    const uint32_t samples = 512;
    const uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t dataChunkSize = samples * bytesPerSample;
    const uint32_t riffChunkSize = 4 + (8 + 16) + (8 + dataChunkSize);

    wav.write("RIFF", 4);
    writeUint32(wav, riffChunkSize);
    wav.write("WAVE", 4);
    wav.write("fmt ", 4);
    writeUint32(wav, 16);
    writeUint16(wav, 1);
    writeUint16(wav, channels);
    writeUint32(wav, sampleRate);
    writeUint32(wav, sampleRate * channels * bytesPerSample);
    writeUint16(wav, channels * bytesPerSample);
    writeUint16(wav, bitsPerSample);
    wav.write("data", 4);
    writeUint32(wav, dataChunkSize);
    for (uint32_t i = 0; i < samples; ++i) {
        writeUint16(wav, static_cast<uint16_t>(i & 0x7FFFU));
    }
    return static_cast<bool>(wav);
}
} // namespace

int main() {
    const fs::path path = fs::temp_directory_path() / "Aestra_audition_lifecycle.wav";
    if (!writeTestWav(path.string())) {
        std::cerr << "failed to write audition lifecycle fixture\n";
        return 1;
    }

    {
        Aestra::Audio::AuditionEngine engine;
        engine.addToQueue(path.string());
        engine.play();
        engine.clearQueue();
    }

    fs::remove(path);
    std::cout << "AestraAuditionEngineLifecycleTest: PASS\n";
    return 0;
}
