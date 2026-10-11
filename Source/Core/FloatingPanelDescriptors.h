// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Floating-panel registry — the UI-FREE half of P5.
//
// Geometry and identity live here rather than in AestraContent.cpp so a headless
// test can pin the registry's completeness. AestraContent.h drags in the whole
// UI layer, and no test includes it, because AESTRA_HEADLESS_ONLY builds do not
// compile those targets — a test that needed it could only run in a lane that
// most of CI does not.
//
// This was the reason the "one descriptor table" needed splitting in two. The
// panel POINTER cannot live here: it is a pointer-to-member-function on
// AestraContent, which needs that class complete. So this file owns what a
// headless build can check — that every floating ViewType has exactly one row,
// with a unique store key and usable dimensions — and AestraContent.cpp pairs
// each row with its panel accessor. The pairing is a compile error if a row names
// a ViewType the accessor table does not cover, so the two halves cannot drift
// apart silently.

#pragma once

#include "UISurfaceStore.h"
#include "ViewTypes.h"

#include <array>
#include <cstddef>

namespace Aestra {
namespace Content {

/**
 * One floating panel: its identity and its geometry.
 *
 * The geometry is the surviving half of the V8-C14 step-5 migration. Sizes
 * preserve the previous first-open geometry (ViewState defaults for
 * mixer/piano-roll/sequencer, toggle literals for history/takes); anchors
 * preserve the previous effective placement (top-left of free space for the
 * workspace trio, centred-x below the transport bar for history/takes).
 * Minimums are the previous wireFloatingPanel floors (takes inherits the
 * WindowPanel default).
 */
struct FloatingPanelGeometry {
    Audio::ViewType view;
    const char* storeKey;
    double defaultWidth;
    double defaultHeight;
    double defaultAnchorX;
    double defaultAnchorY;
    double minWidth;
    double minHeight;
};

/**
 * Every floating panel, in ViewType order.
 *
 * `Playlist` is absent BY DESIGN: the timeline is the workspace itself rather
 * than an overlay panel, so it has no surface geometry and no store key. That
 * absence is why this is a lookup rather than an array indexed by ViewType —
 * and it is the case the old `default:` arm got wrong, handing Playlist the
 * mixer's key and size.
 *
 * Adding a ViewType means adding a row here and an accessor in
 * AestraContent.cpp. PanelDescriptorRegistryTest fails until both are done.
 */
inline constexpr std::array<FloatingPanelGeometry, 5> kFloatingPanelGeometry{{
    {Audio::ViewType::Mixer, UISurfaceKeys::kPanelMixer, 800.0, 400.0, 0.0, 0.0, 560.0, 300.0},
    {Audio::ViewType::Sequencer, UISurfaceKeys::kPanelSequencer, 600.0, 300.0, 0.0, 0.0, 520.0, 220.0},
    {Audio::ViewType::PianoRoll, UISurfaceKeys::kPanelPianoRoll, 800.0, 450.0, 0.0, 0.0, 560.0, 280.0},
    {Audio::ViewType::History, UISurfaceKeys::kPanelHistory, 280.0, 460.0, 0.5, 0.0, 240.0, 220.0},
    {Audio::ViewType::Takes, UISurfaceKeys::kPanelTakes, 320.0, 480.0, 0.5, 0.0, 280.0, 180.0},
}};

/// Every ViewType, in enum order. Written out by hand, so the static_assert
/// below holds it to the enum: the array is sized by ViewType::Count, and an
/// entry left out reads as a value-initialized Mixer in the wrong position.
inline constexpr std::array<Audio::ViewType, static_cast<size_t>(Audio::ViewType::Count)> kAllViewTypes{{
    Audio::ViewType::Mixer,
    Audio::ViewType::Sequencer,
    Audio::ViewType::PianoRoll,
    Audio::ViewType::Playlist,
    Audio::ViewType::History,
    Audio::ViewType::Takes,
}};

// kAllViewTypes is what the registry guard iterates, so a view missing from it
// is a view the guard never checks. This makes "every ViewType, in order" a
// compile-time fact instead of a convention: a value added to the enum before
// Count grows the array, and the list stops building until it is added here too.
inline constexpr bool allViewTypesListedInOrder() {
    for (size_t i = 0; i < kAllViewTypes.size(); ++i)
        if (static_cast<size_t>(kAllViewTypes[i]) != i)
            return false;
    return true;
}
static_assert(allViewTypesListedInOrder(),
              "kAllViewTypes must list every ViewType in enum order -- one was added to the enum but not here");

/// The ViewType that is the workspace rather than an overlay.
inline constexpr Audio::ViewType kWorkspaceView = Audio::ViewType::Playlist;

/// True when @p view is described by a row in kFloatingPanelGeometry.
inline constexpr bool isFloatingView(Audio::ViewType view) {
    for (const FloatingPanelGeometry& g : kFloatingPanelGeometry) {
        if (g.view == view)
            return true;
    }
    return false;
}

} // namespace Content
} // namespace Aestra