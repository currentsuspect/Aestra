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

/**
 * @brief The value a curve of this target evaluates to when it has no points.
 *
 * A Volume curve is a gain multiplier, so its neutral is unity — 0.0f is
 * silence, not "no automation". Pan is bipolar with centre at 0. Custom is
 * plugin-defined, where 0 is the only safe assumption.
 *
 * This lives in the model deliberately (FD-20): a Volume curve is unity
 * because it is a Volume curve, not because one UI path remembered to say so.
 * Before this existed, exactly one call site patched it — the UI's
 * create-on-first-click path — so a curve arriving by any other route was
 * neutral-at-silence.
 */
inline constexpr float neutralDefaultFor(AutomationTarget target) noexcept {
    return target == AutomationTarget::Volume ? 1.0f : 0.0f;
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
    // deviceInstanceId is the semantic target (Automation Identity Contract):
    // which plugin instance, resolved through the chain snapshot's
    // findSlotByInstanceId. effectSlot is LEGACY — kept only for v1 project
    // migration and older-build compatibility; it never participates in
    // resolution. paramId is the plugin's stable parameter id (AGENTS.md §19).
    // The engine applies Custom curves to Internal-format plugins only:
    // their parameter storage is atomic, so per-block setParameter from the
    // render thread is RT-safe; third-party formats need host param queues.
    uint64_t deviceInstanceId{0};
    uint32_t effectSlot{0};
    uint32_t paramId{0};

    AutomationCurve() = default;
    // defaultValue follows the target, so no construction path can produce a
    // Volume curve that is neutral-at-silence. Only reachable when points is
    // empty — getValueAtBeat returns a point value in every other case.
    AutomationCurve(const std::string& n, AutomationTarget t)
        : name(n), target(t), defaultValue(neutralDefaultFor(t)) {}

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

/**
 * @brief Compare two automation points by their undoable content.
 *
 * `selected` is UI state and `sample` is a stale tempo cache that nothing may
 * evaluate or sort by (see AutomationPoint), so neither is content. Without
 * this distinction a click that merely selected a point would register as an
 * edit and push an undo step for nothing.
 */
inline bool automationPointsEqual(const AutomationPoint& a, const AutomationPoint& b) noexcept {
    return a.beat == b.beat && a.value == b.value && a.curve == b.curve;
}

/** @brief Compare two curve vectors by undoable content; the V8-A1 no-op guard. */
inline bool automationCurvesEqual(const std::vector<AutomationCurve>& a,
                                  const std::vector<AutomationCurve>& b) noexcept {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const AutomationCurve& x = a[i];
        const AutomationCurve& y = b[i];
        if (x.name != y.name || x.target != y.target || x.mixerChannelId != y.mixerChannelId ||
            x.defaultValue != y.defaultValue || x.deviceInstanceId != y.deviceInstanceId || x.paramId != y.paramId) {
            return false;
        }
        if (x.points.size() != y.points.size())
            return false;
        for (size_t p = 0; p < x.points.size(); ++p) {
            if (!automationPointsEqual(x.points[p], y.points[p]))
                return false;
        }
    }
    return true;
}

} // namespace Audio
} // namespace Aestra
