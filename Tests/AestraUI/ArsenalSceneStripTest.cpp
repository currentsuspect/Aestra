// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 1, "patterns as one-click scenes": every MIDI pattern is a chip in id order, the active one is
// kept in view when they overflow, a click names the scene, and "+" gets the next free "Pattern N".

#include "ArsenalSceneStrip.h"

#include <iostream>
#include <string>

using namespace Aestra;
using Audio::MidiPayload;

namespace {
// A stand-in for the track manager: just what quietDefaults reads.
struct FakeClip { Audio::PatternID patternId; };
struct FakeLane { std::vector<FakeClip> clips; };
struct FakePlaylist {
    std::vector<FakeLane> lanes;
    template <class Fn> auto withLanes(Fn&& fn) const { return fn(lanes); }
};
struct FakeUnit { Audio::PatternID defaultPatternId; };
struct FakeUnits {
    std::vector<FakeUnit> units;
    std::vector<int> getAllUnitIDs() const { std::vector<int> ids; for (size_t i = 0; i < units.size(); ++i) ids.push_back(static_cast<int>(i)); return ids; }
    const FakeUnit* getUnit(int id) const { return id >= 0 && id < static_cast<int>(units.size()) ? &units[id] : nullptr; }
};
struct FakeTracks {
    Audio::PatternManager pm;
    FakePlaylist playlist;
    FakeUnits unitList;
    const FakePlaylist& getPlaylistModel() const { return playlist; }
    const FakeUnits& getUnitManager() const { return unitList; }
    const Audio::PatternManager& getPatternManager() const { return pm; }
};

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
} // namespace

int main() {
    std::cout << "[1] chips fit: no scrolling, in order\n";
    float off = -1.0f;
    auto p = ArsenalSceneStrip::place({60, 80, 60}, 0, 600, 0, true, off);
    check(p.size() == 3 && off == 0.0f, "three chips, offset 0");
    check(p[1].x == 60 + ArsenalSceneStrip::kGap, "second chip sits one gap after the first");

    std::cout << "[2] overflow keeps the active chip in view and clamps the scroll\n";
    const std::vector<float> w(10, 80.0f);
    p = ArsenalSceneStrip::place(w, 9, 300, 0, true, off);
    check(off > 0.0f, "scrolled toward the last chip");
    check(p[9].x + p[9].w <= 300 - ArsenalSceneStrip::kAddW + 0.01f, "last chip ends before the + chip");
    p = ArsenalSceneStrip::place(w, 0, 300, 5000, false, off);
    float total = 10 * 80.0f + 9 * ArsenalSceneStrip::kGap;
    check(off <= total, "manual scroll is clamped");
    p = ArsenalSceneStrip::place(w, 0, 300, -50, false, off);
    check(off == 0.0f, "never scrolls below zero");

    std::cout << "[3] widths and names\n";
    check(ArsenalSceneStrip::chipWidth("A") == ArsenalSceneStrip::kCardW && ArsenalSceneStrip::chipWidth(std::string(60, 'x')) == ArsenalSceneStrip::kCardW, "every scene is one card width, whatever its name");
    {
        std::vector<Audio::MidiNote> notes(5);
        for (int i = 0; i < 5; ++i) notes[i].startBeat = (i < 4 ? 0.0 : 3.9);
        const auto thumb = ArsenalSceneStrip::thumbnail(notes, 4.0);
        check(thumb.size() == ArsenalSceneStrip::kThumbBars && thumb[0] == 1.0f && thumb[15] >= 0.2f && thumb[15] < 1.0f && thumb[7] == 0.0f, "the thumbnail is the busiest bucket full, a lone note shorter, silence empty");
        check(ArsenalSceneStrip::thumbnail({}, 4.0)[0] == 0.0f, "an empty pattern has an empty thumbnail");
    }
    check(ArsenalSceneStrip::nextName({{1, "Pattern 1"}, {2, "Pattern 2"}}) == "Pattern 3", "next name counts on");
    check(ArsenalSceneStrip::nextName({{1, "Pattern 2"}}) == "Pattern 3", "next name skips one that is taken");

    std::cout << "[4] the strip reads patterns, ignores non-MIDI, and answers clicks\n";
    Audio::PatternManager pm;
    const auto a = pm.createMidiPattern("Verse", 8.0, MidiPayload{});
    const auto b = pm.createMidiPattern("Chorus", 8.0, MidiPayload{});
    ArsenalSceneStrip strip;
    const AestraUI::NUIRect rect{10, 20, 700, ArsenalSceneStrip::kHeight};
    strip.refresh(pm, b.value, rect);
    check(strip.scenes().size() == 2, "two scenes");
    check(strip.scenes()[0].name == "Verse" && strip.scenes()[1].name == "Chorus", "in creation order");
    const auto r0 = strip.chipRect(strip.placed()[0]);
    auto hit = strip.hitTest({r0.x + 4, r0.y + 4});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::Scene && hit.id == a.value, "clicking the first chip names that pattern");
    hit = strip.hitTest({strip.addRect().x + 2, strip.addRect().y + 2});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::Add, "clicking + asks for a new scene");
    hit = strip.hitTest({0, 0});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::None, "outside the strip is nothing");

    std::cout << "[5] swing is a drag zone at the right end\n";
    {
        check(ArsenalSceneStrip::swingPercent(0.0f) == 0 && ArsenalSceneStrip::swingPercent(1.0f) == 100, "swing reads 0% to 100%");
        const auto sw = strip.swingRect();
        check(sw.right() < strip.placeRect().x && strip.placeRect().right() == rect.right() && strip.addRect().right() < sw.x, "the swing zone sits right of the + chip, and the place button ends the strip");
        AestraUI::NUIMouseEvent e;
        e.position = {sw.x + sw.width * 0.5f, sw.y + 4};
        e.pressed = true;
        e.button = AestraUI::NUIMouseButton::Left;
        ArsenalSceneStrip::Action act;
        check(strip.onMouse(e, pm, b.value, act) && act.kind == ArsenalSceneStrip::Action::Kind::SwingEdited, "pressing the zone edits swing");
        check(std::abs(pm.getPattern(b)->swing - 0.5f) < 0.02f, "halfway across is about half swing");
        e.position.x = sw.right() + 400;
        e.pressed = true;
        strip.onMouse(e, pm, b.value, act);
        check(pm.getPattern(b)->swing == 1.0f, "dragging past the end holds full swing");
        e.pressed = false;
        strip.onMouse(e, pm, b.value, act);
        e.position = {0, 0};
        check(!strip.onMouse(e, pm, b.value, act), "after release the strip leaves pointer events outside it alone");
        check(pm.getPattern(a)->swing == 0.0f, "other patterns keep their own swing");
    }

    std::cout << "[6] the place button asks the panel to place the pattern\n";
    {
        AestraUI::NUIMouseEvent e;
        const auto pl = strip.placeRect();
        e.position = {pl.x + 10, pl.y + 10};
        e.pressed = true;
        e.button = AestraUI::NUIMouseButton::Left;
        ArsenalSceneStrip::Action act;
        check(strip.onMouse(e, pm, b.value, act) && act.kind == ArsenalSceneStrip::Action::Kind::PlaceInTimeline && act.id == b.value, "pressing it asks to place the active pattern");
    }

    std::cout << "[7] a unit's own empty pattern is plumbing, not a scene (a kit must not bring a card per sound)\n";
    {
        FakeTracks tm;
        const auto song = tm.pm.createMidiPattern("Pattern 1", 8.0, MidiPayload{});
        const auto kick = tm.pm.createMidiPattern("Kick Pattern", 8.0, MidiPayload{});
        const auto snare = tm.pm.createMidiPattern("Snare Pattern", 8.0, MidiPayload{});
        MidiPayload withNote;
        withNote.notes.resize(1);
        const auto hats = tm.pm.createMidiPattern("Hats Pattern", 8.0, withNote);
        const auto bass = tm.pm.createMidiPattern("Bass Pattern", 8.0, MidiPayload{});
        tm.unitList.units = {{kick}, {snare}, {hats}, {bass}, {Audio::PatternID{}}};
        tm.playlist.lanes = {{{{bass}}}};
        const auto quiet = ArsenalSceneStrip::quietDefaults(tm);
        check(quiet.size() == 2 && quiet[0] == kick.value && quiet[1] == snare.value, "only the empty, unplaced unit patterns are quiet");
        ArsenalSceneStrip s;
        s.refresh(tm.pm, song.value, rect, quiet);
        check(s.scenes().size() == 3 && s.scenes()[0].name == "Pattern 1", "the strip shows Pattern 1, the one with notes and the placed one");
        s.refresh(tm.pm, kick.value, rect, quiet);
        check(s.scenes().size() == 4, "the pattern being edited always shows, even when quiet");
        s.refresh(tm.pm, song.value, rect);
        check(s.scenes().size() == 5, "without a hidden list everything shows, as before");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
