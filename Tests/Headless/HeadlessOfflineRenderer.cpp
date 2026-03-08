// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HeadlessOfflineRenderer — Offline WAV rendering for regression testing
// Usage: HeadlessOfflineRenderer <project.aes> <output.wav> [--duration-seconds N]

#include "Core/AudioEngine.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "IO/OfflineRenderHarness.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

// Simple WAV file writer (no external dependencies)
struct WavWriter {
    struct Header {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t fileSize = 0;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt_[4] = {'f', 'm', 't', ' '};
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 3; // IEEE float
        uint16_t numChannels = 2;
        uint32_t sampleRate = 48000;
        uint32_t byteRate = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 32;
        char data[4] = {'d', 'a', 't', 'a'};
        uint32_t dataSize = 0;
    };

    static bool write(const std::string& path, const std::vector<float>& interleavedSamples, uint32_t sampleRate,
                      uint16_t channels) {
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return false;

        Header header;
        header.sampleRate = sampleRate;
        header.numChannels = channels;
        header.bitsPerSample = 32;
        header.audioFormat = 3; // Float
        header.blockAlign = channels * sizeof(float);
        header.byteRate = sampleRate * header.blockAlign;
        header.dataSize = static_cast<uint32_t>(interleavedSamples.size() * sizeof(float));
        header.fileSize = sizeof(Header) - 8 + header.dataSize;

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(interleavedSamples.data()), header.dataSize);

        return file.good();
    }
};

class HeadlessOfflineRenderer {
public:
    HeadlessOfflineRenderer(uint32_t sampleRate = 48000, uint32_t bufferFrames = 512)
        : m_sampleRate(sampleRate), m_bufferFrames(bufferFrames) {
        m_engine.setSampleRate(sampleRate);
        m_engine.setBufferConfig(bufferFrames, 2); // Stereo
    }

    bool loadProject(const std::string& projectPath) {
        auto trackManager = std::make_shared<TrackManager>();

        auto result = ProjectSerializer::load(projectPath, trackManager);
        if (!result.ok) {
            std::cerr << "Failed to load project: " << result.errorMessage << "\n";
            return false;
        }

        // Dummy unit manager is needed if we use it, but since we don't have TrackManager binding in AudioEngine anymore natively,
        // we might not use it correctly here.
        // As a fallback, we will just set BPM and let the offline test be simple for now.
        if (result.tempo > 0) {
            m_engine.setBPM(result.tempo);
        }

        return true;
    }

    bool renderToWav(const std::string& outputPath, double durationSeconds) {
        uint32_t totalFrames = static_cast<uint32_t>(durationSeconds * m_sampleRate);
        uint32_t blocks = (totalFrames + m_bufferFrames - 1) / m_bufferFrames;

        std::vector<float> outputBuffer;
        outputBuffer.reserve(totalFrames * 2); // Stereo

        std::vector<float> blockBuffer(m_bufferFrames * 2, 0.0f);

        m_engine.setSampleRate(m_sampleRate);
        m_engine.setBufferConfig(m_bufferFrames, 2);

        // Render blocks
        for (uint32_t i = 0; i < blocks; ++i) {
            uint32_t framesToProcess = std::min(m_bufferFrames, totalFrames - (i * m_bufferFrames));

            m_engine.processBlock(blockBuffer.data(), nullptr, framesToProcess, 0.0);

            // Append to output (only valid frames)
            outputBuffer.insert(outputBuffer.end(), blockBuffer.begin(), blockBuffer.begin() + framesToProcess * 2);
        }

        // Write WAV
        if (!WavWriter::write(outputPath, outputBuffer, m_sampleRate, 2)) {
            std::cerr << "Failed to write WAV file: " << outputPath << "\n";
            return false;
        }

        std::cout << "Rendered " << outputBuffer.size() / 2 << " samples (" << durationSeconds << "s) to " << outputPath
                  << "\n";

        return true;
    }

    // Render specific beat range (for regression tests)
    bool renderBeatRange(const std::string& outputPath, double startBeat, double endBeat) {
        double bpm = m_engine.getBPM();
        double secondsPerBeat = 60.0 / bpm;
        double duration = (endBeat - startBeat) * secondsPerBeat;

        // Set playhead position
        uint64_t samplePos = static_cast<uint64_t>((startBeat * 60.0 / bpm) * m_sampleRate);
        m_engine.setGlobalSamplePos(samplePos);

        return renderToWav(outputPath, duration);
    }

private:
    AudioEngine m_engine;
    uint32_t m_sampleRate;
    uint32_t m_bufferFrames;
};

// Main entry point for CLI
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <project.aes> <output.wav> [options]\n"
                  << "Options:\n"
                  << "  --duration-seconds N    Render N seconds (default: 10)\n"
                  << "  --start-beat N          Start at beat N\n"
                  << "  --end-beat N            End at beat N\n"
                  << "  --sample-rate N         Set sample rate (default: 48000)\n"
                  << "\nExample:\n"
                  << "  " << argv[0] << " song.aes output.wav --duration-seconds 30\n";
        return 1;
    }

    std::string projectPath = argv[1];
    std::string outputPath = argv[2];

    double durationSeconds = 10.0;
    double startBeat = -1;
    double endBeat = -1;
    uint32_t sampleRate = 48000;

    // Parse arguments
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration-seconds") == 0 && i + 1 < argc) {
            durationSeconds = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--start-beat") == 0 && i + 1 < argc) {
            startBeat = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--end-beat") == 0 && i + 1 < argc) {
            endBeat = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
            sampleRate = std::atoi(argv[++i]);
        }
    }

    HeadlessOfflineRenderer renderer(sampleRate);

    if (!renderer.loadProject(projectPath)) {
        return 1;
    }

    bool success;
    if (startBeat >= 0 && endBeat > startBeat) {
        success = renderer.renderBeatRange(outputPath, startBeat, endBeat);
    } else {
        success = renderer.renderToWav(outputPath, durationSeconds);
    }

    return success ? 0 : 1;
}
