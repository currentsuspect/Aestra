// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "DSP/DelayLine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

using namespace Aestra::Audio;

void testBasicDelay() {
    DelayLine<float, 1024> delay;
    delay.setDelay(10);
    
    // Feed impulse
    float output = delay.process(1.0f);
    assert(output == 0.0f); // First output is silence
    
    // Feed 9 more zeros
    for (int i = 0; i < 9; ++i) {
        output = delay.process(0.0f);
        assert(output == 0.0f);
    }
    
    // 10th sample should be the impulse
    output = delay.process(0.0f);
    assert(output == 1.0f);
    
    // Rest should be silence
    output = delay.process(0.0f);
    assert(output == 0.0f);
    
    std::cout << "[PASS] testBasicDelay\n";
}

void testZeroDelay() {
    DelayLine<float, 1024> delay;
    delay.setDelay(0);
    
    // With zero delay, output should equal input immediately
    float output = delay.process(1.0f);
    assert(output == 1.0f);
    
    output = delay.process(0.5f);
    assert(output == 0.5f);
    
    std::cout << "[PASS] testZeroDelay\n";
}

void testBlockProcessing() {
    DelayLine<float, 1024> delay;
    delay.setDelay(5);
    
    std::vector<float> input = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> output(input.size());
    
    delay.processBlock(input.data(), output.data(), input.size());
    
    // First 5 samples should be silence (delay)
    for (int i = 0; i < 5; ++i) {
        assert(output[i] == 0.0f);
    }
    
    // 6th sample should be the impulse
    assert(output[5] == 1.0f);
    
    // Rest should be silence
    for (size_t i = 6; i < output.size(); ++i) {
        assert(output[i] == 0.0f);
    }
    
    std::cout << "[PASS] testBlockProcessing\n";
}

void testReset() {
    DelayLine<float, 1024> delay;
    delay.setDelay(10);
    
    // Fill with data
    for (int i = 0; i < 20; ++i) {
        delay.process(1.0f);
    }
    
    // Reset
    delay.reset();
    
    // Should output silence
    for (int i = 0; i < 20; ++i) {
        float output = delay.process(0.0f);
        assert(output == 0.0f);
    }
    
    std::cout << "[PASS] testReset\n";
}

void testMaxCapacity() {
    constexpr size_t kCapacity = 256;
    DelayLine<float, kCapacity> delay;
    
    // Set delay to max capacity
    delay.setDelay(kCapacity);
    
    // Feed impulse
    delay.process(1.0f);
    
    // Feed zeros until we should see the impulse
    for (size_t i = 0; i < kCapacity - 1; ++i) {
        float output = delay.process(0.0f);
        assert(output == 0.0f);
    }
    
    // Should see impulse at exactly kCapacity samples
    float output = delay.process(0.0f);
    assert(output == 1.0f);
    
    std::cout << "[PASS] testMaxCapacity\n";
}

void testExceedsCapacity() {
    constexpr size_t kCapacity = 256;
    DelayLine<float, kCapacity> delay;
    
    // Try to set delay beyond capacity (should clamp)
    delay.setDelay(kCapacity + 100);
    
    // Should clamp to capacity
    assert(delay.getDelay() == kCapacity);
    
    std::cout << "[PASS] testExceedsCapacity\n";
}

int main() {
    std::cout << "Running DelayLine tests...\n";
    
    testBasicDelay();
    testZeroDelay();
    testBlockProcessing();
    testReset();
    testMaxCapacity();
    testExceedsCapacity();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
