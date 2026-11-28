// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// MIDI file <-> pattern bridge, and the single-undo-step import command.

#include "Commands/CommandHistory.h"
#include "Commands/ImportMidiPatternsCommand.h"
#include "Midi/MidiPatternBridge.h"

#include <iostream>
#include <memory>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Midi;

namespace {
int g_failures = 0;
void check(bool c, const std::string& w) { if (!c) { std::cerr << "[FAIL] " << w << "\n"; ++g_failures; } }

SmfNote mk(int pitch, double start, double dur) {
    SmfNote n; n.note.pitch = pitch; n.note.startBeat = start; n.note.durationBeats = dur; n.note.velocity = 0.8f; return n;
}

SmfData sample() {
    SmfData d;
    d.tempos = {{0.0, 133.0}};
    SmfTrack conductor; // no notes: skipped
    SmfTrack a; a.name = "Bass"; a.notes = {mk(36, 0, 1), mk(38, 4.5, 1)};   // ends 5.5 -> 8 beats
    SmfTrack b; b.notes = {mk(60, 0, 4)};                                      // ends 4.0 -> 4 beats, unnamed
    SmfTrack c; c.notes = {mk(62, 0, 0.5)};                                    // unnamed again
    d.tracks = {conductor, a, b, c};
    return d;
}

void testPlan() {
    auto plan = planPatternImport(sample(), "Groove", 7);
    check(plan.size() == 3, "one pattern per track with notes");
    if (plan.size() != 3) return;
    check(plan[0].name == "Bass" && plan[1].name == "Groove" && plan[2].name == "Groove 2", "names from track or file");
    check(plan[0].lengthBeats == 8.0 && plan[1].lengthBeats == 4.0 && plan[2].lengthBeats == 4.0, "lengths round up to bars, min one");
    check(plan[0].notes[1].unitId == 7, "notes routed to the unit");
    check(planPatternImport(SmfData{}, "x", 1).empty(), "no tracks, no patterns");
}

void testCommandIsOneUndoStep() {
    PatternManager pm;
    CommandHistory history;
    auto cmd = std::make_shared<ImportMidiPatternsCommand>(pm, planPatternImport(sample(), "Groove", 7));
    history.pushAndExecute(cmd);
    check(pm.getAllPatterns().size() == 3, "import creates three patterns");
    const auto ids = cmd->createdIds();
    check(history.undo(), "undo works");
    check(pm.getAllPatterns().empty(), "ONE undo removes all three patterns");
    check(!history.canUndo(), "nothing else on the stack: it was a single step");
    check(history.redo(), "redo works");
    check(pm.getAllPatterns().size() == 3, "redo restores all three");
    bool sameIds = true;
    for (auto id : ids) sameIds = sameIds && pm.getPattern(id) && pm.getPattern(id)->isMidi();
    check(sameIds, "redo restores the same ids with notes");
    if (pm.getPattern(ids[0])) check(pm.getPattern(ids[0])->getMidiNotes().size() == 2 && pm.getPattern(ids[0])->name == "Bass", "content intact after redo");
    // an empty plan records nothing
    PatternManager pm2; CommandHistory h2;
    h2.pushAndExecute(std::make_shared<ImportMidiPatternsCommand>(pm2, std::vector<ImportedPattern>{}));
    check(pm2.getAllPatterns().empty(), "empty plan creates nothing");
}

void testExportRoundTrip() {
    std::vector<MidiNote> notes;
    for (int i = 0; i < 4; ++i) { MidiNote n; n.pitch = 48 + i; n.startBeat = i * 0.25; n.durationBeats = 0.25; n.velocity = 0.6f; n.unitId = i < 2 ? 1 : 2; notes.push_back(n); }
    auto smf = buildPatternExport("Verse", notes, 140.0, [](uint64_t u) { return u == 1 ? std::string("Kick") : std::string("Hat"); });
    auto r = readStandardMidiFile(writeStandardMidiFile(smf));
    check(r.ok, "export parses");
    if (!r.ok) return;
    check(r.data.tempos.size() == 1 && r.data.tempos[0].bpm > 139.99 && r.data.tempos[0].bpm < 140.01, "tempo exported");
    auto plan = planPatternImport(r.data, "x", 1);
    check(plan.size() == 2 && plan[0].name == "Kick" && plan[1].name == "Hat", "one track per unit, named by unit");
    if (plan.size() == 2) check(plan[1].notes.size() == 2 && plan[1].notes[0].pitch == 50 && plan[1].notes[0].startBeat == 0.5, "notes survive export -> import");
    auto single = buildPatternExport("Solo", {notes[0]}, 120.0, nullptr);
    check(single.tracks.size() == 1 && single.tracks[0].name == "Solo", "single unit track takes the pattern name");
    auto empty = buildPatternExport("Empty", {}, 120.0, nullptr);
    check(readStandardMidiFile(writeStandardMidiFile(empty)).ok, "empty pattern still exports a valid file");
}
} // namespace

int main() {
    testPlan();
    testCommandIsOneUndoStep();
    testExportRoundTrip();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "MidiPatternBridgeTest passed\n";
    return 0;
}
