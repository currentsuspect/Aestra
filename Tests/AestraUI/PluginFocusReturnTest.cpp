// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A plugin editor embedded as an X child window takes keyboard focus when you click it, and nothing gave it back, so Space
// (and every other Aestra key) stopped working until you clicked Aestra again. The owner chose: while the pointer is over
// the plugin, keys go to it (typing a preset name works); a moment after the pointer leaves, with no button held, Aestra
// takes the focus back. shouldReturnFocusToApp() (AestraUI/Widgets/PluginFocusReturn.h) decides. Header-only.

#include "../../AestraUI/Widgets/PluginFocusReturn.h"

#include <iostream>
#include <string>

namespace {
int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

using namespace AestraUI::EditorFocus;

struct Row {
    const char* name;
    bool holds, over, button;
    double away;
    bool expected;
};

// Written without the early returns, so the table and the rule cannot share a mistake.
bool reference(const Row& r) {
    return r.holds && !r.over && !r.button && r.away >= kReturnGraceSeconds;
}
} // namespace

int main() {
    const Row table[] = {
        {"plugin has focus, pointer left a while ago, nothing held: give it back", true, false, false, 1.0, true},
        {"exactly at the grace period: give it back", true, false, false, kReturnGraceSeconds, true},
        {"pointer only just left: wait, so a slip off the edge does not steal focus", true, false, false, 0.05, false},
        {"pointer is over the plugin: keys stay with it (typing a preset name)", true, true, false, 5.0, false},
        {"a button is down (dragging a knob out of the window): never", true, false, true, 5.0, false},
        {"the plugin does not hold focus: nothing to give back", false, false, false, 5.0, false},
        {"Aestra already has it and the pointer is over the plugin", false, true, false, 0.0, false},
    };
    for (const Row& r : table) {
        expect(shouldReturnFocusToApp(r.holds, r.over, r.button, r.away) == r.expected, std::string("table: ") + r.name);
        expect(reference(r) == r.expected, std::string("reference agrees with table: ") + r.name);
    }

    // Exhaustive over the booleans, with times either side of the grace period.
    for (int bits = 0; bits < 8; ++bits) {
        for (const double away : {0.0, 0.1, kReturnGraceSeconds, 2.0}) {
            const Row r{"", (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, away, false};
            expect(shouldReturnFocusToApp(r.holds, r.over, r.button, r.away) == reference(r),
                   "exhaustive " + std::to_string(bits) + " away " + std::to_string(away));
        }
    }

    // Time away counts only while the pointer is away, and starts again when it comes back.
    expect(awaySeconds(0.0, 0.1, false) == 0.1, "away time accumulates while the pointer is away");
    expect(awaySeconds(0.4, 0.1, false) > 0.49 && awaySeconds(0.4, 0.1, false) < 0.51, "and keeps accumulating");
    expect(awaySeconds(0.4, 0.1, true) == 0.0, "coming back over the plugin starts the count again");

    // Negative controls: each plausible wrong rule must disagree with the table somewhere.
    using Rule = bool (*)(bool, bool, bool, double);
    const struct { const char* name; Rule fn; } mutants[] = {
        {"always give focus back", [](bool, bool, bool, double) { return true; }},
        {"never give it back", [](bool, bool, bool, double) { return false; }},
        {"ignores the pointer being over the plugin", [](bool h, bool, bool b, double a) { return h && !b && a >= kReturnGraceSeconds; }},
        {"ignores a held button", [](bool h, bool o, bool, double a) { return h && !o && a >= kReturnGraceSeconds; }},
        {"ignores that the plugin holds focus", [](bool, bool o, bool b, double a) { return !o && !b && a >= kReturnGraceSeconds; }},
        {"ignores the grace period", [](bool h, bool o, bool b, double) { return h && !o && !b; }},
    };
    for (const auto& m : mutants) {
        bool caught = false;
        for (const Row& r : table)
            if (m.fn(r.holds, r.over, r.button, r.away) != r.expected) caught = true;
        expect(caught, std::string("negative control not caught: ") + m.name);
    }

    if (g_failures == 0) std::cout << "PluginFocusReturnTest: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
