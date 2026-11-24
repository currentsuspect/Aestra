#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace {
float quantizeTypeNorm(float norm) {
    static constexpr float steps[] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 4; ++i) {
        const float d = std::abs(norm - steps[i]);
        if (d < bestD) {
            bestD = d;
            best = steps[i];
        }
    }
    return best;
}

float quantizeSlopeNorm(float norm) {
    static constexpr float steps[] = {0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 7; ++i) {
        float d = std::abs(norm - steps[i]);
        if (d < bestD) {
            bestD = d;
            best = steps[i];
        }
    }
    return best;
}

} // namespace
} // namespace AestraUI
