// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Music/ScaleContext.h"
#include "PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testHarmonyContextRoundtripThroughPanel() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    const PatternID patternId = patternManager.createMidiPattern("Harmony Roundtrip", 4.0, MidiPayload{});

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);
    editor.applyHarmonyContextEdit(9, AestraUI::ScaleType::Minor, true);

    const PatternSource* stored = patternManager.getPattern(patternId);
    expect(stored != nullptr, "edited pattern should remain available");
    expect(stored && stored->scaleOverride.has_value(), "panel edit should persist a scale override");
    if (stored && stored->scaleOverride) {
        expect(stored->scaleOverride->rootKey == 9, "stored root should match the view edit");
        expect(stored->scaleOverride->scaleKind == ScaleKind::Minor, "stored scale should match the view edit");
        expect(stored->scaleOverride->snapToScale, "stored snap state should match the view edit");
    }

    PianoRollPanel reloadedEditor(trackManager);
    reloadedEditor.loadPattern(patternId);
    const ScaleContext restored = reloadedEditor.getHarmonyContext();
    expect(restored.rootKey == 9, "loadPattern should restore the edited root");
    expect(restored.scaleKind == ScaleKind::Minor, "loadPattern should restore the edited scale");
    expect(restored.snapToScale, "loadPattern should restore the edited snap state");
}

void testSelectionSurvivesCommittedEditRoundTrip() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();

    MidiPayload payload;
    Aestra::Audio::MidiNote n1;
    n1.pitch = 60; n1.startBeat = 0.0; n1.durationBeats = 1.0; n1.velocity = 0.8f;
    Aestra::Audio::MidiNote n2;
    n2.pitch = 64; n2.startBeat = 2.0; n2.durationBeats = 1.0; n2.velocity = 0.8f;
    Aestra::Audio::MidiNote n3;
    n3.pitch = 67; n3.startBeat = 4.0; n3.durationBeats = 1.0; n3.velocity = 0.8f;
    payload.notes = {n1, n2, n3};
    const PatternID patternId = patternManager.createMidiPattern("Selection Roundtrip", 8.0, payload);

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);
    editor.setSnap(AestraUI::SnapGrid::Beat); // fixed, so a nudge is one beat (Smart Snap would follow the zoom)

    // Ctrl+A selects every note.
    AestraUI::NUIKeyEvent ctrlA;
    ctrlA.keyCode = AestraUI::NUIKeyCode::A;
    ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
    ctrlA.pressed = true;
    editor.handleKeyEvent(ctrlA);

    const auto& selected = editor.getNotes();
    expect(selected.size() == 3, "Ctrl+A should select all three notes");
    expect(selected.size() == 3 && selected[0].selected && selected[1].selected && selected[2].selected,
           "all notes should be selected after Ctrl+A");

    // Nudge right: a committed edit that runs savePattern -> CommandHistory ->
    // OnStateChanged. The selection must survive that round trip.
    AestraUI::NUIKeyEvent right;
    right.keyCode = AestraUI::NUIKeyCode::Right;
    right.pressed = true;
    editor.handleKeyEvent(right);

    const auto& afterMove = editor.getNotes();
    expect(afterMove.size() == 3, "committed move should not change note count");
    expect(afterMove.size() == 3 && afterMove[0].selected && afterMove[1].selected && afterMove[2].selected,
           "selection must survive the save/reload round trip after a committed edit");

    // The edit itself must still persist (default snap = Beat, +1.0 beat).
    const PatternSource* stored = patternManager.getPattern(patternId);
    expect(stored != nullptr && stored->isMidi(), "edited pattern should remain available");
    if (stored && stored->isMidi()) {
        const auto& storedNotes = std::get<MidiPayload>(stored->payload).notes;
        expect(storedNotes.size() == 3, "saved pattern should keep all notes");
        expect(storedNotes.size() == 3 && std::abs(storedNotes[0].startBeat - 1.0) < 0.001,
               "nudge should persist +1 beat to the pattern manager");
    }

    // A second committed edit must not wipe the selection either.
    editor.handleKeyEvent(right);
    const auto& afterSecond = editor.getNotes();
    expect(afterSecond.size() == 3 && afterSecond[0].selected && afterSecond[1].selected && afterSecond[2].selected,
           "selection must survive repeated committed edits");
}

// Review, #981 (owner ruling 2026-09-28): a note edit only ever grows a pattern. Shrinking is
// the length control's job. Before, nudging the one note of an explicitly 4-bar pattern
// recomputed the length from content and collapsed the loop to 1 bar.
void testNoteEditsGrowButNeverShrinkThePattern() {
    auto nudgeAll = [](PianoRollPanel& editor) {
        AestraUI::NUIKeyEvent ctrlA;
        ctrlA.keyCode = AestraUI::NUIKeyCode::A;
        ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
        ctrlA.pressed = true;
        editor.handleKeyEvent(ctrlA);
        AestraUI::NUIKeyEvent right;
        right.keyCode = AestraUI::NUIKeyCode::Right;
        right.pressed = true;
        editor.handleKeyEvent(right);
    };
    auto oneNotePattern = [](PatternManager& pm, const char* name, double noteStart, double lengthBeats) {
        MidiPayload payload;
        Aestra::Audio::MidiNote n;
        n.pitch = 60; n.startBeat = noteStart; n.durationBeats = 1.0; n.velocity = 0.8f;
        payload.notes = {n};
        return pm.createMidiPattern(name, lengthBeats, payload);
    };

    auto trackManager = std::make_shared<TrackManager>();
    auto& pm = trackManager->getPatternManager();

    const PatternID wide = oneNotePattern(pm, "Four bars", 0.0, 16.0);
    PianoRollPanel editor(trackManager);
    editor.loadPattern(wide);
    editor.setSnap(AestraUI::SnapGrid::Beat); // fixed, so a nudge is one beat
    nudgeAll(editor);
    const PatternSource* stored = pm.getPattern(wide);
    expect(stored && std::abs(std::get<MidiPayload>(stored->payload).notes[0].startBeat - 1.0) < 0.001,
           "precondition: the nudge reached the pattern");
    expect(stored && std::abs(stored->lengthBeats - 16.0) < 0.001,
           "a note edit keeps an explicitly 4-bar pattern at 4 bars");

    const PatternID tight = oneNotePattern(pm, "One bar", 3.0, 4.0);
    editor.loadPattern(tight);
    nudgeAll(editor); // the note now ends at beat 5, past the 1-bar pattern
    stored = pm.getPattern(tight);
    expect(stored && std::abs(stored->lengthBeats - 8.0) < 0.001,
           "a note edit past the end still grows the pattern to the next whole bar");
}

} // namespace

// SPEC 3 §2.1 at the caller (review, #961): the helper tests prove the mapping, this proves
// onUpdate() actually puts it on screen. While the timeline plays through a clip of the loaded
// pattern, the editor's playhead sits at the clip-local beat, not parked at 0.
void testTimelinePlaybackMovesThePanelPlayhead() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    const PatternID patternId = patternManager.createMidiPattern("Timeline Playhead", 8.0, MidiPayload{});
    auto& playlist = trackManager->getPlaylistModel();
    const PlaylistLaneID lane = playlist.createLane("A");
    ClipInstance clip;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = 8.0;
    clip.durationBeats = 8.0;
    playlist.addClip(lane, clip);

    PianoRollPanel editor(trackManager);
    editor.setVisible(true);
    editor.loadPattern(patternId);

    const double bpm = std::max(1.0, playlist.getBPM());
    trackManager->setPosition(10.5 * 60.0 / bpm); // arrangement beat 10.5, 2.5 beats into the clip
    trackManager->play();
    editor.onUpdate(1.0 / 60.0);
    expect(std::abs(editor.getPlayheadBeat() - 2.5) < 1e-6,
           "timeline playback through a clip should put the editor playhead at the clip-local beat");
    trackManager->stop();
}

// FD-63 (V83-PR1): the piano roll carries a note's chance and ratchet. Before, its note type had neither, so a note it
// wrote back (a duplicate, a paste) came out at 100% and x1: duplicating a 60% note made a copy that always played.
void testDuplicateKeepsChanceAndRatchet() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();

    MidiPayload payload;
    Aestra::Audio::MidiNote n;
    n.pitch = 62; n.startBeat = 0.0; n.durationBeats = 1.0; n.velocity = 0.8f;
    n.chance = 0.6f; n.ratchet = 3;
    payload.notes = {n};
    const PatternID patternId = patternManager.createMidiPattern("Expression Duplicate", 8.0, payload);

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);

    AestraUI::NUIKeyEvent ctrlA;
    ctrlA.keyCode = AestraUI::NUIKeyCode::A;
    ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
    ctrlA.pressed = true;
    editor.handleKeyEvent(ctrlA);
    AestraUI::NUIKeyEvent ctrlD = ctrlA;
    ctrlD.keyCode = AestraUI::NUIKeyCode::D;
    editor.handleKeyEvent(ctrlD);

    const PatternSource* stored = patternManager.getPattern(patternId);
    expect(stored != nullptr && stored->isMidi(), "duplicated pattern should remain available");
    if (!stored || !stored->isMidi()) return;
    const auto& notes = std::get<MidiPayload>(stored->payload).notes;
    expect(notes.size() == 2, "Ctrl+D should add one copy");
    for (const auto& note : notes) {
        expect(std::abs(note.chance - 0.6f) < 1e-6f, "every note, original and copy, should keep its 60% chance");
        expect(note.ratchet == 3, "every note, original and copy, should keep its x3 ratchet");
    }

    // And the editor shows it: what it paints is what plays.
    for (const auto& ui : editor.getNotes()) {
        expect(std::abs(ui.chance - 0.6f) < 1e-6f && ui.ratchet == 3, "the editor's notes should carry chance and ratchet");
    }
}

// V83-HS1 (FD-63): the panel names the pattern's chords for the ruler and re-reads them after an edit.
void testChordsReachTheRulerAndFollowEdits() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    MidiPayload payload;
    const int bars[4][3] = {{50, 53, 57}, {46, 50, 53}, {53, 57, 60}, {55, 58, 62}}; // Dm  Bb  F  Gm
    for (int bar = 0; bar < 4; ++bar)
        for (int pitch : bars[bar]) {
            Aestra::Audio::MidiNote n;
            n.pitch = pitch; n.startBeat = bar * 4.0; n.durationBeats = 3.5; n.velocity = 0.8f;
            payload.notes.push_back(n);
        }
    const PatternID patternId = patternManager.createMidiPattern("Harmony", 16.0, payload);

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);
    const auto& chords = editor.getChords();
    const auto nameAt = [&](double beat) -> std::string {
        for (const auto& c : editor.getChords())
            if (beat >= c.startBeat && beat < c.endBeat) return c.name;
        return "";
    };
    expect(!chords.empty(), "a pattern of triads gives the ruler chords");
    expect(nameAt(1.0) == "Dm" && nameAt(5.0) == "B\xE2\x99\xAD" && nameAt(9.0) == "F" && nameAt(13.0) == "Gm",
           "bars 1-4 read Dm, B\xE2\x99\xAD, F, Gm (the ruler spells flats with the flat sign)");
    bool romanOk = false;
    for (const auto& c : chords)
        if (c.name == "Dm") romanOk = c.roman == "i";
    expect(romanOk, "Dm is i in the detected D minor");
    bool tonesOk = false;
    for (const auto& c : chords)
        if (c.name == "F") tonesOk = c.toneMask == ((1u << 5) | (1u << 9) | (1u << 0));
    expect(tonesOk, "F's chord-tone rows are F, A and C");

    // Select all and move everything up a whole step: Dm becomes Em, and the ruler follows the committed edit.
    AestraUI::NUIKeyEvent ctrlA;
    ctrlA.keyCode = AestraUI::NUIKeyCode::A;
    ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
    ctrlA.pressed = true;
    editor.handleKeyEvent(ctrlA);
    AestraUI::NUIKeyEvent up;
    up.keyCode = AestraUI::NUIKeyCode::Up;
    up.pressed = true;
    editor.handleKeyEvent(up);
    editor.handleKeyEvent(up);
    expect(nameAt(1.0) == "Em", "after transposing up two semitones bar 1 reads Em");
}

// V83-MU1 (FD-63 Quiet): ideas come after a pause, for the bar after the last note; an edit clears them; Off is off.
void testMuseIdeasAfterAPause() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    MidiPayload payload;
    const double mel[][3] = {{69, 0, 0.5}, {72, 0.5, 0.5}, {70, 1, 1}, {69, 2, 0.5}, {67, 2.5, 0.5}, {65, 3, 1}};
    for (const auto& m : mel) {
        Aestra::Audio::MidiNote n;
        n.pitch = static_cast<int>(m[0]); n.startBeat = m[1]; n.durationBeats = m[2]; n.velocity = 0.8f;
        payload.notes.push_back(n);
    }
    for (int pitch : {50, 53, 57}) {
        Aestra::Audio::MidiNote n;
        n.pitch = pitch; n.startBeat = 0.0; n.durationBeats = 1.75; n.velocity = 0.6f;
        payload.notes.push_back(n);
    }
    const PatternID patternId = patternManager.createMidiPattern("Muse", 8.0, payload);

    PianoRollPanel editor(trackManager);
    editor.setVisible(true);
    editor.loadPattern(patternId);
    expect(editor.getMuseIdeas().empty(), "no ideas the moment the pattern opens");
    editor.onUpdate(0.5);
    expect(editor.getMuseIdeas().empty(), "none before the pause is long enough");
    editor.onUpdate(0.5);
    editor.onUpdate(0.5);
    const auto& ideas = editor.getMuseIdeas();
    expect(ideas.size() == 3, "three ideas after a pause");
    bool inBar2 = !ideas.empty();
    for (const auto& idea : ideas)
        for (const auto& n : idea.notes) inBar2 = inBar2 && n.startBeat >= 4.0 && n.startBeat < 8.0;
    expect(inBar2, "every idea is for bar 2, the bar after the last note");
    expect(!ideas.empty() && ideas[0].label == "Answer bar 1, a step up", "the first is the answer a step up");

    AestraUI::NUIKeyEvent ctrlA;
    ctrlA.keyCode = AestraUI::NUIKeyCode::A;
    ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
    ctrlA.pressed = true;
    editor.handleKeyEvent(ctrlA);
    AestraUI::NUIKeyEvent up;
    up.keyCode = AestraUI::NUIKeyCode::Up;
    up.pressed = true;
    editor.handleKeyEvent(up);
    expect(editor.getMuseIdeas().empty(), "an edit clears ideas for the old notes");

    editor.setMuseMode(AestraUI::MuseMode::Off);
    for (int i = 0; i < 5; ++i) editor.onUpdate(0.5);
    expect(editor.getMuseIdeas().empty(), "Off: no ideas, however long the pause");
}

int main() {
    testDuplicateKeepsChanceAndRatchet();
    testChordsReachTheRulerAndFollowEdits();
    testMuseIdeasAfterAPause();
    testTimelinePlaybackMovesThePanelPlayhead();
    testHarmonyContextRoundtripThroughPanel();
    testSelectionSurvivesCommittedEditRoundTrip();
    testNoteEditsGrowButNeverShrinkThePattern();
    if (failures == 0) {
        std::cout << "PianoRollPanelPersistenceTest passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "PianoRollPanelPersistenceTest: " << failures << " failure(s)\n";
    return EXIT_FAILURE;
}
