// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Automation target types
 */
enum class AutomationTarget : uint8_t { Volume = 0, Pan = 1, Custom = 255 };

struct AutomationPoint {
    uint64_t sample{0};
    float value{0.0f};
    double beat{0.0};  // For serialization
    float curve{0.0f}; // For serialization (curve tension)
    bool selected{false};
};

struct AutomationCurve {
    uint32_t laneId{0};
    std::vector<AutomationPoint> points;

    // Extended fields for full AutomationCurve API
    std::string name;
    AutomationTarget target{AutomationTarget::Custom};
    float defaultValue{0.0f};
    bool visible{true};

    AutomationCurve() = default;
    AutomationCurve(const std::string& n, AutomationTarget t) : name(n), target(t) {}

    /**
     * @brief Get the automation target type
     */
    AutomationTarget getAutomationTarget() const { return target; }

    /**
     * @brief Get target as double for JSON serialization
     */
    double getTarget() const { return static_cast<double>(target); }

    /**
     * @brief Get default value
     */
    float getDefaultValue() const { return defaultValue; }

    /**
     * @brief Get all points
     */
    std::vector<AutomationPoint>& getPoints() { return points; }
    const std::vector<AutomationPoint>& getPoints() const { return points; }

    /**
     * @brief Get interpolated value at a given beat position
     * @param beat The beat position
     * @return Interpolated value, or defaultValue if no points
     */
    float getValueAtBeat(double beat) const {
        if (points.empty()) {
            return defaultValue;
        }

        // Simple linear interpolation between points
        // Convert beat to sample for lookup (assume 48000 sample rate, 120 BPM as default)
        const double samplesPerBeat = (48000.0 * 60.0) / 120.0;
        const uint64_t targetSample = static_cast<uint64_t>(beat * samplesPerBeat);

        // Find surrounding points
        const AutomationPoint* prev = nullptr;
        const AutomationPoint* next = nullptr;

        for (const auto& pt : points) {
            if (pt.sample <= targetSample) {
                prev = &pt;
            } else {
                next = &pt;
                break;
            }
        }

        if (!prev && !next) {
            return defaultValue;
        }
        if (!prev) {
            return next->value;
        }
        if (!next) {
            return prev->value;
        }

        // Linear interpolation
        const double sampleRange = static_cast<double>(next->sample - prev->sample);
        if (sampleRange <= 0.0) {
            return prev->value;
        }

        const double t = static_cast<double>(targetSample - prev->sample) / sampleRange;
        return prev->value + static_cast<float>(t) * (next->value - prev->value);
    }

    /**
     * @brief Set the default value for this curve
     */
    void setDefaultValue(float val) { defaultValue = val; }

    /**
     * @brief Add a control point to the curve
     * @param beat Beat position
     * @param value Value at this point (normalized 0-1 for volume/pan)
     * @param tension Curve tension (0-1, currently unused)
     */
    void addPoint(double beat, float value, float /*tension*/ = 0.5f) {
        const double samplesPerBeat = (48000.0 * 60.0) / 120.0;
        AutomationPoint pt;
        pt.sample = static_cast<uint64_t>(beat * samplesPerBeat);
        pt.value = value;

        // Insert in sorted order
        auto it = points.begin();
        while (it != points.end() && it->sample < pt.sample) {
            ++it;
        }
        points.insert(it, pt);
    }

    void removePoint(size_t index) {
        if (index < points.size()) {
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void sortPoints() {
        std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
            return a.sample < b.sample;
        });
    }

    bool isVisible() const { return visible; }
};

} // namespace Audio
} // namespace Aestra
