// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PanelPlacementTest — SPEC 3 §1.1. The mode model and its store round trip:
//   defaults per panel kind, each field independent, survives a restart, never touches geometry.

#include "PanelPlacement.h"

#include <filesystem>
#include <iostream>
#include <vector>

using namespace Aestra;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
} // namespace

int main() {
    std::cout << "[1] defaults\n";
    check(defaultPanelMode(true).keepOpen == false && defaultPanelMode(true).docked == false,
          "plugin editors: Floating + Contextual");
    check(defaultPanelMode(false).keepOpen == true && defaultPanelMode(false).docked == false,
          "workspace panels: Floating + Keep Open (today's behaviour)");

    std::cout << "[2] a store with nothing stored gives the defaults\n";
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "aestra_panel_placement_test.json";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    {
        UISurfaceStoreFile store(path.string());
        check(readPanelMode(&store, "panel.mixer", false) == defaultPanelMode(false), "mixer");
        check(readPanelMode(&store, kPluginEditorsModeKey, true) == defaultPanelMode(true), "editors");
        check(readPanelMode<UISurfaceStoreFile>(nullptr, "panel.mixer", false) == defaultPanelMode(false),
              "no store at all");

        std::cout << "[3] fields are independent, per panel\n";
        AestraUI::PanelMode docked = defaultPanelMode(false);
        docked.docked = true;
        writePanelMode(&store, "panel.mixer", docked);
        check(readPanelMode(&store, "panel.mixer", false).docked && readPanelMode(&store, "panel.mixer", false).keepOpen,
              "mixer is now docked and still Keep Open");
        check(readPanelMode(&store, "panel.piano-roll", false) == defaultPanelMode(false), "another panel is untouched");
        AestraUI::PanelMode contextual = defaultPanelMode(false);
        contextual.keepOpen = false;
        writePanelMode(&store, "panel.piano-roll", contextual);
        check(!readPanelMode(&store, "panel.piano-roll", false).keepOpen && !readPanelMode(&store, "panel.piano-roll", false).docked,
              "piano roll is contextual and still floating");
    }
    std::cout << "[4] survives a restart, and does not disturb geometry\n";
    {
        UISurfaceStoreFile reopened(path.string());
        check(readPanelMode(&reopened, "panel.mixer", false).docked, "mixer is still docked");
        check(!readPanelMode(&reopened, "panel.piano-roll", false).keepOpen, "piano roll is still contextual");
        check(!reopened.surfaceGeometry("panel.mixer").has_value(), "no geometry was invented for the mixer");
    }
    std::filesystem::remove(path, ec);

    std::cout << "[5] which contextual panels close after an outside press\n";
    {
        const std::vector<int> saw{1, 2, 3};
        const auto open = [](int v) { return v != 2; }; // panel 2 was closed by the time we look
        auto closed = contextualViewsToClose(saw, false, open);
        check(closed == std::vector<int>({1, 3}), "a plain outside press closes the panels still open");
        check(contextualViewsToClose(saw, true, open).empty(),
              "a press that toggled a view (the transport button) closes nothing");
        check(contextualViewsToClose(std::vector<int>{}, false, open).empty(), "no press, nothing to close");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
