// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"

namespace AestraUI {

class NUIRenderer;
struct NUIThemeProperties;

/**
 * @brief The Design Spec's three depths, drawn once so every part obeys the same light (FD-79).
 *
 * Light comes from above. A **bed** is sunk: darker than what surrounds it, shaded on its top-left inner edge with
 * a faint highlight opposite. A **surface** is flat: a plain fill, no shadow, no outline. An **object** is a thing
 * you grab: lit on top, shaded underneath, casting a soft shadow down; grabbing it lifts it a little more.
 * Floating cards (menus, the preset fold, toasts, tooltips, dialogs) are objects too.
 *
 * Every helper here is a handful of SDF quads, so none of it costs a blur pass per frame.
 */
namespace Depth {

/// How deep a bed is cut. Wells for fields and switch tracks are Shallow; transport keys held down and the
/// mixer bed are Deep; meter and fader slots are Slot.
enum class Cut { Shallow, Deep, Slot };

/// A sunk well filled with @p fill (backgroundPrimary when @p fill is omitted). High contrast adds the inner edge
/// that keeps the control findable.
void bed(NUIRenderer& renderer, const NUIRect& rect, float radius, Cut cut = Cut::Shallow);
void bed(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill, Cut cut = Cut::Shallow);

/// What an object is drawn with: its base colour and whether it is held (grabbed, lifted).
struct ObjectLook {
    NUIColor base;
    bool held = false;
    float lightTop = 0.16f;    ///< white mixed into the top of the cap
    float shadeBottom = 0.24f; ///< black mixed into its bottom
};
/// A cap, thumb or knob face sitting on whatever is beneath it.
void object(NUIRenderer& renderer, const NUIRect& rect, float radius, const ObjectLook& look);
/// The default object: surfaceRaised, not held.
void object(NUIRenderer& renderer, const NUIRect& rect, float radius, bool held = false);

/// A surface resting on a bed (Design Spec, Mixer: a strip on the mixer bed): a flat fill of @p fill, set apart by
/// a soft shadow under it, never by a line.
void restingPanel(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill);

/// Design Spec, Empty state (locked): the sunk well (8px round) a panel shows where its content would be, inset
/// 8px inside @p area.
void emptyWell(NUIRenderer& renderer, const NUIRect& area);

/// A floating card: menus, folds, tooltips, toasts and dialogs. @p lift scales the shadow (1 for menus,
/// 2 for a dialog over the dimmed window).
void floatingCard(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill, float lift = 1.0f);

/// The one outline everywhere: 2px of focusRing, 2px off the control, on keyboard focus only.
void focusRing(NUIRenderer& renderer, const NUIRect& rect, float radius, float gap = 2.0f);

/// The fixed pane of glass over a read-only display (graphs, VU faces, scopes). Draw it after the display.
/// Never over a control, the timeline, clips or the mixer's bar meters.
void glass(NUIRenderer& renderer, const NUIRect& rect, float radius);

/// Design Spec, Slider (locked): a fader on its side. A 6px sunk slot across @p track (centred vertically), the
/// travelled part in primary from the left, or from @p origin for a bipolar value (pan, width), with a 1px centre tick.
/// @p t and @p origin are 0..1 along the slot. @p enabled false draws the travel in textDisabled.
void sliderSlot(NUIRenderer& renderer, const NUIRect& track, float t, float origin = 0.0f, bool enabled = true);
/// The slider's cap: the fader cap turned 90 degrees, 22 by 16, lit on top, vertical grip ridges and one
/// textPrimary centre line; @p held lifts it.
void sliderCap(NUIRenderer& renderer, const NUIPoint& centre, bool held = false);

/// Design Spec, Mixer (locked): a fader cap you could pick up. Lit on top, grip ridges, one centre line, a shadow cast
/// down into the slot; @p held lifts it. A channel cap is surfaceRaised with a textPrimary line; the master cap is
/// mixed from textPrimary (silver on dark themes, graphite on light) with a primary line, so it always reads as the
/// master.
void faderCap(NUIRenderer& renderer, const NUIRect& cap, bool master, bool held = false);

/// Design Spec, Knob (locked): the cap is a lit disc of @p radius at @p centre, shaded underneath and casting a soft
/// shadow, with a textPrimary pointer at @p angle (radians). The pointer turns; the light does not. @p held lifts it.
void knobCap(NUIRenderer& renderer, const NUIPoint& centre, float radius, float angle, bool held = false,
             bool enabled = true);

/// The radius scale (Design Spec rule 4). Clips, the timeline and plugin windows are sharp.
namespace Radius {
inline constexpr float sharp = 0.0f;
inline constexpr float small = 3.0f;   ///< fader caps, key tags, M / S / R
inline constexpr float medium = 8.0f;  ///< transport keys, strips, rows inside a card
inline constexpr float large = 12.0f;  ///< floating cards
/// A pill on a rect of height @p h.
inline float pill(float h) { return h * 0.5f; }
} // namespace Radius

} // namespace Depth
} // namespace AestraUI
