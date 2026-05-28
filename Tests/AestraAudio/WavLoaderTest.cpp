#include "MiniAudioDecoder.h"
#include "PlaylistTrack.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace Aestra::Audio;

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

void writeTestWav(const std::string& path, uint16_t bitsPerSample, uint32_t sampleRate, uint16_t numChannels,
                  const std::vector<int32_t>& samples, bool insertJunkChunk) {
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
    std::string path = makeTempPath("Aestra_basic16.wav");
    writeTestWav(path, 16, 44100, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = decodeAudioFile(path, audio, sampleRate, channels);
    fs::remove(path);

    // Note: decodeAudioFile forces stereo, so channel count will be 2
    if (!ok || sampleRate != 44100 || channels != 2 || audio.size() != samples.size() * 2) {
        std::cout << " FAILED (Metadata mismatch) SR: " << sampleRate << " CH: " << channels
                  << " Size: " << audio.size() << "\n";
        return false;
    }

    // Check first sample (left channel of first frame)
    if (!approxEqual(audio[2], 32767 / 32768.0f) || !approxEqual(audio[4], -1.0f, 1e-4f)) {
        std::cout << " FAILED (sample mismatch)\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool runJunkChunkTest() {
    std::cout << "Test 2: 16-bit PCM with JUNK chunk...";
    std::vector<int32_t> samples = {1000, -1000, 2000, -2000};
    std::string path = makeTempPath("Aestra_junk16.wav");
    writeTestWav(path, 16, 48000, 2, samples, true);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = decodeAudioFile(path, audio, sampleRate, channels);
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
    std::string path = makeTempPath("Aestra_24bit.wav");
    writeTestWav(path, 24, 44100, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = decodeAudioFile(path, audio, sampleRate, channels);
    fs::remove(path);

    // Forces stereo
    if (!ok || audio.size() != samples.size() * 2 || sampleRate != 44100 || channels != 2) {
        std::cout << " FAILED\n";
        return false;
    }

    if (!(audio[0] <= 1.0f && audio[0] > 0.99f) || !(audio[2] >= -1.0f && audio[2] < -0.99f)) {
        std::cout << " FAILED (24-bit values out of range)\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool run32BitPcmTest() {
    std::cout << "Test 4: 32-bit PCM conversion...";
    std::vector<int32_t> samples = {0, 1073741824, -2147483647 - 1};
    std::string path = makeTempPath("Aestra_32bit_pcm.wav");
    writeTestWav(path, 32, 48000, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = decodeAudioFile(path, audio, sampleRate, channels);
    fs::remove(path);

    if (!ok || audio.size() != samples.size() * 2 || sampleRate != 48000 || channels != 2) {
        std::cout << " FAILED\n";
        return false;
    }

    if (!approxEqual(audio[0], 0.0f) ||
        !approxEqual(audio[2], 0.5f, 1e-5f) ||
        !approxEqual(audio[4], -1.0f, 1e-5f)) {
        std::cout << " FAILED (32-bit PCM values out of range)\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool runInvalidMetadataTest() {
    std::cout << "Test 5: Invalid WAV metadata rejection...";
    std::vector<int32_t> samples = {0, 1};
    std::string path = makeTempPath("Aestra_invalid_channels.wav");
    writeTestWav(path, 16, 44100, 0, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool ok = decodeAudioFile(path, audio, sampleRate, channels);
    fs::remove(path);

    std::string badDataPath = makeTempPath("Aestra_invalid_data_size.wav");
    {
        std::ofstream out(badDataPath, std::ios::binary);
        out.write("RIFF", 4);
        writeUint32(out, 36 + 3);
        out.write("WAVE", 4);
        out.write("fmt ", 4);
        writeUint32(out, 16);
        writeUint16(out, 1);
        writeUint16(out, 2);
        writeUint32(out, 44100);
        writeUint32(out, 44100 * 2 * 2);
        writeUint16(out, 4);
        writeUint16(out, 16);
        out.write("data", 4);
        writeUint32(out, 3);
        const char malformed[3] = {};
        out.write(malformed, sizeof(malformed));
    }
    audio.clear();
    sampleRate = 0;
    channels = 0;
    const bool badDataOk = decodeAudioFile(badDataPath, audio, sampleRate, channels);
    fs::remove(badDataPath);

    if (ok || badDataOk) {
        std::cout << " FAILED\n";
        return false;
    }

    std::cout << " OK\n";
    return true;
}

bool runPreviewDecodeLimitTest() {
    std::cout << "Test 6: Preview decode frame limit...";
    std::vector<int32_t> samples(100, 1024);
    std::string path = makeTempPath("Aestra_preview_limit.wav");
    writeTestWav(path, 16, 48000, 1, samples, false);

    std::vector<float> audio;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    const bool ok = decodeAudioPreview(path, audio, sampleRate, channels, 4);
    fs::remove(path);

    if (!ok || sampleRate != 48000 || channels != 2 || audio.size() != 8) {
        std::cout << " FAILED (SR: " << sampleRate << " CH: " << channels << " Size: " << audio.size() << ")\n";
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
    success &= run32BitPcmTest();
    success &= runInvalidMetadataTest();
    success &= runPreviewDecodeLimitTest();

    if (success) {
        std::cout << "All WAV loader tests passed.\n";
        return 0;
    }

    std::cout << "WAV loader tests failed.\n";
    return 1;
}
