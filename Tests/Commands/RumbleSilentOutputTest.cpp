// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// With no note playing, Rumble clears the output buffers instead of leaving whatever the host put there.

#include "RumbleInstance.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
bool allSilent(const std::vector<float>& buffer) {
    return std::all_of(buffer.begin(), buffer.end(), [](float value) { return std::isfinite(value) && value == 0.0f; });
}
} // namespace

int main() {
    std::cout << "\n=== Rumble Silent Output Test ===\n";

    Aestra::Plugins::RumbleInstance rumble;
    if (!rumble.initialize(48000.0, 64)) {
        std::cerr << "failed: Rumble did not initialize\n";
        return 1;
    }

    std::vector<float> left(64, 0.25f);
    std::vector<float> right(64, -0.25f);
    float* outputs[2] = {left.data(), right.data()};

    rumble.process(nullptr, outputs, 0, 2, 64, nullptr, nullptr);

    if (!allSilent(left) || !allSilent(right)) {
        std::cerr << "failed: Rumble left stale samples in the output with no note playing\n";
        return 1;
    }

    std::cout << "Rumble clears its output when no note is playing.\n";
    return 0;
}
