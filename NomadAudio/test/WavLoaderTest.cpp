// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PlaylistTrack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

namespace fs = std::filesystem;

namespace Nomad {
namespace Audio {
    bool loadWavFile(const std::string& filePath, std::vector<float>& audioData,
                     uint32_t& sampleRate, uint32_t& numChannels);
}
}

using namespace Nomad::Audio;

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

void writeSample(std::ofstream& out, int32_t value, uint16_t bitsPerSample) {
    if (bitsPerSample == 16) {
        writeUint16(out, static_cast<uint16_t>(value & 0xFFFF));
    } else if (bitsPerSample == 24) {
        out.put(static_cast<char>(value & 0xFF));
        out.put(static_cast<char>((value >> 8) & 0xFF));
        out.put(static_cast<char>((value >> 16) & 0xFF));
    } else if (bitsPerSample == 32) {
        writeUint32(out, static_cast<uint32_t>(value));
    }
}

std::string makeTempPath(const std::string& name) {
    fs::path temp = fs::temp_directory_path() / name;
    return temp.string();
}

void writeTestWav(const std::string& path,
                  uint16_t bitsPerSample,
                  uint32_t sampleRate,
                  uint16_t numChannels,
                  const std::vector<int32_t>& samples,
                  bool insertJunkChunk) {
    std::ofstream wav(path, std::ios::binary);
    const uint16_t audioFormat = 1; // PCM
    const uint32_t fmtChunkSize = 16;
    const uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t dataChunkSize = static_cast<uint32_t>(samples.size()) * bytesPerSample;
    const uint32_t junkSize = insertJunkChunk ? 8 : 0;
    const uint32_t riffChunkSize = 4 + junkSize + (8 + fmtChunkSize) + (8 + dataChunkSize);

    // RIFF header
    wav.write("RIFF", 4);
    writeUint32(wav, riffChunkSize);
    wav.write("WAVE", 4);

    if (insertJunkChunk) {
        wav.write("JUNK", 4);
        writeUint32(wav, junkSize);
        wav.write("12345678", 8);
    }

    // fmt chunk
    wav.write("fmt ", 4);
    writeUint32(wav, fmtChunkSize);
    writeUint16(wav, audioFormat);
    writeUint16(wav, numChannels);
    writeUint32(wav, sampleRate);
    uint32_t byteRate = sampleRate * numChannels * bytesPerSample;
    writeUint32(wav, byteRate);
    uint16_t blockAlign = numChannels * bytesPerSample;
    writeUint16(wav, blockAlign);
    writeUint16(wav, bitsPerSample);

    // data chunk
    wav.write("data", 4);
    writeUint32(wav, dataChunkSize);
    for (int32_t sample : samples) {
        writeSample(wav, sample, bitsPerSample);
    }
}

bool approxEqual(float a, float b, float epsilon = 1e-5f) {
    return std::abs(a - b) <= epsilon;
}

bool runBasic16BitTest() {
    std::cout << "Test 1: Basic 16-bit PCM...";
    std::vector<int32_t> samples = {0, 32767, -32768, 16384};
    std::string path = makeTempPath("nomad_basic16.wav");
    writeTestWav(path, 16, 44100, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = loadWavFile(path, audio, sampleRate, channels);
    fs::remove(path);

    if (!ok || sampleRate != 44100 || channels != 1 || audio.size() != samples.size()) {
        std::cout << " FAILED\n";
        return false;
    }

    if (!approxEqual(audio[1], 32767 / 32768.0f) || !approxEqual(audio[2], -1.0f, 1e-4f)) {
        std::cout << " FAILED (sample mismatch)\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool runJunkChunkTest() {
    std::cout << "Test 2: 16-bit PCM with JUNK chunk...";
    std::vector<int32_t> samples = {1000, -1000, 2000, -2000};
    std::string path = makeTempPath("nomad_junk16.wav");
    writeTestWav(path, 16, 48000, 2, samples, true);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = loadWavFile(path, audio, sampleRate, channels);
    fs::remove(path);

    if (!ok || sampleRate != 48000 || channels != 2 || audio.size() != samples.size()) {
        std::cout << " FAILED\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool run24BitTest() {
    std::cout << "Test 3: 24-bit PCM conversion...";
    std::vector<int32_t> samples = {0x7FFFFF, -0x800000};
    std::string path = makeTempPath("nomad_24bit.wav");
    writeTestWav(path, 24, 44100, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = loadWavFile(path, audio, sampleRate, channels);
    fs::remove(path);

    if (!ok || audio.size() != samples.size() || sampleRate != 44100 || channels != 1) {
        std::cout << " FAILED\n";
        return false;
    }

    if (!(audio[0] <= 1.0f && audio[0] > 0.99f) || !(audio[1] >= -1.0f && audio[1] < -0.99f)) {
        std::cout << " FAILED (24-bit values out of range)\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

} // namespace

int main() {
    bool success = true;
    success &= runBasic16BitTest();
    success &= runJunkChunkTest();
    success &= run24BitTest();

    if (success) {
        std::cout << "All WAV loader tests passed.\n";
        return 0;
    }

    std::cout << "WAV loader tests failed.\n";
    return 1;
}
