// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file InspectorCollapseStateTest.cpp
 * @brief Regression tests for mixer inspector collapse: explicit intent vs. derived fit.
 *
 * The contract under test:
 *
 *     effectiveExpanded = expandedPreference && !forcedCollapsed
 *
 * with the rule that a width change must NEVER write to expandedPreference.
 * The transition orderings are the easy thing to get wrong and are invisible in
 * a screenshot, which is why they are asserted directly.
 */

#include "Widgets/InspectorCollapseState.h"
#include "MixerUIPreferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace AestraUI;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "  PASS: " << msg << "\n"; ++testsPassed; } while(0)
#define FAIL(msg) do { std::cout << "  FAIL: " << msg << "\n"; ++testsFailed; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ---------------------------------------------------------------------------
// Defaults and the core formula
// ---------------------------------------------------------------------------
static void test_first_run_defaults_expanded() {
    InspectorCollapseState s;
    ASSERT(s.expandedPreference, "first run preference is expanded");
    ASSERT(!s.forcedCollapsed, "first run is not width-constrained");
    ASSERT(s.effectiveExpanded(), "first run renders expanded");
    PASS("first run defaults to expanded");
}

static void test_effective_is_preference_and_not_forced() {
    InspectorCollapseState s;

    s.expandedPreference = true;  s.forcedCollapsed = false;
    ASSERT(s.effectiveExpanded(), "wants open, room available -> expanded");

    s.expandedPreference = true;  s.forcedCollapsed = true;
    ASSERT(!s.effectiveExpanded(), "wants open, no room -> collapsed");

    s.expandedPreference = false; s.forcedCollapsed = false;
    ASSERT(!s.effectiveExpanded(), "wants closed, room available -> collapsed");

    s.expandedPreference = false; s.forcedCollapsed = true;
    ASSERT(!s.effectiveExpanded(), "wants closed, no room -> collapsed");

    PASS("effectiveExpanded = preference && !forced");
}

// ---------------------------------------------------------------------------
// Width changes must not corrupt the stored intent
// ---------------------------------------------------------------------------
static void test_width_constraint_never_writes_preference() {
    InspectorCollapseState s;  // preference = expanded

    s.setForcedCollapsed(true);
    ASSERT(s.expandedPreference,
           "auto-collapse must not overwrite the user's explicit preference");
    ASSERT(!s.effectiveExpanded(), "but it must collapse visually");

    s.setForcedCollapsed(false);
    ASSERT(s.expandedPreference, "preference still intact after the constraint lifts");
    PASS("width constraint never writes the preference");
}

// ---------------------------------------------------------------------------
// The two sequences that are easiest to get wrong
// ---------------------------------------------------------------------------

/// 1. user collapses -> 2. narrow -> 3. wide again -> 4. STAYS collapsed.
/// The failure mode is the widen step "restoring" a panel the user closed.
static void test_explicit_collapse_survives_narrow_then_wide() {
    InspectorCollapseState s;

    s.onRailClicked();                       // 1. explicit collapse
    ASSERT(!s.expandedPreference, "click collapsed the preference");
    ASSERT(!s.effectiveExpanded(), "collapsed on screen");

    s.setForcedCollapsed(true);              // 2. narrow
    ASSERT(!s.effectiveExpanded(), "still collapsed while narrow");

    s.setForcedCollapsed(false);             // 3. wide again
    ASSERT(!s.expandedPreference, "widening must not resurrect the preference");
    ASSERT(!s.effectiveExpanded(),           // 4.
           "regression: inspector reopened itself after a width round-trip "
           "even though the user had explicitly collapsed it");
    PASS("explicit collapse survives narrow -> wide round-trip");
}

/// 1. user prefers expanded -> 2. narrow auto-collapses -> 3. wide again ->
/// 4. RESTORES expanded. The failure mode is the auto-collapse having
/// overwritten the preference, so the panel never comes back.
static void test_auto_collapse_restores_expanded_when_width_returns() {
    InspectorCollapseState s;                // 1. preference = expanded
    ASSERT(s.effectiveExpanded(), "starts expanded");

    s.setForcedCollapsed(true);              // 2. narrow
    ASSERT(!s.effectiveExpanded(), "auto-collapsed while narrow");

    s.setForcedCollapsed(false);             // 3. wide again
    ASSERT(s.effectiveExpanded(),            // 4.
           "regression: auto-collapse clobbered the preference, so the "
           "inspector never returned when the width did");
    PASS("auto-collapse restores expanded when width returns");
}

// ---------------------------------------------------------------------------
// Clicking the rail while width-constrained
// ---------------------------------------------------------------------------
static void test_click_while_forced_records_expand_intent() {
    InspectorCollapseState s;
    s.onRailClicked();            // user collapses
    s.setForcedCollapsed(true);   // then the window narrows

    s.onRailClicked();            // user clicks the rail: "open this"

    ASSERT(s.expandedPreference,
           "a click while width-constrained must record expand intent, not be discarded");
    ASSERT(!s.effectiveExpanded(),
           "…but it stays visually collapsed while there is still no room");

    s.setForcedCollapsed(false);  // room returns
    ASSERT(s.effectiveExpanded(), "the recorded intent takes effect once width allows");
    PASS("click while constrained records intent, applies when width returns");
}

/// While constrained the panel is collapsed regardless, so a click can only
/// mean "expand" — it must not toggle the preference off.
static void test_click_while_forced_does_not_toggle_off() {
    InspectorCollapseState s;      // preference = expanded
    s.setForcedCollapsed(true);

    s.onRailClicked();

    ASSERT(s.expandedPreference,
           "clicking a forced-collapsed rail must not flip an expanded preference to collapsed");
    PASS("click while constrained never toggles the preference off");
}

static void test_click_toggles_normally_when_unconstrained() {
    InspectorCollapseState s;

    s.onRailClicked();
    ASSERT(!s.expandedPreference, "first click collapses");
    s.onRailClicked();
    ASSERT(s.expandedPreference, "second click expands");
    ASSERT(s.effectiveExpanded(), "and it shows");
    PASS("click toggles normally when unconstrained");
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------
static void test_reset_restores_default() {
    InspectorCollapseState s;
    s.onRailClicked();            // collapse
    s.setForcedCollapsed(true);

    s.reset();

    ASSERT(s.expandedPreference, "reset restores the expanded default");
    ASSERT(!s.forcedCollapsed, "reset clears the derived constraint");
    ASSERT(s.effectiveExpanded(), "reset renders expanded");
    PASS("reset restores the first-run default");
}


// ---------------------------------------------------------------------------
// Settings round trip
//
// The state machine above is only half the feature: the preference has to
// survive a restart, and an absent or damaged settings file must leave the
// default alone rather than collapsing the inspector. Driven through
// MixerUIPreferences rather than MixerPanel so it needs no panel, TrackManager
// or renderer.
// ---------------------------------------------------------------------------

namespace {

std::string scratchSettingsPath(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "aestra-mixer-prefs-test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / name).string();
}

void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::trunc);
    out << contents;
}

} // namespace

static void test_settings_round_trip_both_values() {
    std::cout << "\n[settings round trip]\n";

    for (bool expanded : {true, false}) {
        const std::string path = scratchSettingsPath("roundtrip.json");
        std::filesystem::remove(path);

        Aestra::MixerUIPreferences saved;
        saved.inspectorExpanded = expanded;
        ASSERT(saved.save(path), "save reports success");

        const Aestra::MixerUIPreferences loaded = Aestra::MixerUIPreferences::load(path);
        // Parenthesised: '<<' binds tighter than '?:', so an unbracketed
        // ternary would be parsed as part of the stream expression.
        ASSERT(loaded.inspectorExpanded == expanded,
               (expanded ? "expanded=true survives the round trip"
                         : "expanded=false survives the round trip"));
    }
    PASS("both preference values round-trip through the settings file");
}

static void test_missing_file_keeps_default() {
    const std::string path = scratchSettingsPath("does-not-exist.json");
    std::filesystem::remove(path);

    const Aestra::MixerUIPreferences loaded = Aestra::MixerUIPreferences::load(path);
    ASSERT(loaded.inspectorExpanded, "first run leaves the inspector expanded");
    PASS("absent settings file keeps the expanded default");
}

static void test_absent_key_keeps_default() {
    // The trap this guards: reading a missing key as a default-constructed
    // false, which would collapse the inspector for anyone whose settings file
    // predates the key.
    const std::string path = scratchSettingsPath("other-keys.json");
    writeFile(path, "{\"version\": 1, \"somethingElse\": true}");

    const Aestra::MixerUIPreferences loaded = Aestra::MixerUIPreferences::load(path);
    ASSERT(loaded.inspectorExpanded, "absent key does not apply false");
    PASS("settings file without the key keeps the expanded default");
}

static void test_wrong_typed_key_keeps_default() {
    // JSON::asBool() yields its default false for any non-Boolean node, so a
    // present-but-wrong-typed value slips past a bare has() check and collapses
    // the inspector. Same failure as the absent-key case, different route.
    const char* payloads[] = {
        "{\"inspectorExpanded\": 1}",
        "{\"inspectorExpanded\": \"true\"}",
        "{\"inspectorExpanded\": null}",
        "{\"inspectorExpanded\": []}",
    };
    for (const char* payload : payloads) {
        const std::string path = scratchSettingsPath("wrong-type.json");
        writeFile(path, payload);
        const Aestra::MixerUIPreferences loaded = Aestra::MixerUIPreferences::load(path);
        ASSERT(loaded.inspectorExpanded, "non-Boolean value does not collapse the inspector");
    }
    PASS("present-but-wrong-typed key keeps the expanded default");
}

static void test_corrupt_file_keeps_default() {
    const std::string path = scratchSettingsPath("corrupt.json");
    writeFile(path, "{\"inspectorExpanded\": fal");  // truncated mid-write

    const Aestra::MixerUIPreferences loaded = Aestra::MixerUIPreferences::load(path);
    ASSERT(loaded.inspectorExpanded, "unparseable settings do not collapse the inspector");
    PASS("corrupt settings file keeps the expanded default");
}

static void test_saved_file_does_not_carry_forced_state() {
    // forcedCollapsed is width-derived and must never reach disk: a session
    // that happened to end in a narrow window must not rewrite the choice.
    const std::string path = scratchSettingsPath("no-forced.json");
    Aestra::MixerUIPreferences prefs;
    prefs.inspectorExpanded = true;
    ASSERT(prefs.save(path), "save reports success");

    std::ifstream in(path);
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    ASSERT(contents.find("forcedCollapsed") == std::string::npos,
           "no width-derived state is written");
    ASSERT(contents.find("inspectorExpanded") != std::string::npos,
           "the explicit preference is written");
    PASS("only the explicit preference is persisted");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "============================================\n";
    std::cout << "  Inspector Collapse State Regression Tests\n";
    std::cout << "============================================\n\n";

    test_first_run_defaults_expanded();
    test_effective_is_preference_and_not_forced();
    test_width_constraint_never_writes_preference();
    test_explicit_collapse_survives_narrow_then_wide();
    test_auto_collapse_restores_expanded_when_width_returns();
    test_click_while_forced_records_expand_intent();
    test_click_while_forced_does_not_toggle_off();
    test_click_toggles_normally_when_unconstrained();
    test_reset_restores_default();

    test_settings_round_trip_both_values();
    test_missing_file_keeps_default();
    test_absent_key_keeps_default();
    test_wrong_typed_key_keeps_default();
    test_corrupt_file_keeps_default();
    test_saved_file_does_not_carry_forced_state();

    std::cout << "\n============================================\n";
    if (testsFailed == 0) {
        std::cout << "  All " << testsPassed << " tests passed.\n";
    } else {
        std::cout << "  " << testsPassed << " passed, " << testsFailed << " failed.\n";
    }
    std::cout << "============================================\n";
    return testsFailed > 0 ? 1 : 0;
}
