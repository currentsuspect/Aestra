// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Ink theme: the bed, the card and the control greys keep their order, and the places that paint a whole panel or
// area use the bed or the card, never a control grey (21 Nov 2026 sweep: "a flat mid-grey region bigger than a
// control").
//
// Part 1 is a table over every theme: the card rises from the bed by at least the design step, and a control grey
// (surfaceTertiary, surfaceRaised) is never the bed. Part 2 pins each big-area fill the sweep corrected, by reading
// the painter's source line, so a later edit that puts a control grey back as a slab fails here with the file named.

#include "ContrastRatio.h"
#include "NUIThemeSystem.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {
int g_failures = 0;
int g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
float ratio(const NUIColor& a, const NUIColor& b) {
    return Aestra::ContrastRatio::ratio(a.r, a.g, a.b, b.r, b.g, b.b);
}
float luma(const NUIColor& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// One painter site: the file, the line that fills the big area (any text from it that is unique), and the token that
// line must carry.
struct Site {
    const char* file;
    const char* anchor;
    const char* token;
    const char* what;
};
} // namespace

int main() {
    // Part 1: the order of the surfaces, every theme.
    constexpr float kCardStep = 1.10f; // the card over the bed, as a contrast ratio (Ink dark, slate ladder: panel to card 1.07)
    for (const auto& kit : NUIThemePresets::kitThemes()) {
        const std::string key = kit.key;
        const NUIThemeProperties t = NUIThemePresets::createKitTheme(key);
        const bool dark = kit.mode != 1;
        if (key.rfind("ink-", 0) != 0 || kit.mode == 2) continue; // Ink dark and light; high contrast keeps its own, smaller steps
        const std::string at = key + ": ";
        check(ratio(t.backgroundSecondary, t.backgroundPrimary) >= (key == "ink-dark" ? 1.06f : kCardStep), at + "the card rises from the bed by the design step");
        check(luma(t.backgroundSecondary) > luma(t.backgroundPrimary), at + "the card is lighter than the bed");
        if (key == "ink-dark") { // Platinum (owner lock, 21 Nov): the panel and the outer bed are the one #181a1c surface, the card a quiet step
            check(t.backgroundPrimary.r == t.timelineBed.r && t.backgroundPrimary.g == t.timelineBed.g && t.backgroundPrimary.b == t.timelineBed.b, at + "the panel body is the outer bed (#181a1c)");
            check(ratio(t.backgroundSecondary, t.backgroundPrimary) >= 1.06f && ratio(t.backgroundSecondary, t.backgroundPrimary) <= 1.08f, at + "card over panel is 1.06:1");
            check(ratio(t.surfaceTertiary, t.backgroundPrimary) >= 1.2f, at + "a control reads against the panel");
        }
        for (const NUIColor* control : {&t.surfaceTertiary, &t.surfaceRaised}) {
            if (dark) { // on dark the control greys climb past the card; on light they step down from the bed instead
                check(luma(*control) > luma(t.backgroundSecondary), at + "a control grey is lighter than the card");
            } else {
                check(luma(*control) < luma(t.backgroundPrimary), at + "a control grey is darker than the bed");
            }
        }
    }

    // Part 2: the big areas the sweep put back on the bed or the card.
    const std::string root = AESTRA_SOURCE_DIR;
    const std::vector<Site> sites = {
        {"Source/Components/TrackManagerUIRender.cpp", "renderer.fillRect(controlBg, themeManager.getColor(\"backgroundPrimary\"));", "backgroundPrimary", "timeline track list column"},
        {"Source/Components/FileBrowserNavigation.cpp", "paneBg = themeManager.getColor(\"backgroundSecondary\")", "backgroundSecondary", "file browser Places pane"},
        {"Source/Components/FilePreviewPanel.cpp", "bgColor = theme.getColor(\"backgroundSecondary\")", "backgroundSecondary", "file preview panel"},
        {"Source/Panels/AuditionHub.cpp", "r.fillRect(f.nav, theme.getColor(\"backgroundPrimary\"));", "backgroundPrimary", "Audition sidebar"},
        {"Source/Panels/AuditionHub.cpp", "if (active) r.fillRect(rect, theme.getColor(\"backgroundSecondary\"));", "backgroundSecondary", "Audition active page row"},
        {"Source/Panels/TakesPanel.cpp", "bounds.bottom() - layout.bar.y), theme.getColor(\"backgroundPrimary\"));", "backgroundPrimary", "Versions panel body"},
        {"Source/Panels/PatternBrowserPanel.cpp", "m_backgroundColor = themeManager.getColor(\"backgroundPrimary\");", "backgroundPrimary", "Patterns list body"},
        {"Source/Panels/ArsenalRackDraw.h", "renderer.fillRoundedRect(cell, tp.radiusM, theme.getColor(\"backgroundPrimary\"));", "backgroundPrimary", "Arsenal add-a-sound tiles"},
        {"AestraUI/Widgets/PianoRollInspector.cpp", "renderer.fillRect(b, bed);", "bed", "piano roll side panel body"},
        {"Source/Panels/SampleEditorPanel.cpp", "renderer.fillRect(b, theme.getColor(\"backgroundPrimary\"));\n        const NUIColor rule", "backgroundPrimary", "Sample Editor body"},
        {"Source/Panels/AudioClipEditorPanel.cpp", "renderer.fillRect(bounds, canvasColor());", "canvasColor", "Audio Clip Editor body"},
        {"Source/Panels/WindowPanel.cpp", "auto bodyColor = theme.getColor(\"backgroundPrimary\");", "backgroundPrimary", "docked and floating panel body"},
        {"AestraUI/Widgets/RumblePluginEditor.cpp", "b.height - headerHeight()}, token(\"backgroundPrimary\"));", "backgroundPrimary", "Rumble editor body"},
        {"Source/Components/TrackManagerUIRender.cpp", "themeManager.getColor(\"backgroundPrimary\"));\n    renderer.drawLine({rulerBounds.x, rulerBounds.bottom()}, {gridStartX", "backgroundPrimary", "timeline ruler corner"},
        {"Source/Components/TrackManagerUIToolbar.cpp", "renderer.fillRect(row, theme.getColor(\"backgroundPrimary\"));", "backgroundPrimary", "timeline toolbar strip"},
        {"Source/Panels/WindowPanel.cpp", "auto titleBarColor = theme.getColor(\"backgroundPrimary\");", "backgroundPrimary", "panel title bar (the panel surface, separated by the border line)"},
        {"Source/Components/TimelineMinimapBar.cpp", "colors_.glassFill = theme.getColor(\"backgroundPrimary\");", "backgroundPrimary", "minimap fill"},
        {"Source/Components/TimelineMinimapBar.cpp", "colors_.glassBorder = theme.getColor(\"backgroundSecondary\");", "backgroundSecondary", "minimap border"},
        {"Source/Components/AudioVisualizer.cpp", "renderer.fillRoundedRect(bounds, 4.0f, backgroundColor_);", "backgroundColor_ (the theme's panel colour)", "meter / scope / waveform fill"},
        {"Source/Components/AudioVisualizer.cpp", "renderer.fillRoundedRect(meter, 4.0f, backgroundColor_);", "backgroundColor_ (the theme's panel colour)", "VU meter fill"},
        {"AestraUI/Widgets/PianoRollToolbar.cpp", "toolbarBg = themeManager.getColor(\"backgroundSecondary\");", "backgroundSecondary", "piano roll toolbar strip"},
        {"AestraUI/Base/NUICustomTitleBar.cpp", "const NUIColor bgColor = themeManager.getColor(\"timelineBed\");", "timelineBed", "title bar (the outer bed)"},
        {"Source/Panels/TransportBar.cpp", "renderer.fillRect(bounds, theme.getColor(\"timelineBed\"));", "timelineBed", "transport strip (the outer bed)"},
        {"Source/Components/FileBrowser.cpp", "renderer.fillRect(fileBrowserBounds, themeManager.getColor(\"timelineBed\"));", "timelineBed", "file browser sidebar (the outer bed)"},
        {"Source/Settings/MissingAssetsDialog.cpp", "theme.radiusL, theme.backgroundSecondary)", "backgroundSecondary", "Missing assets dialog card"},
    };
    for (const auto& s : sites) {
        const std::string text = slurp(root + "/" + s.file);
        check(!text.empty(), std::string(s.file) + ": readable");
        check(text.find(s.anchor) != std::string::npos,
              std::string(s.what) + ": " + s.file + " no longer paints it with " + s.token);
    }

    // Things that must be gone: a gradient to another colour under the meter, scope and waveform (it made a strip of another
    // colour on the transport bar), and rounded corners on the panel frame (a 2 px curve clipped by the window left a broken stub).
    const struct { const char* file; const char* gone; const char* what; } absent[] = {
        {"Source/Components/AudioVisualizer.cpp", "gradientColor", "the meter, scope and waveform fill is flat, with no gradient to another colour"},
        {"Source/Components/AudioVisualizer.cpp", "NUIColor::lerp(topBg", "the VU and scope backgrounds are not lerped"},
        {"Source/Panels/WindowPanel.cpp", "fillRoundedRect(bounds", "the panel body is a square rectangle"},
        {"Source/Panels/WindowPanel.cpp", "strokeRoundedRect(bounds", "the panel border is a square rectangle, so its lines are whole"},
        {"Source/Panels/WindowPanel.cpp", "windowRadius", "the panel frame has no corner radius"},
    };
    for (const auto& a : absent) {
        const std::string text = slurp(root + "/" + a.file);
        check(!text.empty() && text.find(a.gone) == std::string::npos, std::string(a.what) + " (" + a.file + ")");
    }

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
