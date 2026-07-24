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
 * Prevents the uint8_t wrap-to-Volume hazard (raw value 256 wraps to 0 = Volume
 * under unguarded static_cast). Values >= 256 and negative values are clamped to
 * 255 (Custom). Values 2–254 are preserved as-is (renderer skips unrecognized
 * targets). This is the single ingress validator for any integer→AutomationTarget
 * path (JSON, API, migration).
 */
inline AutomationTarget automationTargetFromRawInt(int rawValue) noexcept {
    if (rawValue < 0) return AutomationTarget::Custom;
    return static_cast<AutomationTarget>(static_cast<uint8_t>(std::clamp(rawValue, 0, 255)));
}

struct AutomationPoint {
    // beat is the authoritative position domain: it is what the serializer
    // persists, what the UI edits, and what evaluation/sorting use. sample is
    // a legacy cache computed at addPoint() time with that moment's tempo —
    // it goes stale on tempo changes and UI drags, so nothing may evaluate or
    // sort by it.
    uint64_t sample{0};
    float value{0.0f};
    double beat{0.0};
    float curve{0.0f};    // Curve tension (serialized; rendering does not use it yet)
    bool selected{false}; // Selection state for UI
};

struct AutomationCurve {
    uint32_t laneId{0};
    /** Stable mixer insert destination. Zero means unassigned, not lane index zero. */
    uint32_t mixerChannelId{0};
    std::vector<AutomationPoint> points;

    // Extended fields for full AutomationCurve API
    std::string name;
    AutomationTarget target{AutomationTarget::Custom};
    float defaultValue{0.0f};

    // Plugin-parameter addressing — meaningful only when target == Custom.
    // effectSlot indexes the lane's channel effect chain; paramId is the
    // plugin's stable parameter id (stability guaranteed per AGENTS.md §19).
    // The engine applies Custom curves to Internal-format plugins only:
    // their parameter storage is atomic, so per-block setParameter from the
    // render thread is RT-safe; third-party formats need host param queues.
    uint32_t effectSlot{0};
    uint32_t paramId{0};

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
     * @brief Get interpolated value at a given beat position.
     *
     * Evaluation is purely beat-domain. The old implementation compared the
     * stale sample cache against a target derived from the *current* tempo,
     * which shifted every point in musical time after a BPM change and made
     * UI point drags (which update beat only) inaudible.
     *
     * @param beat The beat position
     * @return Interpolated value, or defaultValue if no points
     */
    float getValueAtBeat(double beat) const {
        if (points.empty()) {
            return defaultValue;
        }

        // Find surrounding points (points are sorted by beat)
        const AutomationPoint* prev = nullptr;
        const AutomationPoint* next = nullptr;

        for (const auto& pt : points) {
            if (pt.beat <= beat) {
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

        // Linear interpolation in beat domain
        const double beatRange = next->beat - prev->beat;
        if (beatRange <= 0.0) {
            return prev->value;
        }

        const double t = (beat - prev->beat) / beatRange;
        return prev->value + static_cast<float>(t) * (next->value - prev->value);
    }

    /**
     * @brief Legacy overload — samplesPerBeat is ignored; beat is authoritative.
     *
     * Kept so older call sites/tests compile; new code should call the
     * single-argument form.
     */
    float getValueAtBeat(double beat, double /*samplesPerBeat*/) const { return getValueAtBeat(beat); }

    /**
     * @brief Set the default value for this curve
     */
    void setDefaultValue(float val) { defaultValue = val; }

    /**
     * @brief Add a control point to the curve
     * @param beat Beat position
     * @param samplesPerBeat Samples per beat for the current project tempo/rate
     * @param value Value at this point (normalized 0-1 for volume/pan)
     * @param tension Curve tension for serialization (rendering does not use it yet)
     */
    void addPoint(double beat, float value, double samplesPerBeat, float tension = 0.5f) {
        AutomationPoint pt;
        pt.sample = static_cast<uint64_t>(beat * samplesPerBeat);
        pt.value = value;
        // beat and curve are what ProjectSerializer persists — leaving them at
        // their zero defaults made every point created through this API (loader,
        // UI draw path) serialize at beat 0 on the next save.
        pt.beat = beat;
        pt.curve = tension;

        // Insert in sorted order (beat domain — see AutomationPoint docs)
        auto it = points.begin();
        while (it != points.end() && it->beat < pt.beat) {
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
        // Beat domain: the UI drag path updates beat (not the sample cache),
        // so sorting by sample could scramble order after edits.
        std::sort(points.begin(), points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.beat < b.beat; });
    }

    bool isVisible() const { return true; }
};

} // namespace Audio
} // namespace Aestra
