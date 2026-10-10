// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AutomationCurve.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief How an automation lane reads and draws its curves (V8-A2).
 *
 * A lane holds several curves (Volume, Pan, plugin parameters) and edits one at
 * a time: the curve for its edited target. Pure functions, so the lane's
 * gesture code and the tests share one definition.
 */

/// The value span a target's curve covers. The engine applies Pan on -1…+1, so a
/// Pan curve is drawn and edited on that span; everything else is 0…1.
struct AutomationValueRange {
    double min = 0.0;
    double max = 1.0;
};

inline AutomationValueRange automationValueRange(AutomationTarget target) {
    return target == AutomationTarget::Pan ? AutomationValueRange{-1.0, 1.0} : AutomationValueRange{0.0, 1.0};
}

/// Value at a height in the lane: @p fraction 0 is the bottom edge, 1 the top.
inline double automationValueAtHeight(AutomationTarget target, double fraction) {
    const auto r = automationValueRange(target);
    return r.min + std::clamp(fraction, 0.0, 1.0) * (r.max - r.min);
}

/// Height in the lane (0 bottom, 1 top) where @p value is drawn.
inline double automationHeightOfValue(AutomationTarget target, double value) {
    const auto r = automationValueRange(target);
    return std::clamp((value - r.min) / (r.max - r.min), 0.0, 1.0);
}

/// Index of the lane's curve for @p target, or -1 when it has none.
inline int automationCurveIndexFor(const std::vector<AutomationCurve>& curves, AutomationTarget target) {
    for (size_t i = 0; i < curves.size(); ++i) {
        if (curves[i].getAutomationTarget() == target) return static_cast<int>(i);
    }
    return -1;
}

/// A new, neutral curve for @p target: unity volume, centre pan.
inline AutomationCurve makeAutomationCurve(AutomationTarget target) {
    AutomationCurve curve(target == AutomationTarget::Pan ? "Pan" : "Volume", target);
    curve.setDefaultValue(target == AutomationTarget::Pan ? 0.0f : 1.0f);
    return curve;
}

/// A point under the pointer: which curve, which point. -1/-1 for none.
struct AutomationPointHit {
    int curve = -1;
    int point = -1;
    bool found() const { return curve >= 0 && point >= 0; }
};

/**
 * @brief The point within @p radius of (x, y), the edited curve's points first.
 *
 * Every visible curve is reachable, so a lane with Volume and Pan can have
 * either edited: a press on another curve's point is how that curve becomes the
 * edited one. @p screenOf maps (curveIndex, point) to its screen position.
 */
template <typename ScreenOf>
AutomationPointHit hitAutomationPoint(const std::vector<AutomationCurve>& curves, int editedCurve, float x, float y,
                                      float radius, ScreenOf screenOf) {
    auto search = [&](int c) -> AutomationPointHit {
        if (c < 0 || c >= static_cast<int>(curves.size()) || !curves[static_cast<size_t>(c)].isVisible()) return {};
        const auto& points = curves[static_cast<size_t>(c)].getPoints();
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const auto [px, py] = screenOf(c, points[static_cast<size_t>(i)]);
            if (std::hypot(px - x, py - y) < radius) return {c, i};
        }
        return {};
    };
    if (const auto hit = search(editedCurve); hit.found()) return hit;
    for (int c = 0; c < static_cast<int>(curves.size()); ++c) {
        if (c == editedCurve) continue;
        if (const auto hit = search(c); hit.found()) return hit;
    }
    return {};
}

} // namespace Audio
} // namespace Aestra
