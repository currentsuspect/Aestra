// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A plugin editor embedded as an X child window can take X input focus and nothing returns it, so typing in Aestra's
// text fields goes nowhere. shouldReclaimInputFocus() (AestraPlat/src/Linux/InputFocusPolicy.h) decides when a press on
// Aestra's own window takes the focus back. Header-only: this test links nothing.

#include "../../AestraPlat/src/Linux/InputFocusPolicy.h"

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

struct Row {
    const char* name;
    bool known, ownerIsAestra, pressHit, underChild;
    bool expected;
};

// The decision as the rule is meant to be read, written without the early returns, so the table and the policy
// cannot share a mistake.
bool reference(const Row& r) {
    return r.known && !r.ownerIsAestra && r.pressHit && !r.underChild;
}

} // namespace

int main() {
    const Row table[] = {
        {"plugin holds focus, press on Aestra's browser: take it back", true, false, true, false, true},
        {"Aestra already holds focus: nothing to do", true, true, true, false, false},
        {"press landed over the plugin's own window: never steal", true, false, true, true, false},
        {"press did not hit Aestra's window", true, false, false, false, false},
        {"focus owner unknown (query failed)", false, false, true, false, false},
        {"unknown owner and Aestra flag set", false, true, true, false, false},
        {"Aestra holds focus and the press is over a child", true, true, true, true, false},
    };
    for (const Row& r : table) {
        const bool got = Aestra::shouldReclaimInputFocus(r.known, r.ownerIsAestra, r.pressHit, r.underChild);
        expect(got == r.expected, std::string("table: ") + r.name);
        expect(reference(r) == r.expected, std::string("reference agrees with table: ") + r.name);
    }

    // Exhaustive: the policy equals the reference on every combination.
    for (int bits = 0; bits < 16; ++bits) {
        const Row r{"", (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0, false};
        expect(Aestra::shouldReclaimInputFocus(r.known, r.ownerIsAestra, r.pressHit, r.underChild) == reference(r),
               "exhaustive combination " + std::to_string(bits));
    }

    // Negative control: each plausible wrong policy must disagree with the table somewhere, or the table proves nothing.
    using Policy = bool (*)(bool, bool, bool, bool);
    const struct { const char* name; Policy fn; } mutants[] = {
        {"always reclaim", [](bool, bool, bool, bool) { return true; }},
        {"ignores the plugin-window guard", [](bool k, bool a, bool h, bool) { return k && !a && h; }},
        {"ignores that Aestra already holds focus", [](bool k, bool, bool h, bool c) { return k && h && !c; }},
        {"acts on unknown focus", [](bool, bool a, bool h, bool c) { return !a && h && !c; }},
        {"never reclaims", [](bool, bool, bool, bool) { return false; }},
    };
    for (const auto& m : mutants) {
        bool caught = false;
        for (const Row& r : table)
            if (m.fn(r.known, r.ownerIsAestra, r.pressHit, r.underChild) != r.expected) caught = true;
        expect(caught, std::string("negative control not caught: ") + m.name);
    }

    if (g_failures == 0) std::cout << "InputFocusPolicyTest: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
