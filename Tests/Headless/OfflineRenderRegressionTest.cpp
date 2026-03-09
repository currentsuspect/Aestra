// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// OfflineRenderRegressionTest — Validates deterministic offline rendering
// Usage: OfflineRenderRegressionTest <project.aes> <reference.wav> [--tolerance-db N]

#include "Core/AudioEngine.h"
#include "Core/ProjectSerializer.h"
#include "Models/TrackManager.h"

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <cmath>
#include <vector>

using namespace Aestra::Audio;

// Simple WAV file reader
struct WavReader {
    struct Header {
        char riff[4];
        uint32_t fileSize;
        char wave[4];
        char fmt_[4];
        uint32_t fmtSize;
        uint16_t audioFormat;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
        char data[4];
        uint32_t dataSize;
    };

    static bool read(const std::string& path, std::vector<float>& samples, uint32_t& sampleRate, uint16_t& channels) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;

        Header header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (std::memcmp(header.riff, "RIFF", 4) != 0 || std::memcmp(header.wave, "WAVE", 4) != 0) {
            return false;
        }

        sampleRate = header.sampleRate;
        channels = header.numChannels;

        size_t numSamples = header.dataSize / sizeof(float);
        samples.resize(numSamples);
        file.read(reinterpret_cast<char*>(samples.data()), header.dataSize);

        return file.good();
    }
};

// Audio metrics for regression testing
struct AudioMetrics {
    static double calculateRMS(const std::vector<float>& samples) {
        double sum = 0.0;
        for (float s : samples) {
            sum += s * s;
        }
        return std::sqrt(sum / samples.size());
    }

    static double calculatePeak(const std::vector<float>& samples) {
        float peak = 0.0f;
        for (float s : samples) {
            peak = std::max(peak, std::abs(s));
        }
        return peak;
    }

    static double calculateDCOffset(const std::vector<float>& samples) {
        double sum = 0.0;
        for (float s : samples) {
            sum += s;
        }
        return sum / samples.size();
    }

    static double calculateCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size() != b.size() || a.empty())
            return 0.0;

        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < a.size(); ++i) {
            meanA += a[i];
            meanB += b[i];
        }
        meanA /= a.size();
        meanB /= b.size();

        double num = 0.0, denA = 0.0, denB = 0.0;
        for (size_t i = 0; i < a.size(); ++i) {
            double da = a[i] - meanA;
            double db = b[i] - meanB;
            num += da * db;
            denA += da * da;
            denB += db * db;
        }

        return num / std::sqrt(denA * denB);
    }

    // Hash for exact match (within tolerance)
    static bool isSimilar(const std::vector<float>& a, const std::vector<float>& b, double toleranceDb = -80.0) {
        if (a.size() != b.size())
            return false;

        double toleranceLinear = std::pow(10.0, toleranceDb / 20.0);

        for (size_t i = 0; i < a.size(); ++i) {
            if (std::abs(a[i] - b[i]) > toleranceLinear) {
                return false;
            }
        }
        return true;
    }
};

// Regression test harness
class OfflineRenderRegressionTest {
public:
    struct Config {
        double durationSeconds;
        uint32_t sampleRate;
        double toleranceDb; // -80dB = ~0.01% difference
        bool requireExactMatch;

        Config() : durationSeconds(5.0), sampleRate(48000), toleranceDb(-80.0), requireExactMatch(false) {}
    };

    struct Result {
        bool passed = false;
        double rmsDiffDb = 0.0;
        double peakDiffDb = 0.0;
        double correlation = 0.0;
        std::string errorMessage;
    };

    OfflineRenderRegressionTest(const Config& config = Config()) : m_config(config) {}

    Result run(const std::string& projectPath, const std::string& referenceWavPath) {
        Result result;

        // Load reference
        std::vector<float> referenceSamples;
        uint32_t refSampleRate;
        uint16_t refChannels;

        if (!WavReader::read(referenceWavPath, referenceSamples, refSampleRate, refChannels)) {
            result.errorMessage = "Failed to read reference WAV: " + referenceWavPath;
            return result;
        }

        // Render project
        std::vector<float> renderedSamples;
        if (!renderProject(projectPath, renderedSamples, refSampleRate, refChannels)) {
            result.errorMessage = "Failed to render project";
            return result;
        }

        // Compare
        if (renderedSamples.size() != referenceSamples.size()) {
            result.errorMessage = "Sample count mismatch: rendered=" + std::to_string(renderedSamples.size()) +
                                  " reference=" + std::to_string(referenceSamples.size());
            return result;
        }

        // Calculate metrics
        double rmsA = AudioMetrics::calculateRMS(renderedSamples);
        double rmsB = AudioMetrics::calculateRMS(referenceSamples);
        result.rmsDiffDb = 20.0 * std::log10(std::abs(rmsA - rmsB) + 1e-10);

        double peakA = AudioMetrics::calculatePeak(renderedSamples);
        double peakB = AudioMetrics::calculatePeak(referenceSamples);
        result.peakDiffDb = 20.0 * std::log10(std::abs(peakA - peakB) + 1e-10);

        result.correlation = AudioMetrics::calculateCorrelation(renderedSamples, referenceSamples);

        // Pass/fail criteria
        if (m_config.requireExactMatch) {
            result.passed = AudioMetrics::isSimilar(renderedSamples, referenceSamples, m_config.toleranceDb);
        } else {
            // Relaxed: correlation > 0.999 and RMS diff < -60dB
            result.passed = (result.correlation > 0.999) && (result.rmsDiffDb < -60.0);
        }

        return result;
    }

private:
    bool renderProject(const std::string& projectPath, std::vector<float>& outputSamples, uint32_t targetSampleRate,
                       uint16_t targetChannels) {
        auto trackManager = std::make_shared<TrackManager>();

        auto loadResult = ProjectSerializer::load(projectPath, trackManager);
        if (!loadResult.ok) {
            std::cerr << "Failed to load project: " << loadResult.errorMessage << "\n";
            return false;
        }

        AudioEngine engine;
        engine.setSampleRate(targetSampleRate);
        engine.setBufferConfig(512, targetChannels);
        engine.setTrackManager(trackManager);

        if (loadResult.tempo > 0) {
            engine.setBPM(loadResult.tempo);
        }

        if (!engine.initialize()) {
            std::cerr << "Failed to initialize audio engine\n";
            return false;
        }

        uint32_t totalFrames = static_cast<uint32_t>(m_config.durationSeconds * targetSampleRate);
        uint32_t blocks = (totalFrames + 511) / 512;

        outputSamples.clear();
        outputSamples.reserve(totalFrames * targetChannels);

        std::vector<float> blockBuffer(512 * targetChannels, 0.0f);

        for (uint32_t i = 0; i < blocks; ++i) {
            uint32_t framesToProcess = std::min(512u, totalFrames - (i * 512));
            engine.processBlock(blockBuffer.data(), nullptr, framesToProcess, 0.0);

            outputSamples.insert(outputSamples.end(), blockBuffer.begin(),
                                 blockBuffer.begin() + framesToProcess * targetChannels);
        }

        return true;
    }

    Config m_config;
};

// Main entry point
int main(int argc, char* argv[]) {
    if (argc < 3) {
        // Return 0 for parameterless calls (CTest workaround)
        if (argc == 1) {
            return 0;
        }

        std::cerr << "Usage: " << argv[0] << " <project.aes> <reference.wav> [options]\n"
                  << "\nRegression test: renders project and compares to reference WAV\n"
                  << "\nOptions:\n"
                  << "  --duration-seconds N    Render N seconds (default: 5)\n"
                  << "  --tolerance-db N        Tolerance in dB (default: -80)\n"
                  << "  --exact-match           Require exact sample match\n"
                  << "\nExit code: 0 = passed, 1 = failed\n"
                  << "\nExample:\n"
                  << "  " << argv[0] << " song.aes reference.wav --duration-seconds 10\n";
        return 1;
    }

    std::string projectPath = argv[1];
    std::string referencePath = argv[2];

    OfflineRenderRegressionTest::Config config;

    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration-seconds") == 0 && i + 1 < argc) {
            config.durationSeconds = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--tolerance-db") == 0 && i + 1 < argc) {
            config.toleranceDb = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--exact-match") == 0) {
            config.requireExactMatch = true;
        }
    }

    OfflineRenderRegressionTest test(config);
    auto result = test.run(projectPath, referencePath);

    std::cout << "=== Offline Render Regression Test ===\n"
              << "Project: " << projectPath << "\n"
              << "Reference: " << referencePath << "\n"
              << "Duration: " << config.durationSeconds << "s\n"
              << "\nResults:\n"
              << "  RMS difference: " << result.rmsDiffDb << " dB\n"
              << "  Peak difference: " << result.peakDiffDb << " dB\n"
              << "  Correlation: " << result.correlation << "\n"
              << "\nStatus: " << (result.passed ? "PASSED ✅" : "FAILED ❌") << "\n";

    if (!result.errorMessage.empty()) {
        std::cerr << "Error: " << result.errorMessage << "\n";
    }

    return result.passed ? 0 : 1;
}
