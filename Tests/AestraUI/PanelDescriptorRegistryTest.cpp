// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PanelDescriptorRegistryTest — the floating-panel registry cannot silently drift.
//
// P5's exit criterion had two halves. "Two switches become one lookup" is the
// code; this is the other half, and it is the half that matters six months from
// now:
//
//   the next floating panel that forgets to register fails a test instead of
//   silently defaulting
//
// Before this, adding a panel meant editing two switches over the same ViewType.
// Miss the second one and it compiled clean, returned nullptr at runtime, and the
// panel appeared with no geometry and no persistence — no warning, no error. A
// `default:` arm makes that failure mode the DEFAULT path, because forgetting is
// what a `default:` arm is for.
//
// The check is derived from the ViewType enum, not from a hand-written list, so
// adding an enum value changes the arithmetic and the test notices. Playlist is
// the one ViewType that must NOT be registered: the timeline is the workspace
// itself rather than an overlay, and it has no surface geometry.
//
// This test is deliberately headless. It includes FloatingPanelDescriptors.h,
// which has no UI dependency, because AESTRA_HEADLESS_ONLY builds do not compile
// the UI targets and no test includes AestraContent.h. A registry guard that only
// ran in a lane most of CI skips would be a guard that mostly does not run.

#include "Core/FloatingPanelDescriptors.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace Aestra::Content;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

const char* nameOf(ViewType v) {
    switch (v) {
    case ViewType::Mixer: return "Mixer";
    case ViewType::Sequencer: return "Sequencer";
    case ViewType::PianoRoll: return "PianoRoll";
    case ViewType::Playlist: return "Playlist";
    case ViewType::History: return "History";
    case ViewType::Takes: return "Takes";
    }
    return "?";
}

} // namespace

int main() {
    // ---------------------------------------------------------------------
    // The arithmetic that makes this test a guard: every ViewType is either
    // registered as a floating panel or is the workspace, and nothing is
    // neither. A new enum value with no row fails here.
    // ---------------------------------------------------------------------
    size_t registered = 0;
    for (const ViewType v : kAllViewTypes) {
        const bool floating = isFloatingView(v);
        if (v == kWorkspaceView) {
            check(!floating, std::string(nameOf(v)) + " is the workspace and must NOT be a floating panel");
            continue;
        }
        check(floating, std::string(nameOf(v)) + " is a floating view but has no registry row");
        if (floating)
            ++registered;
    }
    check(registered == kFloatingPanelGeometry.size(),
          "every registry row corresponds to a registered floating ViewType");

    // ---------------------------------------------------------------------
    // No duplicates, so a lookup can never be ambiguous.
    // ---------------------------------------------------------------------
    for (size_t i = 0; i < kFloatingPanelGeometry.size(); ++i) {
        for (size_t j = i + 1; j < kFloatingPanelGeometry.size(); ++j) {
            check(kFloatingPanelGeometry[i].view != kFloatingPanelGeometry[j].view,
                  std::string(nameOf(kFloatingPanelGeometry[i].view)) + " is registered twice");
        }
    }

    // ---------------------------------------------------------------------
    // Store keys must be present and unique. Two panels sharing a key would
    // overwrite each other's saved geometry — the failure the surface store
    // cannot report, because from its side they are the same row.
    // ---------------------------------------------------------------------
    for (size_t i = 0; i < kFloatingPanelGeometry.size(); ++i) {
        const auto& g = kFloatingPanelGeometry[i];
        check(g.storeKey != nullptr && std::strlen(g.storeKey) > 0,
              std::string(nameOf(g.view)) + " has a store key");
        if (!g.storeKey)
            continue;
        for (size_t j = i + 1; j < kFloatingPanelGeometry.size(); ++j) {
            const char* other = kFloatingPanelGeometry[j].storeKey;
            if (other && std::strcmp(g.storeKey, other) == 0) {
                check(false, std::string("store key \"") + g.storeKey + "\" is shared by " +
                                 nameOf(g.view) + " and " + nameOf(kFloatingPanelGeometry[j].view));
            }
        }
    }

    // ---------------------------------------------------------------------
    // Geometry must be usable, not merely present. A zero or negative default
    // opens an invisible panel, which looks exactly like a panel that failed to
    // register — the confusion this whole pass exists to remove.
    // ---------------------------------------------------------------------
    for (const auto& g : kFloatingPanelGeometry) {
        const std::string who = nameOf(g.view);
        check(g.defaultWidth > 0.0, who + " has a positive default width");
        check(g.defaultHeight > 0.0, who + " has a positive default height");
        check(g.minWidth > 0.0, who + " has a positive minimum width");
        check(g.minHeight > 0.0, who + " has a positive minimum height");
        // A default smaller than its own minimum would clamp the moment the
        // panel opens, so the recorded first-open geometry is not what ships.
        check(g.defaultWidth >= g.minWidth, who + " is not narrower than its own minimum");
        check(g.defaultHeight >= g.minHeight, who + " is not shorter than its own minimum");
        check(g.defaultAnchorX >= 0.0 && g.defaultAnchorX <= 1.0,
              who + " has an anchor X inside the unit range");
        check(g.defaultAnchorY >= 0.0 && g.defaultAnchorY <= 1.0,
              who + " has an anchor Y inside the unit range");
    }

    // ---------------------------------------------------------------------
    // The five panels this registry actually covers, named explicitly. The
    // derived checks above say "some set of five"; this says which five, so a
    // refactor that quietly drops one cannot pass.
    // ---------------------------------------------------------------------
    for (const ViewType expected : {ViewType::Mixer, ViewType::Sequencer, ViewType::PianoRoll,
                                    ViewType::History, ViewType::Takes}) {
        check(isFloatingView(expected),
              std::string(nameOf(expected)) + " is still a registered floating panel");
    }

    if (g_failures == 0) {
        std::cout << "=== PanelDescriptorRegistryTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== PanelDescriptorRegistryTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}