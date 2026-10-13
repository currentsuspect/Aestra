// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The sound drawer (V81-AR1, FD-27 point 4): search narrows the folder, arrows swap to each match, Enter keeps
// the one you are on, Esc puts back the sound the drawer opened on, and the drawer owns the keyboard while open.

#include "ArsenalSoundDrawer.h"

#include <iostream>
#include <string>
#include <vector>

using Aestra::ArsenalSoundDrawer;
using AestraUI::NUIKeyCode;
using AestraUI::NUIKeyEvent;
using AestraUI::NUIModifiers;
using Result = ArsenalSoundDrawer::Result;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
NUIKeyEvent key(NUIKeyCode code, char character = 0, NUIModifiers mods = NUIModifiers::None) {
    NUIKeyEvent e;
    e.keyCode = code;
    e.character = character;
    e.modifiers = mods;
    e.pressed = true;
    return e;
}
} // namespace

int main() {
    const std::vector<std::string> list{"/s/Hat_Closed_01.wav", "/s/Hat_Closed_02.wav", "/s/Hat_Open_01.wav", "/s/Kick_808.wav", "/s/Snare_Tight.wav"};

    // Filtering: every word must appear in the name, anywhere, in any case.
    check(ArsenalSoundDrawer::filtered(list, "").size() == 5, "no filter keeps every sound");
    check(ArsenalSoundDrawer::filtered(list, "hat").size() == 3, "hat finds the three hats");
    check(ArsenalSoundDrawer::filtered(list, "HAT open").size() == 1, "two words narrow, case does not matter");
    check(ArsenalSoundDrawer::filtered(list, "808").size() == 1 && ArsenalSoundDrawer::filtered(list, "808")[0] == 3, "a number in the name matches");
    check(ArsenalSoundDrawer::filtered(list, "zzz").empty(), "nothing matches nonsense");

    // Open on the current sound: it is selected and in view.
    auto s = ArsenalSoundDrawer::open(list, "/s/Hat_Open_01.wav");
    check(s.selected == 2 && s.original == "/s/Hat_Open_01.wav", "opening selects the current sound and remembers it");

    // Arrows swap to each neighbour and stop at the ends.
    auto out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Down));
    check(out.result == Result::Swap && out.path == "/s/Kick_808.wav" && s.selected == 3, "Down swaps to the next sound");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Down));
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Down));
    check(s.selected == 4 && out.path == "/s/Snare_Tight.wav", "Down stops at the last sound");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Up));
    check(out.result == Result::Swap && out.path == "/s/Kick_808.wav", "Up swaps back");

    // Enter keeps the one you are on; Esc returns to the one it opened on.
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Enter));
    check(out.result == Result::Keep && out.path == "/s/Kick_808.wav", "Enter keeps the selected sound");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Escape));
    check(out.result == Result::Revert && out.path == "/s/Hat_Open_01.wav", "Esc returns to the sound it opened on");

    // Typing edits the filter and selects the first match; Backspace edits it, and is the drawer's, not a delete.
    s = ArsenalSoundDrawer::open(list, "/s/Kick_808.wav");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::H, 'h'));
    check(out.result == Result::Consumed && s.filter == "h" && s.selected == 0, "typing filters and selects the first match");
    ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::A, 'a'));
    ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::T, 't'));
    ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Down));
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Enter));
    check(out.result == Result::Keep && out.path == "/s/Hat_Closed_02.wav", "Enter after a search keeps the match you moved to");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Backspace));
    check(out.result == Result::Consumed && s.filter == "ha", "Backspace edits the filter and is consumed");
    ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Z, 'z'));
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::Enter));
    check(out.result == Result::Keep && out.path.empty(), "Enter with no match keeps nothing");

    // The drawer owns the keyboard, but not shortcuts.
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::F3));
    check(out.result == Result::Consumed, "another key is swallowed while the drawer is open");
    out = ArsenalSoundDrawer::onKey(s, list, key(NUIKeyCode::S, 's', NUIModifiers::Ctrl));
    check(out.result == Result::Ignored, "Ctrl shortcuts still reach the app");

    // The visible window follows the selection, and scrolling is clamped.
    std::vector<std::string> many;
    for (int i = 0; i < 20; ++i) many.push_back("/s/snare_" + std::to_string(100 + i) + ".wav");
    auto w = ArsenalSoundDrawer::open(many, many[0]);
    for (int i = 0; i < 9; ++i) ArsenalSoundDrawer::onKey(w, many, key(NUIKeyCode::Down));
    check(w.selected == 9 && w.scroll == 9 - ArsenalSoundDrawer::kVisibleRows + 1, "the window scrolls to keep the selection in view");
    ArsenalSoundDrawer::scrollBy(w, many.size(), 100);
    check(w.scroll == 20 - ArsenalSoundDrawer::kVisibleRows, "scrolling stops at the end");
    ArsenalSoundDrawer::scrollBy(w, many.size(), -100);
    check(w.scroll == 0, "and at the start");

    // Row hit-testing matches the window.
    const AestraUI::NUIRect r{0.0f, 0.0f, 300.0f, ArsenalSoundDrawer::kHeight};
    check(ArsenalSoundDrawer::rowAt(w, many.size(), r, {10.0f, 5.0f}) == -1, "the search line is not a row");
    check(ArsenalSoundDrawer::rowAt(w, many.size(), r, {10.0f, ArsenalSoundDrawer::kSearchH + 3.0f}) == 0, "the first visible row");
    check(ArsenalSoundDrawer::rowAt(w, many.size(), r, {10.0f, ArsenalSoundDrawer::kSearchH + ArsenalSoundDrawer::kRowH * 2.5f}) == 2, "the third");

    std::cout << (g_failures ? "FAILED\n" : "ArsenalSoundDrawerTest passed\n");
    return g_failures ? 1 : 0;
}
