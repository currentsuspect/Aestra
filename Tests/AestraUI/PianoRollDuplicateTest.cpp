// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-PR1 (FD-52): duplicate in the piano roll like FL Studio. The step is the selection's length rounded up to whole
// bars; a ruler highlight selects its notes and Duplicate copies the highlighted block, moving the highlight onto the
// copy; the copies are selected so the next press goes on. The rules are pure (PianoRollDuplicate.h); the second half
// drives the real view: a ruler drag, Ctrl+D, the undo step and pattern growth.

#include "Common/MusicHelpers.h"
#include "Widgets/NUIPianoRollWidgets.h"
#include "Widgets/PianoRollDuplicate.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace AestraUI;
namespace Dup = AestraUI::PianoRollDuplicate;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

MidiNote note(int pitch, double start, double dur, bool selected = false) {
    MidiNote n{};
    n.pitch = pitch;
    n.startBeat = start;
    n.durationBeats = dur;
    n.velocity = 0.8f;
    n.selected = selected;
    return n;
}
size_t selectedCount(const std::vector<MidiNote>& notes) {
    size_t n = 0;
    for (const auto& x : notes) n += (x.selected && !x.isDeleted) ? 1 : 0;
    return n;
}
} // namespace

int main() {
    std::cout << "[1] the step is the selection's length rounded up to whole bars, at least one\n";
    check(near(Dup::selectionStep(1.0, 1.5, 4.0), 4.0), "a selection inside a bar steps one bar");
    check(near(Dup::selectionStep(0.0, 4.0, 4.0), 4.0), "exactly one bar steps one bar");
    check(near(Dup::selectionStep(0.0, 4.01, 4.0), 8.0), "a hair over a bar steps two");
    check(near(Dup::selectionStep(0.5, 5.0, 4.0), 8.0), "a phrase from the middle of bar 1 to bar 2 steps two bars");
    check(near(Dup::selectionStep(2.0, 2.0, 4.0), 4.0), "a zero-length selection still steps one bar");
    check(near(Dup::selectionStep(0.0, 4.0, 3.0), 6.0), "in 3/4 a 4-beat selection spans two bars of 3");

    std::cout << "[2] selection: copies land one step on, in the same place in the block, and are selected\n";
    {
        std::vector<MidiNote> notes{note(60, 2.5, 1.0, true), note(64, 2.0, 0.5, false), note(67, 3.0, 0.5, true)};
        const auto plan = Dup::plan(notes, std::nullopt, 4.0);
        check(plan && !plan->rangeMode && near(plan->shift, 4.0), "notes inside bar 1 step one bar");
        check(plan && plan->sources == std::vector<size_t>({0, 2}), "only the selected notes are copied");
        const auto made = Dup::copies(notes, *plan);
        check(made.size() == 2 && near(made[0].startBeat, 6.5) && near(made[1].startBeat, 7.0), "2.5 -> 6.5 and 3.0 -> 7.0: same place in the next bar");
        check(made[0].selected && made[1].selected && made[0].pitch == 60 && near(made[0].durationBeats, 1.0), "the copies are selected and keep pitch and length");
        check(near(Dup::endOf(made), 7.5), "the copies end at 7.5");
    }
    {
        std::vector<MidiNote> notes{note(60, 0.5, 1.0, true), note(62, 4.5, 0.5, true)};
        const auto plan = Dup::plan(notes, std::nullopt, 4.0);
        check(plan && near(plan->shift, 8.0), "a phrase over two bars steps two bars");
    }
    {
        std::vector<MidiNote> notes{note(60, 0.0, 1.0, false)};
        check(!Dup::plan(notes, std::nullopt, 4.0), "nothing selected, no highlight: nothing to do");
        notes[0].selected = true;
        notes[0].isDeleted = true;
        check(!Dup::plan(notes, std::nullopt, 4.0), "a note that is being deleted is not copied");
    }

    std::cout << "[3] ruler range: copies its notes into the next block of the same length\n";
    {
        std::vector<MidiNote> notes{note(60, 4.0, 0.5), note(62, 5.0, 0.5), note(64, 7.5, 1.0), // start inside [4,8)
                                    note(65, 3.9, 0.5), note(67, 8.0, 0.5),                    // just outside
                                    note(69, 2.0, 4.0)};                                        // began earlier, runs through
        const Dup::Range range{4.0, 8.0};
        const auto plan = Dup::plan(notes, range, 4.0);
        check(plan && plan->rangeMode && near(plan->shift, 4.0), "range mode, one range length on");
        check(plan && plan->sources == std::vector<size_t>({0, 1, 2}), "the notes that START inside [4, 8) are copied; 3.9, 8.0 and the long earlier note are not");
        check(plan && near(plan->to.first, 8.0) && near(plan->to.second, 12.0), "the highlight moves to [8, 12)");
        const auto made = Dup::copies(notes, *plan);
        check(made.size() == 3 && near(made[2].startBeat, 11.5), "7.5 -> 11.5: a note keeps its place in the block, and may run past it");
    }
    {
        std::vector<MidiNote> notes{note(60, 2.0, 0.5), note(62, 6.0, 0.5)};
        const auto plan = Dup::plan(notes, Dup::Range{2.0, 8.0}, 4.0);
        check(plan && near(plan->shift, 6.0) && near(plan->to.second, 14.0), "a 6-beat highlight is NOT rounded to bars: it copies 6 beats on");
    }
    {
        std::vector<MidiNote> notes{note(60, 12.0, 0.5)};
        const auto plan = Dup::plan(notes, Dup::Range{4.0, 8.0}, 4.0);
        check(plan && plan->rangeMode && plan->sources.empty() && near(plan->to.first, 8.0), "an empty highlight still moves on (nothing to copy)");
    }

    std::cout << "[4] which one a press means, with a highlight up\n";
    {
        const Dup::Range range{4.0, 8.0};
        std::vector<MidiNote> notes{note(60, 4.0, 0.5, true), note(62, 5.0, 0.5, true), note(64, 6.0, 0.5, false), note(65, 1.0, 0.5, false)};
        check(Dup::plan(notes, range, 4.0)->rangeMode, "selection equal to the highlight's notes: the highlight");
        notes[1].selected = false;
        check(Dup::plan(notes, range, 4.0)->rangeMode && Dup::plan(notes, range, 4.0)->sources.size() == 3, "one note clicked inside the highlight: still the whole highlight");
        notes[0].selected = notes[1].selected = false;
        check(Dup::plan(notes, range, 4.0)->rangeMode, "nothing selected: the highlight");
        notes[3].selected = true;
        const auto p = Dup::plan(notes, range, 4.0);
        check(p && !p->rangeMode && p->sources == std::vector<size_t>({3}), "a selected note OUTSIDE the highlight: the selection, not the highlight");
        check(near(p->shift, 4.0), "and it steps by bars");
    }

    std::cout << "[5] pressing again goes on: the copies sit in the new highlight\n";
    {
        std::vector<MidiNote> notes{note(60, 4.0, 0.5, true), note(62, 6.0, 0.5, true)};
        Dup::Range range{4.0, 8.0};
        for (int press = 1; press <= 3; ++press) {
            const auto plan = Dup::plan(notes, range, 4.0);
            if (!plan) break;
            const auto made = Dup::copies(notes, *plan);
            for (auto& n : notes) n.selected = false;
            notes.insert(notes.end(), made.begin(), made.end());
            range = plan->to;
        }
        check(notes.size() == 8 && near(range.first, 16.0) && near(range.second, 20.0), "three presses: 8 notes, the highlight at [16, 20)");
        check(near(notes.back().startBeat, 18.0) && selectedCount(notes) == 2, "the latest copy is selected, the earlier ones are not");
    }

    std::cout << "[6] growing the pattern\n";
    check(Dup::barsToGrow(10.0, 8.0, 4.0) == 1, "ending at 10 in an 8-beat pattern needs one more bar");
    check(Dup::barsToGrow(8.0, 8.0, 4.0) == 0, "ending exactly at the end needs none");
    check(Dup::barsToGrow(12.01, 8.0, 4.0) == 2, "just past 12 needs two");
    check(Dup::barsToGrow(3.0, 8.0, 4.0) == 0, "inside the pattern needs none");

    std::cout << "[7] in the real view: drag a ruler range, Ctrl+D, Ctrl+D\n";
    {
        PianoRollView view;
        view.setBounds({0.0f, 0.0f, 900.0f, 600.0f});
        view.onResize(900, 600);
        view.setBeatsPerBar(4);
        view.setTotalDurationBeats(16.0);
        view.setViewWindow(0.0, 16.0);
        view.setNotes({note(60, 4.0, 0.5), note(62, 5.0, 0.5), note(64, 9.0, 0.5)});

        int commits = 0;
        std::vector<MidiNote> latest;
        view.setOnNotesChanged([&](const std::vector<MidiNote>& n) { ++commits; latest = n; });
        int growBars = 0;
        double total = 16.0; // what the panel would hold; the view has no getter
        view.setOnAdjustPatternLength([&](int bars) { growBars += bars; total += bars * 4.0; view.setTotalDurationBeats(total); });
        std::optional<std::pair<double, double>> zone;
        view.setOnLoopZoneChanged([&](std::optional<std::pair<double, double>> z) { zone = z; });

        const NUIRect ruler = view.getRulerBounds();
        const double ppb = static_cast<double>(view.getGridBounds().width) / view.getViewDurationBeats();
        const float y = ruler.y + ruler.height * 0.5f;
        const auto xAt = [&](double beat) { return ruler.x + static_cast<float>(beat * ppb); };
        auto mouse = [](NUIMouseEventType type, NUIMouseButton button, float x, float yy) {
            NUIMouseEvent e;
            e.type = type;
            e.button = button;
            e.position = {x, yy};
            e.pressed = type == NUIMouseEventType::Down;
            e.released = type == NUIMouseEventType::Up;
            return e;
        };
        view.onMouseEvent(mouse(NUIMouseEventType::Down, NUIMouseButton::Right, xAt(4.02), y));
        view.onMouseEvent(mouse(NUIMouseEventType::Move, NUIMouseButton::None, xAt(7.98), y));
        view.onMouseEvent(mouse(NUIMouseEventType::Up, NUIMouseButton::Right, xAt(7.98), y));
        check(zone && near(zone->first, 4.0) && near(zone->second, 8.0), "the drag makes the highlight [4, 8)");
        check(selectedCount(view.getNotes()) == 2, "and selects the two notes inside it (not the one at 9)");

        NUIKeyEvent ctrlD;
        ctrlD.keyCode = NUIKeyCode::D;
        ctrlD.pressed = true;
        ctrlD.modifiers = NUIModifiers::Ctrl;
        view.onKeyEvent(ctrlD);
        check(commits == 1, "Duplicate is one commit, so one undo step");
        check(latest.size() == 5, "two copies were added: 5 notes");
        check(zone && near(zone->first, 8.0) && near(zone->second, 12.0), "the highlight moved to [8, 12)");
        check(selectedCount(view.getNotes()) == 2, "the copies are the selection now");
        bool copyAt8 = false, copyAt9 = false;
        for (const auto& n : latest) { copyAt8 = copyAt8 || near(n.startBeat, 8.0); copyAt9 = copyAt9 || (near(n.startBeat, 9.0) && n.pitch == 62); }
        check(copyAt8 && copyAt9, "copies at 8.0 and 9.0 (4.0 and 5.0, one block on)");
        check(growBars == 0, "the pattern (16 beats) already holds them");

        view.onKeyEvent(ctrlD);
        // The block [8, 12) now holds the two copies AND the note that was already at 9.0: the highlight is a block of
        // time, so everything starting in it is copied.
        check(commits == 2 && latest.size() == 8 && zone && near(zone->first, 12.0) && near(zone->second, 16.0), "pressing again copies [8, 12), the note already at 9.0 included, into [12, 16)");
        check(growBars == 0, "[12, 16) just fits a 16-beat pattern");

        view.onKeyEvent(ctrlD);
        check(commits == 3 && latest.size() == 11 && zone && near(zone->first, 16.0) && near(zone->second, 20.0), "and once more, into [16, 20)");
        check(growBars == 1 && near(total, 20.0), "which grew the pattern by one bar first");
    }

    std::cout << "[8] in the real view: no highlight, Ctrl+D copies the selection one bar on\n";
    {
        PianoRollView view;
        view.setBounds({0.0f, 0.0f, 900.0f, 600.0f});
        view.onResize(900, 600);
        view.setBeatsPerBar(4);
        view.setTotalDurationBeats(16.0);
        view.setViewWindow(0.0, 16.0);
        view.setNotes({note(60, 1.0, 0.5, true), note(62, 2.0, 0.5, true), note(64, 9.0, 0.5, false)});
        int commits = 0;
        view.setOnNotesChanged([&](const std::vector<MidiNote>&) { ++commits; });
        NUIKeyEvent ctrlD;
        ctrlD.keyCode = NUIKeyCode::D;
        ctrlD.pressed = true;
        ctrlD.modifiers = NUIModifiers::Ctrl;
        view.onKeyEvent(ctrlD);
        const auto& notes = view.getNotes();
        check(notes.size() == 5 && commits == 1, "two copies, one commit");
        bool at5 = false, at6 = false;
        for (const auto& n : notes) { at5 = at5 || (near(n.startBeat, 5.0) && n.selected); at6 = at6 || (near(n.startBeat, 6.0) && n.selected); }
        check(at5 && at6 && selectedCount(notes) == 2, "copies at 5.0 and 6.0, selected; the originals are not");
        view.setNotes({note(60, 0.0, 0.5, false)});
        commits = 0;
        view.onKeyEvent(ctrlD);
        check(commits == 0 && view.getNotes().size() == 1, "nothing selected, nothing happens");
    }

    std::cout << "[9] the highlight clears two ways: a left press outside it, or a right press\n";
    {
        PianoRollView view;
        view.setBounds({0.0f, 0.0f, 900.0f, 600.0f});
        view.onResize(900, 600);
        view.setBeatsPerBar(4);
        view.setTotalDurationBeats(16.0);
        view.setViewWindow(0.0, 16.0);
        view.setTool(PianoRollTool::Pointer);
        view.setNotes({note(60, 4.5, 0.5), note(62, 10.0, 0.5)});
        int clears = 0;
        view.setOnLoopZoneChanged([&](std::optional<std::pair<double, double>> z) { clears += z ? 0 : 1; });

        const NUIRect grid = view.getGridBounds();
        const NUIRect ruler = view.getRulerBounds();
        const double ppb = static_cast<double>(grid.width) / view.getViewDurationBeats();
        const auto gx = [&](double beat) { return grid.x + static_cast<float>(beat * ppb); };
        const float gy = grid.y + 6.0f; // a row with no note on it
        auto press = [&](NUIMouseButton button, float x, float y) {
            NUIMouseEvent e;
            e.type = NUIMouseEventType::Down;
            e.button = button;
            e.position = {x, y};
            e.pressed = true;
            view.onMouseEvent(e);
            NUIMouseEvent up = e;
            up.type = NUIMouseEventType::Up;
            up.pressed = false;
            up.released = true;
            view.onMouseEvent(up);
        };
        const auto raise = [&] { view.setLoopZone(std::make_pair(4.0, 8.0)); };

        raise();
        press(NUIMouseButton::Left, gx(6.0), gy);
        check(view.getLoopZone().has_value(), "a left press INSIDE the highlight (empty grid) leaves it");
        press(NUIMouseButton::Left, gx(2.0), gy);
        check(!view.getLoopZone() && clears == 1, "a left press on the grid outside it, before it, clears it");
        raise();
        press(NUIMouseButton::Left, gx(12.0), gy);
        check(!view.getLoopZone(), "a left press outside it, after it, clears it");
        raise();
        press(NUIMouseButton::Left, gx(8.0), gy);
        check(!view.getLoopZone(), "the end is outside: [4, 8) does not include 8");
        raise();
        press(NUIMouseButton::Right, gx(6.0), gy);
        check(!view.getLoopZone(), "a right press clears it, even inside it");
        raise();
        press(NUIMouseButton::Right, gx(14.0), gy);
        check(!view.getLoopZone(), "and outside it");

        raise();
        press(NUIMouseButton::Left, gx(4.75), grid.y + 6.0f);
        check(view.getLoopZone().has_value(), "a left press inside it that selects or adds does not clear it");

        raise();
        press(NUIMouseButton::Right, ruler.x + static_cast<float>(10.0 * ppb), ruler.y + ruler.height * 0.5f);
        check(!view.getLoopZone(), "a right click on the ruler, without dragging out a new range, clears it");
        raise();
        press(NUIMouseButton::Left, ruler.x + static_cast<float>(10.0 * ppb), ruler.y + ruler.height * 0.5f);
        check(view.getLoopZone().has_value(), "a left press on the ruler is the scrub: it leaves the highlight");

        raise();
        press(NUIMouseButton::Left, grid.x - 20.0f, gy);
        check(view.getLoopZone().has_value(), "a press outside the grid (the key lane) is not 'outside the highlight'");
        NUIKeyEvent esc;
        esc.keyCode = NUIKeyCode::Escape;
        esc.pressed = true;
        view.onKeyEvent(esc);
        check(!view.getLoopZone(), "Esc still clears it too");
    }

    std::cout << (g_failures == 0 ? "PianoRollDuplicateTest: all passed\n" : "PianoRollDuplicateTest: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
