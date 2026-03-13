// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleStateTest
// Verifies state save/load round-trip and versioned blob rejection for Aestra Rumble MVP.

#include "RumbleInstance.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using Aestra::Plugins::RumbleInstance;

namespace {
bool nearlyEqual(float a, float b, float epsilon = 1.0e-6f) {
    return std::fabs(a - b) <= epsilon;
}

bool testStateRoundTrip() {
    std::cout << "TEST: rumble state round-trip... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    rumble.setParameter(0, 0.62f); // Decay
    rumble.setParameter(1, 0.27f); // Drive
    rumble.setParameter(2, 0.41f); // Tone
    rumble.setParameter(3, 0.53f); // Output Gain

    const auto state = rumble.saveState();
    assert(!state.empty());

    RumbleInstance restored;
    assert(restored.initialize(48000.0, 512));
    assert(restored.loadState(state));

    assert(nearlyEqual(restored.getParameter(0), 0.62f));
    assert(nearlyEqual(restored.getParameter(1), 0.27f));
    assert(nearlyEqual(restored.getParameter(2), 0.41f));
    assert(nearlyEqual(restored.getParameter(3), 0.53f));

    std::cout << "✅ PASS\n";
    return true;
}

bool testRejectsCorruptStateSize() {
    std::cout << "TEST: reject corrupt state size... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    std::vector<uint8_t> invalid(3, 0xAA);
    assert(!rumble.loadState(invalid));

    std::cout << "✅ PASS\n";
    return true;
}

bool testRejectsInvalidMagic() {
    std::cout << "TEST: reject invalid state magic... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    auto state = rumble.saveState();
    assert(state.size() >= sizeof(uint32_t) * 2);

    uint32_t badMagic = 0xDEADBEEFu;
    std::memcpy(state.data(), &badMagic, sizeof(badMagic));
    assert(!rumble.loadState(state));

    std::cout << "✅ PASS\n";
    return true;
}

bool testRejectsUnsupportedVersion() {
    std::cout << "TEST: reject unsupported state version... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    auto state = rumble.saveState();
    assert(state.size() >= sizeof(uint32_t) * 2);

    uint32_t badVersion = 999u;
    std::memcpy(state.data() + sizeof(uint32_t), &badVersion, sizeof(badVersion));
    assert(!rumble.loadState(state));

    std::cout << "✅ PASS\n";
    return true;
}
} // namespace

int main() {
    std::cout << "\n=== Aestra Rumble State Test ===\n";

    testStateRoundTrip();
    testRejectsCorruptStateSize();
    testRejectsInvalidMagic();
    testRejectsUnsupportedVersion();

    std::cout << "\nAll Rumble state tests passed.\n";
    return 0;
}
