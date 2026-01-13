#include "../NomadAudio/include/SampleRateConverter.h"
#include <vector>
#include <cmath>
#include <iostream>
#include <cassert>

namespace Nomad {
    namespace Log {
        void info(const std::string& msg) { std::cout << "[INFO] " << msg << std::endl; }
        void warning(const std::string& msg) { std::cerr << "[WARN] " << msg << std::endl; }
        void error(const std::string& msg) { std::cerr << "[ERROR] " << msg << std::endl; }
    }
}

using namespace Nomad::Audio;

void testContinuity() {
    std::cout << "Testing Upsampling Continuity (44.1k -> 48k)..." << std::endl;
    SampleRateConverter src;
    const uint32_t inputRate = 44100;
    const uint32_t outputRate = 48000;
    src.configure(inputRate, outputRate, 1, SRCQuality::Sinc16);

    // Generate a sine wave
    const size_t blockSize = 100;
    std::vector<float> inputBlock1(blockSize);
    std::vector<float> inputBlock2(blockSize);

    double phase = 0.0;
    double phaseInc = 2.0 * 3.14159 * 440.0 / inputRate;

    for (size_t i = 0; i < blockSize; ++i) {
        inputBlock1[i] = std::sin(phase);
        phase += phaseInc;
    }
    for (size_t i = 0; i < blockSize; ++i) {
        inputBlock2[i] = std::sin(phase);
        phase += phaseInc;
    }

    std::vector<float> outputBlock1(blockSize * 2);
    std::vector<float> outputBlock2(blockSize * 2);

    uint32_t frames1 = src.process(inputBlock1.data(), blockSize, outputBlock1.data(), outputBlock1.size());
    uint32_t frames2 = src.process(inputBlock2.data(), blockSize, outputBlock2.data(), outputBlock2.size());

    float lastB1 = outputBlock1[frames1 - 1];
    float firstB2 = outputBlock2[0];

    float delta = std::abs(firstB2 - lastB1);
    std::cout << "Delta: " << delta << std::endl;

    if (delta > 0.15) {
        std::cerr << "FAILURE: Discontinuity detected." << std::endl;
        exit(1);
    }
    std::cout << "Upsampling Continuity Passed." << std::endl;
}

void testDownsampling() {
    std::cout << "Testing Downsampling (96k -> 44.1k)..." << std::endl;
    SampleRateConverter src;
    const uint32_t inputRate = 96000;
    const uint32_t outputRate = 44100;
    src.configure(inputRate, outputRate, 1, SRCQuality::Sinc16);

    // Verify it uses local bank (implicitly check if process crashes or returns 0)
    std::vector<float> input(100, 0.5f);
    std::vector<float> output(100);

    uint32_t frames = src.process(input.data(), 100, output.data(), 100);

    if (frames == 0) {
        std::cerr << "FAILURE: Downsampling produced 0 frames." << std::endl;
        exit(1);
    }
    std::cout << "Downsampling processed " << frames << " frames. Passed." << std::endl;
}

int main() {
    testContinuity();
    testDownsampling();
    return 0;
}
