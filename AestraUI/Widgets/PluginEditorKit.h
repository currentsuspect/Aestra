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


// ---------------------------------------------------------------------------------------------------------------
// Ember control kit (FD-73). The flat look every built-in editor is moving to: sharp corners, 1 px edges, one
// violet accent, teal and amber only on meters. Every colour is a theme token, resolved on each call, so a theme
// change reaches the editor on the next frame. Geometry is the mock's, in pixels.
// ---------------------------------------------------------------------------------------------------------------

/// The theme tokens the kit paints with, named by role rather than by value.
struct EmberColors {
    NUIColor well;      ///< pure-black bed of a graph or meter
    NUIColor body;      ///< editor body; also the fill under a control
    NUIColor panel;     ///< title and header strips
    NUIColor control;   ///< knob disc, dropdown fill
    NUIColor raised;    ///< hovered fill
    NUIColor border;    ///< hairline between regions
    NUIColor edge;      ///< edge of a control (3:1 against the body)
    NUIColor ink;       ///< values, selected text
    NUIColor inkSoft;   ///< labels
    NUIColor inkMuted;  ///< section headers, axis text
    NUIColor inkOff;    ///< bypassed or disabled marks
    NUIColor primary;   ///< value arc, selected underline
    NUIColor primaryHover;
    NUIColor secondary; ///< curves and readable violet
    NUIColor selected;  ///< fill of the "on" segment or toggle
    NUIColor guide;     ///< threshold or crossover line
    NUIColor meter;     ///< level meter body
    NUIColor warn;      ///< meter above -6 dB, gain reduction
    NUIColor gridMajor;
    NUIColor gridMinor;
};
EmberColors emberColors();

enum class KnobSize { Large, Medium, Small };
enum class ControlState { Idle, Hover, Drag, Disabled };

/// Width of a knob's cell (its label and value sit inside it) and the disc's diameter.
float emberKnobCellWidth(KnobSize size);
float emberKnobDiscSize(KnobSize size);
/// Height of a knob's cell: the disc, its label and its value.
float emberKnobCellHeight(KnobSize size);

/// Left-aligned text, vertically centred on @p centreY.
void drawTextLeft(NUIRenderer& renderer, const std::string& text, float x, float centreY, float size, NUIColor color);
/// Right-aligned text ending at @p rightX, vertically centred on @p centreY.
void drawTextRight(NUIRenderer& renderer, const std::string& text, float rightX, float centreY, float size,
                   NUIColor color);
/// Upper-case, letter-spaced text. Returns the width drawn.
float drawCaps(NUIRenderer& renderer, const std::string& text, float x, float centreY, float size, NUIColor color);

/// Section header: caps label, then a hairline to the end of the span.
void drawSectionHeader(NUIRenderer& renderer, float x, float y, float width, const std::string& label);
/// Pure-black display well with a 1 px border.
void drawWell(NUIRenderer& renderer, const NUIRect& rect);

/// The Ember knob in a cell whose top-left is @p cell. Draws the disc, arcs, needle, label and value.
void drawEmberKnob(NUIRenderer& renderer, const NUIPoint& cell, KnobSize size, float value, const std::string& label,
                   const std::string& valueText, ControlState state, bool bipolar, bool bypassed);

/// Segmented control: one cell per label, the selected one filled with a primary underline.
void drawSegmented(NUIRenderer& renderer, const NUIRect& rect, const std::string* labels, int count, int selected,
                   int hovered, bool bypassed);
/// A labelled on/off button with a square indicator.
void drawToggleButton(NUIRenderer& renderer, const NUIRect& rect, const std::string& label, bool on, bool hovered,
                      bool bypassed);
/// A plain labelled push button.
void drawPushButton(NUIRenderer& renderer, const NUIRect& rect, const std::string& label, bool hovered);

/// A horizontal slider: label, 2 px track, 4 px fill, thumb, value at the right. The track runs from
/// trackLeft() to trackRight() so an editor can hit-test the same span it draws.
struct EmberSliderGeometry {
    float x0 = 0.0f; ///< track start
    float x1 = 0.0f; ///< track end
};
EmberSliderGeometry emberSliderGeometry(const NUIRect& rect, float labelWidth);
void drawEmberSlider(NUIRenderer& renderer, const NUIRect& rect, float labelWidth, const std::string& label,
                     float value, const std::string& valueText, ControlState state, bool bypassed);

/// x for @p hz on a log axis that spans @p lo..@p hi across [x, x + width].
float logFrequencyX(float hz, float x, float width, float lo = 20.0f, float hi = 20000.0f);
/// Log-frequency grid over @p plot: a hairline at every 10 Hz step (stronger on decades) and the 20, 100, 1k,
/// 10k, 20k labels along the bottom.
void drawFrequencyGrid(NUIRenderer& renderer, const NUIRect& plot);

enum class MeterKind { Level, GainReduction };
/// Vertical meter. Level fills from the bottom over -48..0 dB (teal, amber above -6 dB) with a peak cap;
/// gain reduction hangs from the top over 0..-24 dB.
void drawVerticalMeter(NUIRenderer& renderer, const NUIRect& rect, float db, MeterKind kind, bool bypassed);

} // namespace EditorKit
} // namespace AestraUI
