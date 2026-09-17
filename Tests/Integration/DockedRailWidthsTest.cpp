// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 5c (FD-23): the two docked rail widths live in the surface store
// under "panel.browser" and "panel.pattern-browser". Only the width is user
// state; layout owns position and clamps. These checks pin the step's two
// decision points: a stored width applies wholesale while an absent entry
// leaves the live pref for the computed default, and the legacy ui_state.json
// value imports exactly once — never when the store already holds the key,
// never when the legacy value is the untouched default.

#include "../../Source/Core/DockedRailWidths.h"
#include "../Support/TestTempDirectory.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    using namespace Aestra;

    {
        float pref = -1.0f;
        expect(!applyStoredRailWidth(pref, std::nullopt), "absent entry reports no apply");
        expect(pref < 0.0f, "absent entry leaves the pref for the computed default");
    }
    {
        float pref = -1.0f;
        UISurfaceGeometry stored;
        stored.width = 320.0;
        expect(applyStoredRailWidth(pref, stored), "stored entry reports apply");
        expect(pref == 320.0f, "stored width applies wholesale");
    }

    const Tests::ScopedTempDirectory tempDirScope{"DockedRailWidths"};
    const auto tempDir = tempDirScope.path();

    {
        UISurfaceStoreFile store((tempDir / "import-once.json").string());
        const auto first =
            importLegacyBrowserRailWidth(store, 300.0f, 250.0f, 1700000000);
        expect(first == LegacyImportResult::Imported, "expressed legacy width imports once");
        const auto stored = store.surfaceGeometry(UISurfaceKeys::kPanelBrowser);
        expect(stored.has_value() && stored->width == 300.0, "imported width reads back");
        const auto second =
            importLegacyBrowserRailWidth(store, 400.0f, 250.0f, 1700000001);
        expect(second == LegacyImportResult::AlreadyInStore, "store value wins over later legacy reads");
        expect(store.surfaceGeometry(UISurfaceKeys::kPanelBrowser)->width == 300.0,
               "second import leaves the store untouched");
    }
    {
        UISurfaceStoreFile store((tempDir / "import-default.json").string());
        const auto result = importLegacyBrowserRailWidth(store, 250.0f, 250.0f, 1700000000);
        expect(result == LegacyImportResult::NothingToImport, "untouched legacy default imports nothing");
        expect(!store.surfaceGeometry(UISurfaceKeys::kPanelBrowser).has_value(),
               "no store entry is created for the default");
    }

    if (g_failures == 0) {
        std::cout << "All DockedRailWidths tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " DockedRailWidths test(s) failed.\n";
    return 1;
}
