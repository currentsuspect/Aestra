// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Automation target types
 *
 * Underlying type is uint8_t. Known values: Volume(0), Pan(1), Custom(255).
 * Values 2–254 are valid uint8_t but unrecognized — renderer skips them.
 * Raw integer 256+ wraps to 0 (Volume) under unguarded static_cast.
 * Use automationTargetFromRawInt() for any integer-origin ingress.
 */
enum class AutomationTarget : uint8_t { Volume = 0, Pan = 1, Custom = 255 };

/**
 * @brief Clamp a raw integer to a safe AutomationTarget.
 *
 * Prevents the uint8_t wrap-AutomationTarget(wraptoVolume- hazard (raw value 256
 * wraps to 0 = Volume under unguarded static_cast). Values >= 256 are clamped to
 * 255 (Custom). Values 2–254 are preserved as-is (renderer skips unrecognized
 * targets). This is the single ingress validator for any integer→AutomationTarget
 * path (JSON, API, migration).
 */
inline AutomationTarget automationTargetFromRawInt(int rawValue) noexcept {
    return static_cast<AutomationTarget>(static_cast<uint8_t>(std::clamp(rawValue, 0, 255)));
}

struct AutomationPoint {
    uint64_t sample{0};
    float value{0.0f};
    double beat{0.0};  // For serialization
    float curve{0.0f}; // For serialization (curve tension)
    bool selected{false}; // Selection state for UI
};

struct AutomationCurve {
    uint32_t laneId{0};
    std::vector<AutomationPoint> points;

    // Extended fields for full AutomationCurve API
    std::string name;
    AutomationTarget target{AutomationTarget::Custom};
    float defaultValue{0.0f};

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
    const std::vector<AutomationPoint>& getPoints() const { return points; }

    std::vector<AutomationPoint>& getPoints() { return points; }

    /**
     * @brief Get interpolated value at a given beat position
     * @param beat The beat position
     * @param samplesPerBeat Samples per beat for the current project tempo/rate
     * @return Interpolated value, or defaultValue if no points
     */
    float getValueAtBeat(double beat, double samplesPerBeat) const {
        if (points.empty()) {
            return defaultValue;
        }

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
     * @param samplesPerBeat Samples per beat for the current project tempo/rate
     * @param value Value at this point (normalized 0-1 for volume/pan)
     * @param tension Curve tension (0-1, currently unused)
     */
    void addPoint(double beat, float value, double samplesPerBeat, float /*tension*/ = 0.5f) {
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
            points.erase(points.begin() + index);
        }
    }

    void sortPoints() {
        std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
            return a.sample < b.sample;
        });
    }

    bool isVisible() const { return true; }
};

} // namespace Audio
} // namespace Aestra
