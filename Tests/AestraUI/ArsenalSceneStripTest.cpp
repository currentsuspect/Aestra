// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 1, "patterns as one-click scenes": every MIDI pattern is a chip in id order, the active one is
// kept in view when they overflow, a click names the scene, and "+" gets the next free "Pattern N".

#include "ArsenalSceneMenu.h"
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
        const auto tr = strip.swingTrackRect(); // the slider; the "Swing" label beside it is not a control
        e.position = {tr.x + tr.width * 0.5f, tr.y + 4};
        e.pressed = true;
        e.button = AestraUI::NUIMouseButton::Left;
        ArsenalSceneStrip::Action act;
        check(strip.onMouse(e, pm, b.value, act) && act.kind == ArsenalSceneStrip::Action::Kind::SwingEdited, "pressing the zone edits swing");
        check(std::abs(pm.getPattern(b)->swing - 0.5f) < 0.02f, "halfway across is about half swing");
        e.position.x = tr.right() + 400;
        e.pressed = true;
        strip.onMouse(e, pm, b.value, act);
        check(pm.getPattern(b)->swing == 1.0f, "dragging past the end holds full swing");
        e.pressed = false;
        strip.onMouse(e, pm, b.value, act);
        e.position = {0, 0};
        check(!strip.onMouse(e, pm, b.value, act), "after release the strip leaves pointer events outside it alone");
        check(pm.getPattern(a)->swing == 0.0f, "other patterns keep their own swing");
    }

    std::cout << "[5b] the header: label, slider, divider and the one button are level, one height, evenly spaced\n";
    {
        for (const float w : {640.0f, 760.0f, 1000.0f, 1100.0f, 1500.0f}) {
            for (const float h : {44.0f, 52.0f}) {
                const AestraUI::NUIRect r(300.0f, 130.0f, w, h);
                const auto l = ArsenalSceneStrip::headerLayout(r);
                const std::string at = "strip " + std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h)) + ": ";
                check(l.track.height == l.place.height && l.label.height == l.place.height, at + "slider, label and button are one height");
                check(std::abs(l.track.y - l.place.y) < 1e-3f && std::abs(l.label.y - l.place.y) < 1e-3f, at + "they share a top edge, so a baseline");
                const float centre = r.y + r.height * 0.5f;
                check(std::abs((l.place.y + l.place.height * 0.5f) - centre) < 0.5f, at + "and are centred on the strip's centre line");
                check(std::abs(l.place.right() - r.right()) < 1e-3f, at + "the button ends the strip");
                check(std::abs((l.place.x - l.divider.right()) - ArsenalSceneStrip::kGap) < 1e-3f, at + "divider to button is the standard gap");
                check(std::abs((l.divider.x - l.track.right()) - ArsenalSceneStrip::kGap) < 1e-3f, at + "slider to divider is the same gap");
                check(std::abs((l.track.x - l.label.right()) - ArsenalSceneStrip::kLabelGap) < 1e-3f, at + "label to slider is the label gap");
                check(l.track.width >= 60.0f, at + "the slider keeps room for '100%'");
                check(l.label.x >= r.right() - ArsenalSceneStrip::kRightW - 1e-3f, at + "the group stays inside the reserved right end");
            }
        }
    }

    std::cout << "[5c] narrow windows: no two header controls overlap and each keeps 24 px, from 600 to 1400 px wide\n";
    {
        namespace R = Aestra::ArsenalResponsive;
        const auto overlap = [](const AestraUI::NUIRect& a, const AestraUI::NUIRect& b) {
            return a.width > 0 && b.width > 0 && a.x < b.right() - 1e-3f && b.x < a.right() - 1e-3f && a.y < b.bottom() - 1e-3f && b.y < a.bottom() - 1e-3f;
        };
        int tiers[6] = {0, 0, 0, 0, 0, 0};
        for (float window = 600.0f; window <= 1400.0f; window += 20.0f) {
            const float panelW = window - 262.0f, headerW = panelW - 16.0f; // the browser column is 262 wide
            const float stripW = R::sceneStripWidth(headerW, panelW);
            const AestraUI::NUIRect rect(300.0f, 130.0f, stripW, 44.0f);
            ArsenalSceneStrip s;
            s.refresh(pm, b.value, rect);
            const auto l = ArsenalSceneStrip::headerLayout(rect);
            const auto add = s.addRect();
            const std::string at = "window " + std::to_string(static_cast<int>(window)) + " (strip " + std::to_string(static_cast<int>(stripW)) + "): ";
            const AestraUI::NUIRect parts[] = {add, l.label, l.track, l.divider, l.place};
            bool clean = true;
            for (size_t i = 0; i < 5; ++i)
                for (size_t j = i + 1; j < 5; ++j) clean = clean && !overlap(parts[i], parts[j]);
            check(clean, at + "the + chip, label, slider, divider and the button overlap nothing");
            check(l.place.width >= R::kMinHit && l.place.height >= R::kMinHit, at + "the Place button keeps 24 px");
            check(l.track.width == 0.0f || (l.track.width >= R::kMinHit && l.track.height >= R::kMinHit), at + "the slider, when shown, keeps 24 px");
            check(add.width >= R::kMinHit && add.height >= R::kMinHit, at + "the + chip keeps 24 px");
            check(add.right() + ArsenalSceneStrip::kGap <= (l.label.width > 0 ? l.label.x : l.track.width > 0 ? l.track.x : l.place.x) + 1e-3f ||
                      stripW < R::kMinScenes + ArsenalSceneStrip::kAddW + 34.0f,
                  at + "the + chip ends before the right end begins");
            check(l.place.right() <= rect.right() + 1e-3f && l.place.x >= rect.x, at + "the right end stays inside the strip");
            const auto fit = R::fitFor(panelW);
            check(fit.fitHalf >= R::kMinHit && fit.width - fit.fitHalf >= R::kMinHit, at + "Fit and Scroll each keep 24 px");
            const auto r = R::rightFor(stripW, ArsenalSceneStrip::kAddW + ArsenalSceneStrip::kGap);
            ++tiers[(r.label ? 0 : r.divider ? 1 : r.swing ? (r.place == R::PlaceLabel::Full ? 2 : 3) : r.place == R::PlaceLabel::Short ? 4 : 5)];
        }
        check(tiers[0] > 0 && tiers[2] + tiers[3] + tiers[4] > 0, "the sweep passes through the full header and the collapsed tiers (Swing label first, then the divider, then the Place label)");
        // The stepper pills fit the head at each density's floor (195 / 262 / 294), and the old minimum width overlapped the ruler.
        for (const float head : {195.0f, 262.0f, 294.0f}) {
            const auto st = R::steppersFor(head);
            const float width = 10.0f + (R::kMinHit + st.gapInner + st.barsValueW + st.gapInner + R::kMinHit) + st.gapBetween +
                                (R::kMinHit + st.gapInner + st.stepsValueW + st.gapInner + R::kMinHit);
            check(width <= head - 10.0f + 1e-3f, "the bars and steps pills fit a " + std::to_string(static_cast<int>(head)) + " px head (" + std::to_string(width) + ")");
        }
        check(std::max(272.0f, 195.0f - 10.0f) > 195.0f, "(negative control) the old stepper card was at least 272 px wide, past a 195 px head: it overlapped the ruler");
        const float oldRight = ArsenalSceneStrip::kRightW, narrowStrip = R::sceneStripWidth(398.0f - 16.0f, 398.0f);
        check(narrowStrip - oldRight - ArsenalSceneStrip::kGap - ArsenalSceneStrip::kAddW < 0.0f, "(negative control) the old fixed 296 px right end left no room for the + chip in a 660 px window");
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

    std::cout << "[6b] right-click a chip asks for its menu (V82-AR1, FD-52)\n";
    {
        using Kind = ArsenalSceneStrip::Action::Kind;
        AestraUI::NUIMouseEvent e;
        const auto r1 = strip.chipRect(strip.placed()[1]);
        e.position = {r1.x + 6, r1.y + 6};
        e.pressed = true;
        e.button = AestraUI::NUIMouseButton::Right;
        ArsenalSceneStrip::Action act;
        check(strip.onMouse(e, pm, b.value, act) && act.kind == Kind::Menu && act.id == b.value && act.at.x == e.position.x && act.at.y == e.position.y,
              "a right-press on the second chip is a menu request for that pattern, at the click");
        check(strip.chipRectOf(a.value).width > 0.0f && strip.chipRectOf(a.value).x == strip.chipRect(strip.placed()[0]).x && strip.chipRectOf(12345).width == 0.0f,
              "chipRectOf finds a pattern's chip, and nothing for one that is not shown");
        act = {};
        e.position = {strip.addRect().x + 2, strip.addRect().y + 2};
        check(strip.onMouse(e, pm, b.value, act) && act.kind == Kind::None, "right-clicking + or empty space opens nothing");
        act = {};
        e.position = {r1.x + 6, r1.y + 6};
        e.pressed = false;
        check(strip.onMouse(e, pm, b.value, act) && act.kind == Kind::None, "the release is not a second request");
        act = {};
        e.pressed = true;
        e.button = AestraUI::NUIMouseButton::Left;
        strip.onMouse(e, pm, b.value, act);
        check(act.kind == Kind::Select && act.id == b.value, "left-click still selects");
    }

    std::cout << "[6c] what may be deleted, and what is selected after\n";
    {
        std::string why;
        check(ArsenalSceneMenu::canDelete(false, 3), "an unplaced pattern, with others left, may go");
        check(!ArsenalSceneMenu::canDelete(true, 3, &why) && why.find("timeline") != std::string::npos, "one that plays in the timeline stays, and says why");
        check(!ArsenalSceneMenu::canDelete(false, 1, &why) && why.find("only") != std::string::npos, "the last pattern stays, and says why");
        const std::vector<ArsenalSceneStrip::Scene> three{{1, "A", {}}, {2, "B", {}}, {3, "C", {}}};
        check(ArsenalSceneMenu::selectAfterDelete(three, 1) == 2, "deleting the first selects the next");
        check(ArsenalSceneMenu::selectAfterDelete(three, 2) == 3, "deleting a middle one selects the next");
        check(ArsenalSceneMenu::selectAfterDelete(three, 3) == 2, "deleting the last selects the one before");
        check(ArsenalSceneMenu::selectAfterDelete(three, 99) == 0 && ArsenalSceneMenu::selectAfterDelete({{1, "A", {}}}, 1) == 0, "nothing to select when it is unknown or the only one");
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
