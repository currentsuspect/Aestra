// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <string>
#include <numeric>

#include "SampleRateConverter.h"
#include "NomadLog.h"

using namespace Nomad::Audio;

// Simple scoped timer
class ScopedTimer {
public:
    ScopedTimer(const std::string& name, uint64_t items) : m_name(name), m_items(items) {
        m_start = std::chrono::high_resolution_clock::now();
    }
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double durationMs = std::chrono::duration<double, std::milli>(end - m_start).count();
        double nsPerItem = (durationMs * 1000000.0) / static_cast<double>(m_items);

        std::cout << std::left << std::setw(30) << m_name
                  << " | Time: " << std::fixed << std::setprecision(2) << durationMs << " ms"
                  << " | Per Sample: " << std::setprecision(2) << nsPerItem << " ns"
                  << " | Speed: " << std::setprecision(2) << (static_cast<double>(m_items)/durationMs/1000.0) << " MHz"
                  << std::endl;
    }
private:
    std::string m_name;
    uint64_t m_items;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

void runBenchmark(const std::string& name, uint32_t srcRate, uint32_t dstRate, SRCQuality quality) {
    SampleRateConverter src;
    src.configure(srcRate, dstRate, 2, quality);

    // Create 10 seconds of audio (stereo)
    const uint32_t durationSec = 10;
    const uint32_t inputFrames = srcRate * durationSec;
    std::vector<float> input(inputFrames * 2);

    // Fill with sine wave
    for (uint32_t i = 0; i < inputFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(srcRate);
        float val = std::sin(2.0f * 3.14159f * 440.0f * t);
        input[i * 2] = val;     // L
        input[i * 2 + 1] = val; // R
    }

    // Output buffer (estimated)
    uint32_t estOut = static_cast<uint32_t>(static_cast<double>(inputFrames) * static_cast<double>(dstRate) / static_cast<double>(srcRate)) + 100;
    std::vector<float> output(estOut * 2);

    std::string qName;
    switch(quality) {
        case SRCQuality::Linear: qName = "Linear"; break;
        case SRCQuality::Cubic: qName = "Cubic"; break;
        case SRCQuality::Sinc8: qName = "Sinc8"; break;
        case SRCQuality::Sinc16: qName = "Sinc16"; break;
        case SRCQuality::Sinc64: qName = "Sinc64 (Turbo)"; break;
    }

    std::string fullName = name + " [" + qName + "]";

    // Warmup
    src.process(input.data(), 1024, output.data(), estOut);
    src.reset();

    // Benchmark
    {
        ScopedTimer timer(fullName, inputFrames);
        src.process(input.data(), inputFrames, output.data(), estOut);
    }
}

int main() {
    Nomad::Log::init(std::make_shared<Nomad::ConsoleLogger>(Nomad::LogLevel::Info));
    Nomad::Log::info("Starting Resampler Benchmark...");

    // Force SIMD check log
    SampleRateConverter::hasAVX();

    std::cout << "\n=== Upsampling (44.1 -> 48 kHz) ===\n";
    runBenchmark("Up 44.1->48", 44100, 48000, SRCQuality::Linear);
    runBenchmark("Up 44.1->48", 44100, 48000, SRCQuality::Cubic);
    runBenchmark("Up 44.1->48", 44100, 48000, SRCQuality::Sinc8);
    runBenchmark("Up 44.1->48", 44100, 48000, SRCQuality::Sinc16);
    runBenchmark("Up 44.1->48", 44100, 48000, SRCQuality::Sinc64);

    std::cout << "\n=== Downsampling (48 -> 44.1 kHz) ===\n";
    runBenchmark("Down 48->44.1", 48000, 44100, SRCQuality::Linear);
    runBenchmark("Down 48->44.1", 48000, 44100, SRCQuality::Cubic);
    runBenchmark("Down 48->44.1", 48000, 44100, SRCQuality::Sinc8);
    runBenchmark("Down 48->44.1", 48000, 44100, SRCQuality::Sinc16);
    runBenchmark("Down 48->44.1", 48000, 44100, SRCQuality::Sinc64);

    std::cout << "\n=== Extreme Upsampling (48 -> 192 kHz) ===\n";
    runBenchmark("Up 48->192", 48000, 192000, SRCQuality::Cubic);
    runBenchmark("Up 48->192", 48000, 192000, SRCQuality::Sinc64);

    return 0;
}
