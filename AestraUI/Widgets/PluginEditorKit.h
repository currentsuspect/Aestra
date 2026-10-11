// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"

#include <cstdint>
#include <string>

namespace AestraUI {

class NUIRenderer;

/**
 * @brief Shared drawing for built-in plugin editors.
 *
 * Every built-in editor used to carry its own copy of the same arc routine and,
 * in Sat, OTT, Filter and LFO, the same rotary knob, differing only in an accent
 * colour and a few pixel constants. Those copies drifted one constant at a time.
 * This is the one copy. An editor keeps what is genuinely its own (layout,
 * accent, visualizer) and asks the kit to draw the parts that are the same.
 *
 * Pure rendering: no parameter access, no input. Input lives in
 * AestraPanelWindow::handleKnobDrag(), next to the cursor capture it drives.
 */
namespace EditorKit {

constexpr float kPi = 3.14159265358979323846f;
/// A knob's travel: 270 degrees, starting down-left and sweeping clockwise.
constexpr float kKnobStart = kPi * 0.75f;
constexpr float kKnobSweep = kPi * 1.5f;

/// A polyline arc from @p startAngle to @p endAngle (radians, either order).
/// Draws nothing for an arc shorter than about a thousandth of a radian.
void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color);

/// The variable parts of the standard knob. The defaults are the house knob;
/// an editor overrides only what it actually draws differently.
struct KnobLook {
    bool large = false;            ///< hero knob: thicker arc, bigger needle tip and well
    bool bipolar = false;          ///< fill from the top-centre detent instead of from zero
    float smallNeedleInset = 9.0f; ///< needle stops this far inside the arc (small knob)
    float smallWellRatio = 0.28f;  ///< centre well radius / knob radius (small knob)
    float valueLabelPad = 14.0f;   ///< how far the value text may overhang each side
};

/// The standard rotary knob: inset disc, track arc, value arc, needle, centre
/// well, then the label and the value text underneath @p rect.
/// @param value     normalised 0..1
/// @param valueText the parameter's display string ("-3.0 dB")
void drawKnob(NUIRenderer& renderer, const NUIRect& rect, float value, const char* label, const std::string& valueText,
              NUIColor accent, NUIColor insetSurface, const KnobLook& look = {});

} // namespace EditorKit
} // namespace AestraUI
