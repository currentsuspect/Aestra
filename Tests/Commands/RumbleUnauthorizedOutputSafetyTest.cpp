// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "RumbleInstance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace {
void setIsolatedLicenseDataDir() {
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "aestra_rumble_unauthorized_safety";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot);
#ifdef _WIN32
    _putenv_s("AESTRA_DATA_DIR", tempRoot.string().c_str());
#else
    setenv("AESTRA_DATA_DIR", tempRoot.string().c_str(), 1);
#endif
}

bool allSilent(const std::vector<float>& buffer) {
    return std::all_of(buffer.begin(), buffer.end(), [](float value) { return std::isfinite(value) && value == 0.0f; });
}
} // namespace

int main() {
    std::cout << "\n=== Rumble Unauthorized Output Safety Test ===\n";
    setIsolatedLicenseDataDir();

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
        std::cerr << "failed: Rumble process left stale output samples in gated/inactive processing path\n";
        return 1;
    }

    std::cout << "Rumble gated/inactive processing clears output buffers.\n";
    return 0;
}
