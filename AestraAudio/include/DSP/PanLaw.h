// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace PanLaw {

constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kEqualPowerCenterGain = 0.7071067811865475f;

inline void equalPower(float pan, float gain, float& leftGain, float& rightGain) noexcept {
    const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
    const float p = (clampedPan + 1.0f) * 0.5f;
    leftGain = std::cos(p * kHalfPi) * gain;
    rightGain = std::sin(p * kHalfPi) * gain;
}

inline void equalPower(double pan, double gain, double& leftGain, double& rightGain) noexcept {
    const double clampedPan = std::clamp(pan, -1.0, 1.0);
    const double p = (clampedPan + 1.0) * 0.5;
    leftGain = std::cos(p * static_cast<double>(kHalfPi)) * gain;
    rightGain = std::sin(p * static_cast<double>(kHalfPi)) * gain;
}

} // namespace PanLaw
} // namespace Audio
} // namespace Aestra
