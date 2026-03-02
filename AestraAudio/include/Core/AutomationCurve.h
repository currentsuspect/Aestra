// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

struct AutomationPoint {
    uint64_t sample{0};
    float value{0.0f};
};

struct AutomationCurve {
    uint32_t laneId{0};
    std::vector<AutomationPoint> points;
};

} // namespace Audio
} // namespace Aestra
