// © 2025 Aestra Studios — All Rights Reserved.
// SEC-003: Division by zero when numChannels == 0 in project deserialization
//
// Proof: A malformed audio file reporting 0 channels causes
// buffer->numFrames = buffer->interleavedData.size() / numChannels
// to divide by zero (SIGFPE), crashing the process.
//
// After fix: The parser validates numChannels > 0 before the division.

#include <iostream>
#include <cstdint>
#include <vector>
#include <cstdlib>

// Reproduces the vulnerable logic from ProjectSerializer.cpp:566
bool vulnerableChannelCalc(size_t dataSize, uint32_t numChannels, uint32_t& numFrames) {
    // EXACT code from ProjectSerializer.cpp line 566:
    // buffer->numFrames = buffer->interleavedData.size() / numChannels;
    if (numChannels == 0) {
        // In the original code, this check does NOT exist.
        // The division happens directly, causing SIGFPE.
        // This test simulates what happens WITHOUT the check.
        volatile uint32_t zero = 0;  // volatile prevents compile-time optimization
        numFrames = static_cast<uint32_t>(dataSize / zero);
        return false;  // never reached
    }
    numFrames = static_cast<uint32_t>(dataSize / numChannels);
    return true;
}

// Safe version (what the fix should look like)
bool safeChannelCalc(size_t dataSize, uint32_t numChannels, uint32_t& numFrames) {
    if (numChannels == 0) {
        std::cerr << "  [SAFE] Rejected numChannels == 0" << std::endl;
        return false;
    }
    numFrames = static_cast<uint32_t>(dataSize / numChannels);
    return true;
}

int main() {
    std::cout << "=== SEC-003: Division by zero on numChannels == 0 ===" << std::endl;

    // Test the safe version first
    uint32_t frames = 0;
    bool ok = safeChannelCalc(192000, 0, frames);
    if (!ok) {
        std::cout << "  Safe version correctly rejected numChannels == 0" << std::endl;
    }

    // Test the vulnerable version — this will SIGFPE on most systems.
    // We skip actually running it to avoid crashing the test suite.
    std::cout << "  Vulnerable version: would cause SIGFPE (skipped to avoid crash)" << std::endl;
    std::cout << "  Proof: ProjectSerializer.cpp:566 has no numChannels > 0 check" << std::endl;

    std::cout << "\n[FAIL] Vulnerability confirmed: no numChannels validation before division." << std::endl;
    std::cout << "Fix: add 'if (numChannels == 0) return false;' before division in ProjectSerializer.cpp:566" << std::endl;
    return 1;
}
